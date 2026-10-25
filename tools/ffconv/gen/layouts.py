#!/usr/bin/env python3
"""Generate the 32-bit -> 64-bit layout tables ffconv widens fastfile structs with.

libclang parses src/database/db_load.cpp twice, once for the retail 32-bit target and once for the 64-bit target.
Every type the loader streams (the pointee of each `var*` global, plus the zone header types) is flattened into leaf
fields: a pointer, a union that holds a pointer, or a run of plain bytes. Leaves are matched between the two targets
by their field path, so each one gets its 32-bit and 64-bit offset. Padding is kept as byte leaves where the 64-bit
layout has room for it. Integer fields listed in VALUE_FIXUPS also get a value fixup entry (see ff_layout.h), and the
`intptr_t` fields listed in WIDENED_INTEGERS become handle leaves (4 bytes on 32-bit, 8 on 64-bit).

    tools/ffconv/gen/layouts.py --out ff_layouts.gen.h [--target64 x86_64-w64-mingw32] [--dxsdk DIR]

A 64-bit build of another ABI may read the same zones only if every streamed type lays out the same there. The
posix x86-64 build checks it at build time, parsing db_load.cpp as that build compiles it (src/platform/posix shim,
the DirectX SDK headers it copies): the tables must equal the --target64 ones, or the check names the types that differ.

    tools/ffconv/gen/layouts.py --check-target64 x86_64-linux-gnu --posix-dxsdk BUILD/dxsdk --stamp FILE

--check-switch does the same for the Nintendo Switch ABI: it parses db_load.cpp as the devkitA64 + libnx build
compiles it (aarch64-none-elf, AAPCS64/LP64, the devkitA64 newlib + libstdc++ headers and libnx/portlibs) and
checks that layout against --target64's. It needs --devkitpro (the devkitPro root) besides --posix-dxsdk and --stamp.

    tools/ffconv/gen/layouts.py --check-switch --devkitpro /opt/devkitpro --posix-dxsdk BUILD/dxsdk --stamp FILE

A type the tables cannot describe (a base class, a size that differs for another reason than pointers or a listed
widened integer) stops the generator with its name: ffconv must never guess a layout. A union mixing pointers with
wider data becomes an opaque leaf, which ffconv rejects if the loader ever streams it as a whole.
"""
import argparse
import os
import subprocess
import sys

import clang.cindex as ci

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..'))
EXTRA_ROOTS = ['XAsset', 'XAssetList', 'ScriptStringList']

BYTES, PTR, UNION_PTR, OPAQUE, HANDLE = 'LEAF_BYTES', 'LEAF_PTR', 'LEAF_UNION_PTR', 'LEAF_OPAQUE', 'LEAF_HANDLE'
FIXUP = 'FIXUP'

# (record, field, unit): an int field of `record` that holds a byte distance between two elements of one array of
# `unit`. Its value depends on the unit's size, so ffconv rescales it. docs/PORTING.md (phase 3) lists the integer
# fields audited for this and why the others keep their value.
VALUE_FIXUPS = {
    # r_bsp_load_obj.cpp: tree->childrenOffset = (char *)&rgl.aabbTrees[first child] - (char *)tree.
    ('GfxAabbTree', 'childrenOffset'): 'GfxAabbTree',
}
# (record, field): an integer the engine declares `intptr_t` because it holds a pointer at runtime. It grows from 4 to
# 8 bytes, which is refused for any other integer: ffconv sign-extends it, and fails a zone where its streamed value
# is not 0, since a 32-bit runtime pointer would mean nothing to the 64-bit engine. docs/PORTING.md (decisions).
WIDENED_INTEGERS = {
    ('DynEntityClient', 'physObjId'),         # DynEntCl_CreatePhysObj: a PhysObjUserData pointer
    ('PhysConstraint', 'constraintHandle'),   # DynEnt_SetupConstraints: a rigid_body_constraint_* pointer
}
POINTER_KINDS = {ci.TypeKind.POINTER, ci.TypeKind.MEMBERPOINTER, ci.TypeKind.BLOCKPOINTER}
SIGNED_INTEGER_KINDS = {ci.TypeKind.INT, ci.TypeKind.LONG, ci.TypeKind.LONGLONG}


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


