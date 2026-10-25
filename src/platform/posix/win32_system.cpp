// Win32 process, time, memory, system information and module functions on POSIX (Linux).

#include "win32_handles.h"

#include <ShlObj.h>
#include <XInput.h>
#if !defined(__SWITCH__) // no dynamic loader on the console
#include <dlfcn.h>
#endif
#include <dsound.h>
#include <dxerr9.h>
#include <errno.h>
#include <psapi.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__SWITCH__)
#include <malloc.h> // memalign: no mmap/mprotect on the console (libnx maps all memory as heap)
#else
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#endif
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#if defined(__SWITCH__)
// newlib hides the CPU-time clock ids behind a feature macro (their values are still 2/3) and ships no timegm.
#ifndef CLOCK_PROCESS_CPUTIME_ID
#define CLOCK_PROCESS_CPUTIME_ID 2
#endif
#ifndef CLOCK_THREAD_CPUTIME_ID
#define CLOCK_THREAD_CPUTIME_ID 3
#endif
namespace
{
// Seconds since the Unix epoch from a UTC broken-down time (proleptic Gregorian), standing in for the missing timegm.
time_t timegm(struct tm *tm)
{
    static const int cumulativeDays[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    const long year = 1900 + tm->tm_year;
    const int month = ((tm->tm_mon % 12) + 12) % 12;
    time_t days = (year - 1970) * 365 + (year - 1969) / 4 - (year - 1901) / 100 + (year - 1601) / 400;
    days += cumulativeDays[month];
    if (month > 1 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0))
        days += 1;
    days += tm->tm_mday - 1;
    return ((days * 24 + tm->tm_hour) * 60 + tm->tm_min) * 60 + tm->tm_sec;
}
} // namespace
#endif

