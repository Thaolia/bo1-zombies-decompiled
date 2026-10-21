# CLAUDE.md — BO1 Zombies, Nintendo Switch 1 homebrew port

Fork of `MisaDev4/bo1-zombies-decompiled` (remote `upstream`, branch `main`). All port work lives on branch
`switch`. The goal is a playable `.nro` (Atmosphère, AArch64, libnx) of solo offline zombies on the base maps.

## Where things are
| Question | Read |
|---|---|
| Phase plan, status, decisions log | `docs/PORTING.md` |
| Port layers (platform, ffconv, renderer split) | `docs/ARCHITECTURE.md` |
| Renderer backend contract and its review checklist | `docs/RENDERER.md` |
| Upstream mod system | `mods/*/README.md`, `mods/mapkit/MAPKIT.md` |

## Golden rules
- **Never debug two unknowns at once.** Fix 64-bit problems on Windows x64 with the original D3D9 renderer. Validate
  the GL renderer on Linux desktop. Only then move to the Switch. deko3d comes last and is optional.
- **The `win32-msvc` build is the reference.** Every change must keep it building and behaving identically. Guard
  port code with `#if` on platform/arch macros. Never delete the Win32/D3D9 paths.
- **Never touch retail game files.** `ffconv` reads the user's Steam install and writes new `IWffs` zones elsewhere.
  Never commit, upload or embed game data (zones, iwd, sounds, shaders extracted from fastfiles).
- **Upstream layout stays as is** (`src/`, `tl/`, `DemonWare/`, `mods/`, `data/`, `tools/` at the root). New code goes in:
  - `src/platform/` for compat and the OS layer;
  - `src/gfx_d3d9emu/` and `src/gfx_backend/` for the renderer;
  - `tools/ffconv/` for the converter;
  - `tests/` for tests;
  - `third_party/` for vendored libraries.
- Code comments are in English and only explain a non-obvious *why*.

## 64-bit rules
- A pointer stored in an integer is `uintptr_t`, never `unsigned int`, `int`, `DWORD` or `LODWORD(ptr)`.
- Do not "fix" a `static_assert(sizeof(X) == N)` by deleting it. Make it arch-conditional with the real 64-bit size,
  and know why the size changed.
- Asset structs from fastfiles are only ever native-64 after `ffconv`. Never add runtime 32→64 patching to
  `db_load.cpp`. The 64-bit loader is the relocation loader under `BO1_NATIVE64_FF`.
- `qcommon/msg.cpp` float rounding must stay bit-exact with the x87 version. Run its test after any change there.
- Script VM code positions (`EmitCodepos`, `Scr_ReadCodePos`, `Scr_ReadUnsigned`) must all use the same width.

## Renderer rules
- `src/gfx_d3d9emu/` must contain **no GL or deko3d calls**. It only talks to `src/gfx_backend/gfx_backend.h`.
- Any change to `gfx_backend.h` must pass the checklist in `docs/RENDERER.md`: no implicit global state, no
  `glGet`-style queries, no implicit sync, explicit memory/usage and pipeline-state objects.
- Shaders come from `ffconv` as GLSL 4.60 core with explicit bindings and constants in std140 UBOs. Never hand-edit
  generated shaders; fix the translator/normaliser instead.

## Workflow
- Changes touching 5+ files: work in a sibling `git worktree` (`../BO-Zombies_switch_new`) and let the user compare
  before merging into `switch`.
- After each implementation step, update `docs/PORTING.md` (status + decisions) and any affected doc in the same
  session.
- Commits: English messages, no Claude co-author line.
- Before claiming a phase step done, run its verification listed in `docs/PORTING.md` and report the actual
  output. "It compiles" is not "it works".
- Tooling runs from WSL2. MSVC/clang-cl builds run on the Windows side. devkitPro lives in `/opt/devkitpro`.
