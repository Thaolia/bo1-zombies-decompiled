#!/usr/bin/env python3
"""Generate the 32-bit -> 64-bit layout tables ffconv widens fastfile structs with.

libclang parses src/database/db_load.cpp twice, once for the retail 32-bit target and once for the 64-bit target.
Every type the loader streams (the pointee of each `var*` global, plus the zone header types) is flattened into leaf
fields: a pointer, a union that holds a pointer, or a run of plain bytes. Leaves are matched between the two targets
by their field path, so each one gets its 32-bit and 64-bit offset. Padding is kept as byte leaves where the 64-bit
layout has room for it.

    tools/ffconv/gen/layouts.py --out ff_layouts.gen.h [--target64 x86_64-w64-mingw32] [--dxsdk DIR]

A type the tables cannot describe (a base class, a size that differs for another reason than pointers) stops the
generator with its name: ffconv must never guess a layout. A union mixing pointers with wider data becomes an
opaque leaf, which ffconv rejects if the loader ever streams it as a whole.
"""
import argparse
import os
import subprocess
import sys

import clang.cindex as ci

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..'))
EXTRA_ROOTS = ['XAsset', 'XAssetList', 'ScriptStringList']

BYTES, PTR, UNION_PTR, OPAQUE = 'LEAF_BYTES', 'LEAF_PTR', 'LEAF_UNION_PTR', 'LEAF_OPAQUE'
POINTER_KINDS = {ci.TypeKind.POINTER, ci.TypeKind.MEMBERPOINTER, ci.TypeKind.BLOCKPOINTER}


class LayoutError(Exception):
    pass


def resource_dir():
    # libclang does not know where clang's own headers (intrinsics, stddef.h) live; the clang driver does.
    return subprocess.run(['clang', '-print-resource-dir'], capture_output=True, text=True, check=True).stdout.strip()


def parse(target, dxsdk):
    args = ['-x', 'c++', '-std=c++20', f'--target={target}', '-include', os.path.join(ROOT, 'src/platform/compat.h'),
            '-w', '-DWIN32', '-D_CONSOLE', '-DBO1_MP', '-DBO1_OFFLINE', f'-I{ROOT}', f'-I{ROOT}/src',
            f'-I{ROOT}/src/libs', '-idirafter', os.path.join(ROOT, 'tools/toolchains/mingw-include'),
            '-idirafter', os.path.join(dxsdk, 'Include'), '-mms-bitfields', '-ferror-limit=0', '-resource-dir', resource_dir()]
    tu = ci.Index.create().parse(os.path.join(ROOT, 'src/database/db_load.cpp'), args=args)
    fatal = [d for d in tu.diagnostics if d.severity >= ci.Diagnostic.Fatal]
    if fatal:
        raise LayoutError(f'{target}: {fatal[0]}')
    return tu


def root_types(tu):
    roots = {}
    for cur in tu.cursor.get_children():
        if cur.kind == ci.CursorKind.VAR_DECL and cur.spelling.startswith('var') and \
                cur.location.file and cur.location.file.name.endswith('db_load.cpp'):
            pointee = cur.type.get_canonical().get_pointee()
            if pointee.kind == ci.TypeKind.RECORD:
                roots[record_name(pointee)] = pointee
    for name in EXTRA_ROOTS:
        found = find_record(tu.cursor, name)
        if found is None:
            raise LayoutError(f'root type {name} not found')
        roots[name] = found
    return roots


def find_record(cursor, name):
    for cur in cursor.walk_preorder():
        if cur.kind in (ci.CursorKind.STRUCT_DECL, ci.CursorKind.UNION_DECL) and cur.spelling == name and \
                cur.is_definition():
            return cur.type.get_canonical()
    return None


def record_name(t):
    name = t.get_canonical().spelling
    for prefix in ('struct ', 'union ', 'const ', 'volatile '):
        if name.startswith(prefix):
            name = name[len(prefix):]
    return name


