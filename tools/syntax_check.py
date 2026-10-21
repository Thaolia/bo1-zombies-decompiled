#!/usr/bin/env python3
"""Run every compile command of a CMake build dir with -fsyntax-only and summarise the errors.

Phase 1 of the Switch port (docs/PORTING.md): configure a GNU-dialect preset (mingw32-gcc / mingw32-clang), then

    tools/syntax_check.py build/mingw32-gcc [--filter REGEX] [--exclude REGEX] [--jobs N] [--log-dir DIR]

Exit status is 0 only when no file reports an error.
"""
import argparse
import collections
import concurrent.futures
import json
import os
import re
import shlex
import subprocess
import sys

DIAG = re.compile(r"^(?P<file>[^:\n]+):(?P<line>\d+):(?:\d+:)? (?:fatal )?error: (?P<msg>.*)$")
QUOTED = re.compile(r"(‘[^’]*’|'[^']*'|\"[^\"]*\")")


def syntax_command(entry):
    args = entry.get("arguments") or shlex.split(entry["command"])
    out = []
    skip = False
    for arg in args:
        if skip:
            skip = False
            continue
        if arg == "-o":
            skip = True
            continue
        if arg == "-c":
            continue
        out.append(arg)
    out.insert(1, "-fsyntax-only")
    return out


def run(entry):
    proc = subprocess.run(syntax_command(entry), cwd=entry["directory"], capture_output=True, text=True,
                          errors="replace")
    return entry["file"], proc.returncode, proc.stderr


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("build_dir")
    parser.add_argument("--filter", help="only check files whose path matches this regex")
    parser.add_argument("--exclude", help="skip files whose path matches this regex")
    parser.add_argument("--jobs", type=int, default=os.cpu_count())
    parser.add_argument("--log-dir", help="write each failing file's full diagnostics here")
    parser.add_argument("--top", type=int, default=40, help="how many error kinds and files to list")
    opts = parser.parse_args()

    with open(os.path.join(opts.build_dir, "compile_commands.json"), encoding="utf-8") as f:
        entries = json.load(f)
    if opts.filter:
        entries = [e for e in entries if re.search(opts.filter, e["file"])]
    if opts.exclude:
        entries = [e for e in entries if not re.search(opts.exclude, e["file"])]

    root = os.path.commonpath([e["file"] for e in entries]) if entries else ""
    kinds = collections.Counter()
    origins = collections.Counter()
    per_file = {}
    with concurrent.futures.ThreadPoolExecutor(opts.jobs) as pool:
        for path, code, stderr in pool.map(run, entries):
            errors = [m for m in (DIAG.match(l) for l in stderr.splitlines()) if m]
            if code == 0 and not errors:
                continue
            rel = os.path.relpath(path, root)
            per_file[rel] = max(len(errors), 1)
            for m in errors:
                kinds[QUOTED.sub("'…'", m["msg"])] += 1
                origins[os.path.normpath(m["file"])] += 1
            if opts.log_dir:
                log = os.path.join(opts.log_dir, rel + ".log")
                os.makedirs(os.path.dirname(log), exist_ok=True)
                with open(log, "w", encoding="utf-8") as f:
                    f.write(stderr)

    print(f"{len(entries)} files checked, {len(per_file)} with errors, {sum(kinds.values())} errors")
    if per_file:
        print("\nerror kinds:")
        for msg, n in kinds.most_common(opts.top):
            print(f"  {n:6}  {msg}")
        print("\nerrors by the file they point at:")
        for path, n in origins.most_common(opts.top):
            print(f"  {n:6}  {path}")
    return 1 if per_file else 0


if __name__ == "__main__":
    sys.exit(main())
