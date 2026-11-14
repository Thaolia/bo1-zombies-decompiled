#!/bin/bash
# Builds uam, deko3d's offline shader compiler (https://github.com/devkitPro/uam), which compiles a Switch package's
# shaders.dksh (tools/ffconv/gen/make_dksh_pack.py --uam): for Windows, where devkitPro ships none, and for Linux
# without devkitPro (the Switch package's setup_switch.sh).
#
#   tools/uam/build.sh [windows] [linux]       default: both
#
# Output: build/uam/windows/uam.exe (MinGW-w64, only KERNEL32 and msvcrt) and build/uam/linux/uam (statically
# linked), with uam-LICENSE.txt beside each. The sources are uam's v1.1.0 tag, the version devkitPro's binary
# reports, fetched into $UAM_WORK (default ~/.cache/uam) and built there. Needs git, meson, ninja, bison, flex,
# python3-mako, gcc/g++ and, for windows, the x86_64 MinGW-w64 toolchain (mingw-w64).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="$ROOT/build/uam"
WORK="${UAM_WORK:-${XDG_CACHE_HOME:-$HOME/.cache}/uam}"
REPO=https://github.com/devkitPro/uam
COMMIT=5a5afc2bae8b55409ab36ba45be63fcb73f68993 # v1.1.0

targets=("$@")
[ ${#targets[@]} -gt 0 ] || targets=(windows linux)
for tool in git meson ninja bison flex; do
    command -v "$tool" > /dev/null || { echo "uam: $tool not found" >&2; exit 1; }
done
python3 -c 'import mako' 2> /dev/null || { echo "uam: python3's mako module not found (python3-mako)" >&2; exit 1; }

mkdir -p "$WORK"
if [ "$(git -C "$WORK/src" rev-parse HEAD 2> /dev/null)" != "$COMMIT" ]; then
    rm -rf "$WORK/src"
    git clone -q "$REPO" "$WORK/src"
    git -C "$WORK/src" -c advice.detachedHead=false checkout -q "$COMMIT"
fi
# meson.build checks mako's version with distutils, which Python 3.12 removed: compare it without distutils.
sed -i 's/^from distutils.version import StrictVersion$/StrictVersion = lambda v: tuple(int(x) for x in v.split(".")[:3])/' \
    "$WORK/src/meson.build"
cat > "$WORK/mingw64.ini" << 'INI'
[binaries]
c = 'x86_64-w64-mingw32-gcc'
cpp = 'x86_64-w64-mingw32-g++'
ar = 'x86_64-w64-mingw32-ar'
strip = 'x86_64-w64-mingw32-strip'
windres = 'x86_64-w64-mingw32-windres'

[built-in options]
c_link_args = ['-static']
cpp_link_args = ['-static']

[host_machine]
system = 'windows'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'
INI

build() { # <target> <meson setup options...>
    local target="$1"
    shift
    rm -rf "$WORK/build-$target"
    meson setup "$@" "$WORK/build-$target" "$WORK/src" > "$WORK/setup-$target.log" \
        || { tail -20 "$WORK/setup-$target.log" >&2; exit 1; }
    ninja -C "$WORK/build-$target" > "$WORK/build-$target.log" || { tail -20 "$WORK/build-$target.log" >&2; exit 1; }
}

for t in "${targets[@]}"; do
    case "$t" in
    windows)
        command -v x86_64-w64-mingw32-g++ > /dev/null || { echo "uam: x86_64-w64-mingw32-g++ not found" >&2; exit 1; }
        build windows --cross-file "$WORK/mingw64.ini"
        bin=uam.exe ;;
    linux)
        build linux -Dc_link_args=-static -Dcpp_link_args=-static
        bin=uam ;;
    *) echo "uam: unknown target '$t' (windows, linux)" >&2; exit 2 ;;
    esac
    mkdir -p "$OUT/$t"
    cp "$WORK/build-$t/$bin" "$OUT/$t/$bin"
    cp "$WORK/src/LICENSE" "$OUT/$t/uam-LICENSE.txt"
    echo "uam: $OUT/$t/$bin ($(stat -c %s "$OUT/$t/$bin") bytes)"
done