def has_pointer(t):
    t = t.get_canonical()
    if t.kind in POINTER_KINDS:
        return True
    if t.kind == ci.TypeKind.CONSTANTARRAY:
        return has_pointer(t.element_type)
    if t.kind == ci.TypeKind.RECORD:
        return any(has_pointer(f.type) for f in t.get_fields())
    return False


def leaves(t, path, base, out):
    """Append (path, offset, size, kind) for type t at byte offset base."""
    t = t.get_canonical()
    size = t.get_size()
    if t.kind in POINTER_KINDS:
        out.append((path, base, size, PTR))
    elif t.kind == ci.TypeKind.CONSTANTARRAY:
        elem = t.element_type.get_canonical()
        if not has_pointer(elem):
            out.append((path, base, size, BYTES))
        else:
            for i in range(t.element_count):
                leaves(elem, f'{path}[{i}]', base + i * elem.get_size(), out)
    elif t.kind == ci.TypeKind.RECORD:
        decl = t.get_declaration()
        if any(c.kind == ci.CursorKind.CXX_BASE_SPECIFIER for c in decl.get_children()):
            raise LayoutError(f'{record_name(t)} has a base class')
        if decl.kind == ci.CursorKind.UNION_DECL:
            union_leaf(t, path, base, out)
        else:
            fields = list(t.get_fields())
            i = 0
            while i < len(fields):
                f = fields[i]
                if f.is_bitfield():
                    # A run of bitfields is copied as the bytes of its storage units (MS layout: one unit of the
                    # declared type, aligned to its size). Bitfields never hold pointers.
                    start, end = bitfield_unit(f)
                    while i + 1 < len(fields) and fields[i + 1].is_bitfield():
                        i += 1
                        end = max(end, bitfield_unit(fields[i])[1])
                    out.append((f'{path}.{f.spelling}:bits', base + start, end - start, BYTES))
                else:
                    leaves(f.type, f'{path}.{f.spelling or "@" + str(i)}', base + f.get_field_offsetof() // 8, out)
                i += 1
    else:
        if size <= 0:
            raise LayoutError(f'{path}: type {t.spelling} has no size')
        out.append((path, base, size, BYTES))


def bitfield_unit(field):
    """Byte range, relative to the record, of the storage unit holding a bitfield."""
    unit = field.type.get_canonical().get_size()
    first = field.get_field_offsetof() // 8
    start = first - first % unit
    return start, start + unit


def union_leaf(t, path, base, out):
    size = t.get_size()
    members = list(t.get_fields())
    if not any(has_pointer(m.type) for m in members):
        out.append((path, base, size, BYTES))
        return
    # A union that mixes pointers with wider data is only valid while the loader streams its active member on its
    # own (XAnimDeltaPartQuatData); ffconv refuses a stream range that covers it.
    kind = UNION_PTR if all(pointer_sized(m.type) for m in members) else OPAQUE
    out.append((path, base, size, kind))


def pointer_sized(t):
    """True for a pointer, plain data of 4 bytes or less, or a record made only of such members at offset 0."""
    t = t.get_canonical()
    if t.kind in POINTER_KINDS:
        return True
    if not has_pointer(t):
        return t.get_size() <= 4
    if t.kind == ci.TypeKind.RECORD:
        return all(f.get_field_offsetof() == 0 and pointer_sized(f.type) for f in t.get_fields())
    return False


def flatten(t):
    out = []
    leaves(t, '', 0, out)
    return out


def keep_padding(rows, size32, size64):
    """Copy each 32-bit padding gap to the same place after the previous field in the 64-bit layout when it fits there.

    Retail streams sometimes keep data in what the decompiled structs call padding (Glass byte 62); copying it keeps
    the conversion lossless. A gap the 64-bit layout has no room for is left out."""
    out = []
    for i, row in enumerate(rows):
        out.append(row)
        end32 = row[0] + row[2]
        end64 = row[1] + row[3]
        next32 = rows[i + 1][0] if i + 1 < len(rows) else size32
        next64 = rows[i + 1][1] if i + 1 < len(rows) else size64
        gap = next32 - end32
        if gap > 0 and next64 - end64 >= gap:
            out.append((end32, end64, gap, gap, BYTES))
    if rows and rows[0][0] > 0 and rows[0][1] >= rows[0][0]:
        out.insert(0, (0, 0, rows[0][0], rows[0][0], BYTES))
    return out


def merge(rows):
    """Merge neighbouring byte leaves that keep the same 32->64 offset delta."""
    merged = []
    for row in rows:
        if merged and row[4] == BYTES and merged[-1][4] == BYTES and \
                merged[-1][0] + merged[-1][2] == row[0] and merged[-1][1] + merged[-1][3] == row[1]:
            last = merged[-1]
            merged[-1] = (last[0], last[1], last[2] + row[2], last[3] + row[3], BYTES)
        else:
            merged.append(row)
    return merged


def build(tu32, tu64):
    roots32, roots64 = root_types(tu32), root_types(tu64)
    tables = {}
    for name in sorted(roots32):
        t32, t64 = roots32[name], roots64.get(name)
        if t64 is None:
            raise LayoutError(f'{name}: missing on the 64-bit target')
        l32, l64 = flatten(t32), flatten(t64)
        if [p for p, *_ in l32] != [p for p, *_ in l64]:
            raise LayoutError(f'{name}: the two targets flatten to different fields')
        rows = []
        for (path, o32, s32, k32), (_, o64, s64, k64) in zip(l32, l64):
            if k32 != k64:
                raise LayoutError(f'{name}{path}: leaf kind differs between targets')
            if k32 == BYTES and s32 != s64:
                raise LayoutError(f'{name}{path}: {s32} bytes on 32-bit, {s64} on 64-bit')
            rows.append((o32, o64, s32, s64, k32))
        rows = keep_padding(rows, t32.get_size(), t64.get_size())
        tables[name] = (t32.get_size(), t32.get_align(), t64.get_size(), t64.get_align(), merge(rows))
    return tables


def ident(name):
    return ''.join(c if c.isalnum() else '_' for c in name)


def emit(tables, target64, out):
    lines = [
        f'// Generated by tools/ffconv/gen/layouts.py (64-bit target {target64}). Do not edit.',
        '// Included once, by the transformed db_load.cpp: another file declares the specializations it uses.',
        '#pragma once',
        '',
        '#include "ff_layout.h"',
        '',
        'namespace ff',
        '{',
    ]
    for name, (s32, a32, s64, a64, rows) in tables.items():
        lid = ident(name)
        body = ', '.join(f'{{{o32}, {o64}, {l32}, {l64}, {k}}}' for o32, o64, l32, l64, k in rows)
        lines += [
            f'static_assert(sizeof({name}) == {s32} && alignof({name}) == {a32}, "{name}: 32-bit layout");',
            f'inline constexpr Leaf kLeaves_{lid}[] = {{{body}}};',
            f'template <> const TypeLayout &Layout<{name}>()',
            '{',
            f'    static constexpr TypeLayout layout{{"{name}", {s32}, {a32}, {s64}, {a64}, kLeaves_{lid}, '
            f'{len(rows)}}};',
            '    return layout;',
            '}',
        ]
    lines += ['} // namespace ff', '']
    with open(out, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--out', required=True)
    parser.add_argument('--target32', default='i686-w64-mingw32')
    parser.add_argument('--target64', default='x86_64-w64-mingw32')
    parser.add_argument('--dxsdk', default='/mnt/c/SDKs/DXSDK_Jun10/DXSDK')
    opts = parser.parse_args()
    try:
        tables = build(parse(opts.target32, opts.dxsdk), parse(opts.target64, opts.dxsdk))
    except LayoutError as e:
        print(f'layouts.py: {e}', file=sys.stderr)
        return 1
    emit(tables, opts.target64, opts.out)
    print(f'{len(tables)} types -> {opts.out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
