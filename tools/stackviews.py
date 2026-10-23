#!/usr/bin/env python3
"""List casts that view a local variable through a pointer to something bigger than what is left of it.

Hex-Rays often splits one stack object into several locals and then passes the first one as the whole object:
`_BYTE v3[12]; phys_mat44 rv; make_rotate((phys_mat44 *)v3, ...)` writes 64 bytes into a 12-byte local. MSVC x86
happened to place the next locals right after it, so the write landed on them; another compiler or another width
orders the frame differently and the write corrupts something else. For every explicit cast to `T *` whose operand
is the address of a local or a parameter (`&x`, a decayed array `x`, `&x[N]`, `&x.member`, `(char *)&x + N`), this
reports the cast when sizeof(T) exceeds the bytes from that address to the end of the variable. It also reports a
call argument that is such an address when the parameter points to a bigger type (only possible implicitly through
a base class, so rare).

    tools/stackviews.py BUILD_DIR [--jobs N] [--filter REGEX] [--width {32,64,both}]

Each file is parsed with the build's flags for i686 and x86_64 (both by default); a hit on either width is listed
with the widths it appears on. `&x[i]` with a non-literal index is measured from x[0] and marked `[?]`; it is also
listed when x holds at most two views of another type (`*(_WORD *)&src[2 * i]` on `unsigned char src[2]`). It is
an inventory: a view whose tail is never touched (only the first field is read) is still listed, and a plain `x[i]`
past the end of a local array (no cast) is out of its reach.
"""
import argparse
import concurrent.futures
import json
import os
import re
import sys

import clang.cindex as ci

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from litsizes import OWN_CODE, ROOT, parse_args, strip  # noqa: E402

CASTS = (ci.CursorKind.CSTYLE_CAST_EXPR, ci.CursorKind.CXX_REINTERPRET_CAST_EXPR)
CHAR_KINDS = (ci.TypeKind.CHAR_S, ci.TypeKind.CHAR_U, ci.TypeKind.SCHAR, ci.TypeKind.UCHAR, ci.TypeKind.VOID)
FUNCTIONS = (ci.CursorKind.FUNCTION_DECL, ci.CursorKind.CXX_METHOD, ci.CursorKind.CONSTRUCTOR,
             ci.CursorKind.DESTRUCTOR, ci.CursorKind.FUNCTION_TEMPLATE, ci.CursorKind.CONVERSION_FUNCTION,
             ci.CursorKind.LAMBDA_EXPR)
TARGETS = {'32': 'i686-w64-mingw32', '64': 'x86_64-w64-mingw32'}


def skip_wrappers(e):
    while e.kind in (ci.CursorKind.UNEXPOSED_EXPR, ci.CursorKind.PAREN_EXPR):
        kids = list(e.get_children())
        if len(kids) != 1:
            break
        e = kids[0]
    return e


def literal_value(e):
    """The value of an integer literal, else None (`3 * i` is not a literal offset)."""
    e = strip(e)
    if e.kind != ci.CursorKind.INTEGER_LITERAL:
        return None
    toks = [t.spelling for t in e.get_tokens()]
    return int(toks[0].rstrip('uUlL'), 0) if toks else None


def pointee(t):
    t = t.get_canonical()
    return t.get_pointee().get_canonical() if t.kind == ci.TypeKind.POINTER else None


def is_stack_var(decl):
    if decl is None or decl.kind not in (ci.CursorKind.VAR_DECL, ci.CursorKind.PARM_DECL):
        return False
    if decl.type.get_canonical().kind in (ci.TypeKind.LVALUEREFERENCE, ci.TypeKind.RVALUEREFERENCE):
        return False
    if decl.kind == ci.CursorKind.VAR_DECL:
        if decl.storage_class in (ci.StorageClass.STATIC, ci.StorageClass.EXTERN):
            return False
        parent = decl.semantic_parent
        if parent is None or parent.kind not in FUNCTIONS:
            return False
    return decl.type.get_size() > 0


