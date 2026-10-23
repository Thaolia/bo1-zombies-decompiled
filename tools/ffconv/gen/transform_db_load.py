#!/usr/bin/env python3
"""Write ffconv's copy of src/database/db_load.cpp with typed stream hooks.

    tools/ffconv/gen/transform_db_load.py SRC_DB_LOAD OUT_CPP

The engine's Load_* functions stay as they are; only three things change in the copy:
- `#include "ff_hooks.h"` and `#include "ff_layouts.gen.h"` after the engine includes;
- `Load_Stream(start, ptr, size)` becomes `FF_LoadStream(start, var, ptr, size)`, where `var` is the `var*` global the
  pointer is taken from, so the hook knows the C++ type being streamed and the field it starts at;
- `DB_ConvertOffsetToPointer(slot)`, in the `else` of an `if (ptr == -1) { inline branch }`, becomes
  `ff::ConvertOffsetToPointer(slot, layout, count, site, [&] { inline branch })`: the type and element count the
  inline branch streams (from its `var* =` assignment and its `Load_*(1[, count])` call; -1 for a string; READ_COUNTS
  where the engine reads another count through the pointer), and the inline branch itself, which ffconv runs when the retail target cannot serve as that pointee on 64-bit.
Any Load_Stream or DB_ConvertOffsetToPointer call the patterns do not recognise stops the script: a silent miss would
convert a struct without its type.
"""
import re
import sys

LOAD_STREAM = re.compile(r'\bLoad_Stream\((?P<start>[^,]+), (?P<ptr>[^;]*?), (?P<size>[^;]+)\);')
VAR = re.compile(r'\bvar[A-Za-z0-9_]+')
CONVERT = re.compile(r'^\s*DB_ConvertOffsetToPointer\((?P<slot>.*)\);\s*$')
ASSIGN = re.compile(r'^\s*(?P<var>var\w+) = ', re.M)
LOAD_INLINE = re.compile(r'\bLoad_\w+\(\s*1\s*(?:,\s*(?P<count>.*?))?\)\s*;', re.S)
STRING_LOAD = re.compile(r'\bLoad_(?:X|Temp)StringCustom\(')
# Slots whose inline branch streams another count than the engine reads through them. CollisionPartition::borders
# points into clipMap_t::borders (always an offset in retail zones) and the engine reads borderCount elements; with
# borderCount 0 it is a past-the-end pointer that nothing reads.
READ_COUNTS = {'varCollisionPartition->borders': 'varCollisionPartition->borderCount'}


def is_comment(line):
    return line.lstrip().startswith('//')


def inline_branch(lines, i):
    """The body lines of the `if` block whose `else` holds the call on line i."""
    j = i - 1
    while is_comment(lines[j]):
        j -= 1
    if lines[j].strip() != '{' or lines[j - 1].strip() != 'else' or lines[j - 2].strip() != '}':
        raise SystemExit(f'db_load.cpp:{i + 1}: DB_ConvertOffsetToPointer outside `}} else {{`')
    end = j - 2
    k, depth = end, 0
    while True:
        depth += lines[k].count('}') - lines[k].count('{')
        if depth == 0:
            break
        k -= 1
    if lines[k].strip() != '{' or not lines[k - 1].lstrip().startswith('if'):
        raise SystemExit(f'db_load.cpp:{i + 1}: the inline branch before the else is not an if block')
    return lines[k + 1:end]


def convert_call(lines, i):
    m = CONVERT.match(lines[i])
    body = inline_branch(lines, i)
    text = ''.join(body)
    if STRING_LOAD.search(text):
        pointee, count = 'char', '-1'
    else:
        assign = ASSIGN.search(text)
        load = LOAD_INLINE.search(text)
        if not assign or not load:
            raise SystemExit(f'db_load.cpp:{i + 1}: no var assignment or Load_*(1) call in the inline branch')
        var = assign.group('var')
        count = ' '.join((load.group('count') or '1').split())
        if re.search(rf'\b{var}\b', count):
            raise SystemExit(f'db_load.cpp:{i + 1}: the count reads {var}, which only the inline branch sets')
        pointee = f'std::remove_cv_t<std::remove_reference_t<decltype(*{var})>>'
        count = next((read for field, read in READ_COUNTS.items() if field in m.group('slot')), count)
    indent = lines[i][:len(lines[i]) - len(lines[i].lstrip())]
    slot = m.group('slot')
    out = [f'{indent}ff::ConvertOffsetToPointer((unsigned int *)({slot}), ff::Layout<{pointee}>(), {count}, '
           f'"{slot.replace(chr(34), "")}", [&] {{\n']
    out += body
    out.append(f'{indent}}});\n')
    return out


def transform(lines):
    out, includes_done, streams, converts = [], False, 0, 0
    last_include = max(i for i, line in enumerate(lines) if line.startswith('#include'))
    for i, line in enumerate(lines):
        if 'Load_Stream(' in line:
            m = LOAD_STREAM.search(line)
            var = VAR.search(m.group('ptr')) if m else None
            if not var:
                raise SystemExit(f'db_load.cpp:{i + 1}: unrecognised Load_Stream: {line.strip()}')
            line = line[:m.start()] + (f'FF_LoadStream({m.group("start")}, {var.group(0)}, {m.group("ptr")}, '
                                       f'{m.group("size")});') + line[m.end():]
            streams += 1
        if 'DB_ConvertOffsetToPointer(' in line and not is_comment(line):
            if not CONVERT.match(line):
                raise SystemExit(f'db_load.cpp:{i + 1}: unrecognised DB_ConvertOffsetToPointer: {line.strip()}')
            out += convert_call(lines, i)
            converts += 1
            continue
        out.append(line)
        if i == last_include and not includes_done:
            out.append('#include "ff_hooks.h"\n#include "ff_layouts.gen.h"\n')
            includes_done = True
    return out, streams, converts


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    with open(sys.argv[1], encoding='utf-8', errors='surrogateescape') as f:
        lines = f.readlines()
    out, streams, converts = transform(lines)
    with open(sys.argv[2], 'w', encoding='utf-8', errors='surrogateescape', newline='\n') as f:
        f.write(f'// Generated by tools/ffconv/gen/transform_db_load.py from {sys.argv[1]}. Do not edit.\n')
        f.writelines(out)
    print(f'{streams} Load_Stream calls and {converts} DB_ConvertOffsetToPointer calls hooked -> {sys.argv[2]}')


if __name__ == '__main__':
    main()
