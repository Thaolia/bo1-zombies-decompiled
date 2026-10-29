# BO1 Zombies

Decompiled engine code for Black Ops 1 Zombies, so mods can change the engine itself, not just scripts and maps.

![](images/1.webp)

![](images/2.webp)

## Nintendo Switch port (branch `switch`)

This fork ports the engine to Nintendo Switch 1 homebrew (`.nro`, AArch64, libnx). The v1 target is solo offline
zombies on the base maps. It is a work in progress: see `docs/PORTING.md` for the plan and status, and
`docs/ARCHITECTURE.md` for how the port is layered.

On the console the game starts in a text menu, where you choose:
- a solo game, or a dedicated server to join: the **Serveur** line opens its own screen, with the connection on or
  off, the server's IP address and port (typed with the console's keyboard), your player name, and the console's own
  network address (`docs/SERVER.md`, "Joining from a Switch");
- the map;
- the aim assist and its slowdown, lock-on and auto-aim;
- god mode;
- the zombie counter beside the scoreboard (mods/zinfo: zombies alive and left this round), on by default;
- the renderer (deko3d or OpenGL), the frame-rate cap, shadows, texture filtering and the FPS/battery counter;
- the controllers (the list of detected pads, and the system screen to connect them).

**+** starts the game straight on the map, or opens the game's menu, which then joins the server by itself. The choices are kept in `sdmc:/switch/bo1zm/menu.cfg`, and Start writes
`launch.cfg` and `gfx.cfg`. An empty `skip_menu` file there boots without the menu, on those two files as they are
(handy on an emulator). `docs/ARCHITECTURE.md` lists the files.

No game data is distributed. A converter run on your own PC (`tools/ffconv`) turns your Steam install's
fastfiles into the 64-bit format the port loads, and translates their shaders to GLSL:

    cd tools/ffconv && cmake --preset ffconv && cmake --build --preset ffconv      # from WSL or Linux, MinGW + python3-clang
    build\ffconv\ffconv.exe --verify --game "C:\...\Call of Duty Black Ops" --out C:\bo1zm

It reads the zones of every zombies map (about 3 GB) and writes `zone\` and `shaders.pack` to `--out`, never to the
install. `--verify` reads each written zone back and checks it against the original.

On Windows, the port's offline build (no Steam, Bink, NVAPI, DemonWare or voice chat) can already be played on the
files `setup.ps1` copied: `cmake --preset win32-msvc-offline`, `cmake --build --preset win32-msvc-offline-release`,
then `Play.bat offline`.

The 64-bit build (work in progress: Kino der Toten loads and plays for a minute; longer games are not tested yet)
runs on converted zones: convert into `build/zones64` with `ffconv --verify --game <install> --out build\zones64`,
build with `cmake --preset win64-msvc` and `cmake --build --preset win64-msvc-release`, then `Play.bat x64`.

On Linux x86-64 (work in progress) the engine runs as a dedicated server, a headless client that draws nothing, or
a windowed client rendering with OpenGL 4.3 core (keyboard and mouse; no sound or gamepad yet): `cmake --preset
linux64-clang -DDXSDK_DIR=<June 2010 DirectX SDK>` and `cmake --build --preset linux64-clang` (clang, Ninja, the
development files of SDL2 and GL (`libsdl2-dev`, `libgl-dev`), and python3-clang for the zone layout check). It reads
the same converted zones. Run it from a folder holding the program, your install's `localization.txt`, a `zone`
folder of converted zones and the converter's `shaders.pack`: `./BO1Zombies +set fs_b <install> +set fs_h <that
folder> +devmap zombie_theater` for the windowed client, with `+set bo1_headless 1` for the dedicated server (and
`+set bo1_headless_client 1` for the headless client). `BO1_GFX_BACKEND=null` forces the backend that draws nothing,
`BO1_GL_DEBUG=1` logs OpenGL's debug messages. Converted zones from before 2026-10-24 need a new `shaders.pack`
(`ffconv` again).

A dedicated zombies server (work in progress, branch `server`) is the same program started with `+set bo1_server 1`
on Windows or Linux: no window, listening on UDP on every interface, `server.cfg` for its settings. `Server.bat
[map] [port]` and `./Server.sh [map] [port]` start one. Clients join with `Play.bat net <address>:<port> [name]`
(or `Launch.bat net ...`), with `connect <address>:<port>`, or by themselves with `+set cl_autoConnect
<address>:<port>` in a `launch.cfg` next to the program, and `tools/server_status.py <address>` shows the map and
players. Admin commands typed in its console (or over rcon, `tools/server_rcon.py`): `zm_god`, `zm_points`, `zm_perk`,
`zm_revive`, `zm_kill`, `zm_respawn`, `zm_round`, `zm_reset`, `zm_map`, `zm_mods`, and `zm_startpoints` /
`zm_startperks` for the start of a game. Players can join a game in progress (`zm_jip_mode immediate` or
`nextround`), and `sv_maxclients` lets more than 4 play (up to 32; characters repeat past 4). Each client's
console log is sent to the server (UDP game port + 1) and
written to its `logs/` folder; Tab in the server's console shows the selected player's. See `docs/SERVER.md`.

## Packages

`tools/make_dist.sh` (bash, from WSL) turns a source tree where `setup.ps1` has copied the game into ready-to-copy
folders and archives in `build/dist`, one per platform:

| Package | Holds | Archive |
|---|---|---|
| `BO1Zombies_windows/` | the 64-bit client and dedicated server, `Play.bat` (`Play.bat net <address[:port]> [name]` joins a server), `Server.bat [map] [port]`, the 64-bit runtime DLLs | `.zip` |
| `BO1Zombies_linux/` | the Linux client and dedicated server, `play.sh` (`./play.sh net ...`), `server.sh [map] [port]`, `shaders.pack` | `.tar.gz` |
| `BO1Zombies_switch/` | an SD card tree, `switch/bo1zm/` with the `.nro`, `shaders.pack` and `shaders.dksh` | `.zip` |

Each also holds the converted zones, the install's `main/*.iwd` and `localization.txt` (and `main/video` on PC), a
`LISEZMOI.txt` and a `VERSION.txt` (commit, date, NRO link time).

    tools/make_dist.sh                       # all three
    tools/make_dist.sh switch linux          # some
    tools/make_dist.sh --no-build windows    # package what is built, fail on anything missing or out of date

Unless `--no-build`, it builds what the packages need, incrementally: the `win64-msvc` preset, the Linux and Switch
programs in `--work` (an ext4 folder, `~/.cache/bo1zm-dist`), the converted zones when they are missing or older than
the converter's sources (`ffconv --verify`, then `check_shader_pack.py`; the old zones stay until the new ones
pass), and `shaders.dksh` when its `shaders.pack` changed (it fails if uam rejects a shader). Other options:
`--out`, `--game`, `--zones`, `--dxsdk`, `--archive auto|zip|tar|none`, `--jobs`; `--help` lists them. The packages
hold your own game files: keep them for your own devices, never publish them.

French version: `README.fr.md`.
