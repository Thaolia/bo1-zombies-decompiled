# Renderer backend contract

`src/gfx_backend/gfx_backend.h` is the only thing `src/gfx_d3d9emu/` may call. It is modelled on an explicit,
low-level API so that a deko3d backend can implement it later without changing the frontend or the game. The
header is written in phase 4. This file fixes its rules now.

## Contract
- **Resources**: `CreateBuffer`, `CreateTexture` and `CreateRenderTarget` take a declared size, format, usage
  (static / dynamic / staging / render target / depth) and memory class. Resources never change usage after
  creation.
- **Uploads**: through `UploadStaging` into a resource. The frontend implements D3D9 `Lock`/`LockRect` on top of
  staging memory and never maps a GPU resource directly.
- **Pipeline state**: blend, depth-stencil, rasterizer, vertex layout and the shader pair are hashed into an
  immutable pipeline-state object. The backend creates and caches it on first use.
- **Passes**: `BeginPass(targets, load/clear ops)` … `EndPass()`. All rendering happens inside a pass with
  explicit render targets.
- **Bindings**: UBOs and samplers are bound to fixed slots that match the `layout(binding=N)` written by `ffconv`.
  There are no free uniforms.
- **Draws**: `Draw`, `DrawIndexed`. `Present` and `WaitIdle` are the only sync points the frontend sees.

## Review checklist (every change to `gfx_backend.h`)
1. No implicit global state: everything a draw needs is in its pipeline state, bindings or current pass.
2. No queries that read back GPU or driver state (`glGet*`, sync readbacks) outside `WaitIdle`.
3. No implicit synchronisation. Hazards between passes are expressed by pass boundaries.
4. Every resource declares format, usage and memory class at creation.
5. Nothing names a GL object, enum or extension.
6. The null backend (`src/gfx_backend/null/`) still implements the whole header.

## Shaders
`ffconv` translates D3D9 SM3 bytecode with MojoShader. It then normalises the result to GLSL 4.60 core:
- `c#`, `i#` and `b#` registers live in std140 UBOs;
- samplers have explicit bindings;
- no vendor extensions.

The GL backend compiles this GLSL with mesa. A deko3d backend would compile the same GLSL offline with `uam` into
DKSH (`ffconv --dksh`).
