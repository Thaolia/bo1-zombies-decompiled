#!/usr/bin/env python3
"""List calls that pass a literal byte count for a struct or a pointer array whose size differs on 64 bits.

The decompiled code spells most sizes as numbers: `Hunk_UserAlloc(hunk, 73728, ...)` for 2048 weaponInfo_s,
`memset(p, 0, 0x8D388)`. A 64-bit build compiles them without a warning and then overruns. For every call with an
integer literal argument (alone, multiplied by something, or as a shift), this looks at the element types involved,
either the pointee of another argument or the type the result is cast to, and reports the call when the literal is a
multiple of the element's 32-bit size and that size changes on 64-bit. An element is a struct or a pointer (a 4-byte
stride over an array of pointers: `memset(cornerEntry, 0, 4 * n)`).

    tools/litsizes.py BUILD_DIR [--jobs N] [--filter REGEX]    # BUILD_DIR: a configured 64-bit GNU preset

Each file is parsed with libclang twice, with the build's flags and the target switched to i686 and to x86_64.
When the result is cast, only the cast type counts. It is an inventory, not a proof: a size computed at run time is
out of its reach, and a literal for some other quantity that happens to be a multiple is a false positive.
"""
import argparse
import concurrent.futures
import json
import os
import re
import shlex
import subprocess
import sys

import clang.cindex as ci

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
OWN_CODE = re.compile(r'^(src|tl|DemonWare)/(?!libs/|tracy/|minilzo/|binklib/)')
TARGET = re.compile(r'^--target=')
WRAPPERS = (ci.CursorKind.UNEXPOSED_EXPR, ci.CursorKind.PAREN_EXPR, ci.CursorKind.CSTYLE_CAST_EXPR,
            ci.CursorKind.CXX_STATIC_CAST_EXPR, ci.CursorKind.CXX_REINTERPRET_CAST_EXPR)


def parse_args(entry):
    args = entry.get('arguments') or shlex.split(entry['command'])
    out, skip = [], False
    for arg in args[1:]:
        if skip:
            skip = False
            continue
        if arg == '-o':
            skip = True
            continue
        if arg in ('-c', entry['file']) or arg.startswith('-Werror') or arg.startswith('-ferror-limit'):
            continue
        out.append(arg)
    return [a for a in out if not TARGET.match(a)] + ['-w', '-ferror-limit=0', '-resource-dir', RESOURCE_DIR]


def strip(e):
    while e.kind in WRAPPERS:
        kids = list(e.get_children())
        if not kids:
            break
        e = kids[-1]
    return e


def unwrap_implicit(e):
    """The argument as written, before the implicit conversion to the parameter type (void * for memset)."""
    while e.kind in (ci.CursorKind.UNEXPOSED_EXPR, ci.CursorKind.PAREN_EXPR):
        kids = list(e.get_children())
        if len(kids) != 1:
            break
        e = kids[0]
    return e


def literal_value(e):
    """The integer literal of `N`, `N * x` / `x * N`, or the factor of `x << N`, else None."""
    e = strip(e)
    if e.kind == ci.CursorKind.INTEGER_LITERAL:
        toks = [t.spelling for t in e.get_tokens()]
        return int(toks[0].rstrip('uUlL'), 0) if toks else None
    if e.kind == ci.CursorKind.BINARY_OPERATOR:
        kids = list(e.get_children())
        if len(kids) == 2 and e.binary_operator == ci.BinaryOperator.Shl:
            shift = literal_value(kids[1])
            if shift is not None and literal_value(kids[0]) is None and shift < 32:
                return 1 << shift
        if len(kids) == 2 and any(t.spelling == '*' for t in e.get_tokens()):
            for k in kids:
                v = literal_value(k)
                if v is not None:
                    return v
    return None


def record_of(t):
    t = t.get_canonical()
    if t.kind == ci.TypeKind.CONSTANTARRAY:
        p = t.get_array_element_type().get_canonical()
    elif t.kind == ci.TypeKind.POINTER:
        p = t.get_pointee().get_canonical()
    else:
        return None
    while p.kind in (ci.TypeKind.CONSTANTARRAY,):
        p = p.get_array_element_type().get_canonical()
    # An array of pointers counts too: `memset(cornerEntry, 0, 4 * n)` clears half of n pointers on 64-bit.
    return p if p.kind in (ci.TypeKind.RECORD, ci.TypeKind.POINTER) else None


def out_record(e):
    """T for an out-parameter allocator's `(void **)&p` with p a T *: the cast hides what the bytes are."""
    t = unwrap_implicit(e).type.get_canonical()
    if t.kind != ci.TypeKind.POINTER or t.get_pointee().get_canonical().kind != ci.TypeKind.POINTER:
        return None
    if t.get_pointee().get_pointee().get_canonical().kind != ci.TypeKind.VOID:
        return None
    inner = strip(e).type.get_canonical()
    return record_of(inner.get_pointee()) if inner.kind == ci.TypeKind.POINTER else None


def scan(path, args, target):
    tu = ci.Index.create().parse(path, args=args + [f'--target={target}'])
    found = {}

    def visit(node, parent_cast):
        if node.kind == ci.CursorKind.CALL_EXPR and node.location.file:
            rel = os.path.relpath(node.location.file.name, ROOT)
            if OWN_CODE.match(rel):
                call_args = list(node.get_arguments())
                literals = [v for v in (literal_value(a) for a in call_args) if v is not None and v >= 4]
                if literals:
                    # An allocator's result cast to T * says what the bytes are; its own arguments (the HunkUser
                    # it allocates from) do not.
                    if parent_cast is not None:
                        types = [record_of(parent_cast)]
                    else:
                        types = [record_of(unwrap_implicit(a).type) or out_record(a) or record_of(strip(a).type)
                                 for a in call_args]
                    for t in filter(None, types):
                        size = t.get_size()
                        name = t.spelling
                        for lit in literals:
                            found[(rel, node.location.line, node.location.column, node.spelling, name, lit)] = size
        for child in node.get_children():
            cast = None
            if node.kind in (ci.CursorKind.CSTYLE_CAST_EXPR, ci.CursorKind.CXX_STATIC_CAST_EXPR,
                             ci.CursorKind.CXX_REINTERPRET_CAST_EXPR) and strip(child).kind == ci.CursorKind.CALL_EXPR:
                cast = node.type
            visit(child, cast)

    visit(tu.cursor, None)
    return found


def check(entry):
    args = parse_args(entry)
    s32 = scan(entry['file'], args, 'i686-w64-mingw32')
    s64 = scan(entry['file'], args, 'x86_64-w64-mingw32')
    hits = []
    for key, size32 in s32.items():
        size64 = s64.get(key)
        lit = key[5]
        if size32 > 0 and size64 and size64 > 0 and size32 != size64 and lit % size32 == 0:
            hits.append((*key, size32, size64))
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
    for rel, line, col, callee, name, lit, size32, size64 in sorted(hits):
        print(f'{rel}:{line}:{col}: {callee}(... {lit} ...): {lit // size32} x {name} '
              f'(32-bit {size32}, 64-bit {size64})')
    print(f'{len(hits)} calls, {len(entries)} files', file=sys.stderr)


RESOURCE_DIR = subprocess.run(['clang', '-print-resource-dir'], capture_output=True, text=True).stdout.strip()

if __name__ == '__main__':
    main()
