# Switch port — architecture

The port adds layers around the decompiled engine and keeps the engine code as close to upstream as possible.

```
 retail PC install (zone/*.ff, main/*.iwd)
        │  run once on the user's PC
        ▼
 tools/ffconv  (x86 32-bit, reuses the engine's own Load_* code)
        │  IWffs zones: the same zlib stream, every struct widened to its 64-bit layout
        │  shaders: D3D9 SM3 bytecode → MojoShader → GLSL 4.60 core (std140 UBOs, explicit bindings)
        ▼
 engine (src/, tl/) ── src/platform/compat.h       MSVC-isms for GCC/clang
        │            ── src/platform/{win32,posix,switch}/  Sys_* (threads, files, memory, time)
        │            ── src/platform/stubs/         Steam, Bink, NVAPI, voice, mjpeg, monkey (BO1_OFFLINE)
        ▼
 src/gfx_d3d (unchanged IDirect3DDevice9 calls)
        ▼
 src/gfx_d3d9emu   D3D9 state, constants → UBOs, resources, Lock, formats   (no GL calls)
        ▼
 src/gfx_backend/gfx_backend.h   explicit low-level contract (see RENDERER.md)
        ├── gl/       v1: GL 4.3 core on its own thread, entry points from the platform's context (SDL2 now)
        ├── null/     draws nothing, checks every call against the contract (headless runs, tests)
        └── deko3d/   Switch only, opt-in (`gfx.cfg` = deko3d): shaders compiled offline by uam into
                      `shaders.dksh`, rescheduled at load to Mesa's rules (no dual issue); plays Kino on the
                      console (phase 7, in progress)
```

## Build presets
| Preset | Purpose |
|---|---|
| `win32-msvc` | Upstream reference build, D3D9, retail fastfiles |
| `win32-msvc-offline` | The reference with `BO1_OFFLINE=ON`, into `build/win32-offline`; run it with `Play.bat offline` |
| `win64-msvc` | 64-bit engine with D3D9 still in place, IWffs zones (always `BO1_OFFLINE`) |
| `mingw64-clang` | Phase 3 diagnostics only (`tools/syntax_check.py`): every pointer-width assumption is a clang error |
| `mingw32-gcc`, `mingw32-clang` | Phase 1 syntax pass only (`tools/syntax_check.py`): GNU dialect, 32-bit, MinGW Windows headers |
| `linux64-clang` | Portable platform layer + GL backend, desktop debugging |
| `switch` | devkitA64 + libnx, `.nro` |

The Ninja presets compile through ccache when it is installed (`apt install ccache`); the Visual Studio presets do
not.

## Compiler compatibility (phase 1)
- `src/platform/compat.h` is force-included for GCC and clang (never for MSVC or clang-cl). It maps `__declspec` forms
  explicitly (`align(n)` must become `aligned(n)`; unknown forms fail to compile) and, on MinGW, pulls in `_mingw.h`
  for `__int64` and the calling conventions.
- `src/platform/minmax.h` gives files that relied on the `<windows.h>` `min`/`max` macros the same definitions.
- MSVC-only code stays behind `#if defined(_MSC_VER)` (x87 asm, SEH, `dxerr.h`) with a portable twin when the engine
  needs the result: `qcommon/msg_round.h` reproduces the x87 snapshot rounding bit for bit (`tests/msg_round`).
- Sound: the engine includes `sound/snd_driver.h` (driver API) and `sound/snd_dsp_types.h` (DSP structs). Only
  `snd_driver_xaudio2*.cpp` see XAudio2; on posix `snd_driver_sdl2.cpp` implements the same `snd_driver.h` as a
  software mixer on an SDL2 device. The engine still owns the voice table and computes each voice's speaker map and
  send levels; the driver turns that into samples — the MS-ADPCM/PCM decode (`snd_adpcm`) and the send matrices
  (`snd_mix`, ported from `SDXA2_UpdateVoiceSends`) are factored out pure and unit-tested. RAM voices only so far;
  streamed voices, the reverb bus and the master DSP are deferred (`docs/PORTING.md`, phase 4b step 4).

## ffconv (phase 2)
`tools/ffconv` is a 32-bit Windows tool (MinGW, `tools/ffconv/CMakePresets.json`) that turns the retail zones into
`IWffs` zones and one `shaders.pack`. It never writes to the install.

