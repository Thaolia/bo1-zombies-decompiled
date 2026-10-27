#!/usr/bin/env python3
"""ffconv --dksh: compile the GLSL in a shaders.pack to deko3d DKSH offline, so the deko3d backend loads ready
shader modules with no runtime GLSL compiler (the GL path's OOM, docs/PORTING.md).

    tools/ffconv/gen/make_dksh_pack.py OUT_DIR/shaders.pack OUT_DIR/shaders.dksh [--uam PATH] [--jobs N]

Reads the BO1GLSL1 pack, runs uam (deko3d's offline compiler) on each shader, and writes a BO1DKSH1 pack with the
same header/entry layout (shader_pack.h) so d3d9emu's ShaderPack reads it unchanged. Each payload is the DKSH
followed by one NUL byte, to satisfy the pack's existing NUL-termination check. A shader uam rejects is dropped
(listed at the end): the deko3d backend then skips draws that need it, as with a missing pack entry.
"""
import argparse
import concurrent.futures
import os
import platform
import shutil
import struct
import subprocess
import sys
import tempfile

HEADER = struct.Struct('<8sII')   # magic, version, count
ENTRY = struct.Struct('<QIIII')   # hash, stage, offset, size, reserved
STAGES = {0: 'vert', 1: 'frag'}
# uam's output depends on where its heap lands: with address randomisation 7 of 64 runs on one shader gave other code
# (2026-10-27, 1790 of 13704 shaders differed between two packs of the same GLSL), with it off 64 of 64 matched the
# pack checked on the console. Without setarch the pack still builds, but may differ from run to run.
NO_ASLR = ['setarch', platform.machine(), '-R'] if shutil.which('setarch') else []


def read_glsl_pack(path):
    with open(path, 'rb') as f:
        data = f.read()
    magic, version, count = HEADER.unpack_from(data, 0)
    if magic != b'BO1GLSL1' or version != 1:
        raise SystemExit(f'{path}: not a version 1 BO1GLSL1 shaders.pack')
    out = []
    for i in range(count):
        h, stage, offset, size, _ = ENTRY.unpack_from(data, HEADER.size + i * ENTRY.size)
        if stage not in STAGES:
            raise SystemExit(f'{path}: entry {h:016x} has unknown stage {stage}')
        out.append((h, stage, data[offset:offset + size]))
    return out


def compile_one(uam, work, shader):
    h, stage, glsl = shader
    src = os.path.join(work, f'{h:016x}.{STAGES[stage]}')
    dst = src + '.dksh'
    with open(src, 'wb') as f:
        f.write(glsl)
    proc = subprocess.run(NO_ASLR + [uam, '-s', STAGES[stage], src, '-o', dst], capture_output=True, text=True)
    if proc.returncode or not os.path.exists(dst):
        return (h, stage, None, (proc.stderr or proc.stdout).strip())
    with open(dst, 'rb') as f:
        return (h, stage, f.read(), None)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('glsl_pack')
    parser.add_argument('dksh_pack')
    parser.add_argument('--uam', default=os.path.join(os.environ.get('DEVKITPRO', '/opt/devkitpro'), 'tools/bin/uam'))
    parser.add_argument('--jobs', type=int, default=os.cpu_count())
    opts = parser.parse_args()
    if not os.path.exists(opts.uam):
        raise SystemExit(f'uam not found at {opts.uam} (set --uam or DEVKITPRO)')

    if not NO_ASLR:
        print('setarch not found: uam runs with address randomisation, the pack may differ between runs')
    shaders = read_glsl_pack(opts.glsl_pack)
    print(f'{opts.glsl_pack}: {len(shaders)} GLSL shaders -> compiling with {opts.uam} (jobs={opts.jobs})')
    with tempfile.TemporaryDirectory() as work, concurrent.futures.ThreadPoolExecutor(opts.jobs) as pool:
        results = list(pool.map(lambda s: compile_one(opts.uam, work, s), shaders))

    good = [(h, stage, dksh) for (h, stage, dksh, err) in results if dksh is not None]
    failures = [(h, stage, err) for (h, stage, dksh, err) in results if dksh is None]
    good.sort(key=lambda e: e[0])  # the pack is read with a binary search on the hash

    entries = bytearray()
    payloads = bytearray()
    base = HEADER.size + len(good) * ENTRY.size
    for h, stage, dksh in good:
        offset = base + len(payloads)
        entries += ENTRY.pack(h, stage, offset, len(dksh), 0)
        payloads += dksh + b'\x00'  # the NUL the pack's reader checks for; deko3d uses only the first `size` bytes
    with open(opts.dksh_pack, 'wb') as f:
        f.write(HEADER.pack(b'BO1DKSH1', 1, len(good)))
        f.write(entries)
        f.write(payloads)

    size_mb = (base + len(payloads)) / (1 << 20)
    print(f'{opts.dksh_pack}: {len(good)} DKSH shaders, {size_mb:.1f} MB, {len(failures)} rejected by uam')
    for h, stage, err in failures[:20]:
        print(f'  {h:016x}.{STAGES[stage]}: {err.splitlines()[0] if err else "(no message)"}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
