# Renderer: D3D9 frontend and backend contract

The engine's 64 `gfx_d3d` files keep calling Direct3D 9. On posix (and later the Switch), Direct3D 9 is
`src/gfx_d3d9emu/`, a frontend that records into `src/gfx_backend/gfx_backend.h`, a contract modelled on an
explicit, low-level API, so that a deko3d backend can implement it later without changing the frontend or the game.
`src/gfx_d3d9emu/` must contain no GL or deko3d calls: it only talks to `gfx_backend.h` and to its host
(`d3d9emu_host.h`: displays, windows, the `shaders.pack` path, screenshot files, the log), which the platform layer
implements (`src/platform/posix/posix_d3d9host.cpp`).

The backends sit behind a dispatcher (`gb_backend.cpp`): each fills a table of the contract's functions in its own
namespace (`gb::null`, `gb::gl`), and `gb::Init` picks one. `BO1_GFX_BACKEND=null` or `gl` names it; without the
variable it is GL when the platform gives a context, else null. A headless run gets a context only when
`BO1_GFX_BACKEND=gl` asks for one (its back buffer screenshots then show what GL drew).

## Contract (`gfx_backend.h`)
- **Conventions are D3D9's**: row 0 of a texture is its top row (uploads, attachments, viewports, scissors and
  fragment coordinates count from it), clip-space depth runs from 0 to 1, clockwise triangles face the front. The
  backend reproduces them (GL: `glClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE)`, `glFrontFace(GL_CW)`, a flipped copy
  at present). **Rasterisation is the backend API's own** (pixels sampled at their middle, viewports covering whole
  pixels): D3D9's half-pixel offset is in the shaders, `bo1_pos_fixup` below. A viewport moved by half a pixel
  instead would clip geometry half a pixel inside the first column and row, which multisampling shows as a line of
  clear colour (seen on Kino with 4 samples, 2026-10-24).
- **Resources** declare format, usage (static, dynamic, colour or depth attachment), memory class, whether they are
  sampled, sample count and a component swizzle at creation, and never change. Destruction never waits: the backend
  frees what the GPU still uses later.
- **Uploads**: `UploadBuffer` (ordered, discard, or no-overwrite, D3D9's lock flags) and `UploadTexture` (a box of
  one level and layer), in order with the recorded work around them, from any thread.
- **State objects**: `GetSampler` and `GetPipeline` take a description and return an immutable object, cached by
  value (`gb_common.h` gives the canonical keys). A pipeline holds the shader pair, topology, vertex layout, blend,
  depth-stencil, rasterizer state and the formats of the attachments it renders into. `DestroyShader` ends the
  pipelines made with the shader, so that a new shader at the same address never meets a stale pipeline.
