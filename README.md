# BO1 Zombies

Decompiled engine code for Black Ops 1 Zombies, so mods can change the engine itself, not just scripts and maps.

![](images/1.webp)

![](images/2.webp)

## Nintendo Switch port (branch `switch`)

This fork ports the engine to Nintendo Switch 1 homebrew (`.nro`, AArch64, libnx). The v1 target is solo offline
zombies on the base maps. It is a work in progress: see `docs/PORTING.md` for the plan and status, and
`docs/ARCHITECTURE.md` for how the port is layered.

No game data is distributed. A converter run on your own PC (`tools/ffconv`, phase 2) turns your Steam install's
fastfiles into the 64-bit format the port loads.

French version: `README.fr.md`.
