#!/bin/bash
# Builds ZmClient into one executable per platform with Nuitka (bash, from WSL or Linux), as tools/zmadmin/build.sh
# does for ZmAdmin. ZmClient imports ZmAdmin's package (tools/zmadmin/zmadmin): both go into the binary.
#
#   tools/zmclient/build.sh [linux] [windows]       default: both
#
# Output: build/zmclient/linux/zmclient and build/zmclient/windows/zmclient.exe.
# - Linux: a venv and a copy of the sources in $ZMCLIENT_WORK (ext4; default ~/.cache/zmclient): from /mnt/c every
#   file read waits on 9p. Needs python3-venv, gcc and patchelf.
# - Windows: through powershell.exe, a venv of the Windows python3 (3.13) in build\zmclient\venv-win with nuitka and
#   windows-curses (the standard library has no curses on Windows), compiled by MSVC (Visual Studio Build Tools).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="$ROOT/build/zmclient"
WORK="${ZMCLIENT_WORK:-${XDG_CACHE_HOME:-$HOME/.cache}/zmclient}"
NUITKA_ARGS=(--onefile --assume-yes-for-downloads --remove-output --include-package=zmclient --include-package=zmadmin
             --python-flag=no_site --python-flag=no_docstrings)

targets=("$@")
[ ${#targets[@]} -gt 0 ] || targets=(linux windows)

build_linux() {
    echo "== zmclient: linux"
    [ -x "$WORK/venv-linux/bin/python" ] || python3 -m venv "$WORK/venv-linux"
    "$WORK/venv-linux/bin/python" -m pip install -q --upgrade nuitka zstandard ordered-set
    rsync -a --delete --exclude __pycache__ "$HERE/" "$WORK/src/"
    rsync -a --delete --exclude __pycache__ "$ROOT/tools/zmadmin/zmadmin/" "$WORK/src/zmadmin/"
    mkdir -p "$OUT/linux"
    (cd "$WORK/src" && "$WORK/venv-linux/bin/python" -m nuitka "${NUITKA_ARGS[@]}" \
        --output-dir="$WORK/out-linux" --output-filename=zmclient main.py)
    cp "$WORK/out-linux/zmclient" "$OUT/linux/zmclient"
    "$OUT/linux/zmclient" --version
}

build_windows() {
    echo "== zmclient: windows"
    mkdir -p "$OUT/windows"
    local win_root
    win_root="$(wslpath -w "$ROOT")"
    # PowerShell's own errors must fail the build: $ErrorActionPreference plus an exit code check after each step.
    # PYTHONPATH: Nuitka finds the zmadmin package next to tools\zmclient.
    powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "
        \$ErrorActionPreference = 'Stop'
        Set-Location '$win_root'
        if (-not (Test-Path build\\zmclient\\venv-win\\Scripts\\python.exe)) { python3 -m venv build\\zmclient\\venv-win }
        & build\\zmclient\\venv-win\\Scripts\\python.exe -m pip install -q --upgrade nuitka windows-curses zstandard ordered-set
        if (\$LASTEXITCODE) { exit \$LASTEXITCODE }
        \$env:PYTHONPATH = (Resolve-Path tools\\zmadmin).Path
        Set-Location tools\\zmclient
        & ..\\..\\build\\zmclient\\venv-win\\Scripts\\python.exe -m nuitka ${NUITKA_ARGS[*]} --msvc=latest \`
            --include-module=_curses --output-dir=..\\..\\build\\zmclient\\out-win --output-filename=zmclient.exe main.py
        if (\$LASTEXITCODE) { exit \$LASTEXITCODE }
        Copy-Item ..\\..\\build\\zmclient\\out-win\\zmclient.exe ..\\..\\build\\zmclient\\windows\\zmclient.exe -Force
        & ..\\..\\build\\zmclient\\windows\\zmclient.exe --version
        exit \$LASTEXITCODE
    " | tr -d '\r'
}

for t in "${targets[@]}"; do
    case "$t" in
    linux) build_linux ;;
    windows) build_windows ;;
    *) echo "build.sh: unknown target '$t' (linux, windows)" >&2; exit 2 ;;
    esac
done