def parse_posix(target, posix_dxsdk):
    """db_load.cpp as the posix builds compile it (CMakeLists.txt): Win32 from the shim, no WIN32 define."""
    posix = os.path.join(ROOT, 'src/platform/posix')
    args = ['-x', 'c++', '-std=gnu++20', f'--target={target}', '-include', os.path.join(ROOT, 'src/platform/compat.h'),
            '-w', '-DBO1_MP', '-DBO1_OFFLINE', '-DSTEAM_API_NODLL', '-D__RADINEXE__', f'-I{ROOT}', f'-I{ROOT}/src',
            f'-I{ROOT}/src/libs', f'-I{posix}/include', f'-I{posix}/include_cased', f'-I{posix_dxsdk}',
            '-ferror-limit=0', '-resource-dir', resource_dir()]
    tu = ci.Index.create().parse(os.path.join(ROOT, 'src/database/db_load.cpp'), args=args)
    errors = [d for d in tu.diagnostics if d.severity >= ci.Diagnostic.Error]
    if errors:
        raise LayoutError(f'{target}: {errors[0]} ({len(errors)} errors)')
    return tu


def parse_switch(devkitpro, posix_dxsdk):
    """db_load.cpp as the Switch build compiles it: aarch64-none-elf (AAPCS64/LP64), the devkitA64 newlib and
    libstdc++ headers plus libnx/portlibs, no host system includes. Models the real devkitA64 GCC ABI (its struct
    layout follows AAPCS64 like clang's), so the streamed types the check flattens are the ones the console reads."""
    sr = os.path.join(devkitpro, 'devkitA64/aarch64-none-elf')
    cxx = os.path.join(sr, 'include/c++')
    try:
        ver = sorted(os.listdir(cxx))[-1]  # the one libstdc++ version devkitA64 ships
    except OSError as e:
        raise LayoutError(f'devkitA64 libstdc++ headers not found under {cxx}: {e}')
    posix = os.path.join(ROOT, 'src/platform/posix')
    res = resource_dir()
    args = ['-x', 'c++', '-std=gnu++20', '--target=aarch64-none-elf', '-include',
            os.path.join(ROOT, 'src/platform/compat.h'), '-w', '-DBO1_MP', '-DBO1_OFFLINE', '-DSTEAM_API_NODLL',
            '-D__RADINEXE__', '-D__SWITCH__', '-D_GNU_SOURCE', f'-I{ROOT}', f'-I{ROOT}/src', f'-I{ROOT}/src/libs',
            f'-I{ROOT}/src/platform/switch/include', f'-I{posix}/include', f'-I{posix}/include_cased',
            f'-I{posix_dxsdk}', '-nostdinc', '-isystem', f'{cxx}/{ver}', '-isystem', f'{cxx}/{ver}/aarch64-none-elf',
            '-isystem', f'{cxx}/{ver}/backward', '-isystem', f'{res}/include', '-isystem',
            os.path.join(devkitpro, 'libnx/include'), '-isystem', os.path.join(devkitpro, 'portlibs/switch/include'),
            '-isystem', os.path.join(ROOT, 'third_party/sse2neon'), '-isystem', f'{sr}/include', '-ferror-limit=0',
            '-resource-dir', res]
    tu = ci.Index.create().parse(os.path.join(ROOT, 'src/database/db_load.cpp'), args=args)
    errors = [d for d in tu.diagnostics if d.severity >= ci.Diagnostic.Error]
    if errors:
        raise LayoutError(f'aarch64-none-elf: {errors[0]} ({len(errors)} errors)')
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


def has_listed_field(t, fields):
    t = t.get_canonical()
    if t.kind == ci.TypeKind.CONSTANTARRAY:
        return has_listed_field(t.element_type, fields)
    if t.kind == ci.TypeKind.RECORD:
        name = record_name(t)
        return any((name, f.spelling) in fields or has_listed_field(f.type, fields) for f in t.get_fields())
    return False


def has_fixup(t):
    return has_listed_field(t, VALUE_FIXUPS)


def has_handle(t):
    return has_listed_field(t, WIDENED_INTEGERS)


def fixup_row(record, field, path, base, out):
    t = field.type.get_canonical()
    if t.kind not in (ci.TypeKind.INT, ci.TypeKind.LONG) or t.get_size() != 4:
        raise LayoutError(f'{record}.{field.spelling}: a value fixup needs a 4-byte signed integer')
    out.append((path, base, 4, FIXUP, VALUE_FIXUPS[(record, field.spelling)]))


def handle_row(record, field, path, base, out):
    t = field.type.get_canonical()
    if field.type.spelling != 'intptr_t' or t.kind not in SIGNED_INTEGER_KINDS:
        raise LayoutError(f'{record}.{field.spelling}: a widened integer must be declared intptr_t')
    out.append((path, base, t.get_size(), HANDLE))


