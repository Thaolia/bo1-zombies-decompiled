#!/usr/bin/env python3
"""Compare the 32-bit machine code of changed source files with their version at a git ref.

Phase 3 changes must not change what the 32-bit build does. For each file, this compiles the version at REF and the
working-tree version with the compile command of a GNU 32-bit build directory (mingw32-gcc), disassembles both
objects and diffs the instructions. A pointer kept in `uintptr_t` instead of `unsigned int` gives identical code.

    tools/codediff.py build/mingw32-gcc [--ref HEAD] FILE...       # FILE relative to the repository root

The REF version is compiled inside a copy of the source tree at REF (git archive, cached under
BUILD_DIR/codediff/<sha>), so it sees its own headers. Only .text is compared; a change that moves lines changes the
line numbers the asserts pass, which shows as an immediate operand difference. Exit status is 0 only when every
file's code is identical.
"""
import argparse
import difflib
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
ADDRESS = re.compile(r'^\s*[0-9a-f]+:\s*')
TARGET = re.compile(r'\b[0-9a-f]+ <([^>]+)>')


def command_for(entries, path):
    for entry in entries:
        if os.path.normpath(entry['file']) == path:
            return entry.get('arguments') or shlex.split(entry['command'])
    return None


def compile_to(args, source, obj, root, extra):
    """Compiles `source` (repository-relative) under `root` with the build's command, its paths moved to `root`."""
    out = []
    skip = False
    for arg in args:
        if skip:
            skip = False
            continue
        if arg == '-o':
            skip = True
            continue
        if arg == '-c' or os.path.normpath(arg) == os.path.join(ROOT, source):
            continue
        out.append(arg.replace(ROOT, root))
    out += extra + ['-c', os.path.join(root, source), '-o', obj]
    proc = subprocess.run(out, capture_output=True, text=True)
    if proc.returncode:
        raise RuntimeError(proc.stderr[-2000:])


def tree_at(ref, cache):
    """The source tree at `ref`, extracted once."""
    sha = subprocess.run(['git', '-C', ROOT, 'rev-parse', ref], capture_output=True, text=True,
                         check=True).stdout.strip()
    root = os.path.join(cache, sha)
    if not os.path.isdir(root):
        os.makedirs(root + '.tmp', exist_ok=True)
        archive = subprocess.run(['git', '-C', ROOT, 'archive', sha, 'src', 'tl', 'DemonWare', 'third_party', 'tools'],
                                 capture_output=True, check=True).stdout
        subprocess.run(['tar', '-x', '-C', root + '.tmp'], input=archive, check=True)
        os.rename(root + '.tmp', root)
    return root


def disassemble(objdump, obj):
    text = subprocess.run([objdump, '-d', '--no-show-raw-insn', '-C', '-j', '.text', obj], capture_output=True,
                          text=True, check=True).stdout
    lines = []
    for line in text.splitlines():
        if not line.strip() or line.startswith(obj) or 'file format' in line:
            continue
        line = ADDRESS.sub('', line)
        lines.append(TARGET.sub(r'<\1>', line))
    return lines


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('build_dir')
    parser.add_argument('--ref', default='HEAD')
    parser.add_argument('--objdump', default='i686-w64-mingw32-objdump')
    parser.add_argument('--max-lines', type=int, default=60, help='diff lines printed per file')
    parser.add_argument('--cflags', default='', help='flags appended to both compiles, e.g. "-O0" to take the '
                        'optimiser\'s scheduling out of a difference')
    parser.add_argument('files', nargs='+')
    opts = parser.parse_args()
    with open(os.path.join(opts.build_dir, 'compile_commands.json'), encoding='utf-8') as f:
        entries = json.load(f)
    changed = 0
    base_root = tree_at(opts.ref, os.path.abspath(os.path.join(opts.build_dir, 'codediff')))
    with tempfile.TemporaryDirectory() as work:
        for rel in opts.files:
            rel = os.path.normpath(rel)
            args = command_for(entries, os.path.join(ROOT, rel))
            if not args:
                print(f'{rel}: not in {opts.build_dir}/compile_commands.json')
                changed += 1
                continue
            base_obj = os.path.join(work, 'base.o')
            new_obj = os.path.join(work, 'new.o')
            compile_to(args, rel, base_obj, base_root, shlex.split(opts.cflags))
            compile_to(args, rel, new_obj, ROOT, shlex.split(opts.cflags))
            base, new = disassemble(opts.objdump, base_obj), disassemble(opts.objdump, new_obj)
            if base == new:
                print(f'{rel}: identical ({len(new)} lines)')
                continue
            changed += 1
            diff = list(difflib.unified_diff(base, new, 'base', 'new', n=1, lineterm=''))
            print(f'{rel}: {sum(1 for d in diff if d.startswith(("+", "-")) and not d.startswith(("+++", "---")))} '
                  f'lines differ')
            for line in diff[:opts.max_lines]:
                print('    ' + line)
    return 1 if changed else 0


if __name__ == '__main__':
    sys.exit(main())
