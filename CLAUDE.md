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
- Do not "fix" a `static_assert(sizeof(X) == N)` by deleting it. Make it arch-conditional with the real 64-bit size
  (`BO1_ARCH_SIZE(n32, n64)`, `src/platform/arch_size.h`), and know why the size changed. A class with a base class
  takes `BO1_ABI_SIZE` (MSVC and Itanium differ); measure the MSVC sizes with `clang --target=x86_64-pc-windows-msvc`
  (command in `docs/PORTING.md`, phase 3), not by guessing from the MinGW numbers.
- After any 64-bit fix in a `.cpp`, run `tools/codediff.py build/mingw32-gcc <files>`. "identical" is the goal; read
  and explain every remaining difference before keeping the edit.
- A literal byte count (`Hunk_UserAlloc(hunk, 73728, ...)`, `memset(p, 0, 0x8D388)`) is a 32-bit `sizeof` in disguise.
  Write `sizeof`/`offsetof` instead; `tools/litsizes.py` lists the ones it can see.
- Write `double`, never `long double`: MSVC compiles it as `double`, GCC and clang as 80-bit x87 (x86-64) or 128-bit
  soft-float (AArch64), which changes struct layouts, results and speed.
- A decompiled byte shuffle (vec_perm / pshufb) that reads `v.unitVec[0].array[i]` with a computed `i` above 3 is out
  of bounds: GCC and clang then compute only lane 0 (vertex colours lost their alpha). Read it with
  `Float4Byte(v, i)` (`src/universal/com_math.h`).
- A block aligned below its type's `alignof` (an align(64) struct from a 4-aligned `Hunk_UserAlloc`) takes
  `BO1_ALLOC_ALIGN(retail, T)`, and the size sum that reserves it `BO1_ALLOC_SLACK`: GCC and clang copy over-aligned
  types with aligned vector moves, MSVC never does. `tools/overaligned.py` lists the candidates.
- On posix, an engine file includes `windows.h` only where its Windows build does (directly or through an SDK
  header): `LOWORD`/`HIWORD`/`LOBYTE`/`HIBYTE` take the meaning the include order gives them, as with MSVC.
- When a 64-bit crash reveals a bug family (a 32-bit stride, a 4-byte view, a casted call), inventory the whole tree
  for it before moving on: `tools/ptrcasts.py`, `tools/litsizes.py`, or a replay of the `mingw64-clang` compile commands
  with one clang warning (`-Wfortify-source`, `-Wcast-function-type-mismatch`; method in `docs/PORTING.md`, phase 3).
- Asset structs from fastfiles are only ever native-64 after `ffconv`. Never add runtime 32→64 patching to
  `db_load.cpp`: a 64-bit build reads `IWffs` zones whose stream `ffconv` already widened, with the same `Load_*`
  traversal. Stream rules shared by both sides (alignment, pointer encoding) live in `src/database/db_ff64.h` only.
- `ffconv` never edits `db_load.cpp` to make the conversion work: it transforms a copy at build time. A traversal
  bug is fixed in the transform or in `db_load.cpp` itself, and the 32-bit build must keep behaving the same.
- `qcommon/msg.cpp` float rounding must stay bit-exact with the x87 version. Run its test after any change there.
- Script VM code positions (`EmitCodepos`, `Scr_ReadCodePos`, `Scr_ReadUnsigned`) must all use the same width.

## Renderer rules
- `src/gfx_d3d9emu/` must contain **no GL or deko3d calls**. It only talks to `src/gfx_backend/gfx_backend.h`.
- Any change to `gfx_backend.h` must pass the checklist in `docs/RENDERER.md`: no implicit global state, no
  `glGet`-style queries, no implicit sync, explicit memory/usage and pipeline-state objects.
- Shaders come from `ffconv` as GLSL 4.60 core with explicit bindings and constants in std140 UBOs. Never hand-edit
  generated shaders; fix the translator/normaliser instead.
- D3D9's half-pixel offset lives in the shaders (`bo1_pos_fixup`, filled by the frontend); a backend applies the
  viewport as given. Never move a viewport by a fraction of a pixel: clipping then eats the edge pixels' samples
  (`docs/RENDERER.md`, contract).
- After a change to `gfx_backend.h`, `src/gfx_backend/` or the shader interface, run `d3d9emu_test` and
  `SDL_VIDEODRIVER=offscreen gb_gl_test` from the Linux build folder; both must pass.

## Workflow
- Changes touching 5+ files: work in a sibling `git worktree` (`../BO-Zombies_switch_new`) and let the user compare
  before merging into `switch`.
- After each implementation step, update `docs/PORTING.md` (status + decisions) and any affected doc in the same
  session.
- Commits: English messages, no Claude co-author line.
- Before claiming a phase step done, run its verification listed in `docs/PORTING.md` and report the actual
  output. "It compiles" is not "it works".
- Tooling runs from WSL2. MSVC builds run on the Windows side. devkitPro lives in `/opt/devkitpro`.
- Build the posix target from an ext4 copy of the worktree (rsync it, then CMake/Ninja there): from `/mnt/c` every
  header read waits on 9p. Check a posix change with a headless run compared against a win64 run of the same tree and
  zones, paths and numbers masked (`docs/PORTING.md`, phase 4a step 2); every remaining difference must be explained.
- Search sources with `grep -a`: some files hold Latin-1 bytes, and plain `grep` treats them as binary and prints
  none of their lines.
- After touching a shared header or `src/platform/compat.h`, rerun `tools/syntax_check.py` on both `mingw32-gcc` and
  `mingw32-clang` (command in `docs/PORTING.md`, phase 1).
- Between merges, build only `win64-msvc`. Build `win32-msvc` once before each merge into `switch` (it catches
  MSVC-x86-only errors the GNU checks cannot see, e.g. an intrinsic declared only by `<intrin.h>`).
- A new `BO1_OFFLINE` stub must reproduce a state the engine already handles (service absent, device missing,
  open failed). Never fake success. Find the symbols with the `win32-msvc-offline` link, not by hand, and compare
  headless runs against the reference on the main menu and on a map (`docs/PORTING.md`, phase 1c).
- After a change to `db_load.cpp`, a struct a fastfile streams, `db_ff64.h` or `tools/ffconv`, rebuild `ffconv` and run
  it with `--verify --game` on the install, then `tools/ffconv/gen/check_shader_pack.py` on the pack: every zombies
  zone must round-trip and every shader must validate (`docs/PORTING.md`, phase 2).
- Never make a GNU build pass by changing what MSVC compiles to. Prefer the spelling both accept with the same
  meaning (explicit cast, named constant, explicit include); otherwise guard with `#if defined(_MSC_VER)`.