def leaves(t, path, base, out):
    """Append (path, offset, size, kind) for type t at byte offset base, and (path, offset, 4, FIXUP, unit) for the
    fields VALUE_FIXUPS lists. The fields WIDENED_INTEGERS lists are HANDLE leaves."""
    t = t.get_canonical()
    size = t.get_size()
    if t.kind in POINTER_KINDS:
        out.append((path, base, size, PTR))
    elif t.kind == ci.TypeKind.CONSTANTARRAY:
        elem = t.element_type.get_canonical()
        if not has_pointer(elem) and not has_fixup(elem) and not has_handle(elem):
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
                    fpath = f'{path}.{f.spelling or "@" + str(i)}'
                    fbase = base + f.get_field_offsetof() // 8
                    if (record_name(t), f.spelling) in WIDENED_INTEGERS:
                        handle_row(record_name(t), f, fpath, fbase, out)
                    else:
                        leaves(f.type, fpath, fbase, out)
                    if (record_name(t), f.spelling) in VALUE_FIXUPS:
                        fixup_row(record_name(t), f, fpath, fbase, out)
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
    if any(has_fixup(m.type) for m in members):
        raise LayoutError(f'{path}: a value fixup inside a union')
    if any(has_handle(m.type) for m in members):
        raise LayoutError(f'{path}: a widened integer inside a union')
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


def unit_sizes(tu32, tu64, unit):
    sizes = []
    for tu in (tu32, tu64):
        t = find_record(tu.cursor, unit)
        if t is None:
            raise LayoutError(f'value fixup unit {unit} not found')
        sizes.append(t.get_size())
    return sizes


def build(tu32, tu64):
    roots32, roots64 = root_types(tu32), root_types(tu64)
    units = {unit: unit_sizes(tu32, tu64, unit) for unit in set(VALUE_FIXUPS.values())}
    tables = {}
    for name in sorted(roots32):
        t32, t64 = roots32[name], roots64.get(name)
        if t64 is None:
            raise LayoutError(f'{name}: missing on the 64-bit target')
        l32, l64 = flatten(t32), flatten(t64)
        if [p for p, *_ in l32] != [p for p, *_ in l64]:
            raise LayoutError(f'{name}: the two targets flatten to different fields')
        fixups = [(r32[1], r64[1], *units[r32[4]], r32[4], r32[0])
                  for r32, r64 in zip(l32, l64) if r32[3] == FIXUP]
        l32 = [r for r in l32 if r[3] != FIXUP]
        l64 = [r for r in l64 if r[3] != FIXUP]
        handles = [(path, o32) for path, o32, _, kind in l32 if kind == HANDLE]
        rows = []
        for (path, o32, s32, k32), (_, o64, s64, k64) in zip(l32, l64):
            if k32 != k64:
                raise LayoutError(f'{name}{path}: leaf kind differs between targets')
            if k32 == BYTES and s32 != s64:
                raise LayoutError(f'{name}{path}: {s32} bytes on 32-bit, {s64} on 64-bit')
            if k32 == HANDLE and (s32, s64) != (4, 8):
                raise LayoutError(f'{name}{path}: a widened integer of {s32} bytes on 32-bit, {s64} on 64-bit')
            rows.append((o32, o64, s32, s64, k32))
        rows = keep_padding(rows, t32.get_size(), t64.get_size())
        for o32, o64, *_ in fixups:
            if not any(k == BYTES and r32 <= o32 and o32 + 4 <= r32 + n32 and o64 - o32 == r64 - r32
                       for r32, r64, n32, _, k in rows):
                raise LayoutError(f'{name}: a value fixup at {o32} is not inside a byte leaf')
        tables[name] = (t32.get_size(), t32.get_align(), t64.get_size(), t64.get_align(), merge(rows), fixups,
                        handles)
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
    for name, (s32, a32, s64, a64, rows, fixups, handles) in tables.items():
        lid = ident(name)
        body = ', '.join(f'{{{o32}, {o64}, {l32}, {l64}, {k}}}' for o32, o64, l32, l64, k in rows)
        lines.append(f'static_assert(sizeof({name}) == {s32} && alignof({name}) == {a32}, "{name}: 32-bit layout");')
        for path, o32 in handles:
            lines.append(f'static_assert(offsetof({name}, {path[1:]}) == {o32}, "{name}{path}: widened integer");')
        lines.append(f'inline constexpr Leaf kLeaves_{lid}[] = {{{body}}};')
        fixup_ref = ''
        if fixups:
            for o32, _, u32, _, unit, path in fixups:
                lines.append(f'static_assert(offsetof({name}, {path[1:]}) == {o32} && sizeof({unit}) == {u32}, '
                             f'"{name}{path}: value fixup");')
            fbody = ', '.join(f'{{{o32}, {o64}, {u32}, {u64}, "{name}{path}"}}'
                              for o32, o64, u32, u64, _, path in fixups)
            lines.append(f'inline constexpr ValueFixup kFixups_{lid}[] = {{{fbody}}};')
            fixup_ref = f', kFixups_{lid}, {len(fixups)}'
        lines += [
            f'template <> const TypeLayout &Layout<{name}>()',
            '{',
            f'    static constexpr TypeLayout layout{{"{name}", {s32}, {a32}, {s64}, {a64}, kLeaves_{lid}, '
            f'{len(rows)}{fixup_ref}}};',
            '    return layout;',
            '}',
        ]
    lines += ['} // namespace ff', '']
    with open(out, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines))