- **Passes**: `BeginPass` (attachments, load operations: load, clear, don't care) … `EndPass`. All rendering happens
  inside a pass; copies and readbacks outside. Hazards between passes are resolved at pass boundaries; inside a pass
  nothing samples an attachment. Attachments share their sample count; the pass covers the area they all have.
- **Draws**: `Draw`, `DrawIndexed` take a `DrawState` holding everything the draw reads besides its pipeline (vertex
  and index buffers, textures with their samplers, the two constant blocks, viewport, scissor, stencil reference,
  blend constant): nothing carries over from one draw to the next. Constants are copied into per-frame uniform memory
  with `AllocUniforms`.
- **Sync points**: `ReadTexture` (waits for the work that writes it), `WaitIdle`, `Present`. Query results and fences
  are polled and never wait.
- **Threads**: the frontend makes one call at a time. Creation, destruction and uploads may come from any thread;
  recording from one thread at a time (the render thread), which a thread-bound device (a GL context) follows.

## Review checklist (every change to `gfx_backend.h`)
1. No implicit global state: everything a draw needs is in its pipeline, its `DrawState` or the current pass.
2. No query that reads back GPU or driver state (`glGet*`, sync readbacks) outside `ReadTexture`, `WaitIdle` and
   `Present`; queries and fences only report what is ready.
3. No implicit synchronisation. Hazards between passes are expressed by pass boundaries.
4. Every resource declares format, usage and memory class at creation.
5. Nothing names a GL object, enum or extension.
6. The null backend (`src/gfx_backend/null/`) still implements the whole header, and `tests/gfx_d3d9emu` passes; so
   does `tests/gfx_backend` on the GL backend.

## The frontend (`src/gfx_d3d9emu`)
It implements the part of Direct3D 9 the engine calls, inventoried with libclang over the posix build (87 methods,
`AddRef` and `Release` included); every other method ends the process naming itself (`Unemulated`), so a new caller
is noticed at once. One recursive lock serialises every call, as the runtime does for a `D3DCREATE_MULTITHREADED` device.

- **Objects** have two reference counts, as in the runtime (and Wine): the application's, and the device's while
  the object is bound, so a texture released while bound lives until it is unbound. A resource holds a reference on
  its device while the application holds one on it. Texture levels share their texture's counts, the back buffer and
  the swap chain the device's.
- **Locks**: a lockable resource (managed, dynamic, system memory, scratch, offscreen plain) keeps a CPU copy of each
  locked level, so a lock sees what was written (the model lighting volume is locked every frame for a few texels).
  The unlock uploads the locked region. A copy the GPU wrote since (`StretchRect` into a lockable surface) is read
  back at the next lock.
- **Passes follow the render targets**: changing a target or the depth buffer ends the pass, and the next draw or
  clear begins one. A clear of the whole targets before the pass's first draw becomes its load operation; other
  clears are `gb::Clear` on the viewport, scissor and rects, as D3D9 clears.
- **Pipelines** are rebuilt only when a state that feeds them changed; constants are uploaded (4112 bytes per
  block: the 256 registers and one state vector) only when they changed, and again after each `Present`.
- **Draws** bind only the samplers the pixel shader declares (its `dcl` tokens). A sampler that reads an attachment of
  the pass (undefined in D3D9 too) is left unbound and logged once. `DrawPrimitiveUP` streams through one dynamic
  buffer, padded with zeros past the vertices, and leaves stream 0 unset as the runtime does.
- **Queries**: event queries are fences; occlusion queries count over one backend query per pass they span, and one
  counts at a time (a query opened inside another pauses it); timestamps are in nanoseconds (frequency 1 GHz, never
  disjoint).
- **Vendor tricks the engine uses**: `INTZ` depth textures (sampled as depth in every component), AMD's RESZ resolve
  (`D3DRS_POINTSIZE` = `0x7FA05000` copies the depth buffer into the INTZ texture at sampler 0), `NULL` render
  targets (no colour attachment), and hardware shadow maps (a D24S8/D16 texture created as a depth-stencil and
  sampled with `depthCompare`).
- **The adapter** reports one display (the host's first), no PCI vendor (the engine's NVIDIA/ATI paths stay off), a
  shader model 3 device with the caps `gfx_d3d/r_caps.cpp` requires, the formats the backend supports (D24FS8,
  `ATOC`/`SSAA` transparency AA, vertex texture fetch and sRGB are not reported), and up to 4 GB of texture memory, as
  a 32-bit runtime would. A 9Ex device has no managed pool, and its `Present` returns `S_PRESENT_OCCLUDED` when the
  window cannot be seen (the headless client's hidden window, as on Windows' private desktop).

- **D3DX**: `D3DXSaveSurfaceToFileA` writes the engine's screenshots (`screenshot`, `screenshotJpeg`) from a 32-bit
  surface as JPG, TGA, BMP or PNG with stb_image_write (`third_party/stb`), the file through the host; TGA is
  uncompressed and JPG and BMP drop the alpha, as D3DX writes them.

Not emulated (the engine never calls them on these paths): the fixed-function pipeline (Bink only), vertex textures,
state blocks, sRGB reads and writes, lockable render targets, clip planes, cursors, point sprites, autogenerated
mipmaps.

## The null backend (`src/gfx_backend/null`)
It implements the whole header without a GPU: it draws nothing and keeps no texel, and its results are those of a
device that drew nothing (occlusion queries count 0, fences are reached at once, readbacks return zeros). It checks
every call against the contract and ends the process on a breach (a draw outside a pass, a pipeline whose formats do
not match the pass, an upload outside a resource, a sampled attachment, a blit inside a pass…), so a headless run on
it proves the frontend keeps the contract. `gb_null.h` records every call as a line, which `tests/gfx_d3d9emu` checks.

## The GL backend (`src/gfx_backend/gl`)
OpenGL 4.3 core with `glClipControl` (4.5 or `ARB_clip_control`); `EXT_texture_compression_s3tc` for the BC formats.
It links against no GL library: its host (`gb_gl_host.h`, implemented by `src/platform/posix/posix_glhost.cpp` with
SDL2) makes the context on a hidden 1x1 window, gives the entry points (`gb_gl_functions.h` lists every one it calls)
and the windows to present to; the Switch would implement the same host with EGL.

- **One thread owns the context** (`gb_gl_device.cpp`). The side the frontend calls (`gb_gl.cpp`) hands out the
  objects at once and records the work into chunks, handed over every 512 commands or 1 MB of payload and at each
  `Present`. Compiles and links go on a second lane, served between commands: a `GetPipeline` waits for its link, not
  for the frames queued before it. The only other waits are the contract's sync points (`ReadTexture`, `WaitIdle`,
  `DestroySwapchain` so that the window can go) and `Present` once the thread is a frame behind. A query or fence
  still being recorded is handed over when it is polled (`D3DGETDATA_FLUSH`). The thread's log lines reach the engine
  through the frontend's calls.
