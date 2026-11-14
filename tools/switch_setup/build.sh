#!/bin/bash
# Builds make_dksh_pack.exe: tools/ffconv/gen/make_dksh_pack.py as one Windows executable (Nuitka), so the Switch
# package's setup_switch.ps1 compiles shaders.dksh without Python on the player's PC (bash, from WSL).
#
#   tools/switch_setup/build.sh
#
# Output: build/switch_setup/windows/make_dksh_pack.exe. Through powershell.exe, a venv of the Windows python3 in
# build\switch_setup\venv-win with nuitka, compiled by MSVC (Visual Studio Build Tools), as tools/zmadmin/build.sh.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
mkdir -p "$ROOT/build/switch_setup/windows"
win_root="$(wslpath -w "$ROOT")"
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "
    \$ErrorActionPreference = 'Stop'
    Set-Location '$win_root'
    if (-not (Test-Path build\\switch_setup\\venv-win\\Scripts\\python.exe)) { python3 -m venv build\\switch_setup\\venv-win }
    & build\\switch_setup\\venv-win\\Scripts\\python.exe -m pip install -q --upgrade nuitka zstandard ordered-set
    if (\$LASTEXITCODE) { exit \$LASTEXITCODE }
    & build\\switch_setup\\venv-win\\Scripts\\python.exe -m nuitka --onefile --assume-yes-for-downloads --remove-output \`
        --python-flag=no_site --python-flag=no_docstrings --msvc=latest --output-dir=build\\switch_setup\\out-win \`
        --output-filename=make_dksh_pack.exe tools\\ffconv\\gen\\make_dksh_pack.py
    if (\$LASTEXITCODE) { exit \$LASTEXITCODE }
    Copy-Item build\\switch_setup\\out-win\\make_dksh_pack.exe build\\switch_setup\\windows\\make_dksh_pack.exe -Force
    & build\\switch_setup\\windows\\make_dksh_pack.exe --help | Select-Object -First 1
    exit \$LASTEXITCODE
" < /dev/null | tr -d '\r'
