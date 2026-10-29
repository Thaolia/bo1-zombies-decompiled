#!/bin/bash
# Builds ZmAdmin into one executable per platform with Nuitka (bash, from WSL or Linux).
#
#   tools/zmadmin/build.sh [linux] [windows]       default: both
#
# Output: build/zmadmin/linux/zmadmin and build/zmadmin/windows/zmadmin.exe.
# - Linux: a venv and a copy of the sources in $ZMADMIN_WORK (ext4; default ~/.cache/zmadmin): from /mnt/c every file
#   read waits on 9p. Needs python3-venv, gcc and patchelf.
# - Windows: through powershell.exe, a venv of the Windows python3 (3.13) in build\zmadmin\venv-win with nuitka and
#   windows-curses (the standard library has no curses on Windows), compiled by MSVC (Visual Studio Build Tools).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="$ROOT/build/zmadmin"
WORK="${ZMADMIN_WORK:-${XDG_CACHE_HOME:-$HOME/.cache}/zmadmin}"
NUITKA_ARGS=(--onefile --assume-yes-for-downloads --remove-output --include-package=zmadmin
             --python-flag=no_site --python-flag=no_docstrings)

targets=("$@")
[ ${#targets[@]} -gt 0 ] || targets=(linux windows)

build_linux() {
    echo "== zmadmin: linux"
    [ -x "$WORK/venv-linux/bin/python" ] || python3 -m venv "$WORK/venv-linux"
    "$WORK/venv-linux/bin/python" -m pip install -q --upgrade nuitka zstandard ordered-set
    rsync -a --delete --exclude __pycache__ "$HERE/" "$WORK/src/"
    mkdir -p "$OUT/linux"
    (cd "$WORK/src" && "$WORK/venv-linux/bin/python" -m nuitka "${NUITKA_ARGS[@]}" \
        --output-dir="$WORK/out-linux" --output-filename=zmadmin main.py)
    cp "$WORK/out-linux/zmadmin" "$OUT/linux/zmadmin"
    "$OUT/linux/zmadmin" --version
}

build_windows() {
    echo "== zmadmin: windows"
    mkdir -p "$OUT/windows"
    local win_root
    win_root="$(wslpath -w "$ROOT")"
    # PowerShell's own errors must fail the build: $ErrorActionPreference plus an exit code check after each step
    powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "
        \$ErrorActionPreference = 'Stop'
        Set-Location '$win_root'
        if (-not (Test-Path build\\zmadmin\\venv-win\\Scripts\\python.exe)) { python3 -m venv build\\zmadmin\\venv-win }
        & build\\zmadmin\\venv-win\\Scripts\\python.exe -m pip install -q --upgrade nuitka windows-curses zstandard ordered-set
        if (\$LASTEXITCODE) { exit \$LASTEXITCODE }
        Set-Location tools\\zmadmin
        & ..\\..\\build\\zmadmin\\venv-win\\Scripts\\python.exe -m nuitka ${NUITKA_ARGS[*]} --msvc=latest \`
            --include-module=_curses --output-dir=..\\..\\build\\zmadmin\\out-win --output-filename=zmadmin.exe main.py
        if (\$LASTEXITCODE) { exit \$LASTEXITCODE }
        Copy-Item ..\\..\\build\\zmadmin\\out-win\\zmadmin.exe ..\\..\\build\\zmadmin\\windows\\zmadmin.exe -Force
        & ..\\..\\build\\zmadmin\\windows\\zmadmin.exe --version
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
