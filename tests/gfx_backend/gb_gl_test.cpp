// The GL backend against the contract (src/gfx_backend/gfx_backend.h): it draws with the backend alone and reads the
// pixels back, so each check proves one of D3D9's conventions on GL (row 0 on top, depth from 0 to 1, clockwise front
// faces, two-sided stencil, and pixel centres on integers through the shaders' bo1_pos_fixup, multisampled edges
// included) or one format, copy, query or fence path. It needs a GL 4.3 core context: run it as
// SDL_VIDEODRIVER=offscreen ./gb_gl_test, which shows nothing on the display.

#include <gfx_backend/gfx_backend.h>
#include <gfx_backend/gl/gb_gl_host.h>

#include <SDL.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

namespace
{
int g_failures = 0;
int g_checks = 0;

void Check(bool condition, const char *what)
{
    ++g_checks;
    if (!condition)
    {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

SDL_Window *g_hidden = nullptr;
SDL_GLContext g_context = nullptr;
} // namespace

// The test's host: SDL's context on a hidden window, as posix_glhost.cpp makes it.
namespace gb::gl::host
{
bool Open()
{
    SDL_GL_ResetAttributes();
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
    g_hidden = SDL_CreateWindow("", 0, 0, 1, 1, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!g_hidden)
    {
        std::printf("no OpenGL window: %s\n", SDL_GetError());
        return false;
    }
    g_context = SDL_GL_CreateContext(g_hidden);
    if (!g_context)
    {
        std::printf("no OpenGL 4.3 core context: %s\n", SDL_GetError());
        return false;
    }
    SDL_GL_MakeCurrent(g_hidden, nullptr);
    return true;
}

bool MakeCurrent(void *window)
{
    return SDL_GL_MakeCurrent(window ? static_cast<SDL_Window *>(window) : g_hidden, g_context) == 0;
}

void ReleaseCurrent()
{
    SDL_GL_MakeCurrent(g_hidden, nullptr);
}

void *ProcAddress(const char *name)
{
    return SDL_GL_GetProcAddress(name);
}

void DrawableSize(void *window, int *width, int *height)
{
    SDL_GL_GetDrawableSize(static_cast<SDL_Window *>(window), width, height);
}

void SetSwapInterval(int interval)
{
    SDL_GL_SetSwapInterval(interval);
}

void Swap(void *window)
{
    SDL_GL_SwapWindow(static_cast<SDL_Window *>(window));
}

void Close()
{
    SDL_GL_DeleteContext(g_context);
    SDL_DestroyWindow(g_hidden);
}

void Log(const char *line)
{
    std::printf("%s\n", line);
}
} // namespace gb::gl::host

namespace
{
constexpr int kSize = 64;

// The frontend's shader interface as ffconv writes it (docs/RENDERER.md), bo1_pos_fixup included, with c0.x choosing
// what the pixel shader writes: the vertex colour (0), the texture at s0 (1), the fragment's integer coordinates
// in red and green (2), or the shadow test of s1 with c0.y as the reference depth (3).
const char kVertex[] = R"(#version 430 core
layout(std140, binding = 0) uniform BO1VertexConstants { vec4 vs_c_reg[256]; vec4 bo1_pos_fixup; };
layout(location = 0) in vec4 position;
layout(location = 1) in vec4 colour;
layout(location = 2) in vec2 uv;
layout(location = 0) out vec4 io_colour;
layout(location = 1) out vec2 io_uv;
void main()
{
    gl_Position = position;
    io_colour = colour;
    io_uv = uv;
    gl_Position.xy += bo1_pos_fixup.xy * gl_Position.w;
}
)";

const char kPixel[] = R"(#version 430 core
layout(std140, binding = 1) uniform BO1PixelConstants { vec4 ps_c_reg[256]; vec4 bo1_alpha_test; };
layout(binding = 0) uniform sampler2D s0;
layout(binding = 1) uniform sampler2DShadow s1;
layout(location = 0) in vec4 io_colour;
layout(location = 1) in vec2 io_uv;
layout(location = 0) out vec4 bo1_color0;
void main()
{
    if (ps_c_reg[0].x > 2.5)
        bo1_color0 = vec4(textureProj(s1, vec4(io_uv, ps_c_reg[0].y, 1.0)));
    else if (ps_c_reg[0].x > 1.5)
        bo1_color0 = vec4(floor(gl_FragCoord.xy) / 255.0, 0.0, 1.0);
    else if (ps_c_reg[0].x > 0.5)
        bo1_color0 = texture(s0, io_uv);
    else
        bo1_color0 = io_colour;
}
)";

struct Vertex
{
    float position[4];
    float colour[4];
    float uv[2];
};

struct Rgba
{
    float r, g, b, a;
};

struct Pixel
{
    uint8_t b, g, r, a;
    bool Is(uint8_t red, uint8_t green, uint8_t blue) const
    {
        return r == red && g == green && b == blue;
    }
};

struct Fixture
{
    gb::Buffer *vertices = nullptr;
    uint32_t used = 0; // vertices written this frame
    gb::Shader *vs = nullptr, *ps = nullptr;
    gb::Texture *target = nullptr, *depth = nullptr;
    const gb::Sampler *nearest = nullptr;
    std::vector<Vertex> staging;
    gb::Swapchain *swapchain = nullptr;
    SDL_Window *window = nullptr;
};

Fixture g;

// A quad between NDC x0..x1 and y0..y1 (y up), as two triangles clockwise on screen, with uv from the top-left.
void Quad(std::vector<Vertex> &out, float x0, float y0, float x1, float y1, float z, Rgba c, float u0 = 0.0f,
          float v0 = 0.0f, float u1 = 1.0f, float v1 = 1.0f)
{
    const Vertex tl = {{x0, y1, z, 1.0f}, {c.r, c.g, c.b, c.a}, {u0, v0}};
    const Vertex tr = {{x1, y1, z, 1.0f}, {c.r, c.g, c.b, c.a}, {u1, v0}};
    const Vertex br = {{x1, y0, z, 1.0f}, {c.r, c.g, c.b, c.a}, {u1, v1}};
    const Vertex bl = {{x0, y0, z, 1.0f}, {c.r, c.g, c.b, c.a}, {u0, v1}};
    out.insert(out.end(), {tl, tr, br, tl, br, bl});
}

gb::PipelineDesc Pipeline()
{
    gb::PipelineDesc d;
    d.vertex = g.vs;
    d.pixel = g.ps;
    d.attribCount = 3;
    d.attribs[0] = {0, 0, 0, gb::AttribType::Float4};
    d.attribs[1] = {1, 0, 16, gb::AttribType::Float4};
    d.attribs[2] = {2, 0, 32, gb::AttribType::Float2};
    d.colorCount = 1;
    d.colorFormats[0] = gb::Format::BGRA8;
    d.depthFormat = gb::Format::D24S8;
    return d;
}

const gb::Viewport kFull = {0.0f, 0.0f, float(kSize), float(kSize), 0.0f, 1.0f};

// The frontend's constants for a draw: bo1_pos_fixup, 63/128 of a pixel right and down, from the viewport's size.
gb::DrawState State(const gb::PipelineDesc &desc, float mode, const gb::Texture *texture = nullptr,
                    const gb::Viewport &viewport = kFull, float reference = 0.0f)
{
    gb::DrawState s;
    s.pipeline = gb::GetPipeline(desc);
    s.vertices[0] = {g.vertices, 0, sizeof(Vertex)};
    std::vector<float> vc(1028, 0.0f), pc(1028, 0.0f);
    vc[1024] = 2.0f * (63.0f / 128.0f) / viewport.width;
    vc[1025] = -2.0f * (63.0f / 128.0f) / viewport.height;
    pc[0] = mode;
    pc[1] = reference;
    s.vertexConstants = gb::AllocUniforms(vc.data(), uint32_t(vc.size() * sizeof(float)));
    s.pixelConstants = gb::AllocUniforms(pc.data(), uint32_t(pc.size() * sizeof(float)));
    s.viewport = viewport;
    if (texture)
        s.textures[0] = {texture, g.nearest};
    return s;
}

// Uploads the quads and draws them.
void Draw(gb::DrawState s, const std::vector<Vertex> &quads)
{
    gb::UploadBuffer(g.vertices, g.used * uint32_t(sizeof(Vertex)), quads.data(),
                     uint32_t(quads.size() * sizeof(Vertex)), gb::Upload::NoOverwrite);
    gb::Draw(s, gb::Primitive::TriangleList, uint32_t(quads.size()), g.used);
    g.used += uint32_t(quads.size());
}

void BeginPass(gb::LoadOp load, Rgba clear, gb::LoadOp depthLoad = gb::LoadOp::Clear, float clearDepth = 1.0f)
{
    gb::PassDesc p;
    p.colorCount = 1;
    p.color[0].texture = g.target;
    p.color[0].load = load;
    p.color[0].clear[0] = clear.r;
    p.color[0].clear[1] = clear.g;
    p.color[0].clear[2] = clear.b;
    p.color[0].clear[3] = clear.a;
    p.depth = g.depth;
    p.depthLoad = depthLoad;
    p.stencilLoad = depthLoad;
    p.clearDepth = clearDepth;
    gb::BeginPass(p);
}

std::vector<Pixel> Read(gb::Texture *texture = nullptr)
{
    std::vector<Pixel> pixels(kSize * kSize);
    gb::ReadTexture(texture ? texture : g.target, 0, 0, {0, 0, 0, kSize, kSize, 1}, pixels.data(), kSize * 4);
    return pixels;
}

Pixel At(const std::vector<Pixel> &p, int x, int y)
{
    return p[size_t(y) * kSize + size_t(x)];
}

// Frames end with a Present on the test window, which also starts the next frame's uniform memory.
void EndFrame()
{
    gb::Present(g.swapchain, g.target);
    g.used = 0;
}

const Rgba kRed = {1, 0, 0, 1}, kGreen = {0, 1, 0, 1}, kBlue = {0, 0, 1, 1}, kBlack = {0, 0, 0, 1},
           kWhite = {1, 1, 1, 1};

void TestOrigin()
{
    BeginPass(gb::LoadOp::Clear, kRed);
    std::vector<Vertex> q;
    Quad(q, -1, 0, 1, 1, 0.5f, kGreen);
    gb::PipelineDesc d = Pipeline();
    d.raster.cull = gb::Cull::Back;
    Draw(State(d, 0), q);
    gb::EndPass();
    const auto p = Read();
    Check(At(p, 0, 0).Is(0, 255, 0) && At(p, 63, 31).Is(0, 255, 0), "clip-space +y is row 0 (top)");
    Check(At(p, 0, 32).Is(255, 0, 0) && At(p, 63, 63).Is(255, 0, 0), "the clear load op fills the rest");
    Check(At(p, 0, 0).a == 255, "BGRA8 alpha is stored");
    EndFrame();

    BeginPass(gb::LoadOp::Clear, kBlack);
    q.clear();
    Quad(q, -1, -1, 1, 1, 0.5f, kWhite);
    d.raster.cull = gb::Cull::Front;
    Draw(State(d, 0), q);
    gb::EndPass();
    Check(At(Read(), 10, 10).Is(0, 0, 0), "clockwise triangles are front faces (Cull::Front culls them)");
    EndFrame();
}

void TestPixelCentres()
{
    BeginPass(gb::LoadOp::Clear, kBlack);
    std::vector<Vertex> q;
    // Screen x 0 to 0.5: D3D9 covers column 0 (its centre, x = 0, is on the left edge).
    Quad(q, -1.0f, -1.0f, -1.0f + 0.5f * 2.0f / kSize, 1.0f, 0.5f, kWhite);
    // Screen x 1.6 to 2.4 covers column 2 only (centre 2).
    Quad(q, -1.0f + 1.6f * 2.0f / kSize, -1.0f, -1.0f + 2.4f * 2.0f / kSize, 1.0f, 0.5f, kWhite);
    Draw(State(Pipeline(), 0), q);
    gb::EndPass();
    const auto p = Read();
    Check(At(p, 0, 5).Is(255, 255, 255), "pixel centres on integers: an edge at the centre covers it (column 0)");
    Check(At(p, 1, 5).Is(0, 0, 0), "pixel centres on integers: column 1 is left out");
    Check(At(p, 2, 5).Is(255, 255, 255) && At(p, 3, 5).Is(0, 0, 0), "pixel centres on integers: column 2 only");
    EndFrame();
}

void TestFragCoord()
{
    BeginPass(gb::LoadOp::Clear, kBlack);
    std::vector<Vertex> q;
    Quad(q, -1, -1, 1, 1, 0.5f, kWhite);
    Draw(State(Pipeline(), 2), q);
    gb::EndPass();
    const auto p = Read();
    Check(At(p, 5, 9).r == 5 && At(p, 5, 9).g == 9, "fragment coordinates count rows from the top");
    Check(At(p, 60, 2).r == 60 && At(p, 60, 2).g == 2, "fragment coordinates: x and y");
    EndFrame();
}

void TestDepth()
{
    BeginPass(gb::LoadOp::Clear, kBlack, gb::LoadOp::Clear, 1.0f);
    gb::PipelineDesc d = Pipeline();
    d.depthStencil.depthTest = true;
    d.depthStencil.depthWrite = true;
    d.depthStencil.depthFunc = gb::Compare::LessEqual;
    std::vector<Vertex> q;
    Quad(q, -1, -1, 1, 1, 0.25f, kGreen);
    Draw(State(d, 0), q);
    d.depthStencil.depthFunc = gb::Compare::Less;
    q.clear();
    Quad(q, -1, -1, 0, 1, 0.5f, kRed);
    Draw(State(d, 0), q);
    gb::EndPass();
    Check(At(Read(), 5, 5).Is(0, 255, 0), "the depth test keeps the nearer quad");
    std::vector<uint32_t> depth(kSize * kSize);
    gb::ReadTexture(g.depth, 0, 0, {0, 0, 0, kSize, kSize, 1}, depth.data(), kSize * 4);
    const double z = double(depth[5 * kSize + 5] >> 8) / double((1u << 24) - 1);
    Check(std::fabs(z - 0.25) < 1e-4, "clip-space depth 0.25 stores 0.25 (depth from 0 to 1)");
    EndFrame();

    // The viewport's depth range maps 0..1 into minDepth..maxDepth.
    BeginPass(gb::LoadOp::Clear, kBlack, gb::LoadOp::Clear, 1.0f);
    d.depthStencil.depthFunc = gb::Compare::Always;
    gb::DrawState s = State(d, 0);
    s.viewport.minDepth = 0.5f;
    q.clear();
    Quad(q, -1, -1, 1, 1, 0.5f, kGreen);
    Draw(s, q);
    gb::EndPass();
    gb::ReadTexture(g.depth, 0, 0, {0, 0, 0, kSize, kSize, 1}, depth.data(), kSize * 4);
    const double ranged = double(depth[5 * kSize + 5] >> 8) / double((1u << 24) - 1);
    Check(std::fabs(ranged - 0.75) < 1e-4, "the viewport depth range applies");
    EndFrame();
}

void TestViewportScissor()
{
    BeginPass(gb::LoadOp::Clear, kBlack);
    std::vector<Vertex> q;
    Quad(q, -1, -1, 1, 1, 0.5f, kGreen);
    gb::DrawState s = State(Pipeline(), 0, nullptr, {0.0f, 32.0f, float(kSize), 32.0f, 0.0f, 1.0f});
    Draw(s, q);
    gb::EndPass();
    auto p = Read();
    Check(At(p, 5, 31).Is(0, 0, 0) && At(p, 5, 32).Is(0, 255, 0) && At(p, 5, 63).Is(0, 255, 0),
          "the viewport's y counts from the top");
    EndFrame();

    BeginPass(gb::LoadOp::Clear, kBlack);
    gb::PipelineDesc d = Pipeline();
    d.raster.scissor = true;
    s = State(d, 0);
    s.scissor = {8, 4, 16, 8};
    q.clear();
    Quad(q, -1, -1, 1, 1, 0.5f, kBlue);
    Draw(s, q);
    gb::EndPass();
    p = Read();
    Check(At(p, 8, 4).Is(0, 0, 255) && At(p, 23, 11).Is(0, 0, 255), "the scissor rect is drawn");
    Check(At(p, 7, 4).Is(0, 0, 0) && At(p, 8, 3).Is(0, 0, 0) && At(p, 24, 11).Is(0, 0, 0) && At(p, 23, 12).Is(0, 0, 0),
          "the scissor rect counts from the top-left");
    EndFrame();
}

void TestStencil()
{
    BeginPass(gb::LoadOp::Clear, kBlack, gb::LoadOp::Clear, 1.0f);
    gb::PipelineDesc d = Pipeline();
    d.depthStencil.stencil = true;
    d.depthStencil.front = {gb::Compare::Always, gb::StencilOp::Keep, gb::StencilOp::Keep, gb::StencilOp::Replace};
    d.depthStencil.back = {gb::Compare::Always, gb::StencilOp::Keep, gb::StencilOp::Keep, gb::StencilOp::Keep};
    d.blend.writeMask = 0;
    gb::DrawState s = State(d, 0);
    s.stencilRef = 1;
    std::vector<Vertex> q;
    Quad(q, -1, -1, 0, 1, 0.5f, kWhite);
    Draw(s, q);
    gb::PipelineDesc test = Pipeline();
    test.depthStencil.stencil = true;
    test.depthStencil.front = test.depthStencil.back = {gb::Compare::Equal, gb::StencilOp::Keep, gb::StencilOp::Keep,
                                                        gb::StencilOp::Keep};
    s = State(test, 0);
    s.stencilRef = 1;
    q.clear();
    Quad(q, -1, -1, 1, 1, 0.5f, kGreen);
    Draw(s, q);
    gb::EndPass();
    const auto p = Read();
    Check(At(p, 5, 5).Is(0, 255, 0) && At(p, 40, 5).Is(0, 0, 0),
          "two-sided stencil: the front-face state applies to clockwise triangles");
    EndFrame();
}

void TestTextures()
{
    const uint8_t texels[2][2][4] = {{{0, 0, 255, 255}, {0, 255, 0, 255}}, {{255, 0, 0, 255}, {255, 255, 255, 255}}};
    gb::TextureDesc td = {gb::TextureType::Tex2D, gb::Format::BGRA8, gb::Usage::Static, true, 2, 2, 1, 1, 1, {}};
    gb::Texture *texture = gb::CreateTexture(td);
    gb::UploadTexture(texture, 0, 0, {0, 0, 0, 2, 2, 1}, texels, 8, 16);
    BeginPass(gb::LoadOp::Clear, kBlack);
    std::vector<Vertex> q;
    Quad(q, -1, -1, 1, 1, 0.5f, kWhite);
    Draw(State(Pipeline(), 1, texture), q);
    gb::EndPass();
    auto p = Read();
    Check(At(p, 0, 0).Is(255, 0, 0) && At(p, 63, 0).Is(0, 255, 0), "texture row 0 is the top row (v = 0)");
    Check(At(p, 0, 63).Is(0, 0, 255) && At(p, 63, 63).Is(255, 255, 255), "texture row 1 is below it");
    EndFrame();

    struct Case
    {
        gb::Format format;
        uint8_t data[8];
        uint32_t pitch;
        uint8_t r, g, b;
        const char *what;
    };
    // A 4x4 BC1 block with both endpoints blue (565 0x001F) and every index 0.
    const Case cases[] = {
        {gb::Format::B5G6R5, {0x00, 0xF8}, 2, 255, 0, 0, "B5G6R5: red in the top bits"},
        {gb::Format::B5G5R5A1, {0xE0, 0x83}, 2, 0, 255, 0, "B5G5R5A1: green in bits 5-9"},
        {gb::Format::RGBA8, {0x10, 0x20, 0x30, 0xFF}, 4, 0x10, 0x20, 0x30, "RGBA8: bytes R, G, B, A"},
        {gb::Format::BC1, {0x1F, 0x00, 0x1F, 0x00, 0, 0, 0, 0}, 8, 0, 0, 255, "BC1 decodes"},
    };
    for (const Case &c : cases)
    {
        const uint16_t extent = c.format == gb::Format::BC1 ? 4 : 1;
        gb::TextureDesc d = {gb::TextureType::Tex2D, c.format, gb::Usage::Static, true, extent, extent, 1, 1, 1, {}};
        gb::Texture *t = gb::CreateTexture(d);
        gb::UploadTexture(t, 0, 0, {0, 0, 0, extent, extent, 1}, c.data, c.pitch, c.pitch);
        BeginPass(gb::LoadOp::Clear, kBlack);
        q.clear();
        Quad(q, -1, -1, 1, 1, 0.5f, kWhite);
        Draw(State(Pipeline(), 1, t), q);
        gb::EndPass();
        p = Read();
        Check(At(p, 20, 20).Is(c.r, c.g, c.b), c.what);
        EndFrame();
        gb::DestroyTexture(t);
    }

    // An L8 texture through the frontend's swizzle (R, R, R, One).
    gb::TextureDesc ld = {gb::TextureType::Tex2D, gb::Format::R8, gb::Usage::Static, true, 1, 1, 1, 1, 1, {}};
    ld.swizzle = {gb::Source::R, gb::Source::R, gb::Source::R, gb::Source::One};
    gb::Texture *luminance = gb::CreateTexture(ld);
    const uint8_t l = 0x80;
    gb::UploadTexture(luminance, 0, 0, {0, 0, 0, 1, 1, 1}, &l, 1, 1);
    BeginPass(gb::LoadOp::Clear, kBlack);
    q.clear();
    Quad(q, -1, -1, 1, 1, 0.5f, kWhite);
    Draw(State(Pipeline(), 1, luminance), q);
    gb::EndPass();
    p = Read();
    Check(At(p, 3, 3).Is(0x80, 0x80, 0x80) && At(p, 3, 3).a == 255, "swizzle R, R, R, One");
    EndFrame();
    gb::DestroyTexture(luminance);
    gb::DestroyTexture(texture);
}

// An upload into the texture the backend last uploaded to, after a draw that left another texture unit active: the
// upload must still reach that texture (a stale active unit once sent it to the drawn texture, or to nothing).
void TestUploadAfterDraw()
{
    const gb::TextureDesc td = {gb::TextureType::Tex2D, gb::Format::BGRA8, gb::Usage::Static, true, 1, 1, 1, 1, 1, {}};
    const uint8_t blue[4] = {255, 0, 0, 255}, red[4] = {0, 0, 255, 255}, green[4] = {0, 255, 0, 255};
    gb::Texture *drawn = gb::CreateTexture(td);
    gb::UploadTexture(drawn, 0, 0, {0, 0, 0, 1, 1, 1}, blue, 4, 4);
    gb::Texture *edited = gb::CreateTexture(td);
    gb::UploadTexture(edited, 0, 0, {0, 0, 0, 1, 1, 1}, red, 4, 4);
    std::vector<Vertex> q;
    Quad(q, -1, -1, 1, 1, 0.5f, kWhite);
    BeginPass(gb::LoadOp::Clear, kBlack);
    Draw(State(Pipeline(), 1, drawn), q);
    gb::EndPass();
    gb::UploadTexture(edited, 0, 0, {0, 0, 0, 1, 1, 1}, green, 4, 4);
    BeginPass(gb::LoadOp::Clear, kBlack);
    Draw(State(Pipeline(), 1, edited), q);
    gb::EndPass();
    Check(At(Read(), 5, 5).Is(0, 255, 0), "an upload after a draw reaches its texture");
    BeginPass(gb::LoadOp::Clear, kBlack);
    Draw(State(Pipeline(), 1, drawn), q);
    gb::EndPass();
    Check(At(Read(), 5, 5).Is(0, 0, 255), "an upload after a draw leaves the drawn texture alone");
    EndFrame();
    gb::DestroyTexture(drawn);
    gb::DestroyTexture(edited);
}

void TestCopiesAndClears()
{
    BeginPass(gb::LoadOp::Clear, kBlack);
    std::vector<Vertex> q;
    Quad(q, -1, -1, 1, 1, 0.5f, kWhite);
    Draw(State(Pipeline(), 2), q);
    const gb::Rect rect = {4, 6, 2, 3};
    const float blue[4] = {0, 0, 1, 1};
    gb::Clear(&rect, 1, true, false, false, blue, 1.0f, 0);
    gb::EndPass();
    auto p = Read();
    Check(At(p, 4, 6).Is(0, 0, 255) && At(p, 5, 8).Is(0, 0, 255), "Clear fills its rect");
    Check(At(p, 3, 6).r == 3 && At(p, 6, 6).r == 6 && At(p, 4, 9).g == 9, "Clear stays inside its rect");

    gb::TextureDesc td = {gb::TextureType::Tex2D, gb::Format::BGRA8, gb::Usage::RenderTarget, true, kSize, kSize, 1, 1, 1,
                          {}};
    gb::Texture *copy = gb::CreateTexture(td);
    gb::Blit(g.target, 0, 0, {0, 0, 32, 32}, copy, 0, 0, {32, 32, 32, 32}, gb::Filter::Nearest);
    // Scaled by 1.6, so that no destination centre maps onto a source texel's edge.
    gb::Blit(g.target, 0, 0, {0, 0, 32, 32}, copy, 0, 0, {0, 0, 20, 20}, gb::Filter::Nearest);
    p = Read(copy);
    Check(At(p, 40, 41).r == 8 && At(p, 40, 41).g == 9, "Blit copies rect to rect, rows from the top");
    Check(At(p, 10, 13).r == 16 && At(p, 10, 13).g == 21, "Blit scales");
    EndFrame();
    gb::DestroyTexture(copy);

    // BGRX8: alpha reads 1 whatever was cleared into it.
    td.format = gb::Format::BGRX8;
    gb::Texture *opaque = gb::CreateTexture(td);
    gb::PassDesc pass;
    pass.colorCount = 1;
    pass.color[0].texture = opaque;
    pass.color[0].load = gb::LoadOp::Clear;
    pass.color[0].clear[0] = 1.0f;
    gb::BeginPass(pass);
    gb::EndPass();
    p = Read(opaque);
    Check(At(p, 1, 1).Is(255, 0, 0) && At(p, 1, 1).a == 255, "BGRX8 reads alpha 1");
    gb::DestroyTexture(opaque);
}

// D3D9 hardware moves vertices by half a pixel and clips to the viewport's whole pixels: geometry larger than the
// viewport covers every sample of the edge pixels, and so does a full-screen quad drawn D3D9's way (half a pixel up
// and left). A viewport moved by half a pixel instead would leave half the samples of column 0 and row 0 bare.
void TestMultisampledEdges()
{
    if (gb::GetCaps().maxSamples < 4)
    {
        std::printf("skipped: no 4-sample attachments\n");
        return;
    }
    gb::TextureDesc cd = {gb::TextureType::Tex2D, gb::Format::BGRA8, gb::Usage::RenderTarget, false, kSize, kSize, 1, 1,
                          4, {}};
    gb::TextureDesc dd = {gb::TextureType::Tex2D, gb::Format::D24S8, gb::Usage::DepthStencil, false, kSize, kSize, 1, 1,
                          4, {}};
    gb::Texture *colour = gb::CreateTexture(cd), *depth = gb::CreateTexture(dd);
    gb::PipelineDesc d = Pipeline();
    d.samples = 4;
    const float half = 1.0f / kSize;
    const struct
    {
        float x0, y0, x1, y1;
        const char *what;
    } quads[] = {{-2.0f, -2.0f, 2.0f, 2.0f, "4 samples: geometry past the viewport covers the edge pixels"},
                 {-1.0f - half, -1.0f + half, 1.0f - half, 1.0f + half,
                  "4 samples: a full-screen quad drawn D3D9's way covers the edge pixels"}};
    for (const auto &quad : quads)
    {
        gb::PassDesc pass;
        pass.colorCount = 1;
        pass.color[0].texture = colour;
        pass.color[0].load = gb::LoadOp::Clear;
        pass.color[0].clear[0] = pass.color[0].clear[3] = 1.0f;
        pass.depth = depth;
        pass.depthLoad = pass.stencilLoad = gb::LoadOp::Clear;
        gb::BeginPass(pass);
        std::vector<Vertex> q;
        Quad(q, quad.x0, quad.y0, quad.x1, quad.y1, 0.5f, kGreen);
        Draw(State(d, 0), q);
        gb::EndPass();
        gb::Blit(colour, 0, 0, {0, 0, kSize, kSize}, g.target, 0, 0, {0, 0, kSize, kSize}, gb::Filter::Nearest);
        const auto p = Read();
        Check(At(p, 0, 0).Is(0, 255, 0) && At(p, 0, 30).Is(0, 255, 0) && At(p, 30, 0).Is(0, 255, 0) &&
                  At(p, 63, 63).Is(0, 255, 0),
              quad.what);
        EndFrame();
    }
    gb::DestroyTexture(colour);
    gb::DestroyTexture(depth);
}

// A hardware shadow map (gfx_backend.h, SamplerDesc::depthCompare): a sampled D24S8 attachment read through a shadow
// sampler gives 1 where the reference depth is less than or equal to the stored one.
void TestShadowCompare()
{
    gb::TextureDesc dd = {gb::TextureType::Tex2D, gb::Format::D24S8, gb::Usage::DepthStencil, true, kSize, kSize, 1, 1,
                          1, {}};
    gb::Texture *shadow = gb::CreateTexture(dd);
    gb::PassDesc fill;
    fill.depth = shadow;
    fill.depthLoad = fill.stencilLoad = gb::LoadOp::Clear;
    fill.clearDepth = 0.5f;
    gb::BeginPass(fill);
    gb::EndPass();
    gb::SamplerDesc sd;
    sd.addressU = sd.addressV = gb::Address::Clamp;
    sd.mipFilter = gb::MipFilter::None;
    sd.depthCompare = true;
    const gb::Sampler *compare = gb::GetSampler(sd);
    for (const float reference : {0.25f, 0.75f})
    {
        BeginPass(gb::LoadOp::Clear, kRed);
        std::vector<Vertex> q;
        Quad(q, -1, -1, 1, 1, 0.5f, kWhite);
        gb::DrawState s = State(Pipeline(), 3, nullptr, kFull, reference);
        s.textures[1] = {shadow, compare};
        Draw(s, q);
        gb::EndPass();
        const Pixel p = At(Read(), 10, 10);
        Check(reference < 0.5f ? p.Is(255, 255, 255) : p.Is(0, 0, 0),
              reference < 0.5f ? "shadow compare: a reference in front of the stored depth passes"
                               : "shadow compare: a reference behind the stored depth fails");
        EndFrame();
    }
    gb::DestroyTexture(shadow);
}

// D3D9 clamps COLOR-semantic interpolators to [0, 1] at the vertex output; GL core does not clamp a custom varying,
// so the normaliser clamps it (ff_glsl.cpp). A quad whose top vertices carry COLOR 2 and bottom vertices 0: the top
// row saturates to 255 whether or not it is clamped (the 8-bit store saturates), but the middle row tells the two
// apart -- clamped gives 0.5 (the vertices become 1 and 0), unclamped gives 1.0 (interpolating 2 and 0).
void TestColorClamp()
{
    // The normaliser's output for this case: a COLOR varying clamped at the end of main.
    const char *vs = R"(#version 430 core
layout(std140, binding = 0) uniform BO1VertexConstants { vec4 vs_c_reg[256]; vec4 bo1_pos_fixup; };
layout(location = 0) in vec4 position;
layout(location = 1) in vec4 colour;
layout(location = 16) out vec4 io_10_0;
void main()
{
    gl_Position = position;
    io_10_0 = colour;
    io_10_0 = clamp(io_10_0, 0.0, 1.0);
    gl_Position.xy += bo1_pos_fixup.xy * gl_Position.w;
}
)";
    const char *ps = R"(#version 430 core
layout(location = 16) in vec4 io_10_0;
layout(location = 0) out vec4 bo1_color0;
void main() { bo1_color0 = io_10_0; }
)";
    gb::Shader *vshader = gb::CreateShader(gb::Stage::Vertex, 100, vs, uint32_t(std::strlen(vs)));
    gb::Shader *pshader = gb::CreateShader(gb::Stage::Pixel, 101, ps, uint32_t(std::strlen(ps)));
    gb::PipelineDesc d;
    d.vertex = vshader;
    d.pixel = pshader;
    d.attribCount = 2;
    d.attribs[0] = {0, 0, 0, gb::AttribType::Float4};
    d.attribs[1] = {1, 0, 16, gb::AttribType::Float4};
    d.colorCount = 1;
    d.colorFormats[0] = gb::Format::BGRA8;
    d.depthFormat = gb::Format::D24S8;
    const Vertex top0 = {{-1, 1, 0.5f, 1}, {2, 2, 2, 1}, {0, 0}};
    const Vertex top1 = {{1, 1, 0.5f, 1}, {2, 2, 2, 1}, {0, 0}};
    const Vertex bot1 = {{1, -1, 0.5f, 1}, {0, 0, 0, 1}, {0, 0}};
    const Vertex bot0 = {{-1, -1, 0.5f, 1}, {0, 0, 0, 1}, {0, 0}};
    std::vector<Vertex> q = {top0, top1, bot1, top0, bot1, bot0};
    BeginPass(gb::LoadOp::Clear, kBlack);
    gb::DrawState s;
    s.pipeline = gb::GetPipeline(d);
    s.vertices[0] = {g.vertices, 0, sizeof(Vertex)};
    std::vector<float> vc(1028, 0.0f), pc(8, 0.0f);
    s.vertexConstants = gb::AllocUniforms(vc.data(), uint32_t(vc.size() * sizeof(float)));
    s.pixelConstants = gb::AllocUniforms(pc.data(), uint32_t(pc.size() * sizeof(float)));
    s.viewport = kFull;
    Draw(s, q);
    gb::EndPass();
    const Pixel mid = At(Read(), 32, 32);
    // Clamped: the middle of a 1-to-0 gradient, ~128. Unclamped it would interpolate 2 and 0 and saturate to 255.
    Check(mid.r > 100 && mid.r < 160, "a COLOR output over 1 is clamped before interpolation (middle is grey, not white)");
    EndFrame();
    gb::DestroyShader(vshader);
    gb::DestroyShader(pshader);
}