- **Walk**: the engine's own `db_load.cpp`, copied at build time by `gen/transform_db_load.py` with typed hooks:
  each `Load_Stream` passes the `var*` global it streams from.
  `ff_stream.cpp` implements the engine's stream API (`db_stream.h`, `db_stream_load.h`) with the same 32-bit
  positions as the engine, and tracks the 64-bit positions alongside. `ff_stubs.cpp` replaces registration, D3D,
  sound and mod hooks.
- **Layouts**: `gen/layouts.py` (libclang) parses `db_load.cpp` for the 32-bit and the 64-bit target and writes, per
  streamed type, its leaf fields with both offsets: pointers, unions of pointers, unions of pointers and wider data
  (converted with the member the loader walks), and bytes. 32-bit padding is copied where the 64-bit layout has room.
- **Value fixups**: an integer whose value is a byte distance inside an array of a struct that grows is rescaled,
  not copied. `layouts.py`'s `VALUE_FIXUPS` lists `(record, field, unit)`; the generated layout carries `ValueFixup`
  entries (`ff_layout.h`) with both offsets and both unit sizes. `ff_stream.cpp` writes `value / unit32 * unit64`
  when widening and the inverse when narrowing, and fails the zone if the value is not a whole number of units or
  points outside the array. One entry so far: `GfxAabbTree::childrenOffset` (40 → 48 bytes per node). The audit of
  the other integer fields is in `docs/PORTING.md` (phase 3).
- **Widened integers**: an `intptr_t` field that holds a pointer at runtime grows from 4 to 8 bytes. `layouts.py`'s
  `WIDENED_INTEGERS` lists them (`DynEntityClient::physObjId`, `PhysConstraint::constraintHandle`); each becomes a
  `LEAF_HANDLE` leaf (`ff_layout.h`), sign-extended when widening and narrowed back only if its upper half is the sign
  extension. The zones stream them as 0, since a 32-bit runtime address means nothing to the 64-bit engine: a
  non-zero value fails the zone. Any other integer whose size differs between the two targets still stops
  `layouts.py`.
- **Shared targets**: where identical bytes were already streamed, the retail linker writes an offset instead of a
  copy, whatever type those bytes had: a weapon's all-null `XModel *[16]` points at 64 zero bytes of a script string
  array, a `BrushWrapper::planes` array at the planes its sides streamed one by one (8-aligned apart on 64-bit). The
  transform passes each `DB_ConvertOffsetToPointer` the pointee type and count of its inline branch (`READ_COUNTS`
  where the engine reads another count), and the branch itself. `ff_stream.cpp` checks that every leaf of the pointee
  lands on a leaf of the same kind at the 64-bit position the loader reads; where not, the IWffs slot says "follows"
  and the inline branch streams a widened copy of the target's retail bytes, so the 64-bit loader walks it like any
  inline object. Only targets whose pointers and unions are zero qualify. The copy is a separate object (no engine
  code compares those addresses). `--verify` gets each such slot's retail offset from the widening pass, skips the
  copy with the same branch, requires its narrowed bytes to equal the retail target, and fails on any pointee
  mismatch left in the 64-bit zone.
- **Stream rules** shared with the 64-bit engine: `src/database/db_ff64.h` (header, ABI tag, pointer encoding,
  alignment, block alignment).
- **One zone set for x86-64**: the tables are generated for MinGW x86-64 with MS bitfields, valid for MSVC x64. The
  posix x86-64 build reads the same zones (ABI tag `DB_FF64_ABI_WIN64`): before linking it, its CMake runs
  `layouts.py --check-target64 x86_64-linux-gnu`, which parses `db_load.cpp` as that build compiles it (the posix
  shim, the copied D3D9 headers) and fails, naming them, if any streamed type's tables differ. All 241 match today.
- **Proof**: `--verify` reads the written zone back, narrows it with the same walk and requires the retail stream byte
  for byte, apart from leftover bytes outside a union's walked member.
- **Shaders**: `ff_glsl.cpp` turns each vs_3_0/ps_3_0 into GLSL 4.60 with MojoShader (`third_party/mojoshader`, GLSL
  profile only) and the interface of `docs/RENDERER.md`; `shaders.pack` (`src/gfx_d3d9emu/shader_pack.h`) holds
  one translation per bytecode hash. `gen/check_shader_pack.py` validates a pack with `glslangValidator`.

## Offline build (phase 1c)
`BO1_OFFLINE` is always on outside win32 and an option on win32. It drops these modules from the build:
- online: `DemonWare/`, `src/DW/`, `src/groupvoice/`;
- Windows-only binary SDKs: `steam_api.lib`, `binkw32.lib` (with `src/binklib/`), `nvapi.lib`, `libvpx.lib` (with
  `src/vpx/`);