- **State**: one vertex array per vertex layout (`glVertexAttribFormat`/`Binding`), framebuffers cached by
  attachments, sampler objects, and a cache of everything bound so that a draw makes only the calls that change
  something. Uploads bind on texture unit 16, which no draw samples, and make it the active unit first: a draw
  leaves another one active, and a call on a target acts on the active unit.
- **Uniforms**: frame-local blocks of 1 MB, persistently mapped when the context has buffer storage, recycled behind
  a fence per frame; the frontend side places each `AllocUniforms` (never across a block) and the backend's thread
  writes it at that offset.
- **Conventions**: `glClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE)` makes window row 0 an attachment's first row, so
  viewports, scissors, blits and readbacks take D3D9's rows unchanged; the upper-left origin leaves GL judging facing
  as with its own, so D3D9's clockwise front faces are `GL_CW`. Depth bias: `glPolygonOffset(slope, bias * 2^24)`
  (2^16 on D16). `Present` draws the image into the window's bottom-up framebuffer through the gamma ramp (a 256x1
  texture), scaled to the drawable.
- **Formats**: BGRA8 is `GL_RGBA8` uploaded as `GL_BGRA`; BGRX8 is `GL_RGB8`, so that its alpha reads 1 as D3D9's
  X8 formats do; B5G6R5 and B5G5R5A1 are `GL_RGB565` and `GL_RGB5_A1` with the packed types; BC1-3 the S3TC formats;
  D24S8 `GL_DEPTH24_STENCIL8`. Which formats are sampled and rendered into is tested at start-up with a framebuffer
  per format. The swizzle is `GL_TEXTURE_SWIZZLE_RGBA`.
- `BO1_GL_DEBUG=1` turns on the debug output (synchronous, the first 64 messages into the log).
- Every 10 s `Present` logs a `gl perf:` line: frames, draws a frame, bytes uploaded, new pipelines and links, where
  the frontend side waited (frames, links, sync points) and how long the backend's thread spent on commands, builds
  and swaps.

`tests/gfx_backend/gb_gl_test.cpp` draws with the backend alone under SDL's offscreen driver and reads the pixels
back: row 0 on top, fragment coordinates, depth and the viewport's depth range, viewport and scissor rows, clockwise
front faces and two-sided stencil, pixel centres through `bo1_pos_fixup` (with the fixup turned off it fails),
4-sample edge pixels (with the old half-pixel viewport it fails), texture rows and formats (B5G6R5, B5G5R5A1,
RGBA8, BC1, an L8 swizzle), an upload after a draw that left another unit active, BGRX8 alpha, `Clear` rects,
`Blit` copies and scaling, a hardware shadow map compare, occlusion counts, timestamps and fences, and a shader that
does not compile.

