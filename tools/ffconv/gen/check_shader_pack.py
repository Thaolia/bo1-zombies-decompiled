#!/usr/bin/env python3
"""Check a shaders.pack written by ffconv (src/gfx_d3d9emu/shader_pack.h): its layout, and every GLSL in it with
glslangValidator (OpenGL target, as the GL backend compiles it).

    tools/ffconv/gen/check_shader_pack.py OUT_DIR/shaders.pack [--jobs N]

Exit status is 0 only when the layout is sound and every shader validates.
"""
import argparse
import concurrent.futures
import os
import struct
import subprocess
import sys
import tempfile

HEADER = struct.Struct('<8sII')
ENTRY = struct.Struct('<QIIII')
STAGES = {0: 'vert', 1: 'frag'}


def read_pack(path):
    with open(path, 'rb') as f:
        data = f.read()
    magic, version, count = HEADER.unpack_from(data, 0)
    if magic != b'BO1GLSL1' or version != 1:
        raise SystemExit(f'{path}: not a version 1 shaders.pack')
    entries = [ENTRY.unpack_from(data, HEADER.size + i * ENTRY.size) for i in range(count)]
    hashes = [e[0] for e in entries]
    if hashes != sorted(hashes) or len(set(hashes)) != len(hashes):
        raise SystemExit(f'{path}: entries are not sorted by unique hash')
    shaders = []
    for hash_, stage, offset, size, _ in entries:
        if stage not in STAGES or offset + size >= len(data) or data[offset + size] != 0:
            raise SystemExit(f'{path}: entry {hash_:016x} is out of bounds or not NUL-terminated')
        shaders.append((hash_, STAGES[stage], data[offset:offset + size]))
    return shaders


def validate(work, shader):
    hash_, stage, text = shader
    path = os.path.join(work, f'{hash_:016x}.{stage}')
    with open(path, 'wb') as f:
        f.write(text)
    proc = subprocess.run(['glslangValidator', '--target-env', 'opengl', '-o', os.devnull, path],
                          capture_output=True, text=True)
    errors = [line for line in proc.stdout.splitlines() if line.startswith('ERROR')]
    return (f'{hash_:016x}.{stage}', errors[0] if errors else proc.stdout.strip()) if proc.returncode else None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('pack')
    parser.add_argument('--jobs', type=int, default=os.cpu_count())
    opts = parser.parse_args()
    shaders = read_pack(opts.pack)
    with tempfile.TemporaryDirectory() as work, concurrent.futures.ThreadPoolExecutor(opts.jobs) as pool:
        failures = [r for r in pool.map(lambda s: validate(work, s), shaders) if r]
    stages = {stage: sum(1 for s in shaders if s[1] == stage) for stage in STAGES.values()}
    print(f'{opts.pack}: {len(shaders)} shaders ({stages["vert"]} vertex, {stages["frag"]} pixel), '
          f'{len(failures)} rejected by glslangValidator')
    for name, error in failures:
        print(f'  {name}: {error}')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
