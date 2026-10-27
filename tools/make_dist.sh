#!/bin/bash
# Build the port and package it as ready-to-copy folders and archives, from a source tree where setup.ps1 has copied
# the game into build/Release (README.md, "Packages"):
#   windows  BO1Zombies_windows/   the 64-bit client and dedicated server (Play.bat, Server.bat)
#   linux    BO1Zombies_linux/     the Linux client and dedicated server (play.sh, server.sh)
#   switch   BO1Zombies_switch/    an SD card tree: switch/bo1zm/ with the .nro and its data
#
#   tools/make_dist.sh [options] [windows] [linux] [switch]        no platform: all three
#
#   --out DIR        packages and archives                          default: build/dist
#   --work DIR       ext4 folder for the Linux and Switch builds    default: ~/.cache/bo1zm-dist
#   --game DIR       the game files setup.ps1 copied                 default: build/Release
#   --zones DIR      ffconv's output (zone/, shaders.pack)           default: build/zones64
#   --dxsdk DIR      June 2010 DirectX SDK root (Include/)           default: /mnt/c/SDKs/DXSDK_Jun10/DXSDK
#   --archive KIND   auto | zip | tar | none     auto: zip for windows and switch, tar.gz for linux
#   --no-build       package what is built; fail if anything is missing or out of date
#   --jobs N         parallel jobs                                   default: the CPU count
#
# What it builds when needed (unless --no-build): the win64-msvc preset (Visual Studio, from WSL), the Linux and
# Switch programs (in --work, on ext4: from /mnt/c every header read waits on 9p), the converted zones when they are
# missing or older than the converter's sources (ffconv, about 6 minutes, from WSL), and shaders.dksh when the
# shaders.pack it came from changed. The windows package and the conversion need WSL (they run Windows programs).
#
# The packages hold your own game files: keep them for your own devices, never publish them.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/dist"
WORK="${XDG_CACHE_HOME:-$HOME/.cache}/bo1zm-dist"
GAME="$ROOT/build/Release"
ZONES="$ROOT/build/zones64"
DXSDK="/mnt/c/SDKs/DXSDK_Jun10/DXSDK"
ARCHIVE=auto
BUILD=1
JOBS="$(nproc)"
PLATFORMS=()
CMAKE_EXE="${CMAKE_EXE:-/mnt/c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe}"
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"

usage() { awk 'NR > 1 && !/^#/ { exit } NR > 1 { sub(/^# ?/, ""); print }' "$0"; exit "${1:-0}"; }
die() { echo "make_dist: $*" >&2; exit 1; }
step() { echo; echo "== $*"; }
abspath() { mkdir -p "$1" && (cd "$1" && pwd); }

while [ $# -gt 0 ]; do
    case "$1" in
    windows | linux | switch) PLATFORMS+=("$1") ;;
    all) PLATFORMS+=(windows linux switch) ;;
    --out) OUT="$2"; shift ;;
    --work) WORK="$2"; shift ;;
    --game) GAME="$2"; shift ;;
    --zones) ZONES="$2"; shift ;;
    --dxsdk) DXSDK="$2"; shift ;;
    --archive) ARCHIVE="$2"; shift ;;
    --no-build) BUILD=0 ;;
    --jobs) JOBS="$2"; shift ;;
    -h | --help) usage ;;
    *) echo "make_dist: unknown argument: $1" >&2; usage 2 ;;
    esac
    shift
done
[ ${#PLATFORMS[@]} -gt 0 ] || PLATFORMS=(windows linux switch)
mapfile -t PLATFORMS < <(printf '%s\n' "${PLATFORMS[@]}" | awk '!seen[$0]++')
case "$ARCHIVE" in auto | zip | tar | none) ;; *) die "--archive takes auto, zip, tar or none" ;; esac
wants() { local p; for p in "${PLATFORMS[@]}"; do [ "$p" = "$1" ] && return 0; done; return 1; }