- dev tools: `src/mjpeg/` (movie capture), `src/monkey/` (test harness), Tracy.

`src/platform/stubs/` defines what the rest of the engine still calls, in the state the engine already handles:

| Stub | Behaves like |
|---|---|
| `steam_api_stub.cpp` | Steam client not running: init fails with `NoSteamClient`, every interface accessor returns NULL |
| `bink_stub.cpp` | `BinkOpen` fails, so no movie plays (main menu background included); `BinkGetError` stays empty because the engine asserts on a pending error before each movie |
| `nvapi_stub.cpp` | No NVIDIA driver: `NvAPI_Initialize` returns `NVAPI_LIBRARY_NOT_FOUND` |
| `groupvoice_stub.cpp` | No sound device: `Sound_Init` fails and voice chat stays off |
| `monkey_stub.cpp` | The harness never starts |
| `mjpeg_stub.cpp` | Nothing is ever encoding; the `mjpeg` command prints a warning |

The stubs define the Steam and Bink entry points inside the exe, so `BO1_OFFLINE` also defines `STEAM_API_NODLL` and
`__RADINEXE__` (the SDKs' own switches that drop `dllimport`). `src/live/` and `CubeMapGenLib` stay compiled (see
`docs/PORTING.md`).

## 64-bit engine (phase 3)
The same sources build 32-bit (reference) and 64-bit. Pointer-width fixes are retypings that leave the 32-bit code
as it was:
- **Loader**: a 64-bit build reads `IWffs` zones only. `DB_ReadIWffsHeader` (`db_file_load.cpp`) checks magic,
  version and ABI against `DB_FF64_BUILD_ABI`; `DB_OffsetToAddress` (`db_stream_load.cpp`) decodes 64-bit pointer
  slots; `DB_AllocStreamPos` aligns to at least 8. `db_load.cpp`'s `Load_Stream` sizes are `sizeof`/`offsetof`.
- **Layout checks**: `src/platform/arch_size.h` gives `BO1_ARCH_SIZE(n32, n64)` for struct-size `static_assert`s,
  `BO1_ABI_SIZE` for classes with a base class (MSVC and Itanium lay them out differently), and
  `BO1_SAVE_IMAGE_ASSERT` for the SP save image, which binds 32-bit builds only: 64-bit builds skip saves.
- **Script bytecode**: operands that hold a pointer (code positions, `case` tables, thread-call parameter counts,
  animation slots) are `uintptr_t` wide, written by `EmitCodepos` and read by `Scr_ReadCodePos`/`Scr_ReadUnsigned`;
  integers and jump offsets stay 4 bytes (`EmitInt`/`Scr_ReadInt`). Function handles are `programBuffer` offsets.
- **Decompiled views of 32-bit layouts** keep their shape but name what they mean: `centity_s::flagWord` and
  `flameGeneric_s::flagWord` (unions over the bitfield words read as `+ 201` / `+ 23` dwords),
  `phys_free_list<T>::data_of(node)`/`node_of(data)` (was `(T *)&node[2]`, `node[2].m_prev_T_internal`), `Float4Load`/`Float4Store` (`com_math.h`, a `float4`
  view of unaligned storage), `IK_STATE_ALLOC_SIZE`, `DObj objBuf[3072]`. Stack buffers and streamed arrays that the
  SSE code expects 16-aligned (`GfxSurface`) get `alignas` on 64-bit only.
- **Lock-free physics pools** (`phys_mem_new.h`): the free-list head is a tagged pointer, a `{pointer, tag}` pair on
  32-bit and a 48-bit pointer with a 16-bit tag in one word on 64-bit, both swapped with the 64-bit CAS.
- **Tools** (`tools/`):
  - `syntax_check.py build/mingw64-clang`: lists every pointer cast to a smaller integer and every failing assert;
  - `codediff.py build/mingw32-gcc FILE...`: compares the 32-bit `.text` of each file with its version at a git ref;
  - `litsizes.py build/mingw64-clang`: lists calls passing a literal byte count for a struct that grows on 64-bit
    (including out-parameter allocators such as `R_DebugAlloc((void **)&p, N, ...)`);
  - `ptrcasts.py build/mingw64-clang`: lists casts that read 64-bit data through a 4-byte view (pointer arrays and
    pointer-holding structs cast to `int *`, byte offsets into them, `qsort`/`bsearch` element sizes, `4 * n`
    pointer-array allocations), which compile without a warning;
  - `stackviews.py build/mingw64-clang-full`: lists casts that view a local or a parameter (`&x`, `x[N]`,
    `&x.member`) as a type bigger than what is left of it, the trace of a stack object Hex-Rays split into several
    locals (`make_rotate((phys_mat44 *)v3, ...)` on a 12-byte `v3`), parsed for both i686 and x86_64;
  - `headless.ps1 -ExeDir build\win64-msvc\Release -RunDir <dir> -ZoneDir build\iwffs-full\zone`: runs the 64-bit
    exe on the IWffs zones (a run folder whose `zone` is a junction to `ZoneDir`, without the 32-bit DLLs of
    `build\Release`).