def lvalue_base(e):
    """(stack variable, byte offset, exact) for an lvalue inside a local or parameter, else None."""
    e = skip_wrappers(e)
    if e.kind == ci.CursorKind.DECL_REF_EXPR:
        decl = e.referenced
        return (decl, 0, True) if is_stack_var(decl) else None
    if e.kind == ci.CursorKind.MEMBER_REF_EXPR:
        kids = list(e.get_children())
        field = e.referenced
        if not kids or field is None or field.kind != ci.CursorKind.FIELD_DECL or field.is_bitfield():
            return None
        base = kids[0]
        if base.type.get_canonical().kind == ci.TypeKind.POINTER:
            return None
        inner = lvalue_base(base)
        offset = field.get_field_offsetof()
        if inner is None or offset < 0:
            return None
        return inner[0], inner[1] + offset // 8, inner[2]
    if e.kind == ci.CursorKind.ARRAY_SUBSCRIPT_EXPR:
        kids = list(e.get_children())
        if len(kids) != 2:
            return None
        base = skip_wrappers(kids[0])
        if base.type.get_canonical().kind != ci.TypeKind.CONSTANTARRAY:
            return None
        inner = lvalue_base(base)
        if inner is None:
            return None
        index = literal_value(kids[1])
        if index is None:
            return inner[0], inner[1], False
        return inner[0], inner[1] + index * e.type.get_size(), inner[2]
    return None


def address_of(e):
    """(stack variable, byte offset, exact) when e evaluates to an address inside a local or parameter."""
    e = skip_wrappers(e)
    if e.kind == ci.CursorKind.UNARY_OPERATOR:
        toks = list(e.get_tokens())
        kids = list(e.get_children())
        if toks and toks[0].spelling == '&' and len(kids) == 1:
            return lvalue_base(kids[0])
        return None
    if e.type.get_canonical().kind == ci.TypeKind.CONSTANTARRAY:
        return lvalue_base(e)
    if e.kind in CASTS + (ci.CursorKind.CXX_STATIC_CAST_EXPR,):
        kids = [k for k in e.get_children() if k.kind != ci.CursorKind.TYPE_REF]
        return address_of(kids[-1]) if kids else None
    if e.kind == ci.CursorKind.BINARY_OPERATOR and e.binary_operator in (ci.BinaryOperator.Add,
                                                                         ci.BinaryOperator.Sub):
        kids = list(e.get_children())
        if len(kids) != 2:
            return None
        for ptr, num in ((kids[0], kids[1]), (kids[1], kids[0])):
            step = pointee(skip_wrappers(ptr).type) if skip_wrappers(ptr).type.get_canonical().kind == \
                ci.TypeKind.POINTER else None
            if step is None and skip_wrappers(ptr).type.get_canonical().kind == ci.TypeKind.CONSTANTARRAY:
                step = skip_wrappers(ptr).type.get_canonical().get_array_element_type()
            if step is None:
                continue
            inner = address_of(ptr)
            n = literal_value(num)
            if inner is None:
                return None
            if n is None:
                return inner[0], inner[1], False
            if e.binary_operator == ci.BinaryOperator.Sub:
                n = -n
            size = 1 if step.kind in CHAR_KINDS else step.get_size()
            return inner[0], inner[1] + n * size, inner[2]
    return None


def too_big(target, where):
    if target is None or target.kind in CHAR_KINDS or target.kind == ci.TypeKind.FUNCTIONPROTO:
        return None
    size = target.get_size()
    if size <= 0:
        return None
    decl, offset, exact = where
    left = decl.type.get_size() - offset
    if size > left:
        return size, left, exact
    # `*(_WORD *)&src[2 * i]` on a 2-byte local: a variable index into an array of one or two views is the first
    # slot of a buffer Hex-Rays cut short, whatever the index turns out to be.
    array = decl.type.get_canonical()
    if not exact and array.kind == ci.TypeKind.CONSTANTARRAY and array.get_size() <= 2 * size \
            and array.get_array_element_type().get_canonical().spelling != target.spelling:
        return size, left, exact
    return None