namespace
{
// FILETIME counts 100 ns intervals since 1601-01-01; Unix time starts 11644473600 s later.
const ULONGLONG FILETIME_UNIX_EPOCH = 116444736000000000ull;

FILETIME FileTimeFromHundredNs(ULONGLONG value)
{
    FILETIME ft;
    ft.dwLowDateTime = (DWORD)value;
    ft.dwHighDateTime = (DWORD)(value >> 32);
    return ft;
}

ULONGLONG HundredNsFromTimespec(const timespec &ts)
{
    return (ULONGLONG)ts.tv_sec * 10000000ull + (ULONGLONG)ts.tv_nsec / 100;
}

FILETIME CurrentFileTime()
{
    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return FileTimeFromHundredNs(HundredNsFromTimespec(ts) + FILETIME_UNIX_EPOCH);
}

// Taken during static initialisation, before main: the creation time GetProcessTimes reports.
const FILETIME s_processCreation = CurrentFileTime();

// VirtualAlloc reservations: Windows hands out 64 KB-aligned regions and commits pages inside them.
struct Reservation
{
    SIZE_T size;
    DWORD protect;
    std::vector<bool> committed; // per page
};
std::mutex s_memoryLock;
// Built on first use and never destroyed, like the thread registry (win32_threads.cpp).
std::map<uintptr_t, Reservation> &Reservations()
{
    static std::map<uintptr_t, Reservation> *reservations = new std::map<uintptr_t, Reservation>();
    return *reservations;
}
const SIZE_T ALLOCATION_GRANULARITY = 64 * 1024;

SIZE_T PageSize()
{
    const long size = sysconf(_SC_PAGESIZE);
    return size > 0 ? (SIZE_T)size : 0x1000; // newlib may not know the key; 4 KiB is the Switch page
}

int PosixProtection(DWORD protect)
{
#if defined(__SWITCH__)
    (void)protect;
    return 0; // the console has no page protection; the value is unused (no mprotect)
#else
    switch (protect & 0xFF)
    {
    case PAGE_NOACCESS:
        return PROT_NONE;
    case PAGE_READONLY:
        return PROT_READ;
    case PAGE_EXECUTE:
    case PAGE_EXECUTE_READ:
        return PROT_READ | PROT_EXEC;
    case PAGE_EXECUTE_READWRITE:
        return PROT_READ | PROT_WRITE | PROT_EXEC;
    default:
        return PROT_READ | PROT_WRITE;
    }
#endif
}

std::map<uintptr_t, Reservation>::iterator FindReservation(uintptr_t address)
{
    auto it = Reservations().upper_bound(address);
    if (it == Reservations().begin())
        return Reservations().end();
    --it;
    return address < it->first + it->second.size ? it : Reservations().end();
}

void *ReserveAligned(SIZE_T size, int prot)
{
#if defined(__SWITCH__)
    // libnx already maps all of the application's memory as the heap, so there is no reserve-without-commit: an
    // aligned allocation is both. Protection is a no-op, so `prot` is ignored.
    (void)prot;
    return memalign(ALLOCATION_GRANULARITY, size);
#else
    const SIZE_T padded = size + ALLOCATION_GRANULARITY;
    char *raw = (char *)mmap(nullptr, padded, prot, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (raw == MAP_FAILED)
        return nullptr;
    char *aligned = (char *)(((uintptr_t)raw + ALLOCATION_GRANULARITY - 1) & ~(uintptr_t)(ALLOCATION_GRANULARITY - 1));
    if (aligned > raw)
        munmap(raw, aligned - raw);
    char *end = aligned + size;
    if (raw + padded > end)
        munmap(end, raw + padded - end);
    return aligned;
#endif
}

long ReadProcStatusKb(const char *field)
{
    FILE *f = fopen("/proc/self/status", "r");
    if (!f)
        return -1;
    char line[256];
    long value = -1;
    const size_t length = strlen(field);
    while (fgets(line, sizeof(line), f))
    {
        if (!strncmp(line, field, length) && line[length] == ':')
        {
            value = strtol(line + length + 1, nullptr, 10);
            break;
        }
    }
    fclose(f);
    return value;
}

HMODULE ExecutableModule()
{
#if defined(__SWITCH__)
    return (HMODULE)1; // one statically-linked module on the console; callers only need a stable non-null token
#else
    static HMODULE module = []() -> HMODULE {
        Dl_info info;
        return dladdr((void *)&ExecutableModule, &info) ? (HMODULE)info.dli_fbase : nullptr;
    }();
    return module;
#endif
}

// The process command line as Windows keeps it: the arguments joined by spaces, those with spaces or quotes quoted.
// Read from /proc rather than handed over by main(), because static initialisers already ask for it (r_stream.cpp).
std::string ReadCommandLine()
{
    std::string commandLine;
    FILE *f = fopen("/proc/self/cmdline", "rb");
    if (!f)
        return commandLine;
    std::string argument;
    for (int c = fgetc(f);; c = fgetc(f))
    {
        if (c != EOF && c != 0)
        {
            argument += (char)c;
            continue;
        }
        if (c == EOF && argument.empty())
            break;
        if (!commandLine.empty())
            commandLine += ' ';
        if (argument.empty() || argument.find_first_of(" \t\"") != std::string::npos)
        {
            commandLine += '"';
            for (char ch : argument)
            {
                if (ch == '"')
                    commandLine += '\\';
                commandLine += ch;
            }
            commandLine += '"';
        }
        else
            commandLine += argument;
        argument.clear();
        if (c == EOF)
            break;
    }
    fclose(f);
    return commandLine;
}

std::atomic<LPTOP_LEVEL_EXCEPTION_FILTER> s_exceptionFilter{nullptr};
std::atomic<UINT> s_errorMode{0};

#if !defined(__SWITCH__)
// The structured exception Windows raises for the fault a crash signal reports.
DWORD ExceptionCodeFromSignal(int signal, int code)
{
    switch (signal)
    {
    case SIGILL:
        return 0xC000001D; // EXCEPTION_ILLEGAL_INSTRUCTION
    case SIGFPE:
        return code == FPE_INTDIV ? 0xC0000094 : code == FPE_INTOVF ? 0xC0000095 : 0xC0000090; // INT_DIVIDE_BY_ZERO, INT_OVERFLOW, FLT_INVALID_OPERATION
    case SIGTRAP:
        return 0x80000003; // EXCEPTION_BREAKPOINT (__debugbreak)
    default:
        return 0xC0000005; // EXCEPTION_ACCESS_VIOLATION (SIGSEGV, SIGBUS)
    }
}

// Hands the fault to the unhandled exception filter as Windows does, then ends the process with the signal's default
// action (a core dump) whatever the filter answers: the engine's filters only report.
void CrashSignalHandler(int signal, siginfo_t *info, void *ucontext)
{
    const LPTOP_LEVEL_EXCEPTION_FILTER filter = s_exceptionFilter.load();
    if (filter)
    {
        CONTEXT context;
        Bo1_CaptureContext(&context, static_cast<const ucontext_t *>(ucontext));
        EXCEPTION_RECORD record = {};
        record.ExceptionCode = ExceptionCodeFromSignal(signal, info->si_code);
#if defined(__x86_64__)
        // An int3 trap reports the instruction after it; Windows reports the int3 itself.
        record.ExceptionAddress = (PVOID)(context.Rip - (signal == SIGTRAP ? 1 : 0));
#elif defined(__aarch64__)
        record.ExceptionAddress = (PVOID)context.Pc;
#endif
        if (record.ExceptionCode == 0xC0000005)
        {
            record.NumberParameters = 2;
#if defined(__x86_64__)
            // The page fault error code's bit 1 is set for a write.
            const greg_t error = static_cast<const ucontext_t *>(ucontext)->uc_mcontext.gregs[REG_ERR];
            record.ExceptionInformation[0] = (error & 2) ? 1 : 0;
#endif
            record.ExceptionInformation[1] = (ULONG_PTR)info->si_addr;
        }
        EXCEPTION_POINTERS pointers = {&record, &context};
        filter(&pointers);
    }
    ::signal(signal, SIG_DFL);
    raise(signal);
}

void InstallCrashHandlers()
{
    struct sigaction action = {};
    action.sa_sigaction = CrashSignalHandler;
    action.sa_flags = SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    for (int signal : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGTRAP})
        sigaction(signal, &action, nullptr);
}
#else
// newlib (Switch) has no SA_SIGINFO sigaction/siginfo/getcontext; libnx has its own crash reporting. The engine's
// exception-filter path is a boot-milestone task (docs/PORTING.md, phase 5); installing handlers is a no-op for now.
void InstallCrashHandlers()
{
}
#endif

// GlobalAlloc blocks keep their size in front of the data, for GlobalSize.
const SIZE_T GLOBAL_HEADER = 16;
} // namespace

FILETIME Bo1_FileTimeFromUnix(time_t seconds, long nanoseconds)
{
    return FileTimeFromHundredNs((ULONGLONG)seconds * 10000000ull + (ULONGLONG)nanoseconds / 100 + FILETIME_UNIX_EPOCH);
}