## Shaders
`ffconv` translates D3D9 SM3 bytecode with MojoShader (`third_party/mojoshader`, GLSL profile) and normalises the
result to GLSL 4.60 core without extensions (`tools/ffconv/src/ff_glsl.cpp`). Every shader of the zombies zones is
vs_3_0 or ps_3_0 and only uses float constants (`c#`) and samplers (`s#`); none samples in a vertex shader.

The GL backend compiles this GLSL with mesa. A deko3d backend would compile the same GLSL offline with `uam` into
DKSH (`ffconv --dksh`).

### Shader interface
A shader is found by the FNV-1a 64 hash of its D3D9 bytecode (`ShaderPack_Hash`). The device only receives a pointer:
the frontend measures the bytecode up to its end token (`ShaderBytecodeSize`), which gives the length `ffconv`
hashed for all 13 704 shaders of the zones.

| What | GLSL | Filled from |
|---|---|---|
| Vertex constants `c0..c255` | `layout(std140, binding = 0) uniform BO1VertexConstants { vec4 vs_c_reg[256]; vec4 bo1_pos_fixup; }` | `SetVertexShaderConstantF` registers, then a state vector |
| `bo1_pos_fixup.xy` | `gl_Position.xy += bo1_pos_fixup.xy * gl_Position.w` at the end of `main` | (63/64 / width, -63/64 / height) of the viewport: the vertices move right and down by just under half a pixel before clipping, as D3D9 hardware moves them after the viewport transform; 63/128 rather than 1/2 (wined3d's value) keeps an edge through a D3D9 pixel centre on D3D9's side whichever way the backend breaks ties |
| Pixel constants `c0..c255` | `layout(std140, binding = 1) uniform BO1PixelConstants { vec4 ps_c_reg[256]; vec4 bo1_alpha_test; }` | `SetPixelShaderConstantF` registers, then a state vector |
| `vPos` | `vec4 ps_vPos = vec4(gl_FragCoord.xy - 0.5, gl_FragCoord.zw)` | — (D3D9's integer pixel position; fragment rows already count from the top) |
| `bo1_alpha_test` | `x`: 0 off, 1 `alpha > ref`, 2 `alpha >= ref`; `y`: ref, 0-255 | `D3DRS_ALPHATESTENABLE`, `ALPHAFUNC` (the engine only sets `GREATER` and `GREATEREQUAL`), `ALPHAREF` |
| Sampler `s#` | `layout(binding = #)` | `SetTexture(#)` and its sampler state |
| Shadow map sampler `s#` | `sampler2DShadow` for the samplers the constant table names `shadowmapSamplerSun` or `shadowmapSamplerSpot`; each lookup returns `vec4(...)` of the comparison, with the reference from z (`textureProj`: z / w; `texture` and `textureLod` take `.xyz` where MojoShader passed `.xy`) | the shadow map's depth texture, with a `depthCompare` sampler (`LEQUAL`) |
| Vertex input | `layout(location = d) in`, `d` = `ShaderPack_InputLocation(usage, index)` (`shader_pack.h`), the material stream destination (`s_streamDestInfo`, 0-19) | the vertex declaration's elements |
| Varyings | `io_<usage>_<index>` at location `index` (TEXCOORD0-15) or `16 + index` (COLOR0-1), `centroid` kept on pixel inputs | — |
| COLOR output clamp | each `io_10_#` (COLOR0-1) a vertex shader writes is `clamp(..., 0.0, 1.0)` at the end of `main` | D3D9 clamps COLOR-semantic interpolators to [0, 1] at the vertex output, before interpolation; MojoShader routed them through `gl_FrontColor`/`gl_Color` (clamped by default in compatibility GL), and the rename to core-profile varyings dropped that |
| Pixel output | `layout(location = 0) out vec4 bo1_color0` | — |

Literal constants (`def`) stay in the GLSL; a `def` range the shader indexes with `a0` becomes a `const` array.

### Open for the GL backend
- `#version 460`: Mesa 20.1 on the Switch (nouveau) may offer only 4.3 to 4.5; the normaliser may have to write the
  lowest version the shaders need.
