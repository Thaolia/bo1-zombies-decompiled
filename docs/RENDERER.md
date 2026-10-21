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
`ffconv` translates D3D9 SM3 bytecode with MojoShader (`third_party/mojoshader`, GLSL profile) and normalises the
result to GLSL 4.60 core without extensions (`tools/ffconv/src/ff_glsl.cpp`). Every shader of the zombies zones is
vs_3_0 or ps_3_0 and only uses float constants (`c#`) and samplers (`s#`).

The GL backend compiles this GLSL with mesa. A deko3d backend would compile the same GLSL offline with `uam` into
DKSH (`ffconv --dksh`).

### Shader interface
A shader is found by the FNV-1a 64 hash of its D3D9 bytecode (`ff::ShaderHash`); the frontend computes the same
hash on the bytecode the zone hands to `Load_CreateMaterial*Shader`.

| What | GLSL | Filled from |
|---|---|---|
| Vertex constants `c0..c255` | `layout(std140, binding = 0) uniform BO1VertexConstants { vec4 vs_c_reg[256]; }` | `SetVertexShaderConstantF` registers |
| Pixel constants `c0..c255` | `layout(std140, binding = 1) uniform BO1PixelConstants { vec4 ps_c_reg[256]; vec4 bo1_vpos_flip; vec4 bo1_alpha_test; }` | `SetPixelShaderConstantF` registers, then two state vectors |
| `bo1_vpos_flip.xy` | `vPos.y = gl_FragCoord.y * x + y` | (1, 0) or (-1, target height), whichever gives D3D9's top-left origin for the bound target (phase 4) |
| `bo1_alpha_test` | `x`: 0 off, 1 `alpha > ref`, 2 `alpha >= ref`; `y`: ref, 0-255 | `loadBits[0]` alpha-test bits (`0x1000` > 0, `0x2000` >= 255, `0x3000` >= 128) |
| Sampler `s#` | `layout(binding = #)` | `SetTexture(#)` and its sampler state |
| Vertex input | `layout(location = d) in`, `d` = the material stream destination (`s_streamDestInfo`, 0-19) | the vertex declaration routing |
| Varyings | `io_<usage>_<index>` at location `index` (TEXCOORD0-15) or `16 + index` (COLOR0-1), `centroid` kept on pixel inputs | — |
| Pixel output | `layout(location = 0) out vec4 bo1_color0` | — |

Literal constants (`def`) stay in the GLSL; a `def` range the shader indexes with `a0` becomes a `const` array.

Phase 4 still has to check against D3D9: the clip-space depth range and pixel-centre offset (glClipControl), the
render-target y flip, and whether pixel-shader COLOR inputs need clamping to [0, 1].