## Win32 on posix and switch (phase 4)
The engine keeps its Win32 and MSVC CRT calls. On posix and switch it compiles against our own headers in
`src/platform/posix/include/` (`windows.h`, which brings in `winsock.h` and `mmsystem.h` as the SDK's does, and the
siblings the engine includes: `psapi.h`, `tlhelp32.h`, `dbghelp.h`, `ShlObj.h`, `XInput.h`, `io.h`, `direct.h`,
`dxerr9.h`, the D3DX subset in `d3dx9.h`, `dsound.h` and forwarders) and the SDK's `d3d9*.h`, copied at configure
time.
- Types keep their Windows sizes: `LONG` and `DWORD` are 4 bytes, the 64-bit types are `long long` like `__int64`
  (`int64_t` is `long` on LP64, a different type). Structs keep the Windows layout (`CONTEXT`, `DEVMODEA`,
  `SYSTEM_CPU_SET_INFORMATION` are size-checked). `CRITICAL_SECTION` and `SRWLOCK` hold a `pthread_mutex_t` /
  `pthread_rwlock_t`, so they are big enough for the platform layer on every target.
- Win32 functions are only declared, grouped by area; the platform layer (`src/platform/posix/`) implements them.
  CRT functions that only compute (`sprintf_s`, `strcpy_s`, `_ui64toa`, `localtime_s`, ...) are defined in
  `posix/msvc_intrinsics.h` with MSVC's semantics: an overflowing `_s` call ends the process, as MSVC's default
  invalid parameter handler does. The others are in `msvc_crt.cpp` (`sscanf_s` with its buffer-size arguments,
  `_putenv`, `_snwprintf_s` whose `%s` is wide as in MSVC, the error-mode switches, and `rand`/`srand` as the UCRT's
  generator, which `compat.h` maps them to: RAND_MAX 0x7FFF, which the engine divides by, and one seed per thread) or
  with the files (`_findfirst64i32`, `_mkdir`, `_getcwd`, `bo1_fopen`).
- MSVC's `long` is 4 bytes. Where the decompiled code means that (struct fields, Interlocked operands, bit-scan
  indices, thread ids) it is spelled `bo1_long` / `bo1_ulong` (`arch_size.h`), still `long` for MSVC; the Interlocked
  and bit-scan templates refuse an 8-byte operand.
- `LOBYTE`, `HIBYTE`, `LOWORD` and `HIWORD` exist twice, as with the SDK: windows.h's (bits of the value) and
  `q_shared.h`'s Hex-Rays ones (last part of the object), and each file uses the one its include order defines last.
  The shim defines windows.h's, and an engine header includes `windows.h` on posix only where an SDK header would
  (`profile.h` declares `GetCurrentThreadId` alone), so every use expands as it does with MSVC.
- The Windows sources build on the layer as they are: `win_main.cpp` (`WinMain` and all the headless tooling: the
  emergency log, autoquit, asserts and crash lines), `win_net.cpp`, `win_wndproc.cpp`, `win_voice.cpp`, the other
  `win32/` files and CubeMapGenLib. `posix_main.cpp`'s `main()` hands `WinMain` the command line without the program
  name, as the MSVC CRT does. Replaced: `win_syscon.cpp` by `posix_syscon.cpp` (the terminal: console text on stdout
  without colour codes, from the main thread only as in the console window; stdin lines become commands),
  `win_mini_dumper.cpp` (its filter writes nothing in this tree; `posix_main.cpp` keeps the started flag), the XAudio2
  driver by `snd_driver_sdl2.cpp` (a software mixer on an SDL2 device; it falls back to `nosnd` when no device opens).
  Direct3D 9 is `src/gfx_d3d9emu` on a `gfx_backend` (`docs/RENDERER.md`); its host (displays, windows, the
  `shaders.pack` path next to `zone/`, screenshot files, the console) is `posix_d3d9host.cpp`. The GL backend's host
  (`posix_glhost.cpp`) makes an SDL2 OpenGL 4.3 core context on a hidden window of its own; once it exists the window
  layer makes every game window an OpenGL one. A headless run gets no context and renders on the null backend.
  D3DX saves screenshots (JPG, TGA, BMP, PNG); its other calls return `E_NOTIMPL`.
