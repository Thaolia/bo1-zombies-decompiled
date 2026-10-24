#pragma once

// Objects behind the HANDLEs the posix Win32 layer returns (win32_*.cpp). A HANDLE is a pointer to one of them, or one
// of the two pseudo handles GetCurrentProcess / GetCurrentThread return.

#include <windows.h>

#include <atomic>
#include <dirent.h>
#include <pthread.h>
#include <semaphore.h>
#include <string>
#include <time.h>
#include <ucontext.h>

#define BO1_PSEUDO_PROCESS ((HANDLE)(intptr_t)-1)
#define BO1_PSEUDO_THREAD ((HANDLE)(intptr_t)-2)

enum Bo1ObjectType : uint32_t
{
    BO1_OBJ_EVENT = 0x45564e54,
    BO1_OBJ_THREAD = 0x54485244,
    BO1_OBJ_MUTEX = 0x4d555458,
    BO1_OBJ_FILE = 0x46494c45,
    BO1_OBJ_FIND = 0x46494e44,
    BO1_OBJ_SNAPSHOT = 0x534e4150,
    BO1_OBJ_MEMORY = 0x474d454d,
};

struct Bo1Object
{
    explicit Bo1Object(Bo1ObjectType t) : type(t) {}
    virtual ~Bo1Object() = default;
    const Bo1ObjectType type;
    std::atomic<int> refs{1};
};

struct Bo1Event : Bo1Object
{
    Bo1Event(bool manualReset, bool initialState);
    ~Bo1Event() override;
    // Waits until the event is signalled, or a PulseEvent releases this waiter. Returns false on timeout.
    bool Wait(DWORD milliseconds);
    void Set();
    void Reset();
    void Pulse();

    pthread_mutex_t mutex;
    pthread_cond_t cond;
    const bool manualReset;
    bool signaled;
    int waiters = 0;
    // A PulseEvent releases only threads already waiting: those whose wait began before the generation it started.
    // All of them for a manual-reset event, one (pulseReleases) for an auto-reset one.
    uint64_t pulseGeneration = 0;
    int pulseReleases = 0;
};

struct Bo1Thread : Bo1Object
{
    Bo1Thread();
    ~Bo1Thread() override;

    pthread_t thread{};
    std::atomic<DWORD> id{0};
    LPTHREAD_START_ROUTINE start = nullptr;
    LPVOID parameter = nullptr;
    DWORD exitCode = 0;
    int priority = THREAD_PRIORITY_NORMAL;
    Bo1Event exited{true, false};

    // CreateThread(CREATE_SUSPENDED): the thread waits at startGate until ResumeThread brings the count to 0.
    // SuspendThread on a running thread interrupts it with a signal whose handler records its context and waits.
    pthread_mutex_t gateMutex;
    pthread_cond_t gateCond;
    bool started = false;
    std::atomic<int> suspendCount{0};
    sem_t suspendAck;
    sem_t resume;
    CONTEXT context{};
};

struct Bo1Mutex : Bo1Object
{
    Bo1Mutex();
    ~Bo1Mutex() override;
    pthread_mutex_t mutex;
};

struct Bo1File : Bo1Object
{
    Bo1File(int fileDescriptor) : Bo1Object(BO1_OBJ_FILE), fd(fileDescriptor) {}
    ~Bo1File() override;
    const int fd;
};

struct Bo1Find : Bo1Object
{
    Bo1Find() : Bo1Object(BO1_OBJ_FIND) {}
    ~Bo1Find() override;
    DIR *dir = nullptr;
    std::string directory;
    std::string pattern;
};

// A HANDLE always points at the Bo1Object base, which Bo1_HandleAs casts it back to.
inline HANDLE Bo1_ToHandle(Bo1Object *object)
{
    return object;
}

template <typename T> T *Bo1_HandleAs(HANDLE handle, Bo1ObjectType type)
{
    if (!handle || handle == INVALID_HANDLE_VALUE || handle == BO1_PSEUDO_PROCESS || handle == BO1_PSEUDO_THREAD)
        return nullptr;
    Bo1Object *object = static_cast<Bo1Object *>(handle);
    return object->type == type ? static_cast<T *>(object) : nullptr;
}

void Bo1_AddRef(Bo1Object *object);
void Bo1_Release(Bo1Object *object);
void Bo1_SetLastError(DWORD error);
DWORD Bo1_ErrorFromErrno(int error);
FILETIME Bo1_FileTimeFromUnix(time_t seconds, long nanoseconds);
// The registers of an interrupted thread (a signal handler's ucontext) as a Windows CONTEXT.
void Bo1_CaptureContext(CONTEXT *context, const ucontext_t *uc);
// Windows path to a POSIX one: backslashes become slashes.
std::string Bo1_PosixPath(const char *path);
// Runs the completion routines ReadFileEx queued on this thread (an alertable wait does it). Returns how many ran.
int Bo1_RunQueuedCompletions();
void Bo1_QueueCompletion(LPOVERLAPPED_COMPLETION_ROUTINE routine, DWORD error, DWORD bytes, LPOVERLAPPED overlapped);
