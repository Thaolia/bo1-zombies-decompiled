#!/bin/sh
# Installs clang, libclang and libclang's Python bindings of one LLVM version from apt.llvm.org, and makes it the
# `clang` on PATH. The layout tools (tools/ffconv/gen/layouts.py and the tools/*.py checks) need bindings that know
# every cursor kind libclang returns (C++20's requires-expressions: LLVM 19+), and libclang has to find the MinGW
# libstdc++ headers the distribution's g++-mingw-w64 installs; the stock clang of Ubuntu 24.04 (18) and of the
# devkitPro image do neither. Usage: tools/ci/install_llvm.sh [version], as root or through sudo.
set -eu
version=${1:-21}
sudo=
[ "$(id -u)" -eq 0 ] || sudo=sudo
. /etc/os-release
$sudo apt-get install -y -q curl gnupg ca-certificates
curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key | $sudo gpg --dearmor --yes -o /usr/share/keyrings/apt-llvm.gpg
echo "deb [signed-by=/usr/share/keyrings/apt-llvm.gpg] http://apt.llvm.org/$VERSION_CODENAME/" \
    "llvm-toolchain-$VERSION_CODENAME-$version main" | $sudo tee /etc/apt/sources.list.d/apt-llvm.list
$sudo apt-get update -q
$sudo apt-get install -y -q "clang-$version" "libclang1-$version" "python3-clang-$version"
for tool in clang clang++; do $sudo ln -sf "/usr/bin/$tool-$version" "/usr/local/bin/$tool"; done
clang --version | head -n 1
python3 -c "import clang.cindex as ci; ci.Index.create(); print('libclang', ci.conf.get_filename())"