OUT="$(abspath "$OUT")"
WORK="$(abspath "$WORK")"
LOGS="$OUT/logs"
mkdir -p "$LOGS"
VERSION="$(git -C "$ROOT" describe --always --dirty 2>/dev/null || echo unknown)"

# A step's full output goes to its log; on failure the tail is shown.
run_logged() {
    local name="$1"; shift
    if ! "$@" > "$LOGS/$name.log" 2>&1; then
        tail -30 "$LOGS/$name.log" >&2
        die "$name failed (full log: $LOGS/$name.log)"
    fi
}

have_wsl() { [ -e /proc/sys/fs/binfmt_misc/WSLInterop ] && command -v wslpath > /dev/null; }

# ---------------------------------------------------------------- inputs
[ -f "$GAME/localization.txt" ] && ls "$GAME"/main/*.iwd > /dev/null 2>&1 ||
    die "no game files in $GAME: run setup.ps1 first (or pass --game)"

# The zones are stale when a converter source is newer than their shaders.pack: the pack must match the engine's
# shader interface (input locations, constant layout), or the renderers read the wrong inputs.
FFCONV_INPUTS=("$ROOT/tools/ffconv/src" "$ROOT/tools/ffconv/CMakeLists.txt" "$ROOT/tools/ffconv/gen/layouts.py"
    "$ROOT/tools/ffconv/gen/transform_db_load.py" "$ROOT/src/database/db_load.cpp" "$ROOT/src/database/db_ff64.h"
    "$ROOT/src/gfx_d3d9emu/shader_pack.h")
zones_state() {
    [ -d "$ZONES/zone" ] && [ -f "$ZONES/shaders.pack" ] || { echo missing; return; }
    if [ -n "$(find "${FFCONV_INPUTS[@]}" -type f -not -path '*/__pycache__/*' -newer "$ZONES/shaders.pack" -print -quit)" ]; then
        echo stale
    else
        echo ok
    fi
}

convert_zones() {
    have_wsl || die "the zone conversion runs ffconv.exe and needs WSL; convert on a Windows PC, then pass --zones"
    step "ffconv: building the converter"
    if [ ! -f "$ROOT/build/ffconv/CMakeCache.txt" ]; then
        run_logged ffconv-configure bash -c "cd '$ROOT/tools/ffconv' && cmake --preset ffconv"
    fi
    run_logged ffconv-build bash -c "cd '$ROOT/tools/ffconv' && cmake --build --preset ffconv -j '$JOBS'"
    local next="$ZONES.new"
    rm -rf "$next"
    mkdir -p "$next"
    step "ffconv: converting the zombies zones (about 6 minutes)"
    # < /dev/null: a Windows program run from WSL otherwise reads the caller's stdin.
    run_logged ffconv-run "$ROOT/build/ffconv/ffconv.exe" --verify --game "$(wslpath -w "$GAME")" \
        --out "$(wslpath -w "$next")" < /dev/null
    tail -3 "$LOGS/ffconv-run.log"
    if command -v glslangValidator > /dev/null; then
        step "ffconv: validating shaders.pack with glslangValidator"
        run_logged shader-check python3 "$ROOT/tools/ffconv/gen/check_shader_pack.py" "$next/shaders.pack" --jobs "$JOBS"
        tail -1 "$LOGS/shader-check.log"
    else
        echo "glslangValidator not found: shaders.pack not validated"
    fi
    # Swap only once the new zones passed, so a failed run leaves the old ones in place.
    rm -rf "$ZONES.old"
    [ -e "$ZONES" ] && mv "$ZONES" "$ZONES.old"
    mv "$next" "$ZONES"
    rm -rf "$ZONES.old"
}

step "zones: $ZONES"
state="$(zones_state)"
case "$state" in
ok) echo "up to date" ;;
*)
    [ "$BUILD" = 1 ] || die "zones are $state in $ZONES (run without --no-build to convert them)"
    echo "$state: converting"
    convert_zones
    ;;
esac

# ---------------------------------------------------------------- builds
mirror_sources() {
    # Every build here reads the same snapshot of the tree.
    [ -n "${MIRRORED:-}" ] && return
    rsync -a --delete --exclude=/build/ --exclude=/.git/ --exclude=/docs/ --exclude=/images/ "$ROOT/" "$WORK/src/"
    MIRRORED=1
}

launcher_flags() {
    command -v ccache > /dev/null && echo "-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache"
    return 0
}

build_windows() {
    have_wsl || die "the windows package builds with Visual Studio and needs WSL"
    case "$ROOT" in /mnt/?/*) ;; *) die "the windows build needs the sources on a Windows drive (/mnt/c/...), not $ROOT" ;; esac
    [ -x "$CMAKE_EXE" ] || die "Visual Studio's cmake.exe not found (set CMAKE_EXE): $CMAKE_EXE"
    step "windows: building win64-msvc"
    if [ ! -f "$ROOT/build/win64-msvc/CMakeCache.txt" ]; then
        run_logged win64-configure bash -c "cd '$ROOT' && '$CMAKE_EXE' --preset win64-msvc -DDXSDK_DIR='$(wslpath -m "$DXSDK")'"
    fi
    run_logged win64-build bash -c "cd '$ROOT' && '$CMAKE_EXE' --build --preset win64-msvc-release"
    grep -a 'BO1Zombies.vcxproj ->' "$LOGS/win64-build.log" | tail -1 || true
}

build_linux() {
    step "linux: building in $WORK/linux"
    mirror_sources
    if [ ! -f "$WORK/linux/build.ninja" ]; then
        # shellcheck disable=SC2046
        run_logged linux-configure cmake -S "$WORK/src" -B "$WORK/linux" -G Ninja -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DDXSDK_DIR="$DXSDK" $(launcher_flags)
    fi
    run_logged linux-build ninja -C "$WORK/linux" -j "$JOBS" BO1Zombies
    echo "built $WORK/linux/Release/BO1Zombies"
}

build_switch() {
    [ -f "$DEVKITPRO/cmake/Switch.cmake" ] || die "devkitPro not found in $DEVKITPRO (set DEVKITPRO)"
    step "switch: building in $WORK/switch"
    mirror_sources
    if [ ! -f "$WORK/switch/build.ninja" ]; then
        # shellcheck disable=SC2046
        run_logged switch-configure cmake -S "$WORK/src" -B "$WORK/switch" -G Ninja -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Switch.cmake" -DDXSDK_DIR="$DXSDK" $(launcher_flags)
    fi
    run_logged switch-build ninja -C "$WORK/switch" -j "$JOBS" BO1Zombies
    echo "built $WORK/switch/Release/BO1Zombies.nro ($(cat "$WORK/switch/romfs/build.txt"))"
}

# shaders.dksh is uam's compilation of shaders.pack; the md5s of the pack, the tool and uam that made it are kept
# beside it, so a change to any of them compiles it again.
build_dksh() {
    local dir="$WORK/dksh" md5
    md5="$(cat "$ZONES/shaders.pack" "$ROOT/tools/ffconv/gen/make_dksh_pack.py" "$DEVKITPRO/tools/bin/uam" | md5sum | cut -d' ' -f1)"
    if [ -f "$dir/shaders.dksh" ] && [ "$(cat "$dir/shaders.dksh.from" 2>/dev/null)" = "$md5" ]; then
        echo "shaders.dksh up to date"
        return
    fi
    [ "$BUILD" = 1 ] || die "shaders.dksh is missing or older than shaders.pack (run without --no-build)"
    [ -x "$DEVKITPRO/tools/bin/uam" ] || die "uam not found in $DEVKITPRO/tools/bin (devkitPro's deko3d tools)"
    step "switch: compiling shaders.dksh with uam"
    mkdir -p "$dir"
    cp "$ZONES/shaders.pack" "$dir/shaders.pack"
    run_logged dksh python3 "$ROOT/tools/ffconv/gen/make_dksh_pack.py" "$dir/shaders.pack" "$dir/shaders.dksh.tmp" \
        --jobs "$JOBS"
    local summary
    summary="$(grep -a 'rejected by uam' "$LOGS/dksh.log" | tail -1 || true)"
    echo "$summary"
    # A shader uam rejects makes the deko3d backend skip every draw that needs it: missing walls on the console.
    case "$summary" in *" 0 rejected by uam"*) ;; *) die "uam rejected shaders (log: $LOGS/dksh.log)" ;; esac
    mv "$dir/shaders.dksh.tmp" "$dir/shaders.dksh"
    echo "$md5" > "$dir/shaders.dksh.from"
}

need() { [ -e "$1" ] || die "$2 not found: $1 (run without --no-build to build it)"; }

if wants windows; then [ "$BUILD" = 1 ] && build_windows; need "$ROOT/build/win64-msvc/Release/BO1Zombies.exe" "the win64 program"; fi
if wants linux; then [ "$BUILD" = 1 ] && build_linux; need "$WORK/linux/Release/BO1Zombies" "the Linux program"; fi
if wants switch; then
    [ "$BUILD" = 1 ] && build_switch
    need "$WORK/switch/Release/BO1Zombies.nro" "the Switch program"
    build_dksh
fi

# ---------------------------------------------------------------- packages
# Copies are incremental (rsync): packaging again after a rebuild rewrites only what changed.
game_data() { # <dest> [video]: the game files every platform reads, from the install copy
    mkdir -p "$1/main"
    rsync -a "$GAME/localization.txt" "$1/"
    rsync -a --delete --include='*.iwd' --exclude='*' "$GAME/main/" "$1/main/"
    if [ "${2:-}" = video ]; then rsync -a --delete "$GAME/main/video/" "$1/main/video/"; fi
    rsync -a --delete "$ZONES/zone/" "$1/zone/"
}

check_space() { # <dest dir>: each package is about the game data's size, and as much again when archived
    local per need avail have factor=2
    [ "$ARCHIVE" = none ] && factor=1
    per=$(du -sbc "$GAME"/main/*.iwd "$GAME/main/video" "$ZONES/zone" | tail -1 | cut -f1)
    need=$(( per * ${#PLATFORMS[@]} * factor ))
    # What an earlier run left is updated in place, not written again.
    have=$(du -sbc "$1"/BO1Zombies_* 2>/dev/null | tail -1 | cut -f1 || true)
    need=$(( need - ${have:-0} ))
    avail=$(df --output=avail -B1 "$1" | tail -1)
    if [ "$avail" -lt "$need" ]; then
        die "$(( need >> 30 )) GB may be needed in $1, $(( avail >> 30 )) GB free (try --out elsewhere or --archive none)"
    fi
}

write_version() { # <dest> <platform> [extra line]
    {
        echo "BO1 Zombies port, $2 package"
        echo "sources: $VERSION"
        echo "packaged: $(date '+%Y-%m-%d %H:%M %z')"
        if [ -n "${3:-}" ]; then echo "$3"; fi
    } > "$1/VERSION.txt"
}

copy_windows_runtimes() { # <dest>: what a Windows without Visual C++ or the DirectX runtime lacks, 64-bit
    local sys=/mnt/c/Windows/System32 crt f missing=()
    crt="$(ls -d /mnt/c/Program\ Files*/Microsoft\ Visual\ Studio/*/*/VC/Redist/MSVC/*/x64/Microsoft.VC14*.CRT 2>/dev/null | sort | tail -1 || true)"
    for f in msvcp140.dll vcruntime140.dll vcruntime140_1.dll; do
        if [ -n "$crt" ] && [ -f "$crt/$f" ]; then cp "$crt/$f" "$1/"
        elif [ -f "$sys/$f" ]; then cp "$sys/$f" "$1/"
        else missing+=("$f"); fi
    done
    for f in d3dx9_43.dll xinput1_3.dll; do
        if [ -f "$sys/$f" ]; then cp "$sys/$f" "$1/"; else missing+=("$f"); fi
    done
    [ ${#missing[@]} -eq 0 ] || echo "not found, the player needs the runtimes the README names: ${missing[*]}"
}

package_windows() {
    local d="$OUT/BO1Zombies_windows"
    step "windows: $d"
    mkdir -p "$d/main"
    cp "$ROOT/build/win64-msvc/Release/BO1Zombies.exe" "$d/"
    copy_windows_runtimes "$d"
    rsync -a --delete "$ROOT/mods/" "$d/mods/"
    rsync -a "$ROOT/data/main/" "$d/main/"
    game_data "$d" video
    cat > "$d/Play.bat" << 'EOF'
@echo off
rem Black Ops 1 Zombies, 64-bit. "Play.bat" opens the game's menu (Zombies, Solo, a map).
rem "Play.bat net <address[:port]> [name]" joins a dedicated server once the menu is up, and again whenever the game
rem is back at the menu. Example: Play.bat net 192.168.1.20:3074 Player2
setlocal
set "HERE=%~dp0"
set "HERE=%HERE:~0,-1%"
set "NETARGS=+set net_ip 127.0.0.1"
if /i not "%~1"=="net" goto run
if "%~2"=="" (echo usage: Play.bat net ^<address[:port]^> [name] & pause & exit /b 1)
set "NETARGS=+set cl_autoConnect %~2"
if not "%~3"=="" set "NETARGS=%NETARGS% +set name %~3"
:run
cd /d "%HERE%"
start "" "%HERE%\BO1Zombies.exe" +set bo1_zombies 1 +set fs_b "%HERE%" +set fs_h "%HERE%" +set logfile 2 %NETARGS% +set r_fullscreen 0 +set r_borderless 1 +set vid_xpos 0 +set vid_ypos 0
exit /b 0
EOF
    cat > "$d/Server.bat" << 'EOF'
@echo off
rem Black Ops 1 Zombies dedicated server, listening on UDP on every interface. It opens its own console window.
rem   Server.bat [map] [port] [more + commands]      defaults: zombie_theater 3074
rem   Server.bat zombie_cosmodrome 3074 +set g_password secret
rem Settings: server\main\server.cfg; log: server\main\console_mp.log. Open the UDP port in the Windows firewall for
rem players on other machines: the server shows no firewall prompt.
setlocal
set "HERE=%~dp0"
set "HERE=%HERE:~0,-1%"
set "MAP=%~1"
if "%MAP%"=="" set "MAP=zombie_theater"
set "PORT=%~2"
if "%PORT%"=="" set "PORT=3074"
set "MORE="
shift
shift
:more
if "%~1"=="" goto run
set "MORE=%MORE% %1"
shift
goto more
:run
if not exist "%HERE%\server\main" mkdir "%HERE%\server\main"
start "" /D "%HERE%" "%HERE%\BO1Zombies.exe" +set fs_b "%HERE%" +set fs_h "%HERE%\server" +set bo1_server 1 +set net_ip 0.0.0.0 +set net_port %PORT%%MORE% +devmap %MAP%
exit /b 0
EOF
    sed -i 's/$/\r/' "$d/Play.bat" "$d/Server.bat"
    cat > "$d/LISEZMOI.txt" << 'EOF'
BO1 Zombies, portage 64 bits pour Windows : client et serveur dédié
===================================================================

JOUER       Play.bat           ouvre le menu du jeu (Zombies, Solo, une map).
REJOINDRE   Play.bat net 192.168.1.20:3074 MonNom
            rejoint un serveur dédié dès que le menu est prêt, et y revient tout seul après une coupure.
SERVEUR     Server.bat [map] [port]          par défaut : zombie_theater 3074
            Réglages dans server\main\server.cfg, journal dans server\main\console_mp.log.
            Ouvrez le port UDP dans le pare-feu de Windows pour les joueurs d'autres machines.

Un fichier launch.cfg posé à côté de BO1Zombies.exe est ajouté à la ligne de commande, par exemple :
    +set cl_autoConnect 192.168.1.20:3074
    +set cl_allowWanConnect 1        (pour un serveur hors du réseau local)

Prérequis : Windows 10 ou 11 en 64 bits. Si le jeu ne démarre pas faute de DLL, installez le « Visual C++
Redistributable 2015-2022 (x64) » et le « DirectX End-User Runtime » (juin 2010) de Microsoft.

Ce dossier contient vos propres fichiers de jeu : gardez-le pour vos machines, ne le publiez pas.
EOF
    write_version "$d" windows
}

package_linux() {
    local d="$OUT/BO1Zombies_linux"
    step "linux: $d"
    mkdir -p "$d/main"
    cp "$WORK/linux/Release/BO1Zombies" "$d/"
    rsync -a --delete "$ROOT/mods/" "$d/mods/"
    rsync -a "$ROOT/data/main/" "$d/main/"
    game_data "$d" video
    cp "$ZONES/shaders.pack" "$d/"
    cat > "$d/play.sh" << 'EOF'
#!/bin/bash
# Black Ops 1 Zombies, Linux client (OpenGL 4.3). ./play.sh opens the game's menu (Zombies, Solo, a map).
#   ./play.sh net <address[:port]> [name]   joins a dedicated server once the menu is up, and again whenever the game
#                                           is back at the menu
#   FULLSCREEN=1 ./play.sh                  full screen instead of a 1280x720 window
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
ARGS=(+set bo1_zombies 1 +set fs_b "$HERE" +set fs_h "$HERE" +set logfile 2)
if [ "${1:-}" = net ]; then
    [ -n "${2:-}" ] || { echo "usage: $0 net <address[:port]> [name]" >&2; exit 1; }
    ARGS+=(+set cl_autoConnect "$2")
    if [ -n "${3:-}" ]; then ARGS+=(+set name "$3"); fi
else
    ARGS+=(+set net_ip 127.0.0.1)
fi
if [ "${FULLSCREEN:-0}" = 1 ]; then
    ARGS+=(+set r_fullscreen 1 +set r_borderless 0)
else
    ARGS+=(+set r_fullscreen 0 +set r_borderless 1 +set r_customMode 1280x720 +set vid_xpos 60 +set vid_ypos 60)
fi
cd "$HERE"
exec ./BO1Zombies "${ARGS[@]}"
EOF
    cat > "$d/server.sh" << 'EOF'
#!/bin/bash
# Black Ops 1 Zombies dedicated server, listening on UDP on every interface. In a terminal it shows the curses
# console; started by a service or with its output redirected it keeps the plain console (stdout / stdin).
#   ./server.sh [map] [port] [more + commands]      defaults: zombie_theater 3074
#   ./server.sh zombie_cosmodrome 3074 +set g_password secret
# Settings: server/main/server.cfg; log: server/main/console_mp.log.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
MAP="${1:-zombie_theater}"
PORT="${2:-3074}"
shift $(( $# < 2 ? $# : 2 ))
mkdir -p "$HERE/server/main"
cd "$HERE"
exec ./BO1Zombies +set fs_b "$HERE" +set fs_h "$HERE/server" +set bo1_server 1 +set net_ip 0.0.0.0 +set net_port "$PORT" "$@" +devmap "$MAP"
EOF
    chmod +x "$d/BO1Zombies" "$d/play.sh" "$d/server.sh"
    cat > "$d/LISEZMOI.txt" << 'EOF'
BO1 Zombies, portage Linux x86-64 : client (OpenGL 4.3) et serveur dédié
========================================================================

JOUER       ./play.sh                     ouvre le menu du jeu (Zombies, Solo, une map).
            FULLSCREEN=1 ./play.sh        en plein écran.
REJOINDRE   ./play.sh net 192.168.1.20:3074 MonNom
            rejoint un serveur dédié dès que le menu est prêt, et y revient tout seul après une coupure.
SERVEUR     ./server.sh [map] [port]      par défaut : zombie_theater 3074
            Dans un terminal, il affiche sa console curses. Réglages dans server/main/server.cfg, journal dans
            server/main/console_mp.log.

Prérequis : SDL2 et ncurses (Debian, Ubuntu : sudo apt install libsdl2-2.0-0 libncurses6) et un pilote OpenGL 4.3.
Gardez ce dossier sur un système de fichiers Linux (ext4) : depuis /mnt/c sous WSL, une map met des minutes à charger.
Le client n'a pas encore de son ni de manette.

Ce dossier contient vos propres fichiers de jeu : gardez-le pour vos machines, ne le publiez pas.
EOF
    write_version "$d" linux
}

package_switch() {
    local d="$OUT/BO1Zombies_switch" sd
    sd="$d/switch/bo1zm"
    step "switch: $d"
    mkdir -p "$sd"
    cp "$WORK/switch/Release/BO1Zombies.nro" "$sd/"
    game_data "$sd"
    cp "$ZONES/shaders.pack" "$WORK/dksh/shaders.dksh" "$sd/"
    # Bring-up switches (no menu, debug dumps, applet mode) never belong in a package.
    rm -f "$sd/skip_menu" "$sd/deko3d_debug" "$sd/allow_applet"
    cat > "$d/LISEZMOI.txt" << 'EOF'
BO1 Zombies, portage Nintendo Switch (homebrew, Atmosphère)
===========================================================

INSTALLATION
  Copiez le dossier « switch » de ce répertoire à la RACINE de la carte SD (il se fusionne avec /switch) :
  /switch/bo1zm/BO1Zombies.nro, avec zone/, main/, shaders.pack, shaders.dksh et localization.txt.
  BO1Zombies.nro, shaders.pack et shaders.dksh vont ensemble : remplacez-les toujours tous les trois.

LANCEMENT (mode application obligatoire)
  Depuis l'accueil, MAINTENEZ R en lançant un jeu installé : le Homebrew Menu s'ouvre avec toute la mémoire.
  Choisissez « BO1 Zombies ».

MENU DE DÉPART
  Map, aim assist, god mode, rendu, limite de FPS, ombres, filtre de texture, manettes. + lance la partie.
  « Serveur » ouvre l'écran de connexion : activez-la, entrez l'adresse IP et le port du PC qui fait tourner le
  serveur (Server.bat ou server.sh, port 3074 par défaut) et votre nom. La console doit être en Wi-Fi.

JOURNAL
  /switch/bo1zm/boot.log : à renvoyer en cas de problème.

Ce dossier contient vos propres fichiers de jeu : gardez-le pour vos appareils, ne le publiez pas.
EOF
    write_version "$d" switch "nro build: $(cat "$WORK/switch/romfs/build.txt")"
}

make_archive() { # <package dir name> <default kind>
    local kind="$ARCHIVE" name="$1"
    [ "$kind" = auto ] && kind="$2"
    case "$kind" in
    none) return ;;
    zip)
        step "archive: $name.zip"
        rm -f "$OUT/$name.zip"
        (cd "$OUT" && zip -r -q -1 "$name.zip" "$name")
        ls -l "$OUT/$name.zip" | awk '{print $5, $9}'
        ;;
    tar)
        step "archive: $name.tar.gz"
        tar -czf "$OUT/$name.tar.gz" -C "$OUT" "$name"
        ls -l "$OUT/$name.tar.gz" | awk '{print $5, $9}'
        ;;
    esac
}

check_space "$OUT"
wants windows && { package_windows; make_archive BO1Zombies_windows zip; }
wants linux && { package_linux; make_archive BO1Zombies_linux tar; }
wants switch && { package_switch; make_archive BO1Zombies_switch zip; }

step "done: $OUT"
for p in "${PLATFORMS[@]}"; do du -sh "$OUT/BO1Zombies_$p" | sed 's/\t/  /'; done
echo "These packages hold your own game files: keep them for your own devices, never publish them."