void TestQueriesAndFences()
{
    gb::Query *occlusion = gb::CreateQuery(gb::QueryType::Occlusion);
    gb::Query *start = gb::CreateQuery(gb::QueryType::Timestamp), *end = gb::CreateQuery(gb::QueryType::Timestamp);
    gb::EndQuery(start);
    BeginPass(gb::LoadOp::Clear, kBlack);
    gb::BeginQuery(occlusion);
    std::vector<Vertex> q;
    Quad(q, -1, 0, 1, 1, 0.5f, kWhite);
    Draw(State(Pipeline(), 0), q);
    gb::EndQuery(occlusion);
    gb::EndPass();
    gb::EndQuery(end);
    gb::Fence *fence = gb::InsertFence();
    uint64_t samples = 0, t0 = 0, t1 = 0;
    bool counted = false, reached = false, timed = false;
    for (int i = 0; i < 2000 && !(counted && reached && timed); ++i)
    {
        counted = counted || gb::QueryResult(occlusion, &samples);
        reached = reached || gb::FenceReached(fence);
        timed = timed || (gb::QueryResult(start, &t0) && gb::QueryResult(end, &t1));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Check(counted && samples == uint64_t(kSize) * kSize / 2, "an occlusion query counts the samples drawn");
    Check(reached, "a fence is reached");
    Check(timed && t1 >= t0, "timestamps come in order");
    EndFrame();
    gb::DestroyQuery(occlusion);
    gb::DestroyQuery(start);
    gb::DestroyQuery(end);
    gb::DestroyFence(fence);
}

void TestShaderFailure()
{
    const char broken[] = "#version 430 core\nvoid main() { this is not glsl }\n";
    gb::Shader *bad = gb::CreateShader(gb::Stage::Pixel, 0x1234, broken, uint32_t(sizeof(broken) - 1));
    gb::PipelineDesc d = Pipeline();
    d.pixel = bad;
    Check(gb::GetPipeline(d) == nullptr, "a shader that does not compile fails its pipelines");
    gb::DestroyShader(bad);
}
} // namespace

