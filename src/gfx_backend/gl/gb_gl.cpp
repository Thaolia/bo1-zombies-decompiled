// The GL backend, the side the frontend calls: it hands out the objects at once, records the work into chunks for the
// backend's thread (gb_gl_device.cpp) and waits only where the contract has a sync point (ReadTexture, WaitIdle, the
// first GetPipeline of a shader pair, and Present once the backend's thread is a frame behind). It trusts the frontend
// to keep the contract: the null backend is the one that checks it.

#include "gb_gl.h"

#include "../gb_common.h"
#include "gb_gl_host.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <unordered_map>
#include <utility>

namespace gb::gl
{
namespace
{
// Present waits while the backend's thread has more frames than this left to show.
constexpr uint64_t kFramesAhead = 1;
// A chunk is handed over once it holds this many commands or payload bytes, so the backend's thread starts early.
constexpr size_t kChunkCommands = 512;
constexpr size_t kChunkPayload = 1u << 20;
// How often Present logs the perf line.
constexpr double kStatsSeconds = 10.0;

using Clock = std::chrono::steady_clock;

double Seconds(Clock::duration d)
{
    return std::chrono::duration<double>(d).count();
}

#define GB_GL_CAST(T)                                                                                                 \
    [[maybe_unused]] T *G(gb::T *p)                                                                                   \
    {                                                                                                                 \
        return static_cast<T *>(p);                                                                                   \
    }                                                                                                                 \
    [[maybe_unused]] const T *G(const gb::T *p)                                                                       \
    {                                                                                                                 \
        return static_cast<const T *>(p);                                                                             \
    }
GB_GL_CAST(Buffer)
GB_GL_CAST(Texture)
GB_GL_CAST(Shader)
GB_GL_CAST(Query)
GB_GL_CAST(Fence)
GB_GL_CAST(Swapchain)
#undef GB_GL_CAST

struct Recorder
{
    Queue queue;
    std::thread thread;
    DeviceInfo info;
    Chunk recording;
    uint64_t nextChunk = 1; // the serial the recording chunk gets
    uint64_t frames = 0;    // Presents recorded
    uint32_t uniformCursor = 0;
    std::unordered_map<std::string, std::unique_ptr<Sampler>> samplers;
    std::unordered_map<std::string, Pipeline *> pipelines;
    std::map<std::pair<const Shader *, const Shader *>, Program *> programs;
    // since the last perf line
    struct
    {
        Clock::time_point start = Clock::now();
        uint64_t frames = 0, draws = 0, uploadBytes = 0, pipelines = 0, links = 0;
        Clock::duration frameWait{}, linkWait{}, syncWait{};
    } stats;
};

std::unique_ptr<Recorder> s_recorder;
// What AllocUniforms slices name: the backend's thread places them by offset alone.
Buffer s_uniforms({BufferType::Vertex, Usage::Dynamic, Memory::Shared, 0});
const Caps s_noCaps = {};

Recorder &R()
{
    return *s_recorder;
}

void PrintLog()
{
    std::vector<std::string> lines;
    {
        std::lock_guard<std::mutex> lock(R().queue.lock);
        lines.swap(R().queue.log);
    }
    for (const std::string &line : lines)
        host::Log(line.c_str());
}

void Submit()
{
    Recorder &r = R();
    if (r.recording.commands.empty())
        return;
    r.recording.serial = r.nextChunk++;
    {
        std::lock_guard<std::mutex> lock(r.queue.lock);
        r.queue.chunks.push_back(std::move(r.recording));
        if (!r.queue.spare.empty())
        {
            r.recording = std::move(r.queue.spare.back());
            r.queue.spare.pop_back();
        }
        else
            r.recording = Chunk();
    }
    r.queue.wake.notify_one();
}

// Hands everything recorded over and waits until the backend's thread has executed it.
void Sync()
{
    Submit();
    Recorder &r = R();
    const uint64_t last = r.nextChunk - 1;
    const Clock::time_point start = Clock::now();
    {
        std::unique_lock<std::mutex> lock(r.queue.lock);
        r.queue.progress.wait(lock, [&] { return r.queue.executedChunks >= last; });
    }
    r.stats.syncWait += Clock::now() - start;
    PrintLog();
}

// Every kStatsSeconds: frames, work and where the frontend side waited, so that one log tells where a frame's time
// goes (the backend's thread's share included).
void PrintStats()
{
    Recorder &r = R();
    const Clock::time_point now = Clock::now();
    const double seconds = Seconds(now - r.stats.start);
    if (seconds < kStatsSeconds)
        return;
    const double ms = 1e-6;
    char line[512];
    std::snprintf(line, sizeof(line),
                  "gl perf: %.1f s, %llu frames (%.1f fps), %llu draws a frame, %.1f MB uploaded, %llu new pipelines, "
                  "%llu links; waits: frames %.0f ms, links %.0f ms, sync %.0f ms; backend thread: commands %.0f ms, "
                  "builds %.0f ms, swap %.0f ms",
                  seconds, static_cast<unsigned long long>(r.stats.frames), double(r.stats.frames) / seconds,
                  static_cast<unsigned long long>(r.stats.frames ? r.stats.draws / r.stats.frames : 0),
                  double(r.stats.uploadBytes) / (1 << 20), static_cast<unsigned long long>(r.stats.pipelines),
                  static_cast<unsigned long long>(r.stats.links), Seconds(r.stats.frameWait) * 1e3,
                  Seconds(r.stats.linkWait) * 1e3, Seconds(r.stats.syncWait) * 1e3,
                  double(r.queue.commandNs.exchange(0)) * ms, double(r.queue.buildNs.exchange(0)) * ms,
                  double(r.queue.swapNs.exchange(0)) * ms);
    host::Log(line);
    r.stats = {};
    r.stats.start = now;
}

template <class T> void Emit(const T &command)
{
    Chunk &c = R().recording;
    c.commands.emplace_back(command);
    if (c.commands.size() >= kChunkCommands || c.payload.size() >= kChunkPayload)
        Submit();
}

// Room for `size` payload bytes in the recording chunk; valid until the next Emit.
uint8_t *Reserve(uint32_t size, Bytes *bytes)
{
    std::vector<uint8_t> &payload = R().recording.payload;
    *bytes = {uint32_t(payload.size()), size};
    payload.resize(payload.size() + size);
    return payload.data() + bytes->offset;
}

Bytes Copy(const void *data, uint32_t size)
{
    Bytes bytes;
    std::memcpy(Reserve(size, &bytes), data, size);
    return bytes;
}

void QueueBuild(Shader *shader, Program *program, std::string source)
{
    Queue &q = R().queue;
    {
        std::lock_guard<std::mutex> lock(q.lock);
        q.builds.push_back({shader, program, std::move(source)});
        q.buildsPending.store(true, std::memory_order_release);
    }
    q.wake.notify_one();
}

// The pair's program, linked on the build lane the first time it is asked for.
Program *ProgramFor(const Shader *vertex, const Shader *pixel)
{
    Recorder &r = R();
    Program *&program = r.programs[{vertex, pixel}];
    if (!program)
    {
        program = new Program(vertex, pixel);
        QueueBuild(nullptr, program, std::string());
        const Clock::time_point start = Clock::now();
        {
            std::unique_lock<std::mutex> lock(r.queue.lock);
            r.queue.progress.wait(lock, [&] { return program->status.load(std::memory_order_acquire) != 0; });
        }
        r.stats.linkWait += Clock::now() - start;
        ++r.stats.links;
        PrintLog();
    }
    return program;
}
} // namespace

bool Init()
{
    s_recorder = std::make_unique<Recorder>();
    if (!host::Open())
    {
        s_recorder.reset();
        return false;
    }
    Recorder &r = R();
    const bool started = StartDevice(r.queue, r.info, r.thread);
    PrintLog();
    if (!started)
    {
        host::Close();
        s_recorder.reset();
        return false;
    }
    r.info.caps.device = r.info.device.c_str();
    return true;
}

void Shutdown()
{
    Recorder &r = R();
    Submit();
    {
        std::lock_guard<std::mutex> lock(r.queue.lock);
        r.queue.quit = true;
    }
    r.queue.wake.notify_one();
    r.thread.join();
    PrintLog();
    host::Close();
    // The context's objects went with it; only the frontend side's own are left.
    for (auto &entry : r.pipelines)
        delete entry.second;
    for (auto &entry : r.programs)
        delete entry.second;
    s_recorder.reset();
}

const Caps &GetCaps()
{
    return s_recorder ? R().info.caps : s_noCaps;
}

const char *Name()
{
    return "gl";
}

gb::Buffer *CreateBuffer(const BufferDesc &desc)
{
    Buffer *buffer = new Buffer(desc);
    Emit(cmd::CreateBuffer{buffer});
    return buffer;
}

gb::Texture *CreateTexture(const TextureDesc &desc)
{
    Texture *texture = new Texture(desc);
    Emit(cmd::CreateTexture{texture});
    return texture;
}

void DestroyBuffer(gb::Buffer *buffer)
{
    if (buffer)
        Emit(cmd::DestroyBuffer{G(buffer)});
}

void DestroyTexture(gb::Texture *texture)
{
    if (texture)
        Emit(cmd::DestroyTexture{G(texture)});
}

void UploadBuffer(gb::Buffer *buffer, uint32_t offset, const void *data, uint32_t size, Upload mode)
{
    R().stats.uploadBytes += size;
    const Bytes bytes = Copy(data, size);
    Emit(cmd::UploadBuffer{G(buffer), offset, mode, bytes});
}

void UploadTexture(gb::Texture *handle, uint8_t level, uint8_t layer, const Box &box, const void *data,
                   uint32_t rowPitch, uint32_t slicePitch)
{
    Texture *texture = G(handle);
    const bool block = IsBlockFormat(texture->desc.format);
    const uint32_t rows = block ? (box.height + 3u) / 4u : box.height;
    const uint32_t rowBytes = (block ? (box.width + 3u) / 4u : box.width) * FormatBytes(texture->desc.format);
    Bytes bytes;
    uint8_t *out = Reserve(rowBytes * rows * box.depth, &bytes);
    R().stats.uploadBytes += bytes.size;
    for (uint32_t z = 0; z < box.depth; ++z)
        for (uint32_t y = 0; y < rows; ++y)
        {
            const uint8_t *row = static_cast<const uint8_t *>(data) + size_t(z) * slicePitch + size_t(y) * rowPitch;
            std::memcpy(out + (size_t(z) * rows + y) * rowBytes, row, rowBytes);
        }
    Emit(cmd::UploadTexture{texture, level, layer, box, bytes});
}

const gb::Sampler *GetSampler(const SamplerDesc &desc)
{
    std::unique_ptr<Sampler> &slot = R().samplers[SamplerKey(desc)];
    if (!slot)
    {
        slot = std::make_unique<Sampler>(desc);
        Emit(cmd::CreateSampler{slot.get()});
    }
    return slot.get();
}

gb::Shader *CreateShader(Stage stage, uint64_t hash, const char *code, uint32_t size)
{
    Shader *shader = new Shader(stage, hash);
    QueueBuild(shader, nullptr, std::string(code, size));
    return shader;
}

void DestroyShader(gb::Shader *handle)
{
    if (!handle)
        return;
    Recorder &r = R();
    for (auto it = r.pipelines.begin(); it != r.pipelines.end();)
    {
        if (it->second->desc.vertex == handle || it->second->desc.pixel == handle)
        {
            Emit(cmd::DestroyPipeline{it->second});
            it = r.pipelines.erase(it);
        }
        else
            ++it;
    }
    for (auto it = r.programs.begin(); it != r.programs.end();)
    {
        if (it->first.first == handle || it->first.second == handle)
        {
            Emit(cmd::DestroyProgram{it->second});
            it = r.programs.erase(it);
        }
        else
            ++it;
    }
    Emit(cmd::DestroyShader{G(handle)});
}

const gb::Pipeline *GetPipeline(const PipelineDesc &desc)
{
    Recorder &r = R();
    const std::string key = PipelineKey(desc);
    auto it = r.pipelines.find(key);
    if (it != r.pipelines.end())
        return it->second;
    Program *program = ProgramFor(G(desc.vertex), G(desc.pixel));
    if (program->status.load(std::memory_order_acquire) < 0)
        return nullptr;
    Pipeline *pipeline = new Pipeline(desc, program);
    r.pipelines.emplace(key, pipeline);
    ++r.stats.pipelines;
    return pipeline;
}

void BeginPass(const PassDesc &desc)
{
    Emit(cmd::BeginPass{desc});
}

void EndPass()
{
    Emit(cmd::EndPass{});
}

void Clear(const Rect *rects, uint32_t count, bool color, bool depth, bool stencil, const float rgba[4],
           float depthValue, uint8_t stencilValue)
{
    cmd::Clear c = {Copy(rects, count * uint32_t(sizeof(Rect))), count, color, depth, stencil,
                    {rgba[0], rgba[1], rgba[2], rgba[3]}, depthValue, stencilValue};
    Emit(c);
}

UniformSlice AllocUniforms(const void *data, uint32_t size)
{
    Recorder &r = R();
    uint32_t offset = r.uniformCursor;
    if (offset % UNIFORM_BLOCK_BYTES + size > UNIFORM_BLOCK_BYTES)
        offset = (offset / UNIFORM_BLOCK_BYTES + 1) * UNIFORM_BLOCK_BYTES;
    const uint32_t align = r.info.uniformAlign;
    r.uniformCursor = offset + (size + align - 1) / align * align;
    const Bytes bytes = Copy(data, size);
    Emit(cmd::Uniforms{offset, bytes});
    return UniformSlice{&s_uniforms, offset, size};
}

void Draw(const DrawState &state, Primitive primitive, uint32_t vertexCount, uint32_t firstVertex)
{
    ++R().stats.draws;
    Emit(cmd::Draw{state, primitive, false, vertexCount, firstVertex, 0});
}

void DrawIndexed(const DrawState &state, Primitive primitive, uint32_t indexCount, uint32_t firstIndex,
                 int32_t baseVertex)
{
    ++R().stats.draws;
    Emit(cmd::Draw{state, primitive, true, indexCount, firstIndex, baseVertex});
}

void Blit(gb::Texture *src, uint8_t srcLevel, uint8_t srcLayer, const Rect &srcRect, gb::Texture *dst,
          uint8_t dstLevel, uint8_t dstLayer, const Rect &dstRect, Filter filter)
{
    Emit(cmd::Blit{G(src), srcLevel, srcLayer, srcRect, G(dst), dstLevel, dstLayer, dstRect, filter});
}

void ReadTexture(gb::Texture *texture, uint8_t level, uint8_t layer, const Box &box, void *data, uint32_t rowPitch)
{
    Emit(cmd::ReadTexture{G(texture), level, layer, box, data, rowPitch});
    Sync();
}

gb::Query *CreateQuery(QueryType type)
{
    Query *query = new Query(type);
    Emit(cmd::CreateQuery{query});
    return query;
}

void DestroyQuery(gb::Query *query)
{
    if (query)
        Emit(cmd::DestroyQuery{G(query)});
}

void BeginQuery(gb::Query *query)
{
    Emit(cmd::BeginQuery{G(query)});
}

void EndQuery(gb::Query *handle)
{
    Query *query = G(handle);
    ++query->ends;
    query->endChunk = R().nextChunk;
    Emit(cmd::EndQuery{query, query->ends});
}

bool QueryResult(gb::Query *handle, uint64_t *value)
{
    Query *query = G(handle);
    if (query->ends && query->available.load(std::memory_order_acquire) == query->ends)
    {
        *value = query->value.load(std::memory_order_relaxed);
        return true;
    }
    // D3DGETDATA_FLUSH: a result still being recorded would never come.
    if (query->endChunk >= R().nextChunk)
        Submit();
    return false;
}

gb::Fence *InsertFence()
{
    Fence *fence = new Fence();
    fence->chunk = R().nextChunk;
    Emit(cmd::InsertFence{fence});
    return fence;
}

bool FenceReached(gb::Fence *handle)
{
    Fence *fence = G(handle);
    if (fence->reached.load(std::memory_order_acquire))
        return true;
    if (fence->chunk >= R().nextChunk)
        Submit();
    return false;
}

void DestroyFence(gb::Fence *fence)
{
    if (fence)
        Emit(cmd::DestroyFence{G(fence)});
}

gb::Swapchain *CreateSwapchain(const SwapchainDesc &desc)
{
    Swapchain *swapchain = new Swapchain(desc);
    Emit(cmd::CreateSwapchain{swapchain});
    return swapchain;
}

void ResizeSwapchain(gb::Swapchain *swapchain, uint16_t width, uint16_t height)
{
    // The backend's thread presents at the window's drawable size: only the frontend side keeps this one.
    G(swapchain)->desc.width = width;
    G(swapchain)->desc.height = height;
}

void DestroySwapchain(gb::Swapchain *swapchain)
{
    if (!swapchain)
        return;
    Emit(cmd::DestroySwapchain{G(swapchain)});
    Sync();
}

void SetGamma(gb::Swapchain *swapchain, const uint16_t red[256], const uint16_t green[256], const uint16_t blue[256])
{
    Bytes bytes;
    uint8_t *out = Reserve(3 * 256 * sizeof(uint16_t), &bytes);
    std::memcpy(out, red, 256 * sizeof(uint16_t));
    std::memcpy(out + 256 * sizeof(uint16_t), green, 256 * sizeof(uint16_t));
    std::memcpy(out + 512 * sizeof(uint16_t), blue, 256 * sizeof(uint16_t));
    Emit(cmd::SetGamma{G(swapchain), bytes});
}

void Present(gb::Swapchain *swapchain, gb::Texture *image)
{
    Recorder &r = R();
    Emit(cmd::Present{G(swapchain), G(image)});
    Submit();
    ++r.frames;
    ++r.stats.frames;
    r.uniformCursor = 0;
    const Clock::time_point start = Clock::now();
    {
        std::unique_lock<std::mutex> lock(r.queue.lock);
        r.queue.progress.wait(lock, [&] { return r.frames - r.queue.presentedFrames <= kFramesAhead; });
    }
    r.stats.frameWait += Clock::now() - start;
    PrintLog();
    PrintStats();
}

void WaitIdle()
{
    Emit(cmd::Finish{});
    Sync();
}

const Backend &Table()
{
#define GB_GL_ENTRY(ret, name, params, args) &gl::name,
    static const Backend table = {&gl::Init, &gl::Name, GB_BACKEND_CALLS(GB_GL_ENTRY)};
#undef GB_GL_ENTRY
    return table;
}
} // namespace gb::gl
