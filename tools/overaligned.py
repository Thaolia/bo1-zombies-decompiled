#!/usr/bin/env python3
"""List allocator results cast to a pointer to an over-aligned type (alignof above 8 on the build's target).

GCC and clang copy and clear such a type with aligned vector moves (`movaps`), which fault on a block the allocator
aligned less: `MatchState` is align(64) and its snapshot blocks were 4-aligned. MSVC never assumes more than the
block gives, so the retail code runs. Each hit prints the call and its arguments; the reader checks that the block
is aligned enough (an alignment argument of at least alignof, or an allocator that guarantees it) and otherwise
gives the call `BO1_ALLOC_ALIGN` (`src/platform/arch_size.h`).

    tools/overaligned.py BUILD_DIR [--jobs N] [--filter REGEX]    # BUILD_DIR: a configured 64-bit GNU build

Only results cast at the call are seen: a block kept in a `void *` or `char *` and cast later is out of reach, as are
pointers into a pool (`FX_EffectFromHandle`), which the pool's own alignment covers.
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
CASTS = (ci.CursorKind.CSTYLE_CAST_EXPR, ci.CursorKind.CXX_STATIC_CAST_EXPR, ci.CursorKind.CXX_REINTERPRET_CAST_EXPR)
WRAPPERS = (ci.CursorKind.UNEXPOSED_EXPR, ci.CursorKind.PAREN_EXPR)


def parse_args(entry):
    args = entry.get('arguments') or shlex.split(entry['command'])
    out, skip = [], False
    for arg in args[1:]:
        if skip:
            skip = False
            continue
        if arg in ('-o', '-MF', '-MT', '-MQ'):
            skip = True
            continue
        if arg in ('-c', '-MD', '-MMD', entry['file']) or arg.startswith('-Werror') or arg.startswith('-ferror-limit'):
            continue
        out.append(arg)
    return out + ['-w', '-ferror-limit=0', '-resource-dir', RESOURCE_DIR]


def strip(e):
    while e.kind in WRAPPERS:
        kids = list(e.get_children())
        if len(kids) != 1:
            break
        e = kids[0]
    return e


def check(entry):
    tu = ci.Index.create().parse(entry['file'], args=parse_args(entry))
    hits = []
    stack = [tu.cursor]
    while stack:
        node = stack.pop()
        stack.extend(node.get_children())
        if node.kind not in CASTS or not node.location.file:
            continue
        rel = os.path.relpath(node.location.file.name, ROOT)
        t = node.type.get_canonical()
        if not OWN_CODE.match(rel) or t.kind != ci.TypeKind.POINTER:
            continue
        pointee = t.get_pointee().get_canonical()
        align = pointee.get_align()
        operands = [k for k in node.get_children() if k.kind.is_expression()]
        if align <= 8 or not operands:
            continue
        call = strip(operands[-1])
        if call.kind == ci.CursorKind.CALL_EXPR:
            # A macro argument has no tokens of its own.
            spelled = [' '.join(tok.spelling for tok in a.get_tokens()) or '<macro>' for a in call.get_arguments()]
            hits.append((rel, node.location.line, pointee.spelling, align, call.spelling, ', '.join(spelled)))
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
    for rel, line, name, align, callee, args in sorted(hits):
        print(f'{rel}:{line}: {name} (align {align}) = {callee}({args})')
    print(f'{len(hits)} casts, {len(entries)} files', file=sys.stderr)


RESOURCE_DIR = subprocess.run(['clang', '-print-resource-dir'], capture_output=True, text=True).stdout.strip()

if __name__ == '__main__':
    main()