int main()
{
    setenv("BO1_GFX_BACKEND", "gl", 1);
    if (SDL_Init(SDL_INIT_VIDEO))
    {
        std::printf("SDL video: %s\n", SDL_GetError());
        return 1;
    }
    if (!gb::Init())
    {
        std::printf("the GL backend has no device\n");
        return 1;
    }
    std::printf("backend %s: %s\n", gb::Name(), gb::GetCaps().device);
    const gb::Caps &caps = gb::GetCaps();
    for (gb::Format f : {gb::Format::BGRA8, gb::Format::BGRX8, gb::Format::RGBA16F, gb::Format::R32F})
        Check(caps.formatFlags[int(f)] & gb::FORMAT_COLOR_TARGET, "the colour formats the frontend renders into");
    Check(caps.formatFlags[int(gb::Format::D24S8)] & gb::FORMAT_DEPTH_TARGET, "D24S8 is a depth attachment");
    Check(caps.formatFlags[int(gb::Format::BC3)] & gb::FORMAT_SAMPLED, "BC3 is sampled");

    g.window = SDL_CreateWindow("gb_gl_test", 0, 0, kSize, kSize, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    g.swapchain = gb::CreateSwapchain({g.window, kSize, kSize, false});
    g.vs = gb::CreateShader(gb::Stage::Vertex, 1, kVertex, uint32_t(sizeof(kVertex) - 1));
    g.ps = gb::CreateShader(gb::Stage::Pixel, 2, kPixel, uint32_t(sizeof(kPixel) - 1));
    g.vertices = gb::CreateBuffer({gb::BufferType::Vertex, gb::Usage::Dynamic, gb::Memory::Shared, 1u << 20});
    gb::TextureDesc rt = {gb::TextureType::Tex2D, gb::Format::BGRA8, gb::Usage::RenderTarget, true, kSize, kSize, 1, 1, 1,
                          {}};
    g.target = gb::CreateTexture(rt);
    gb::TextureDesc ds = {gb::TextureType::Tex2D, gb::Format::D24S8, gb::Usage::DepthStencil, false, kSize, kSize, 1, 1, 1,
                          {}};
    g.depth = gb::CreateTexture(ds);
    gb::SamplerDesc nearest;
    nearest.magFilter = nearest.minFilter = gb::Filter::Nearest;
    nearest.mipFilter = gb::MipFilter::None;
    nearest.addressU = nearest.addressV = gb::Address::Clamp;
    g.nearest = gb::GetSampler(nearest);
    Check(gb::GetPipeline(Pipeline()) != nullptr, "the test's shaders link");

    TestOrigin();
    TestPixelCentres();
    TestFragCoord();
    TestDepth();
    TestViewportScissor();
    TestStencil();
    TestTextures();
    TestUploadAfterDraw();
    TestCopiesAndClears();
    TestMultisampledEdges();
    TestShadowCompare();
    TestColorClamp();
    TestQueriesAndFences();
    TestShaderFailure();
    gb::WaitIdle();

    gb::DestroyTexture(g.target);
    gb::DestroyTexture(g.depth);
    gb::DestroyBuffer(g.vertices);
    gb::DestroyShader(g.vs);
    gb::DestroyShader(g.ps);
    gb::DestroySwapchain(g.swapchain);
    SDL_DestroyWindow(g.window);
    gb::Shutdown();
    SDL_Quit();
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
