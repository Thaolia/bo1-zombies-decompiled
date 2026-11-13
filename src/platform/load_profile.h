#pragma once
// Load profile, posix and Switch builds only (the Windows build keeps retail's code paths untouched): the time each
// kind of work took while a fastfile loaded, printed after its "Loaded fastfile" line (db_file_load.cpp) and reset
// there. Any thread adds to it; the image loads and texture uploads of the streaming thread during play land in the
// next zone's numbers, which only matters for a zone loaded mid-game.
#if !defined(_WIN32)
#include <atomic>
#include <chrono>
#include <cstdint>

enum Bo1LoadKind
{
    BO1_LOAD_READ,           // zone file reads (ReadFileEx)
    BO1_LOAD_INFLATE,        // zone decompression
    BO1_LOAD_SHADER_READ,    // shader pack reads
    BO1_LOAD_SHADER_CREATE,  // backend shader creation
    BO1_LOAD_IMAGE_FILE,     // Image_LoadFromFile: iwd read, decode and upload together
    BO1_LOAD_TEXTURE_UPLOAD, // texture uploads to the backend (inside the image loads, and from the zone itself)
    BO1_LOAD_IMAGE_WORK,     // the image file's own work on the render thread (read, decode, upload), inside the
                             // above: the rest of BO1_LOAD_IMAGE_FILE is the wait for the render thread
    BO1_LOAD_SCREEN,         // loading screen frames the render thread draws between resource requests
    BO1_LOAD_IMAGE_OPEN,     // the image file's lookup and open (FS_FOpenFileRead: the search path, the iwd entry)
    BO1_LOAD_IMAGE_READ,     // the image file's reads (FS_Read: the iwd's inflate and the storage reads)
    BO1_LOAD_IWD_STORAGE,    // seeks and reads of iwd files (unzReadCurrentFile's 16 KB blocks), any file of an iwd
    BO1_LOAD_KINDS
};

inline std::atomic<uint64_t> g_bo1LoadNs[BO1_LOAD_KINDS];
inline std::atomic<uint32_t> g_bo1LoadCount[BO1_LOAD_KINDS];
inline std::atomic<uint64_t> g_bo1ImageBytes; // bytes the image files' reads returned

inline uint64_t Bo1_LoadNowNs()
{
    return uint64_t(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

struct Bo1LoadScope
{
    explicit Bo1LoadScope(Bo1LoadKind kind) : kind(kind), start(Bo1_LoadNowNs()) {}
    ~Bo1LoadScope()
    {
        g_bo1LoadNs[kind] += Bo1_LoadNowNs() - start;
        ++g_bo1LoadCount[kind];
    }
    Bo1LoadScope(const Bo1LoadScope &) = delete;
    Bo1LoadScope &operator=(const Bo1LoadScope &) = delete;

    Bo1LoadKind kind;
    uint64_t start;
};

// The kind's milliseconds and count since the last call, which resets them.
inline double Bo1_LoadTake(Bo1LoadKind kind, uint32_t *count)
{
    *count = g_bo1LoadCount[kind].exchange(0);
    return double(g_bo1LoadNs[kind].exchange(0)) / 1e6;
}
#endif