def scan(path, args, target):
    tu = ci.Index.create().parse(path, args=args + [f'--target={target}'])
    found = {}

    def report(node, kind, func, where, view, result):
        decl, offset, exact = where
        size, left, _ = result
        rel = os.path.relpath(node.location.file.name, ROOT)
        local = decl.spelling + (f'+{offset}' if offset else '') + ('[?]' if not exact else '')
        key = (rel, node.location.line, node.location.column, kind, func, local, decl.type.spelling,
               'param' if decl.kind == ci.CursorKind.PARM_DECL else 'local', view)
        found[key] = (decl.type.get_size(), left, size)

    own_files = {}

    def own(node):
        f = node.location.file
        if f is None:
            return False
        name = f.name
        if name not in own_files:
            own_files[name] = bool(OWN_CODE.match(os.path.relpath(name, ROOT)))
        return own_files[name]

    def visit(node, func):
        kind = node.kind
        if kind in FUNCTIONS and kind != ci.CursorKind.LAMBDA_EXPR and node.is_definition():
            parent = node.semantic_parent
            scoped = parent is not None and parent.kind in (ci.CursorKind.STRUCT_DECL, ci.CursorKind.CLASS_DECL,
                                                            ci.CursorKind.CLASS_TEMPLATE)
            func = f'{parent.spelling}::{node.spelling}' if scoped else node.spelling
        elif kind in CASTS and own(node):
            kids = [k for k in node.get_children() if k.kind != ci.CursorKind.TYPE_REF]
            where = address_of(kids[-1]) if kids else None
            if where is not None:
                result = too_big(pointee(node.type), where)
                if result:
                    report(node, 'cast', func, where, node.type.spelling, result)
        elif kind == ci.CursorKind.CALL_EXPR and node.referenced is not None and own(node):
            params = list(node.referenced.get_arguments() or [])
            for arg, param in zip(node.get_arguments(), params):
                if skip_wrappers(arg).kind in CASTS:
                    continue
                where = address_of(arg)
                if where is not None:
                    result = too_big(pointee(param.type), where)
                    if result:
                        report(arg, 'call', func, where, f'{node.spelling}({param.type.spelling})', result)
        for child in node.get_children():
            visit(child, func)

    # System and third-party headers hold no decompiled code: skip their declarations whole.
    for top in tu.cursor.get_children():
        if own(top):
            visit(top, '?')
    return found


def check(job):
    entry, widths = job
    args = parse_args(entry)
    hits = {}
    for width in widths:
        for key, sizes in scan(entry['file'], args, TARGETS[width]).items():
            hits.setdefault(key, {})[width] = sizes
    return hits


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('build_dir')
    parser.add_argument('--jobs', type=int, default=os.cpu_count())
    parser.add_argument('--filter', default='')
    parser.add_argument('--width', choices=('32', '64', 'both'), default='both')
    opts = parser.parse_args()
    widths = ('32', '64') if opts.width == 'both' else (opts.width,)
    with open(os.path.join(opts.build_dir, 'compile_commands.json'), encoding='utf-8') as f:
        entries = [e for e in json.load(f) if OWN_CODE.match(os.path.relpath(e['file'], ROOT))
                   and e['file'].endswith(('.cpp', '.c')) and re.search(opts.filter, e['file'])]
    hits = {}
    with concurrent.futures.ProcessPoolExecutor(opts.jobs) as pool:
        for result in pool.map(check, [(e, widths) for e in entries]):
            for key, per_width in result.items():
                hits.setdefault(key, {}).update(per_width)
    for key in sorted(hits):
        rel, line, col, kind, func, local, ltype, storage, view = key
        sizes = ', '.join(f'{w}-bit {view_size} > {left} of {total}'
                          for w, (total, left, view_size) in sorted(hits[key].items()))
        print(f'{rel}:{line}:{col}: {kind}: {func}: {storage} {local} ({ltype}) as {view}: {sizes}')
    print(f'{len(hits)} views, {len(entries)} files', file=sys.stderr)


if __name__ == '__main__':
    main()
