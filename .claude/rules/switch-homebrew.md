# Nintendo Switch homebrew (libnx / devkitPro) — rules learned on Wormhole NX

Rules for any Switch 1 homebrew project built with devkitPro + libnx, verified on this port
(2026-10-20 to 2026-10-22). The last section is specific to Clearwing VM (JVM bytecode → C++); skip
it for plain C/C++ projects. Each rule cost at least one build/test round trip to learn.

## Toolchain and environment

- Install devkitPro pacman through its apt repository; `install-devkitpro-pacman` and the key URL
  are blocked by Cloudflare for curl's default user agent: fetch them with `curl -A "dkp apt"`.
- `export DEVKITPRO=/opt/devkitpro` *before* `source .../switchvars.sh`, otherwise the portlibs
  include paths come out empty (`-I/portlibs/...`).
- `switch-openal-soft` is built with its SDL2 backend but `openal.pc` does not say so: link `SDL2`
  right after `openal`, or the first AL call fails at link time (`undefined reference to SDL_*`).
- Libraries missing from the pacman repos (libffi here) are cross-built into
  `$DEVKITPRO/portlibs/switch` with `--host=aarch64-none-elf` and the switchvars flags; check the
  `.a` exists before reinstalling anything.
- GCC 16's libgcc contains guarded GCS/SME instructions (`mrs gcspr_el0`, `gcspopm`,
  `tpidr2_el0`, `smstop`) that never run on hardware but abort Ryujinx's JIT when C++ exceptions
  unwind: patch them out of the ELF after linking (see `switch/tools/patch_ryujinx_mrs.py`).
- Use ccache as the CMake compiler launcher for both host and devkitA64 builds; never put a
  per-build value (commit hash, date) in a compile definition: every file would miss the cache.
  Put such values in a romfs file instead.
- Keep the build tree on a native Linux filesystem (ext4), not on `/mnt/c` (9p) under WSL2.

## Platform behaviour (libnx)

- Run in **application mode** (title override: hold R while launching a game); applet mode
  (album) gives a fraction of the memory. Detect it with `appletGetAppletType()` and refuse
  cleanly instead of crashing later.
- libnx maps *all* of the application's memory as the heap at start-up, so
  `svcGetInfo(InfoType_UsedMemorySize)` always equals the total. Measure real use with
  `mallinfo().uordblks`.
- `std::thread` / `threadCreate` with the default core put the new thread on the *creator's*
  core. Pin worker threads explicitly (`svcSetThreadCoreMask`, or the core argument of
  `threadCreate`); applications get cores 0-2.
- newlib has one malloc lock: never run a massive malloc/free loop (e.g. freeing a million
  objects) on one thread while another thread allocates; everything that allocates, Mesa's GL
  calls included, slows down 20-40×. Batch the frees on the allocating thread, in time slices.
- `appletMainLoop()`: once one call has returned false (exit requested), **never call it again**
  from any thread. The app is in the background by then and the next call waits forever for focus;
  the system eventually kills the process and the attached controllers stay disconnected in the
  HOME menu. If several threads service the applet loop, serialise them with a mutex and an
  "exit seen" flag checked under it.
