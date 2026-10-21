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

French version: `README.fr.md`.