- The layer (`win32_*.cpp`), by area:
  - handles are reference-counted objects (`win32_handles.h`) behind `HANDLE`; thread ids are the kernel's;
  - threads: `CREATE_SUSPENDED`, events with `PulseEvent`'s semantics (only the threads already waiting are
    released), `SuspendThread` / `GetThreadContext` by a real-time signal whose handler records the registers, APCs
    run by alertable `SleepEx` (`ReadFileEx`'s completions). `TerminateThread` fails on a running thread: POSIX cannot
    end one safely, and its only caller (CubeMapGenLib) reaches it after its threads finished;
  - memory: `VirtualAlloc` reserves 64 KB-aligned `mmap` regions and commits pages with `mprotect` (`MEM_COMMIT`
    alone at no address reserves too, as PMem allocates); `VirtualQuery` reports the regions `Z_VirtualFree` walks;
  - files: paths arrive with `\` separators (`FS_ReplaceSeparators`) and are converted at the boundary;
    `ReadFileEx` reads at once and queues the completion; directory searches match case-insensitively, as Windows
    does; `bo1_fopen` (the engine's `FileWrapper_Open`) adds MSVC's text mode; `GetModuleFileNameA` returns a `\`
    path, which the engine takes apart; `GetCommandLineA` reads `/proc/self/cmdline`, since static initialisers ask
    for it before `main()`;
  - crashes: `SetUnhandledExceptionFilter` installs signal handlers that hand SIGSEGV, SIGBUS, SIGILL, SIGFPE and
    SIGTRAP (`__debugbreak`) to the filter as the matching `EXCEPTION_RECORD` with the registers, then end with the
    signal's default action (a core dump); `RaiseException` goes through the filter too. The headless filter's stack
    scan and thread dumps read PE headers and dbghelp, so on posix they print the crash line only;
  - sockets: `include/winsock.h` maps Winsock onto BSD sockets, with the raw Winsock numbers the code passes
    (`SOL_SOCKET` 0xFFFF, `SO_BROADCAST` 0x20, `FIONBIO` 0x8004667E) and errno turned into `WSAE*` codes;
  - windows (`win32_gui.cpp`, on SDL2): windows belong to the layer (class, procedure, styles, placement), each
    backed by an SDL window, with no frame (the window rect is the client area). The procedure gets the messages in
    Windows' order (`WM_NCCREATE`, `WM_CREATE`; a visible top-level window is shown and activated before
    `CreateWindowEx` returns, then `WM_SIZE`, `WM_MOVE`), and SDL's window events become activation, placement and
    close messages. Each thread has its own message queue, as on Windows: the engine makes its window on the render
    thread, which pumps it each frame. Monitors are SDL's displays (`HMONITOR` = display + 1). A headless run keeps
    its windows hidden, as the Windows headless client stays on a private desktop: the messages are the same, and a
    9Ex `Present` reports the window occluded. The engine's full-screen window (`r_fullscreen 1`: a WS_POPUP with
    WS_EX_TOPMOST) becomes an SDL desktop full-screen window (`SDL_WINDOW_FULLSCREEN_DESKTOP`), which covers the
    display without a mode switch; the game renders at its own size and Present scales to the drawable. The
    borderless windowed mode (`r_borderless 1`, WS_POPUP, no WS_EX_TOPMOST) stays a plain borderless window. SDL
    video starts at the first display or window call, so the
    dedicated server never opens it; SDL reads events on the thread that started it, so video the GL backend started
    for the engine's first Direct3D object (`Sys_DetectVideoCard`, on the main thread) stops with that object when no
    window exists (`Bo1_VideoRelease`), and the render thread, which makes and pumps the windows, starts it again.
    Sound is a software mixer on an SDL2 device (`snd_driver_sdl2.cpp`), or `nosnd` when none opens;
  - input (`win32_input.cpp`): SDL's keyboard, text and mouse events become the messages Windows posts. Key messages
    carry the virtual-key code (by the layout for the keys that type letters, as Windows assigns it), the set 1 scan
    code of the key's position and the extended-key flag, which `MapKey` reads (scan code 0x29 opens the console,
    the flag tells the keypad from the dedicated navigation keys); Alt makes them system keys. Printable characters
    come from SDL's text input as `WM_CHAR` in Windows-1252; the control characters (Backspace, Enter, Ctrl+V…) are
    posted by `TranslateMessage`, decided with the modifiers held at key-down as Windows keeps each message's key
    state. `MapVirtualKeyA(vk, MAPVK_VK_TO_CHAR)` answers the layout's unshifted character;
    `GetAsyncKeyState` the tracked keys and buttons. The engine reads mouse movement by polling `GetCursorPos` and
    recentring with `SetCursorPos`: while it hides the cursor over its focused window, the cursor is captured and
    virtual, moved by SDL's relative motion (SDL's relative mode where the driver has one), and released where the
    virtual one is. The clipboard is SDL's, which only the video thread may call and the main thread must never wait
    for (the video thread waits for it every frame): the video thread keeps a copy of its text, refreshed on
    `SDL_CLIPBOARDUPDATE`, and sends what the engine sets at its next pump. A headless run never captures the cursor
    nor opens the clipboard;
  - scripted input for tests (`posix_inputscript.cpp`): `BO1_INPUT_SCRIPT="ms action args; ..."` hands made-up SDL
    events to the input layer at given times after the window first has the keyboard, for a client run under
    `SDL_VIDEODRIVER=offscreen` (a shown window, keyboard focus and an EGL OpenGL context, nothing on the user's
    display).
  - controller (`win32_gamepad.cpp`): the XInput API the engine polls, on `SDL_GameController`. One controller
    (player 1), opened and read on the video thread each pump into a snapshot (`win32_gamepad.h` maps the SDL buttons
    and axes to the XInput bitmask, triggers and sticks; the engine applies its own dead zones); the XInput entry
    points read the snapshot under a lock, and rumble is `SDL_GameControllerRumble`. No pad, or a headless run,
    reports no device, and the engine runs without a controller.
- The Itanium ABI (GCC, clang on Linux and the Switch) differs from MSVC where the decompiled code relied on it:
  global variable names carry no type (two `data` globals of different types clashed; `rb_backend.cpp`'s is
  `static`), a class's typeinfo is emitted with its first non-inline virtual function (interfaces declare theirs
  pure), and glibc's `u_long` is 8 bytes (an IPv4 address is `uint32_t`).
- GCC and clang also compile two things differently from MSVC: `long double` is 80-bit x87 or 128-bit soft-float
  (MSVC's is `double`, which the engine now writes), and a copy of an over-aligned struct uses aligned vector moves
  (MSVC's never assume more than the block gives). Blocks the retail code aligns below their type's alignment get it
  outside MSVC: `BO1_ALLOC_ALIGN` (`arch_size.h`) and `Z_Malloc`'s 176-byte header (`tools/overaligned.py`).
- Includes resolve on case-sensitive file systems: `include_cased/` relays the engine's `Windows.h`, `Psapi.h`,
  `TlHelp32.h` and `DbgHelp.h` spellings.

## Runtime data on the Switch
`sdmc:/switch/bo1zm/`: converted `zone/` from `ffconv`, `main/*.iwd` from the user's install, and the shader
cache. The app must run in application mode (title takeover). Applet mode only gives about 400 MB.

## Client of a remote server (phase 8)

The zombies game is a listen server: the menu itself runs a local server (`frontend`), and a map normally loads in
the same process as its client, which then reads the server's state directly (actor animtrees, `zombiemode`, script
handles). A client of a remote dedicated server has none of that, so three paths differ when `com_sv_running` is 0:

- `CL_Connect_f` kills the menu's local server first: while one runs, `Com_ServerPacketEvent` gives every UDP packet
  to it, and the remote server's replies never reach the client.
- `CL_ParseGamestate` sets `zombiemode` & co. from the gamestate's map name (`Com_SetGameModeDvarsForMap`) before the
  baselines, which the server encoded with the zombies netfield tables.
- `CGScr_LoadScriptsAndAnims` compiles, for an SP level, the same server scripts as the server's `GScr_LoadScripts`
  and in the same order (`GScr_SP_LoadScripts(inst, compileOnly = true)`), so that the snapshot animtree indices
  resolve, and finds the actors' `generic_human` tree for `G_SP_GetActorAnims`. `compileOnly` keeps no handles and
  writes no hunk file data.

Status and the server's own layers (bind, curses console, admin commands, logs): `docs/PORTING.md`, phase 8.