- On exit, in order: stop and join every thread you created, close OpenAL (make no context
  current, destroy the context, close the device — this joins openal-soft's SDL audio thread),
  `psmExit()` and other services you opened, then EGL. Log a final "Shutdown complete" line so a
  user's log proves the exit was clean.
- Input: `padConfigureInput(1, HidNpadStyleSet_NpadStandard)` + `padInitializeDefault()` covers
  the attached Joy-Cons (handheld) and player 1. Read the touch screen too and arbitrate: a finger
  resting on the screen keeps a synthesised left button down, which makes the pad's "click" a no-op.
- Battery: `psmInitialize()`, `psmGetBatteryChargePercentage()`, `psmGetChargerType()` (charging
  when not `PsmChargerType_Unconnected`), `psmExit()` at shutdown.
- romfs paths carry a device prefix (`romfs:/...`): code that parses paths itself (a java.nio
  `Path` implementation, URI helpers) may reject them; plain `fopen`/`FileInputStream` work.
- Graphics: EGL on `nwindowGetDefault()` with a desktop **OpenGL 4.3 core** context works (Mesa
  20.1 / nouveau, GLSL 330). Each draw call costs ~20 µs in the driver: batch by texture, pack
  small images into texture atlases, draw solid fills as quads of a white texel in the same
  batch, cache bindings, stream vertices with `glBufferSubData` into a ring instead of
  `glBufferData` per draw. Going from ~500 to ~25 draws per frame took this game from 26-34 to
  39-48 fps in flight on the console.
- SDL2-switch (libnx EGL/Mesa) presents exactly **one** window — the console screen. A second `SDL_CreateWindow`
  fails with "Switch only supports one window", so the usual desktop pattern (a hidden 1×1 window that owns the GL
  context first, then a separate game window) leaves the game window with **no SDL window**: the backend's swapchain
  captures a null window, `SDL_GL_GetDrawableSize` returns 0×0, the present's blit+swap is silently skipped (its
  `width>0 && height>0` gate), and the frame is drawn but never shown — frames keep counting, "swap 0 ms", screen
  black. Use **one shown window** as both the GL context and the display, and route
  `SDL_GL_MakeCurrent`/`SDL_GL_GetDrawableSize`/`SDL_GL_SwapWindow` to it. `SDL_WINDOW_FULLSCREEN_DESKTOP` takes the
  native display mode (1080p docked); create the window at **1280×720** instead so the framebuffer is 720p and the
  console's compositor scales it to the output (the engine already renders at 720p, so this only drops a wasteful
  upscale). Note stderr is not routed on the console, so a failing `SDL_CreateWindow`/`SDL_InitSubSystem` prints its
  `SDL_GetError()` to nowhere unless you tee it through the engine's own log channel (BO-Zombies_switch, 2026-10-25).
- `consoleExit` does not give stdout back. libnx leaves stdout and stderr on the console device, and the next
  `printf` starts a frame (`framebufferBegin`) on the framebuffer `consoleExit` closed: a data abort at address 0 at
  the first log line after a pre-game text menu (Ryujinx, BO-Zombies_switch 2026-10-26). Save
  `devoptab_list[STD_OUT]` and `[STD_ERR]` (`<sys/iosupport.h>`) before `consoleInit` and put them back after
  `consoleExit`. After that the renderer (deko3d's swapchain or SDL's EGL) takes `nwindowGetDefault` without trouble.
