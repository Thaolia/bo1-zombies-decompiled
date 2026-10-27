# BO1 Zombies

Decompiled engine code for Black Ops 1 Zombies, so mods can change the engine itself, not just scripts and maps.

![](images/1.webp)

![](images/2.webp)

## Nintendo Switch port (branch `switch`)

This fork ports the engine to Nintendo Switch 1 homebrew (`.nro`, AArch64, libnx). The v1 target is solo offline
zombies on the base maps. It is a work in progress: see `docs/PORTING.md` for the plan and status, and
`docs/ARCHITECTURE.md` for how the port is layered.

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
on Windows or Linux: no window, listening on UDP on every interface, `server.cfg` for its settings. Clients join with
`connect <address>:<port>`, or by themselves with `+set cl_autoConnect <address>:<port>` in a `launch.cfg` next to
the program, and `tools/server_status.py <address>` shows the map and players. See `docs/SERVER.md`.

French version: `README.fr.md`.