extern "C" {

// ---- errors and debugging

// Windows hands the text to an attached debugger. Linux debuggers have no such channel, and the console
// (posix_syscon.cpp) and the log files already carry everything the engine sends here.
void OutputDebugStringA(LPCSTR)
{
}

BOOL IsDebuggerPresent(void)
{
    return ReadProcStatusKb("TracerPid") > 0;
}

// The engine raises 0x406D1388 to name a thread for the debugger (MSVC's THREADNAME_INFO). Any other exception has no
// handler here: as on Windows, the unhandled exception filter sees it, then the process ends (abort: a core dump).
void RaiseException(DWORD dwExceptionCode, DWORD dwExceptionFlags, DWORD nNumberOfArguments, const ULONG_PTR *lpArguments)
{
    if (dwExceptionCode == 0x406D1388 && lpArguments && nNumberOfArguments >= 2)
    {
        struct ThreadNameInfo
        {
            DWORD type;
            LPCSTR name;
            DWORD threadId;
            DWORD flags;
        };
        const ThreadNameInfo *info = reinterpret_cast<const ThreadNameInfo *>(lpArguments);
        if (info->type == 0x1000 && info->name && (info->threadId == (DWORD)-1 || info->threadId == GetCurrentThreadId()))
        {
            char name[16];
            strncpy(name, info->name, sizeof(name) - 1);
            name[sizeof(name) - 1] = 0;
            pthread_setname_np(pthread_self(), name);
        }
        return;
    }
    const LPTOP_LEVEL_EXCEPTION_FILTER filter = s_exceptionFilter.load();
    if (filter)
    {
        CONTEXT context;
#if defined(__SWITCH__)
        memset(&context, 0, sizeof(context)); // no getcontext on the console; the filter here only reports
#else
        ucontext_t uc;
        getcontext(&uc);
        Bo1_CaptureContext(&context, &uc);
#endif
        EXCEPTION_RECORD record = {};
        record.ExceptionCode = dwExceptionCode;
        record.ExceptionFlags = dwExceptionFlags;
        record.ExceptionAddress = __builtin_return_address(0);
        record.NumberParameters =
            nNumberOfArguments < EXCEPTION_MAXIMUM_PARAMETERS ? nNumberOfArguments : EXCEPTION_MAXIMUM_PARAMETERS;
        if (lpArguments)
            memcpy(record.ExceptionInformation, lpArguments, record.NumberParameters * sizeof(ULONG_PTR));
        EXCEPTION_POINTERS pointers = {&record, &context};
        filter(&pointers);
    }
    fprintf(stderr, "RaiseException(0x%08x): unhandled exception\n", (unsigned int)dwExceptionCode);
    abort();
}

LPTOP_LEVEL_EXCEPTION_FILTER SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter)
{
    static std::once_flag installed;
    std::call_once(installed, InstallCrashHandlers);
    return s_exceptionFilter.exchange(lpTopLevelExceptionFilter);
}

// There are no critical-error or file-error dialogs to suppress: the mode is only kept.
UINT SetErrorMode(UINT uMode)
{
    return s_errorMode.exchange(uMode);
}