def write_depfile(path, target, tus):
    headers = sorted({os.path.abspath(i.include.name) for tu in tus for i in tu.get_includes()})
    with open(path, 'w', encoding='utf-8') as f:
        f.write(target.replace(' ', '\\ ') + ': ' + ' \\\n  '.join(h.replace(' ', '\\ ') for h in headers) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--out')
    parser.add_argument('--target32', default='i686-w64-mingw32')
    parser.add_argument('--target64', default='x86_64-w64-mingw32')
    parser.add_argument('--dxsdk', default='/mnt/c/SDKs/DXSDK_Jun10/DXSDK')
    parser.add_argument('--depfile', help='write the headers the parses read, for the build to rerun on a change')
    parser.add_argument('--check-target64', help='a posix 64-bit target whose tables must equal --target64\'s')
    parser.add_argument('--check-switch', action='store_true',
                        help='check the Switch ABI (devkitA64 aarch64-none-elf) against --target64 instead')
    parser.add_argument('--devkitpro', default=os.environ.get('DEVKITPRO', '/opt/devkitpro'),
                        help='with --check-switch: the devkitPro root (its devkitA64 headers and libnx)')
    parser.add_argument('--posix-dxsdk', help='with --check-*: the D3D9 headers the posix/switch build copied')
    parser.add_argument('--stamp', help='with --check-*: touched when the tables are equal')
    opts = parser.parse_args()
    if sum((bool(opts.out), bool(opts.check_target64), bool(opts.check_switch))) != 1:
        parser.error('give exactly one of --out, --check-target64, --check-switch')
    checking = opts.check_target64 or opts.check_switch
    if checking and not (opts.posix_dxsdk and opts.stamp):
        parser.error('--check-target64 / --check-switch need --posix-dxsdk and --stamp')
    tu32, tu64 = parse(opts.target32, opts.dxsdk), parse(opts.target64, opts.dxsdk)
    label = opts.check_target64 or 'aarch64-none-elf (Switch)'
    try:
        tables = build(tu32, tu64)
        if checking:
            tu_check = parse_posix(opts.check_target64, opts.posix_dxsdk) if opts.check_target64 \
                else parse_switch(opts.devkitpro, opts.posix_dxsdk)
            checked = build(tu32, tu_check)
    except LayoutError as e:
        print(f'layouts.py: {e}', file=sys.stderr)
        return 1
    if checking:
        differ = sorted(set(tables) ^ set(checked)) + [n for n in tables if n in checked and tables[n] != checked[n]]
        if differ:
            print(f'layouts.py: {len(differ)} streamed types lay out differently on {label} than on '
                  f'{opts.target64}, so its build cannot read those zones: {", ".join(differ[:20])}', file=sys.stderr)
            return 1
        with open(opts.stamp, 'w', encoding='utf-8') as f:
            f.write(f'{len(tables)} streamed types lay out the same on {label} and {opts.target64}\n')
        if opts.depfile:
            write_depfile(opts.depfile, opts.stamp, (tu32, tu64, tu_check))
        return 0
    emit(tables, opts.target64, opts.out)
    if opts.depfile:
        write_depfile(opts.depfile, opts.out, (tu32, tu64))
    print(f'{len(tables)} types, {sum(len(t[5]) for t in tables.values())} value fixups, '
          f'{sum(len(t[6]) for t in tables.values())} widened integers -> {opts.out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
