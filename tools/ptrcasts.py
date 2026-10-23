#!/usr/bin/env python3
"""List pointer casts that read 64-bit data through a 4-byte view.

The decompiled code often walks a struct or an array of pointers as dwords: `*((unsigned int *)def->worldModel + 1)`
for `worldModel[1]`, `*((_DWORD *)cent + 201)` for a bitfield word, `*(float *)((char *)p + 240)` for a field. On
32-bit these are the field; on 64-bit they land on the wrong bytes, and clang accepts them without a warning because
no pointer is narrowed to an integer. This parses each file for x86_64 with libclang and reports:

    ptr-array     a pointer to pointers cast to a pointer to a 4-byte type (`(int *)models`);
    struct-dword  a pointer to a struct that holds pointers cast to a pointer to a 4-byte type (`(_DWORD *)cent`);
    byte-offset   `(T *)((char *)p + N)` where p points to a struct that holds pointers and N is a literal;
    sort-size     a qsort/bsearch whose literal element size differs from the 64-bit size of what the base points to
                  (sort-unknown when the base is a void or byte pointer: check the element type by hand);
    ptr-alloc     a call whose result is cast to a pointer to pointers and whose size is `4 * n`.

    tools/ptrcasts.py BUILD_DIR [--jobs N] [--filter REGEX]    # BUILD_DIR: a configured 64-bit GNU preset

It is an inventory: a cast whose offset happens to land on the same field on both widths (a leading `int`) is still
listed, and arithmetic done after the cast, through a variable, is out of its reach.
"""
import argparse
import concurrent.futures
import json
import os
import re
import sys

import clang.cindex as ci

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from litsizes import OWN_CODE, ROOT, literal_value, parse_args, strip, unwrap_implicit  # noqa: E402

CASTS = (ci.CursorKind.CSTYLE_CAST_EXPR, ci.CursorKind.CXX_REINTERPRET_CAST_EXPR)
CHAR_KINDS = (ci.TypeKind.CHAR_S, ci.TypeKind.CHAR_U, ci.TypeKind.SCHAR, ci.TypeKind.UCHAR)
SORT_SIZE_ARG = {'qsort': 2, 'bsearch': 3}


def pointee(t):
    t = t.get_canonical()
    return t.get_pointee().get_canonical() if t.kind == ci.TypeKind.POINTER else None


def holds_pointer(t, depth=0):
    t = t.get_canonical()
    if t.kind == ci.TypeKind.POINTER:
        return True
    if t.kind == ci.TypeKind.CONSTANTARRAY:
        return holds_pointer(t.get_array_element_type(), depth)
    if t.kind == ci.TypeKind.RECORD and depth < 8:
        return any(holds_pointer(f.type, depth + 1) for f in t.get_fields())
    return False


def operand(cast):
    kids = [k for k in cast.get_children() if k.kind != ci.CursorKind.TYPE_REF]
    return kids[-1] if kids else None


def classify(cast):
    target = pointee(cast.type)
    if target is None or target.kind == ci.TypeKind.VOID or target.get_size() != 4:
        return None
    src = operand(cast)
    if src is None:
        return None
    inner = strip(src) if src.kind != ci.CursorKind.BINARY_OPERATOR else src
    while inner.kind == ci.CursorKind.PAREN_EXPR:
        inner = list(inner.get_children())[0]
    src_pointee = pointee(inner.type)
    if src_pointee is not None:
        if src_pointee.kind == ci.TypeKind.POINTER:
            return 'ptr-array', src_pointee.spelling
        if src_pointee.kind == ci.TypeKind.RECORD and holds_pointer(src_pointee):
            return 'struct-dword', src_pointee.spelling
    if inner.kind == ci.CursorKind.BINARY_OPERATOR:
        for side in inner.get_children():
            side = strip(side) if side.kind not in CASTS else side
            if side.kind in CASTS and (pointee(side.type) or side.type).kind in CHAR_KINDS:
                base = pointee(strip(operand(side)).type) if operand(side) is not None else None
                if base is not None and base.kind == ci.TypeKind.RECORD and holds_pointer(base):
                    return 'byte-offset', base.spelling
    return None


def sort_call(call):
    index = SORT_SIZE_ARG.get(call.spelling)
    args = list(call.get_arguments())
    if index is None or len(args) <= index:
        return None
    size = literal_value(args[index])
    if size is None:
        return None
    element = pointee(unwrap_implicit(args[index - 2]).type)
    if element is None or element.kind == ci.TypeKind.VOID or element.kind in CHAR_KINDS:
        return 'sort-unknown', f'{size}-byte elements'
    if element.get_size() != size:
        return 'sort-size', f'{size}-byte elements of {element.spelling} ({element.get_size()} bytes)'
    return None


def ptr_alloc(call, cast_type):
    target = pointee(cast_type)
    if target is None or target.kind != ci.TypeKind.POINTER:
        return None
    if any(literal_value(a) == 4 for a in call.get_arguments()):
        return 'ptr-alloc', f'4-byte slots for {target.spelling}'
    return None


def check(entry):
    args = parse_args(entry)
    tu = ci.Index.create().parse(entry['file'], args=args + ['--target=x86_64-w64-mingw32'])
    hits = set()

    def report(node, found, type_spelling):
        rel = os.path.relpath(node.location.file.name, ROOT)
        hits.add((rel, node.location.line, node.location.column, found[0], found[1], type_spelling))

    def visit(node, cast_type):
        own = node.location.file and OWN_CODE.match(os.path.relpath(node.location.file.name, ROOT))
        if own and node.kind in CASTS:
            found = classify(node)
            if found:
                report(node, found, node.type.spelling)
        if own and node.kind == ci.CursorKind.CALL_EXPR:
            found = sort_call(node) or (ptr_alloc(node, cast_type) if cast_type is not None else None)
            if found:
                report(node, found, node.spelling)
        for child in node.get_children():
            visit(child, node.type if node.kind in CASTS and strip(child).kind == ci.CursorKind.CALL_EXPR else None)

    visit(tu.cursor, None)
    return hits


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('build_dir')
    parser.add_argument('--jobs', type=int, default=os.cpu_count())
    parser.add_argument('--filter', default='')
    opts = parser.parse_args()
    with open(os.path.join(opts.build_dir, 'compile_commands.json'), encoding='utf-8') as f:
        entries = [e for e in json.load(f) if OWN_CODE.match(os.path.relpath(e['file'], ROOT))
                   and e['file'].endswith(('.cpp', '.c')) and re.search(opts.filter, e['file'])]
    hits = set()
    with concurrent.futures.ProcessPoolExecutor(opts.jobs) as pool:
        for result in pool.map(check, entries):
            hits.update(result)
    for rel, line, col, kind, source, cast in sorted(hits):
        print(f'{rel}:{line}:{col}: {kind}: {source} ({cast})')
    print(f'{len(hits)} casts, {len(entries)} files', file=sys.stderr)


if __name__ == '__main__':
    main()
