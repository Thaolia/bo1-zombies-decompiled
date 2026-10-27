#!/bin/bash
# Black Ops 1 Zombies dedicated server on Linux (docs/SERVER.md): the linux64-clang build on the converted zones,
# listening on UDP on every interface (0.0.0.0). In a terminal it shows the curses console; started by a service or
# with its output redirected it keeps the plain console (stdout / stdin).
#   ./Server.sh [map] [port] [more + commands]      defaults: zombie_theater 3074
#   ./Server.sh zombie_cosmodrome 3074 +set g_password secret
# Paths (environment, defaults inside this repository):
#   BO1_EXE      the program            build/linux64-clang/Release/BO1Zombies
#   BO1_INSTALL  the game files (fs_b): localization.txt and main/*.iwd     build/Release
#   BO1_ZONES    ffconv's output: zone/ and shaders.pack                    build/zones64
#   BO1_SERVER   the server's folder (fs_h): main/server.cfg, main/console_mp.log    build/server-linux
# Keep the game files and zones on a Linux file system: read from /mnt/c (WSL) a map takes minutes to load.
set -eu
ROOT="$(cd "$(dirname "$0")" && pwd)"
EXE="${BO1_EXE:-$ROOT/build/linux64-clang/Release/BO1Zombies}"
INSTALL="${BO1_INSTALL:-$ROOT/build/Release}"
ZONES="${BO1_ZONES:-$ROOT/build/zones64}"
SERVER="${BO1_SERVER:-$ROOT/build/server-linux}"
MAP="${1:-zombie_theater}"
PORT="${2:-3074}"
shift $(( $# < 2 ? $# : 2 ))

for need in "$EXE" "$INSTALL/localization.txt" "$ZONES/zone" "$ZONES/shaders.pack"; do
    [ -e "$need" ] || { echo "not found: $need (see the paths at the top of $0)" >&2; exit 1; }
done
# The program loads zone/ from its own folder and writes its crash log next to itself: run a copy in the server folder.
mkdir -p "$SERVER/main"
cp -u "$EXE" "$SERVER/BO1Zombies"
ln -sfn "$ZONES/zone" "$SERVER/zone"
ln -sfn "$ZONES/shaders.pack" "$SERVER/shaders.pack"
ln -sfn "$INSTALL/localization.txt" "$SERVER/localization.txt"
cd "$SERVER"
exec ./BO1Zombies +set fs_b "$INSTALL" +set fs_h "$SERVER" +set bo1_server 1 +set net_ip 0.0.0.0 +set net_port "$PORT" "$@" +devmap "$MAP"