// Linux has no message table for Win32 error codes: the text names the code.
DWORD FormatMessageA(DWORD dwFlags, LPCVOID, DWORD dwMessageId, DWORD, LPSTR lpBuffer, DWORD nSize, va_list *)
{
    if (!(dwFlags & FORMAT_MESSAGE_FROM_SYSTEM))
    {
        Bo1_SetLastError(ERROR_NOT_SUPPORTED);
        return 0;
    }
    char text[64];
    const int length = snprintf(text, sizeof(text), "Win32 error %u.\r\n", (unsigned int)dwMessageId);
    if (dwFlags & FORMAT_MESSAGE_ALLOCATE_BUFFER)
    {
        char *buffer = (char *)malloc((size_t)length + 1 > nSize ? (size_t)length + 1 : nSize);
        if (!buffer)
        {
            Bo1_SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return 0;
        }
        memcpy(buffer, text, (size_t)length + 1);
        *(LPSTR *)lpBuffer = buffer;
        return (DWORD)length;
    }
    if ((DWORD)length + 1 > nSize)
    {
        Bo1_SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    memcpy(lpBuffer, text, (size_t)length + 1);
    return (DWORD)length;
}

HLOCAL LocalFree(HLOCAL hMem)
{
    free(hMem);
    return nullptr;
}

// The Win32 API this layer provides is that of Windows 8 (NT 6.2), the version Windows reports to an application
// without a compatibility manifest.
BOOL GetVersionExA(LPOSVERSIONINFOA lpVersionInformation)
{
    if (lpVersionInformation->dwOSVersionInfoSize < sizeof(OSVERSIONINFOA))
    {
        Bo1_SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    lpVersionInformation->dwMajorVersion = 6;
    lpVersionInformation->dwMinorVersion = 2;
    lpVersionInformation->dwBuildNumber = 9200;
    lpVersionInformation->dwPlatformId = 2; // VER_PLATFORM_WIN32_NT
    lpVersionInformation->szCSDVersion[0] = 0;
    return TRUE;
}

// ---- process

LPSTR GetCommandLineA(void)
{
    static std::string *commandLine = new std::string(ReadCommandLine());
    return &(*commandLine)[0];
}

BOOL SetEnvironmentVariableA(LPCSTR lpName, LPCSTR lpValue)
{
    const int error = lpValue ? setenv(lpName, lpValue, 1) : unsetenv(lpName);
    if (error)
        Bo1_SetLastError(Bo1_ErrorFromErrno(errno));
    return error == 0;
}

// ExitProcess ends the process without the CRT's exit handlers (those run from exit(), which the engine calls itself).
void ExitProcess(UINT uExitCode)
{
    fflush(nullptr);
    _exit((int)uExitCode);
}

// Unlike ExitProcess, TerminateProcess flushes nothing: the CRT's buffered output is lost, as on Windows.
BOOL TerminateProcess(HANDLE hProcess, UINT uExitCode)
{
    if (hProcess != BO1_PSEUDO_PROCESS)
    {
        Bo1_SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    _exit((int)uExitCode);
}

DWORD GetPriorityClass(HANDLE hProcess)
{
    if (hProcess != BO1_PSEUDO_PROCESS)
    {
        Bo1_SetLastError(ERROR_INVALID_HANDLE);
        return 0;
    }
#if defined(__SWITCH__)
    return NORMAL_PRIORITY_CLASS; // per-thread priority via svcSetThreadPriority at the boot milestone
#else
    const int nice = getpriority(PRIO_PROCESS, 0);
    return nice < 0 ? HIGH_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS;
#endif
}

BOOL SetPriorityClass(HANDLE hProcess, DWORD dwPriorityClass)
{
    if (hProcess != BO1_PSEUDO_PROCESS)
    {
        Bo1_SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
#if defined(__SWITCH__)
    (void)dwPriorityClass;
    return TRUE; // no process-wide priority on the console (per-thread via libnx, boot milestone)
#else
    const int nice = dwPriorityClass == HIGH_PRIORITY_CLASS ? -10 : dwPriorityClass == ABOVE_NORMAL_PRIORITY_CLASS ? -5 : 0;
    if (setpriority(PRIO_PROCESS, 0, nice))
    {
        Bo1_SetLastError(Bo1_ErrorFromErrno(errno));
        return FALSE;
    }
    return TRUE;
#endif
}

BOOL GetProcessAffinityMask(HANDLE hProcess, PDWORD_PTR lpProcessAffinityMask, PDWORD_PTR lpSystemAffinityMask)
{
#if defined(__SWITCH__)
    // Applications get cores 0-2 (core 3 is the system's); core pinning is per-thread (svcSetThreadCoreMask) at the
    // boot milestone (switch-homebrew.md).
    if (hProcess != BO1_PSEUDO_PROCESS)
    {
        Bo1_SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *lpProcessAffinityMask = 0x7;
    *lpSystemAffinityMask = 0x7;
    return TRUE;
#else
    cpu_set_t set;
    CPU_ZERO(&set);
    if (hProcess != BO1_PSEUDO_PROCESS || sched_getaffinity(0, sizeof(set), &set))
    {
        Bo1_SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    DWORD_PTR mask = 0;
    for (int cpu = 0; cpu < (int)(sizeof(DWORD_PTR) * 8); ++cpu)
    {
        if (CPU_ISSET(cpu, &set))
            mask |= (DWORD_PTR)1 << cpu;
    }
    const long count = sysconf(_SC_NPROCESSORS_CONF);
    const DWORD_PTR system = count >= (long)(sizeof(DWORD_PTR) * 8) ? ~(DWORD_PTR)0 : (((DWORD_PTR)1 << count) - 1);
    *lpProcessAffinityMask = mask;
    *lpSystemAffinityMask = system;
    return TRUE;
#endif
}

// Linux affinity is per thread: the mask applies to the calling thread and is inherited by the threads it creates,
// which is how the engine uses it (once, at startup, before its worker threads exist).
BOOL SetProcessAffinityMask(HANDLE hProcess, DWORD_PTR dwProcessAffinityMask)
{
#if defined(__SWITCH__)
    (void)dwProcessAffinityMask;
    if (hProcess != BO1_PSEUDO_PROCESS)
    {
        Bo1_SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE; // per-thread core mask via svcSetThreadCoreMask at the boot milestone
#else
    cpu_set_t set;
    CPU_ZERO(&set);
    for (int cpu = 0; cpu < (int)(sizeof(DWORD_PTR) * 8); ++cpu)
    {
        if (dwProcessAffinityMask & ((DWORD_PTR)1 << cpu))
            CPU_SET(cpu, &set);
    }
    if (hProcess != BO1_PSEUDO_PROCESS || sched_setaffinity(0, sizeof(set), &set))
    {
        Bo1_SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
#endif
}

BOOL GetProcessTimes(HANDLE hProcess, LPFILETIME lpCreationTime, LPFILETIME lpExitTime, LPFILETIME lpKernelTime,
                     LPFILETIME lpUserTime)
{
    rusage usage;
    if (hProcess != BO1_PSEUDO_PROCESS || getrusage(RUSAGE_SELF, &usage))
    {
        Bo1_SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    *lpCreationTime = s_processCreation;
    *lpExitTime = FileTimeFromHundredNs(0);
    *lpKernelTime = FileTimeFromHundredNs((ULONGLONG)usage.ru_stime.tv_sec * 10000000ull + usage.ru_stime.tv_usec * 10ull);
    *lpUserTime = FileTimeFromHundredNs((ULONGLONG)usage.ru_utime.tv_sec * 10000000ull + usage.ru_utime.tv_usec * 10ull);
    return TRUE;
}

BOOL GetProcessHandleCount(HANDLE hProcess, PDWORD pdwHandleCount)
{
    if (hProcess != BO1_PSEUDO_PROCESS)
    {
        Bo1_SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    DIR *dir = opendir("/proc/self/fd");
    if (!dir)
    {
        Bo1_SetLastError(Bo1_ErrorFromErrno(errno));
        return FALSE;
    }
    DWORD count = 0;
    while (dirent *entry = readdir(dir))
    {
        if (entry->d_name[0] != '.')
            ++count;
    }
    closedir(dir);
    *pdwHandleCount = count;
    return TRUE;
}

// No cycle counter per process or thread on Linux: the CPU time in nanoseconds stands in for it (the engine only
// compares these values with each other).
BOOL QueryProcessCycleTime(HANDLE ProcessHandle, PULONG64 CycleTime)
{
    timespec ts;
    if (ProcessHandle != BO1_PSEUDO_PROCESS || clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts))
    {
        Bo1_SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    *CycleTime = (ULONG64)ts.tv_sec * 1000000000ull + (ULONG64)ts.tv_nsec;
    return TRUE;
}

BOOL GetThreadTimes(HANDLE hThread, LPFILETIME lpCreationTime, LPFILETIME lpExitTime, LPFILETIME lpKernelTime,
                    LPFILETIME lpUserTime)
{
    Bo1Thread *thread = hThread == BO1_PSEUDO_THREAD ? nullptr : Bo1_HandleAs<Bo1Thread>(hThread, BO1_OBJ_THREAD);
    clockid_t clock;
    if (hThread == BO1_PSEUDO_THREAD)
        clock = CLOCK_THREAD_CPUTIME_ID;
    else if (!thread || pthread_getcpuclockid(thread->thread, &clock))
    {
        Bo1_SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    timespec ts;
    if (clock_gettime(clock, &ts))
    {
        Bo1_SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    // Linux reports one CPU time per thread; it is all counted as user time.
    *lpCreationTime = FileTimeFromHundredNs(0);
    *lpExitTime = FileTimeFromHundredNs(0);
    *lpKernelTime = FileTimeFromHundredNs(0);
    *lpUserTime = FileTimeFromHundredNs(HundredNsFromTimespec(ts));
    return TRUE;
}

BOOL QueryThreadCycleTime(HANDLE ThreadHandle, PULONG64 CycleTime)
{
    FILETIME creation, exit, kernel, user;
    if (!GetThreadTimes(ThreadHandle, &creation, &exit, &kernel, &user))
        return FALSE;
    *CycleTime = ((ULONG64)user.dwHighDateTime << 32 | user.dwLowDateTime) * 100ull;
    return TRUE;
}

// ---- time

// 10 MHz, the frequency Windows 10 reports on most PCs: code that keeps QueryPerformanceFrequency in 32 bits or does
// float maths on ticks sees the same magnitudes.
BOOL QueryPerformanceCounter(LARGE_INTEGER *lpPerformanceCount)
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    lpPerformanceCount->QuadPart = (LONGLONG)HundredNsFromTimespec(ts);
    return TRUE;
}

BOOL QueryPerformanceFrequency(LARGE_INTEGER *lpFrequency)
{
    lpFrequency->QuadPart = 10000000;
    return TRUE;
}

void GetSystemTimeAsFileTime(LPFILETIME lpSystemTimeAsFileTime)
{
    *lpSystemTimeAsFileTime = CurrentFileTime();
}

void GetSystemTime(LPSYSTEMTIME lpSystemTime)
{
    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    tm utc;
    gmtime_r(&ts.tv_sec, &utc);
    lpSystemTime->wYear = (WORD)(utc.tm_year + 1900);
    lpSystemTime->wMonth = (WORD)(utc.tm_mon + 1);
    lpSystemTime->wDayOfWeek = (WORD)utc.tm_wday;
    lpSystemTime->wDay = (WORD)utc.tm_mday;
    lpSystemTime->wHour = (WORD)utc.tm_hour;
    lpSystemTime->wMinute = (WORD)utc.tm_min;
    lpSystemTime->wSecond = (WORD)utc.tm_sec;
    lpSystemTime->wMilliseconds = (WORD)(ts.tv_nsec / 1000000);
}

BOOL SystemTimeToFileTime(const SYSTEMTIME *lpSystemTime, LPFILETIME lpFileTime)
{
    tm utc = {};
    utc.tm_year = lpSystemTime->wYear - 1900;
    utc.tm_mon = lpSystemTime->wMonth - 1;
    utc.tm_mday = lpSystemTime->wDay;
    utc.tm_hour = lpSystemTime->wHour;
    utc.tm_min = lpSystemTime->wMinute;
    utc.tm_sec = lpSystemTime->wSecond;
    const time_t seconds = timegm(&utc);
    if (seconds == (time_t)-1 || lpSystemTime->wMonth < 1 || lpSystemTime->wMonth > 12)
    {
        Bo1_SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *lpFileTime = FileTimeFromHundredNs((ULONGLONG)seconds * 10000000ull + lpSystemTime->wMilliseconds * 10000ull +
                                        FILETIME_UNIX_EPOCH);
    return TRUE;
}

LONG CompareFileTime(const FILETIME *lpFileTime1, const FILETIME *lpFileTime2)
{
    const ULONGLONG a = (ULONGLONG)lpFileTime1->dwHighDateTime << 32 | lpFileTime1->dwLowDateTime;
    const ULONGLONG b = (ULONGLONG)lpFileTime2->dwHighDateTime << 32 | lpFileTime2->dwLowDateTime;
    return a < b ? -1 : a > b ? 1 : 0;
}

// System-wide CPU times from /proc/stat, in 100 ns units; kernel time includes idle time, as on Windows.
BOOL GetSystemTimes(PFILETIME lpIdleTime, PFILETIME lpKernelTime, PFILETIME lpUserTime)
{
    FILE *f = fopen("/proc/stat", "r");
    unsigned long long user = 0, nice = 0, system = 0, idle = 0, iowait = 0, irq = 0, softirq = 0;
    const int read = f ? fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu", &user, &nice, &system, &idle, &iowait,
                                &irq, &softirq)
                       : 0;
    if (f)
        fclose(f);
    if (read < 4)
    {
        Bo1_SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    const ULONGLONG unit = 10000000ull / (ULONGLONG)sysconf(_SC_CLK_TCK);
    const ULONGLONG idleTime = (idle + iowait) * unit;
    if (lpIdleTime)
        *lpIdleTime = FileTimeFromHundredNs(idleTime);
    if (lpKernelTime)
        *lpKernelTime = FileTimeFromHundredNs((system + irq + softirq) * unit + idleTime);
    if (lpUserTime)
        *lpUserTime = FileTimeFromHundredNs((user + nice) * unit);
    return TRUE;
}

DWORD timeGetTime(void)
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (DWORD)((ULONGLONG)ts.tv_sec * 1000ull + (ULONGLONG)ts.tv_nsec / 1000000ull);
}

DWORD GetTickCount(void)
{
    return timeGetTime();
}

// Linux timers and sleeps already have millisecond resolution or better: no period to raise.
UINT timeBeginPeriod(UINT)
{
    return TIMERR_NOERROR;
}

UINT timeEndPeriod(UINT)
{
    return TIMERR_NOERROR;
}

// ---- memory and system information

LPVOID VirtualAlloc(LPVOID lpAddress, SIZE_T dwSize, DWORD flAllocationType, DWORD flProtect)
{
    if (!dwSize)
    {
        Bo1_SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    const SIZE_T page = PageSize();
    std::lock_guard<std::mutex> lock(s_memoryLock);
    // MEM_COMMIT alone at no address reserves and commits, like both flags (physicalmemory.cpp allocates PMem so).
    if ((flAllocationType & MEM_RESERVE) || ((flAllocationType & MEM_COMMIT) && !lpAddress))
    {
        if (lpAddress)
        {
            // The engine never asks for a fixed address.
            Bo1_SetLastError(ERROR_INVALID_PARAMETER);
            return nullptr;
        }
        const SIZE_T size = (dwSize + page - 1) & ~(page - 1);
        const bool commit = (flAllocationType & MEM_COMMIT) != 0;
        void *base = ReserveAligned(size, commit ? PosixProtection(flProtect) : 0); // PROT_NONE is 0; 0 also suits switch
        if (!base)
        {
            Bo1_SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return nullptr;
        }
        Reservation reservation = {size, flProtect, std::vector<bool>(size / page, commit)};
        Reservations()[(uintptr_t)base] = std::move(reservation);
        return base;
    }
    if (flAllocationType & MEM_COMMIT)
    {
        auto it = FindReservation((uintptr_t)lpAddress);
        if (it == Reservations().end())
        {
            Bo1_SetLastError(ERROR_INVALID_PARAMETER);
            return nullptr;
        }
        const uintptr_t first = (uintptr_t)lpAddress & ~(uintptr_t)(page - 1);
        const uintptr_t last = ((uintptr_t)lpAddress + dwSize + page - 1) & ~(uintptr_t)(page - 1);
#if defined(__SWITCH__)
        // The memory is already committed (memalign gave real pages) and has no per-page protection: just bounds-check.
        if (last > it->first + it->second.size)
#else
        if (last > it->first + it->second.size || mprotect((void *)first, last - first, PosixProtection(flProtect)))
#endif
        {
            Bo1_SetLastError(ERROR_INVALID_PARAMETER);
            return nullptr;
        }
        for (uintptr_t p = first; p < last; p += page)
            it->second.committed[(p - it->first) / page] = true;
        return (LPVOID)first;
    }
    Bo1_SetLastError(ERROR_INVALID_PARAMETER);
    return nullptr;
}

BOOL VirtualFree(LPVOID lpAddress, SIZE_T dwSize, DWORD dwFreeType)
{
    const SIZE_T page = PageSize();
    std::lock_guard<std::mutex> lock(s_memoryLock);
    auto it = FindReservation((uintptr_t)lpAddress);
    if (it == Reservations().end())
    {
        Bo1_SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (dwFreeType & MEM_RELEASE)
    {
        if (dwSize || (uintptr_t)lpAddress != it->first)
        {
            Bo1_SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
#if defined(__SWITCH__)
        free((void *)it->first); // the memalign'd base
#else
        munmap((void *)it->first, it->second.size);
#endif
        Reservations().erase(it);
        return TRUE;
    }
    if (dwFreeType & MEM_DECOMMIT)
    {
        const uintptr_t first = (uintptr_t)lpAddress & ~(uintptr_t)(page - 1);
        const uintptr_t last = dwSize ? (((uintptr_t)lpAddress + dwSize + page - 1) & ~(uintptr_t)(page - 1))
                                      : it->first + it->second.size;
        if (last > it->first + it->second.size)
        {
            Bo1_SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        // Decommitted pages read back as zero once committed again, as on Windows.
#if !defined(__SWITCH__)
        madvise((void *)first, last - first, MADV_DONTNEED);
        mprotect((void *)first, last - first, PROT_NONE);
#else
        // No decommit on the console: the pages stay mapped (they are not zeroed, but the engine only relies on that
        // for guard regions it re-commits, which is acceptable for bring-up). (docs/PORTING.md, phase 5)
#endif
        for (uintptr_t p = first; p < last; p += page)
            it->second.committed[(p - it->first) / page] = false;
        return TRUE;
    }
    Bo1_SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

SIZE_T VirtualQuery(LPCVOID lpAddress, PMEMORY_BASIC_INFORMATION lpBuffer, SIZE_T dwLength)
{
    if (dwLength < sizeof(*lpBuffer))
    {
        Bo1_SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    const SIZE_T page = PageSize();
    const uintptr_t address = (uintptr_t)lpAddress & ~(uintptr_t)(page - 1);
    std::lock_guard<std::mutex> lock(s_memoryLock);
    auto it = FindReservation(address);
    memset(lpBuffer, 0, sizeof(*lpBuffer));
    lpBuffer->BaseAddress = (PVOID)address;
    if (it == Reservations().end())
    {
        // Memory VirtualAlloc did not reserve (the heap, stacks, the image): one committed page.
        lpBuffer->AllocationBase = (PVOID)address;
        lpBuffer->RegionSize = page;
        lpBuffer->State = MEM_COMMIT;
        lpBuffer->Protect = PAGE_READWRITE;
        lpBuffer->AllocationProtect = PAGE_READWRITE;
        lpBuffer->Type = MEM_PRIVATE;
        return sizeof(*lpBuffer);
    }
    const Reservation &reservation = it->second;
    const size_t index = (address - it->first) / page;
    const bool committed = reservation.committed[index];
    size_t end = index;
    while (end < reservation.committed.size() && reservation.committed[end] == committed)
        ++end;
    lpBuffer->AllocationBase = (PVOID)it->first;
    lpBuffer->AllocationProtect = reservation.protect;
    lpBuffer->RegionSize = (end - index) * page;
    lpBuffer->State = committed ? MEM_COMMIT : MEM_RESERVE;
    lpBuffer->Protect = committed ? reservation.protect : 0;
    lpBuffer->Type = MEM_PRIVATE;
    return sizeof(*lpBuffer);
}

BOOL GlobalMemoryStatusEx(LPMEMORYSTATUSEX lpBuffer)
{
#if defined(__SWITCH__)
    // libnx maps all of the application's memory as heap, so svcGetInfo(UsedMemorySize) equals the total; real use is
    // mallinfo().uordblks (switch-homebrew.md). The total is the application-mode budget; svcGetInfo refines it at the
    // boot milestone. This only needs to let the engine's pre-allocation checks pass.
    const DWORDLONG total = 3ull * 1024 * 1024 * 1024; // ~3 GiB in application mode
    const DWORDLONG used = (DWORDLONG)(size_t)mallinfo().uordblks;
    const DWORDLONG available = total > used ? total - used : 0;
    lpBuffer->ullTotalPageFile = total;
    lpBuffer->ullAvailPageFile = available;
#else
    struct sysinfo info;
    if (sysinfo(&info))
    {
        Bo1_SetLastError(Bo1_ErrorFromErrno(errno));
        return FALSE;
    }
    const DWORDLONG unit = info.mem_unit ? info.mem_unit : 1;
    const DWORDLONG total = (DWORDLONG)info.totalram * unit;
    const DWORDLONG available = ((DWORDLONG)info.freeram + info.bufferram) * unit;
    lpBuffer->ullTotalPageFile = total + (DWORDLONG)info.totalswap * unit;
    lpBuffer->ullAvailPageFile = available + (DWORDLONG)info.freeswap * unit;
#endif
    lpBuffer->dwMemoryLoad = total ? (DWORD)(100 - available * 100 / total) : 0;
    lpBuffer->ullTotalPhys = total;
    lpBuffer->ullAvailPhys = available;
    lpBuffer->ullTotalVirtual = 0x7FFFFFFE0000ull; // user address space of a 64-bit process, as on Windows x64
    lpBuffer->ullAvailVirtual = lpBuffer->ullTotalVirtual;
    lpBuffer->ullAvailExtendedVirtual = 0;
    return TRUE;
}

void GlobalMemoryStatus(LPMEMORYSTATUS lpBuffer)
{
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    GlobalMemoryStatusEx(&status);
    lpBuffer->dwMemoryLoad = status.dwMemoryLoad;
    lpBuffer->dwTotalPhys = (SIZE_T)status.ullTotalPhys;
    lpBuffer->dwAvailPhys = (SIZE_T)status.ullAvailPhys;
    lpBuffer->dwTotalPageFile = (SIZE_T)status.ullTotalPageFile;
    lpBuffer->dwAvailPageFile = (SIZE_T)status.ullAvailPageFile;
    lpBuffer->dwTotalVirtual = (SIZE_T)status.ullTotalVirtual;
    lpBuffer->dwAvailVirtual = (SIZE_T)status.ullAvailVirtual;
}

// GlobalAlloc only backs clipboard text (assertive.cpp, the clipboard in win32_input.cpp): a fixed heap block, whose
// lock is its address.
HGLOBAL GlobalAlloc(UINT, SIZE_T dwBytes)
{
    char *block = (char *)calloc(1, GLOBAL_HEADER + dwBytes);
    if (!block)
    {
        Bo1_SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    *(SIZE_T *)block = dwBytes;
    return block + GLOBAL_HEADER;
}

SIZE_T GlobalSize(HGLOBAL hMem)
{
    return hMem ? *(const SIZE_T *)((const char *)hMem - GLOBAL_HEADER) : 0;
}

LPVOID GlobalLock(HGLOBAL hMem)
{
    return hMem;
}

BOOL GlobalUnlock(HGLOBAL)
{
    return FALSE; // the lock count reached zero
}

HGLOBAL GlobalFree(HGLOBAL hMem)
{
    if (hMem)
        free((char *)hMem - GLOBAL_HEADER);
    return nullptr;
}

void GetSystemInfo(LPSYSTEM_INFO lpSystemInfo)
{
    memset(lpSystemInfo, 0, sizeof(*lpSystemInfo));
#if defined(__aarch64__)
    lpSystemInfo->wProcessorArchitecture = 12; // PROCESSOR_ARCHITECTURE_ARM64
#else
    lpSystemInfo->wProcessorArchitecture = 9; // PROCESSOR_ARCHITECTURE_AMD64
#endif
    lpSystemInfo->dwPageSize = (DWORD)PageSize();
    lpSystemInfo->lpMinimumApplicationAddress = (LPVOID)0x10000;
    lpSystemInfo->lpMaximumApplicationAddress = (LPVOID)0x7FFFFFFEFFFFull;
    const long processors = sysconf(_SC_NPROCESSORS_ONLN);
    lpSystemInfo->dwNumberOfProcessors = processors > 0 ? (DWORD)processors : 1;
    lpSystemInfo->dwActiveProcessorMask =
        lpSystemInfo->dwNumberOfProcessors >= sizeof(DWORD_PTR) * 8
            ? ~(DWORD_PTR)0
            : (((DWORD_PTR)1 << lpSystemInfo->dwNumberOfProcessors) - 1);
    lpSystemInfo->dwAllocationGranularity = (DWORD)ALLOCATION_GRANULARITY;
}

BOOL K32GetProcessMemoryInfo(HANDLE Process, PPROCESS_MEMORY_COUNTERS ppsmemCounters, DWORD cb)
{
    if (Process != BO1_PSEUDO_PROCESS || cb < sizeof(PROCESS_MEMORY_COUNTERS))
    {
        Bo1_SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    const long rss = ReadProcStatusKb("VmRSS");
    const long peakRss = ReadProcStatusKb("VmHWM");
    const long data = ReadProcStatusKb("VmData");
    memset(ppsmemCounters, 0, cb);
    ppsmemCounters->cb = cb;
    ppsmemCounters->WorkingSetSize = (SIZE_T)(rss > 0 ? rss : 0) * 1024;
    ppsmemCounters->PeakWorkingSetSize = (SIZE_T)(peakRss > 0 ? peakRss : 0) * 1024;
    ppsmemCounters->PagefileUsage = (SIZE_T)(data > 0 ? data : 0) * 1024;
    ppsmemCounters->PeakPagefileUsage = ppsmemCounters->PagefileUsage;
    if (cb >= sizeof(PROCESS_MEMORY_COUNTERS_EX))
        reinterpret_cast<PROCESS_MEMORY_COUNTERS_EX *>(ppsmemCounters)->PrivateUsage = ppsmemCounters->PagefileUsage;
    return TRUE;
}

// ---- modules: only the executable exists; Windows DLLs the engine looks for (kernel32, imm32, dbghelp...) do not.

HMODULE GetModuleHandleA(LPCSTR lpModuleName)
{
    if (!lpModuleName)
        return ExecutableModule();
    Bo1_SetLastError(ERROR_MOD_NOT_FOUND);
    return nullptr;
}

BOOL GetModuleHandleExA(DWORD dwFlags, LPCSTR lpModuleName, HMODULE *phModule)
{
    if (dwFlags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS)
    {
#if defined(__SWITCH__)
        // One statically-linked module: any address belongs to it.
        (void)lpModuleName;
        *phModule = ExecutableModule();
        return TRUE;
#else
        Dl_info info;
        if (dladdr(lpModuleName, &info) && info.dli_fbase)
        {
            *phModule = (HMODULE)info.dli_fbase;
            return TRUE;
        }
        *phModule = nullptr;
        Bo1_SetLastError(ERROR_MOD_NOT_FOUND);
        return FALSE;
#endif
    }
    *phModule = GetModuleHandleA(lpModuleName);
    return *phModule != nullptr;
}

// The path comes back in Windows form ('\\' separators): the engine takes it apart at '\\' (Sys_EmergencyPath, the
// crash reports' module names), and this layer's file functions take it back.
DWORD GetModuleFileNameA(HMODULE hModule, LPSTR lpFilename, DWORD nSize)
{
    char path[4096];
    ssize_t length;
#if defined(__SWITCH__)
    // One statically-linked module on the console; report a fixed path. The engine only splits it for sibling data
    // dirs and crash-report module names, and romfs/sdmc provide the data instead. (docs/PORTING.md, phase 5)
    (void)hModule;
    static const char kExe[] = "sdmc:/switch/bo1zm/BO1Zombies.nro";
    length = (ssize_t)strnlen(kExe, sizeof(path) - 1);
    memcpy(path, kExe, (size_t)length);
#else
    if (!hModule || hModule == ExecutableModule())
        length = readlink("/proc/self/exe", path, sizeof(path) - 1);
    else
    {
        // A shared library, found by its load address (GetModuleHandleExA's handles).
        Dl_info info;
        if (!dladdr((const void *)hModule, &info) || info.dli_fbase != (void *)hModule || !info.dli_fname)
        {
            Bo1_SetLastError(ERROR_MOD_NOT_FOUND);
            return 0;
        }
        length = (ssize_t)strnlen(info.dli_fname, sizeof(path) - 1);
        memcpy(path, info.dli_fname, (size_t)length);
    }
#endif
    if (length <= 0 || !nSize)
    {
        Bo1_SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    path[length] = 0;
    for (char *c = path; *c; ++c)
    {
        if (*c == '/')
            *c = '\\';
    }
    strncpy(lpFilename, path, nSize - 1);
    lpFilename[nSize - 1] = 0;
    if ((DWORD)length >= nSize)
    {
        Bo1_SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return nSize;
    }
    return (DWORD)length;
}

HMODULE LoadLibraryA(LPCSTR)
{
    Bo1_SetLastError(ERROR_MOD_NOT_FOUND);
    return nullptr;
}

FARPROC GetProcAddress(HMODULE hModule, LPCSTR lpProcName)
{
#if defined(__SWITCH__)
    void *symbol = nullptr; // the console statically links everything; no runtime symbol lookup
    (void)hModule;
    (void)lpProcName;
#else
    void *symbol = hModule == ExecutableModule() ? dlsym(RTLD_DEFAULT, lpProcName) : nullptr;
#endif
    if (!symbol)
        Bo1_SetLastError(ERROR_PROC_NOT_FOUND);
    return (FARPROC)symbol;
}

// ---- folders (ShlObj.h): the XDG locations stand for the Windows user folders.

HRESULT SHGetFolderPathA(HWND, int csidl, HANDLE, DWORD, LPSTR pszPath)
{
    const char *home = getenv("HOME");
    if (!home || !*home)
        return E_FAIL;
    std::string path;
    switch (csidl & 0xFF)
    {
    case 0x05: // CSIDL_PERSONAL
        path = std::string(home) + "/Documents";
        break;
    case 0x1A: // CSIDL_APPDATA
    case 0x1C: // CSIDL_LOCAL_APPDATA
    {
        const char *dataHome = getenv("XDG_DATA_HOME");
        path = dataHome && *dataHome ? dataHome : std::string(home) + "/.local/share";
        break;
    }
    default:
        return E_INVALIDARG;
    }
    if (path.size() >= MAX_PATH)
        return E_FAIL;
    strcpy(pszPath, path.c_str());
    return S_OK;
}

const char *DXGetErrorDescription9A(HRESULT hr)
{
    static thread_local char description[64];
    snprintf(description, sizeof(description), "HRESULT 0x%08x", (unsigned int)hr);
    return description;
}

// ---- devices the posix dedicated build does not have: no DirectSound device, no wave input or mixer device. The
// XInput controller is in win32_gamepad.cpp (SDL_GameController).

HRESULT DirectSoundCreate8(LPCGUID, LPDIRECTSOUND8 *ppDS8, LPUNKNOWN)
{
    if (ppDS8)
        *ppDS8 = nullptr;
    return DSERR_NODRIVER;
}

UINT waveInGetNumDevs(void)
{
    return 0;
}

UINT mixerGetNumDevs(void)
{
    return 0;
}

MMRESULT mixerOpen(LPHMIXER phmx, UINT, DWORD_PTR, DWORD_PTR, DWORD)
{
    if (phmx)
        *phmx = nullptr;
    return MMSYSERR_BADDEVICEID;
}

MMRESULT mixerClose(HMIXER)
{
    return MMSYSERR_INVALHANDLE;
}

MMRESULT mixerGetLineInfoA(HMIXEROBJ, LPMIXERLINEA, DWORD)
{
    return MMSYSERR_INVALHANDLE;
}

MMRESULT mixerGetLineControlsA(HMIXEROBJ, LPMIXERLINECONTROLSA, DWORD)
{
    return MMSYSERR_INVALHANDLE;
}

MMRESULT mixerGetControlDetailsA(HMIXEROBJ, LPMIXERCONTROLDETAILS, DWORD)
{
    return MMSYSERR_INVALHANDLE;
}

MMRESULT mixerSetControlDetails(HMIXEROBJ, LPMIXERCONTROLDETAILS, DWORD)
{
    return MMSYSERR_INVALHANDLE;
}

} // extern "C"