- Ryujinx 1.1.1375 cannot receive UDP with `recvfrom(..., from, &len)`: when the first datagram arrives, its `Bsd.IClient.RecvFrom` throws `InvalidMemoryRegionException` while writing the sender's address (`va` = the 16-byte buffer's size shifted into the address bits, `(16 << 42) | addr`), and the emulator stops. Sending, `bind`, `socketInitializeDefault` and `nifmGetCurrentIpAddress` (the host's IP) work. Prove a network client's start-up on Ryujinx (sockets, the `connect` going out, the server logging it) and judge the exchange on the console (BO-Zombies_switch, 2026-10-27).
- Audio streams refilled from the game loop starve after any frame longer than their queued
  buffers (~1.1 s here): the log then fills with "source not PLAYING". Treat that as a symptom of
  frame stalls, not an audio bug.
- The console's FS gives a file one write handle at a time: while one part of the program keeps `boot.log` open for
  writing, every other `fopen(path, "ab")` of it fails, silently for best-effort loggers. Here the engine kept its
  handle and every line the deko3d backend wrote with fopen/fclose was lost, on the console and on Ryujinx alike (1
  line in the SD log against 13 663 on `svcOutputDebugString`). Give a log file one process-wide writer (one `FILE *`,
  a mutex, `fflush` per write) and route every logger through it (BO-Zombies_switch, 2026-10-26).
- A `__DATE__`/`__TIME__` stamp only changes when its own file recompiles: a log said "built 06:57" for an NRO linked
  at 08:05. Write the link time into romfs from the packaging step and log that.
- SDL2-switch has no dummy video driver (`libSDL2.a` holds only `SWITCH_bootstrap`): `SDL_VIDEODRIVER=dummy`, set
  so that a deko3d app keeps `nwindowGetDefault` for itself, makes `SDL_InitSubSystem(SDL_INIT_VIDEO)` fail. The
  Switch video driver's event pump is also the only caller of `appletMainLoop()` in `libSDL2.a`, and code that pumps
  SDL events only on "the video thread" then pumps nothing: the controllers stay silent (only HOME and capture, which
  the system handles, still work) and the applet messages go unserviced. Without SDL video, pump SDL's events
  yourself (game controllers need no video), call `appletMainLoop()` once a frame minding the never-again rule above,
  and log when a controller opens, so a silent pad shows in the log (BO-Zombies_switch, 2026-10-26).
- SDL2-switch's `SDL_GameController` names the face buttons by position, as on an Xbox pad: its A is the Joy-Con's
  bottom button (labelled B), its X the left one (labelled Y). A game whose prompts name a letter ("press X") must map
  by the printed labels (SDL A→B, B→A, X→Y, Y→X), which also puts menu confirm on the right-hand A as on Nintendo's
  consoles.
- Unresolved here (2026-10-22): with very large mods, the console disconnects the *attached*
  Joy-Cons and shows its "wireless controllers can't be used in airplane mode" dialog during one
  long loading frame (7-9 s of image decoding). Ruled out by measurement: the applet loop not
  being serviced, and the garbage collector. If you hit it, sample `hidGetNpadStyleSet()` every
  100 ms from a watchdog thread and log what the app was doing at the drop (see
  `docs/ARCHITECTURE.md`, controller diagnostics); keep long loads short in the first place.

## Native C++ pitfalls on devkitA64

- A declaration of a global defined elsewhere must not sit inside an anonymous namespace: the
  host linker may tolerate it, the Switch link (PIE) fails with "relocation ... may bind
  externally" / "undefined reference to `(anonymous namespace)::...`". Declare such externs at
  file scope.
- A function added inside an `extern "C"` block has C linkage: declare it `extern "C"` on the
  using side too.
- Two consecutive NROs can have exactly the same size: never identify a build by size. Write the
  commit (`git describe --always --dirty`) and build time into a romfs file and log it at start-up;
  check that line first in every log a user sends. Grep the NRO for a new log string
  (`strings app.nro | grep ...`) before handing it over.
- `char` is **unsigned** on AArch64 (GCC and clang), signed on x86 and with MSVC. Code written for
  Windows/x86 that keeps negative values in a plain `char` (font glyph offsets: text drawn 256 ×
  scale too low) breaks silently: build it with `-fsigned-char`.
- Read the CPU counter with `mrs cntpct_el0` (libnx's `armGetSystemTick`), never `cntvct_el0`: Horizon lets user code
  read the physical counter only, so the virtual one traps on the console while Ryujinx emulates both. An `__rdtsc`
  shim written for AArch64 Linux (`cntvct_el0` is the usual choice there) crashed BO1 at the first frame of its
  profiler, and only on hardware (BO-Zombies_switch, 2026-10-26). `objdump -d app.elf | grep cntvct_el0` must be empty.
- `raise(SIGTRAP)` (a common `__debugbreak` shim) does nothing on the console: newlib/libnx have no handler, so a failed
  assert logs and the program goes on. That is unlike Linux, where SIGTRAP kills the process. An assert that holds
  every frame therefore costs its logging every frame. In BO1 that was two SD files opened, appended and closed per
  assert, and a check left at a 32-bit limit made Kino crawl at ~1 fps (BO-Zombies_switch, 2026-10-26). Never rule
  asserts out of a console slowdown by assuming they crash. Log each assert site a few times, then count, and read the
  assert log first.
- GCC (devkitA64 and i686 alike) exploits an out-of-bounds array index, which decompiled code is
  full of (`v.array[i]` with `i` past the array to reach the next fields): it may compute only the
  in-bounds element and drop the rest. Read such bytes through a `char` pointer to the object.

## Verification workflow

- Build the same C++ for the desktop (SDL2 + GL) and test there first: scripted input
  (`WORMHOLE_SCRIPT`-style: timed clicks, keys, typed text, named frame dumps) and frame dumps
  through `glReadPixels` (PPM → PNG with `convert`, then look at them). Root-window screenshots
  do not work under WSLg.
- Compare dumped frames with `compare -metric AE` against references; animated screens can have
  several valid states: keep one reference per state and rerun before blaming a change.
- Replays are timing-sensitive: check `/proc/loadavg` first (other processes on a shared machine
  made scripted clicks land before the screen loaded), prefer the Release binary, and never
  auto-retry a scenario that consumes state (a "continue saved game" replay eats the save).
- Desktop Debug (-O0) timings mean nothing; desktop Release is closer, but under WSLg GL runs on
  llvmpipe (software), so only the CPU-side share of a desktop profile transfers to the console.
- Ryujinx: launch `Ryujinx.exe` from WSL with the NRO on its virtual SD card
  (`%AppData%\Ryujinx\sdcard`), read the app's own log there, capture the window with a
  **DPI-aware** script (`SetProcessDPIAware`, or the capture is zoomed/cropped at 125 % scaling),
  disable `check_updates_on_start`. Use it to prove boot, rendering and logs; it does not
  reproduce controller, timing or memory behaviour of the console.
- To drive an app's menu on Ryujinx without a pad:
  - back up `%AppData%\Ryujinx\Config.json`;
  - map Player 1 to `WindowKeyboard` (`input_config`);
  - bring the window to the front and send keys with `keybd_event`, each held ~200 ms (a `SendKeys` tap is shorter
    than the emulator's per-frame poll);
  - capture with the DPI-aware script;
  - restore the exact backup afterwards (check the md5), since the user's own pad lives in that file.

  An app with a start menu should also take a presence file to skip it (BO1: `skip_menu`), so scripted runs need no
  input at all.
- Give the user the NRO from one fixed hand-off folder, never the emulator's SD card that the
  test scripts overwrite. Report hardware results only from the user's own runs and logs.
- Make the app's log self-explaining: a periodic `perf:` line (fps, worst frame, update / render
  / swap split, GC, heap, objects, draws, GL time, I/O, memory), a line per frame above ~80 ms
  saying what it loaded, controller connect/disconnect lines with timestamps. Every console
  question then costs one user run instead of several.

## Shell and tooling (Claude Code sessions)

- `pkill -f pattern` kills the calling shell when the pattern appears in its own command line;
  write `pkill -f "[r]un.sh"`.
- Never run two Gradle builds of the same project at once: the second wipes the first's class
  output mid-compile.
- Long builds and replays go to background commands; wait for their notification instead of
  polling or sleeping.

## Clearwing VM (JVM bytecode → C++) specifics

- Pin the transpiler version (`com.thelogicmaster.clearwing-vm:transpiler:v3.1.3`); master is a
  mid-refactor WIP. A missing JDK method is a hard "Method not resolved" at transpile time.
- Run ProGuard (shrink only, no `-dontwarn` on `java.*`) before transpiling: its warnings are the
  exact list of runtime gaps. Provide missing classes in a `jdk-extras` module compiled against
  the Clearwing runtime as bootclasspath (exclude the redefined classes from that jar, or javac
  keeps resolving the originals); inspect runtime classes with
  `javap -bootclasspath <fatjar> -cp <fatjar>`, never against the host JDK.
- Kotlin must target JVM 11 (the ASM 7 visitor rejects `PermittedSubclasses` from JVM 17
  `sealed`); compile lambdas/SAM/string concat as classes / inline (`-Xlambdas=class
  -Xsam-conversions=class -Xstring-concat=inline`); no records, no pattern `switch`.
- Runtime stubs and gaps met on v3.1.3: `RandomAccessFile` throws, `File.renameTo` always
  returns false, `Enum.getDeclaringClass()` returns null, annotation instances miss their own
  methods, `java.nio.file` / `java.util.stream` absent, `String.isBlank/strip` absent,
  `Formatter` without `%n`, `System.getProperty("user.home")` null, `getenv` empty, RegExodus
  rejects `\p{Digit}` (so Kotlin's `toFloatOrNull()` fails: parse in a try instead). Add a source
  lint test for the ones the JVM tests cannot catch.
- A `try` with both `catch` and `finally` over the same range unwinds to the `finally` first:
  avoid that shape. The generated code can also leave a stale exception frame after a `return`
  inside `try`; reset the exception frame depth in push/pop stack frame (runtime patch).
- `useValueChecks` / `useLineNumbers` are tested with `#ifdef` and therefore always on; patch to
  `#if` if you want them off (no measurable gain here).
- GC: thresholds are compile definitions with desktop-sized defaults (collect every allocation
  past `GC_HEAP_THRESHOLD`): set them for the console; its own statistics are all under
  `#if false`, so patch counters in; direct ByteBuffers are not counted in its heap; the stock
  collector thread frees objects one lock at a time (see the malloc rule above): free them on the
  game thread in slices. Patch the generated runtime idempotently from the build script
  (`patchFile` with exact anchors that fail loudly when the runtime changes).
- Every string literal use goes through a mutex + setjmp pool lookup: put a thread-local cache in
  front of it. Bulk-copy floats into direct buffers with one native `memcpy` rather than
  per-element `ByteBuffer.putFloat` calls.

## deko3d bring-up (BO-Zombies_switch, 2026-10-26)

- Iterate deko3d on Ryujinx, not GL: a deko3d backend has no runtime shader compiler (DKSH is compiled offline by
  uam), so Ryujinx boots it in seconds and reproduces hardware crashes exactly. The GL path recompiles ~13k GLSL on
  Ryujinx's Mesa (~15 min) -- don't iterate there.
- Debug with the debug lib `libdeko3dd.a` (link `deko3dd`): it validates every call and reports API misuse through
  the device's `cbDebug`. It is much slower (validates each `dkShaderInitialize` etc.), so loading crawls -- use a
  long timeout, and switch back to `deko3d` (release) once state is clean.
- The release `libdeko3d.a` never calls `cbDebug`: every error goes to `dk::detail::RaiseError`, which is
  `diagAbortWithResult(MAKERESULT(359, DkResult))`, an instant `svcBreak` (error `2359-<DkResult>` on screen) with
  nothing logged. A failed `dkMemBlockCreate` therefore never returns null, and neither does a failed swapchain
  acquire/present, command-memory request or queue submit; a queue flushed after the GPU channel reported a fault
  aborts too. `diagAbortWithResult` is weak in libnx: define it to log the result and the frame-record chain (ELF
  offsets from `_start`) before the same `svcBreak` (BO-Zombies_switch `switch_main.cpp`). Release `RaiseError` keeps
  frame records, so the chain names the failing deko3d entry point and its caller.
- Make deko3d log through `svcOutputDebugString`, not printf or an SD file: Ryujinx does not capture the guest's
  stdout, and buffered virtual-SD writes are LOST when a deko3d assert aborts the process, so a message logged right
  before the assert never reaches the SD boot.log. `svcOutputDebugString` reaches Ryujinx's own log immediately and
  survives the abort. On a console without a debugger it reaches nothing: whatever must come back from a user's run
  (image dumps included) also goes to the SD log, through the process's one writer.
- Maxwell/deko3d cannot use the packed 16-bit colour formats (`DkImageFormat_RGB565_Unorm`, `RGB5A1_Unorm`) as a
  render target: `dkImageLayoutInitialize` with `DkImageFlags_UsageRender` asserts "specified format does not support
  DkImageFlags_UsageRender" (dk_image.cpp:353). Substitute `RGBA8_Unorm` for a render target in those formats. Z24S8
  (depth) and RGBA8/RGBA16/RGBA16F/R32F render fine. Hardcoded GetCaps formatFlags that claim every colour format is
  a render target will make the frontend create such a target; either substitute at image creation or report the
  flag accurately.
- On Ryujinx a bad draw does NOT abort (the debug lib catches API misuse, not runtime faults): the queue stalls and
  the next `dkFenceWait(obj, -1)` hangs forever, which looks like a mid-frame freeze. Give the fence a timeout so a
  fault degrades instead of freezing, and log around draws to find it. On the console the GPU channel reports the
  fault: a fence wait marks the queue errored (`dkQueueIsInErrorState`) and the next flush aborts (release rule above).
- Before blaming a draw for a freeze, check for zero "fence wait" lines: a render thread stuck with no backend log is
  spinning in engine code. D3D9 engines busy-wait on `IDirect3DQuery9::GetData` (occlusion, timestamps, events) while
  it returns S_FALSE: an unimplemented query must report a ready result, never "not ready".
- See the frame, don't guess it: host screenshots of Ryujinx miss its GPU surface (CopyFromScreen grabs whatever
  window is on top, PrintWindow returns black). Copy the presented image to a buffer in `Present`
  (`dkCmdBufCopyImageToBuffer`) and write a PPM to the SD; for a black frame, trace one frame pass by pass (target,
  draw count, first draw's state, bound textures, clears, blits) and capture the colour target of chosen passes. That
  located every bug below in one run each.
- Ryujinx's DMA copy into a block-linear image read-modify-writes a GOB-rounded range: for a small mip in the mip tail
  it runs past a tightly sized memory block and crashes the emulator with `InvalidMemoryRegionException: Not mapped:
  va=0xFFFFFFFFFFFFFFFF` (the unmapped-PTE sentinel) in `DmaClass.DmaCopy`, at a point that moves with timing. Give
  each image block slack (64 KiB worked). `dkCmdBufClear` keeps the memory added to a command buffer: add it once.
- deko3d records a whole frame and submits it at `Present`, so D3D9-style dynamic buffers must be **renamed**: a
  DISCARD (or a plain lock of a buffer the frame already reads) has to go to another backing store, or every draw
  recorded earlier in the frame reads the later data -- big stretched triangles (an engine ring buffer wrapping
  several times a frame). Track per store the last frame that read it; free memory only after that frame's fence.
- Implement D3D9 StretchRect (`dkCmdBufCopyImage` / `dkCmdBufBlitImage`): post-processing samples resolved copies of
  the scene, so a no-op blit gives a black frame while the lit pass holds the whole scene.
- Barriers: render-then-sample needs `dkCmdBufBarrier(DkBarrier_Fragments, DkInvalidateFlags_Image)` (deko3d's
  deferred example); CPU-rewritten descriptors need `DkInvalidateFlags_Descriptors`; CPU writes to a GPU-cached block
  the GPU read before (reused staging, uniform ring) need `DkInvalidateFlags_L2Cache` before the next read.
- deko3d always scissors: when the pipeline's scissor test is off, set the scissor to the whole target yourself.
- Ryujinx 1.1.1375 ignores the vertex-attribute BGRA bit (`DkVtxAttribState::isBgra`, bit 31 of the Maxwell register,
  which libdeko3d writes unchanged): BGRA vertex colours (D3DCOLOR) show red and blue swapped on the emulator only.
  Don't swap them in the backend to please Ryujinx; check colours on the console.
- On Ryujinx, every SD write can fail with EIO once a large map has loaded (the app's own log file stops too). Send
  debug images through `svcOutputDebugString` as hex lines (downscaled, plus full-resolution crops of the region of
  interest) and rebuild them on the PC.
- Depth bias: `dkCmdBufSetDepthBias(constant, clamp, slope)` takes GL's `glPolygonOffset` units (libdeko3d doubles the
  constant as nouveau does); a D3D9 `D3DRS_DEPTHBIAS` is a depth offset, so pass `bias × 2^24` (`2^16` for D16).
- `dkQueueSignalFence(queue, &fence, flush)`: `flush` flushes the GPU caches, it does not submit. Call `dkQueueFlush`
  (a `dkQueuePresentImage` also submits) before `dkFenceWait`, or the wait times out on an idle GPU. To wait without
  freezing on a fault, wait on such a fence in 2 s steps, log each step, and give up only when
  `dkQueueIsInErrorState` says the queue faulted: Ryujinx can keep a healthy queue busy for seconds while it
  translates shaders, and memory the caller reuses after a wait must be idle for real.
- Never give each texture or buffer its own `DkMemBlock`: the console refuses new blocks past ~1330 live
  (`dkMemBlockCreate` → `DkResult_Fail` → abort `2359-0001`, measured twice at 1331/1332 blocks holding only 296 MB),
  while Ryujinx has no such limit (2860 blocks on the BO1 menu, 11 210 in a map, both fine there). Each block is an
  `nvMapCreate` plus an `nvAddressSpaceMap` in nvservices. Suballocate from large blocks, as deko3d's sample framework
  (`CMemPool`) does: images `GpuCached | Image`, data `CpuUncached | GpuCached`, ranges at the layout's alignment; BO1
  went from 1384 to 14 blocks at its first frame. Log the live block count.
- uam is not deterministic: its output depends on its heap addresses, so with address randomisation the same GLSL compiles to different code from run to run (1790 of 13 704 shaders differed between two packs of the same `shaders.pack`). Run it under `setarch $(uname -m) -R` (BO-Zombies_switch `make_dksh_pack.py`): two full builds then give the same bytes. Never compare two DKSH packs, or bisect a console rendering bug across them, without that.
- **Never bind uam's control words as they are.** uam (1.1.0, its Maxwell scheduler carries `fincs-edit`s over Mesa's)
  dual-issues an ALU instruction with the next TEX/IPA/MUFU/branch/memory one (stall 0 + yield bit); Mesa 20.1, which
  the GL path uses, never dual-issues (`canDualIssue`: "TODO, return false"). On the console, whole warps of a draw
  then compute a wrong colour as soon as many warps share an SM: warp-sized 4x8 tiles of one colour (blue in a dark
  lit pass), different every frame, absent on Ryujinx and with GL. Rewrite each DKSH's control words before placing
  the code: stall 0 becomes 1 with the yield bit cleared, and any instruction that sets a write or read barrier gets a
  stall of at least 2 (BO-Zombies_switch `UseMesaSchedule`; 785 000 of BO1's control words, 1 109 blue pixels in a
  test crop down to 0). It only delays issue; measure the frame rate it costs.
- To place a hardware-only rendering fault, run several one-frame experiments in one console session (each switched
  on for one traced frame, the pass crops compared pixel for pixel while the player stands still): a barrier with
  every cache invalidation before each draw tells a cross-draw fault from an intra-draw one; a program header that
  declares 255 registers (a CPU copy of the DKSH control section given to `dkShaderInitialize` through
  `DkShaderMaker::control`, `codeOffset` at the code) leaves ~8 warps per SM and tells warps interfering from a wrong
  computation. Then diff the shader compiler against the one that works (uam's `mesa-imported/codegen` against Mesa's
  same files).
- Ruled out on the way, don't chase them again for such tiles: the fragment register `0x0360` (`param_d8`, uam writes
  `0x087F6080`, Mesa `0x20164010`: no change), vertex outputs vs pixel inputs (compare the program headers' OMAP/IMAP
  per pipeline), `IPA` reading undeclared components, scratch and call-return-stack sizes (all 0; deko3d never checks
  a graphics shader's `per_warp_scratch_sz` against the queue's `perWarpScratchMemorySize`), stale constant caches
  (`DkInvalidateFlags_Shader` drops them), uniform branches (`BRA.U`).
- Decode a DKSH exactly before trusting a hazard scan (envytools `gm107.c` has every field): a `TEXS` writes 1 to 4
  registers depending on its component selector (bits 50-52) and on its second destination (bits 28-35) being RZ, a
  `TEX` one register per enabled component (mask at bits 31-34), and `RRO`, `F2F`, `I2F` read only Rb. Guessing "4
  registers" flagged 11 280 of 13 348 pixel shaders that were in fact hazard-free.
