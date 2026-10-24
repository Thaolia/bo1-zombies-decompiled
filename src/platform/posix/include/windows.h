#pragma once

// The part of the Win32 headers the engine and the DirectX SDK's d3d9.h use, for the posix and switch builds.
// Types keep their Windows sizes (LLP64: LONG and DWORD are 4 bytes, pointers 8) and structs their Windows layout.
// Functions are only declared, with their Windows signatures: the platform layer (src/platform/posix/) implements
// them.

#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <wchar.h>
#include <pthread.h>

typedef int BOOL;
typedef unsigned char BOOLEAN;
typedef unsigned char BYTE;
typedef unsigned char UCHAR;
typedef char CHAR;
typedef wchar_t WCHAR;
typedef short SHORT;
typedef unsigned short USHORT;
typedef unsigned short WORD;
typedef int INT;
typedef unsigned int UINT;
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef uint32_t DWORD;
typedef int32_t INT32;
typedef uint32_t UINT32;
// The 64-bit types are __int64, which is long long on MSVC (compat.h): int64_t is long here, a different type.
typedef long long LONGLONG;
typedef unsigned long long ULONGLONG;
typedef unsigned long long DWORDLONG;
typedef unsigned long long DWORD64;
typedef long long LONG64;
typedef unsigned long long ULONG64;
typedef long long INT64;
typedef unsigned long long UINT64;
typedef float FLOAT;
typedef void VOID;
typedef intptr_t INT_PTR;
typedef uintptr_t UINT_PTR;
typedef intptr_t LONG_PTR;
typedef uintptr_t ULONG_PTR;
typedef uintptr_t DWORD_PTR;
typedef uintptr_t SIZE_T;
typedef intptr_t SSIZE_T;
typedef UINT_PTR WPARAM;
typedef LONG_PTR LPARAM;
typedef LONG_PTR LRESULT;
typedef LONG HRESULT;

typedef void *PVOID;
typedef void *LPVOID;
typedef const void *LPCVOID;
typedef BYTE *LPBYTE;
typedef BYTE *PBYTE;
typedef DWORD *LPDWORD;
typedef DWORD *PDWORD;
typedef DWORD64 *PDWORD64;
typedef DWORD_PTR *PDWORD_PTR;
typedef LONG *PLONG;
typedef ULONG *PULONG;
typedef ULONG64 *PULONG64;
typedef WORD *LPWORD;
typedef BOOL *LPBOOL;
typedef char *LPSTR;
typedef char *PSTR;
typedef const char *LPCSTR;
typedef const char *PCSTR;
typedef WCHAR *LPWSTR;
typedef const WCHAR *LPCWSTR;

typedef void *HANDLE;
typedef HANDLE *PHANDLE;
typedef HANDLE *LPHANDLE;
typedef HANDLE HGLOBAL;
typedef HANDLE HLOCAL;
#define DECLARE_HANDLE(name) typedef struct name##__ *name
DECLARE_HANDLE(HWND);
DECLARE_HANDLE(HINSTANCE);
DECLARE_HANDLE(HMONITOR);
DECLARE_HANDLE(HDC);
DECLARE_HANDLE(HICON);
DECLARE_HANDLE(HBRUSH);
DECLARE_HANDLE(HFONT);
DECLARE_HANDLE(HMENU);
DECLARE_HANDLE(HKEY);
typedef HINSTANCE HMODULE;
typedef HICON HCURSOR;

#define TRUE 1
#define FALSE 0
#define CONST const
#define WINAPI
#define APIENTRY
#define CALLBACK
#define STDMETHODCALLTYPE
#define MAX_PATH 260
#define INFINITE 0xFFFFFFFFu
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))
// minwindef.h's: q_shared.h defines its own under the same names, and the later include wins, as with the SDK.
#undef LOBYTE
#undef LOWORD
#undef HIBYTE
#undef HIWORD
#define LOBYTE(w) ((BYTE)(((DWORD_PTR)(w)) & 0xff))
#define HIBYTE(w) ((BYTE)((((DWORD_PTR)(w)) >> 8) & 0xff))
#define LOWORD(l) ((WORD)(((DWORD_PTR)(l)) & 0xffff))
#define HIWORD(l) ((WORD)((((DWORD_PTR)(l)) >> 16) & 0xffff))

typedef INT_PTR(WINAPI *FARPROC)();

#define S_OK ((HRESULT)0)
#define S_FALSE ((HRESULT)1)
#define E_FAIL ((HRESULT)0x80004005)
#define E_OUTOFMEMORY ((HRESULT)0x8007000E)
#define E_INVALIDARG ((HRESULT)0x80070057)
#define E_NOTIMPL ((HRESULT)0x80004001)
#define E_NOINTERFACE ((HRESULT)0x80004002)
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#define MAKE_HRESULT(sev, fac, code) \
    ((HRESULT)(((uint32_t)(sev) << 31) | ((uint32_t)(fac) << 16) | ((uint32_t)(code))))

// winnt.h's Interlocked forms are the compiler intrinsics (msvc_intrinsics.h).
#define InterlockedIncrement _InterlockedIncrement
#define InterlockedDecrement _InterlockedDecrement
#define InterlockedExchange _InterlockedExchange
#define InterlockedExchangeAdd _InterlockedExchangeAdd
#define InterlockedCompareExchange _InterlockedCompareExchange
#define InterlockedCompareExchange64 _InterlockedCompareExchange64
#define InterlockedExchange64 _InterlockedExchange64
#define InterlockedCompareExchangePointer _InterlockedCompareExchangePointer
#define InterlockedExchangePointer _InterlockedExchangePointer
#define MemoryBarrier() __atomic_thread_fence(__ATOMIC_SEQ_CST)
// winnt.h's C++ overload for an unsigned 64-bit operand: the add is 64 bits wide (_InterlockedExchangeAdd64), whatever
// the size of the object the caller cast the address from.
static inline unsigned long long _InterlockedExchangeAdd(volatile unsigned long long *addend, unsigned long long value)
{
    return __atomic_fetch_add(addend, value, __ATOMIC_SEQ_CST);
}

#define MAKEFOURCC(ch0, ch1, ch2, ch3) \
    ((DWORD)(BYTE)(ch0) | ((DWORD)(BYTE)(ch1) << 8) | ((DWORD)(BYTE)(ch2) << 16) | ((DWORD)(BYTE)(ch3) << 24))

typedef union _LARGE_INTEGER
{
    struct
    {
        DWORD LowPart;
        LONG HighPart;
    };
    LONGLONG QuadPart;
} LARGE_INTEGER;

typedef union _ULARGE_INTEGER
{
    struct
    {
        DWORD LowPart;
        DWORD HighPart;
    };
    ULONGLONG QuadPart;
} ULARGE_INTEGER;

typedef struct _LUID
{
    DWORD LowPart;
    LONG HighPart;
} LUID;

typedef struct _LUID_AND_ATTRIBUTES
{
    LUID Luid;
    DWORD Attributes;
} LUID_AND_ATTRIBUTES;

typedef struct _TOKEN_PRIVILEGES
{
    DWORD PrivilegeCount;
    LUID_AND_ATTRIBUTES Privileges[1];
} TOKEN_PRIVILEGES;

typedef struct tagRECT
{
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECT, *LPRECT;
typedef const RECT *LPCRECT;

typedef struct tagPOINT
{
    LONG x;
    LONG y;
} POINT, *LPPOINT;

typedef struct _POINTL
{
    LONG x;
    LONG y;
} POINTL;

typedef struct tagSIZE
{
    LONG cx;
    LONG cy;
} SIZE;

typedef struct tagPALETTEENTRY
{
    BYTE peRed;
    BYTE peGreen;
    BYTE peBlue;
    BYTE peFlags;
} PALETTEENTRY;

typedef struct _RGNDATAHEADER
{
    DWORD dwSize;
    DWORD iType;
    DWORD nCount;
    DWORD nRgnSize;
    RECT rcBound;
} RGNDATAHEADER;

typedef struct _RGNDATA
{
    RGNDATAHEADER rdh;
    char Buffer[1];
} RGNDATA;

// COM, as the SDK's d3d9.h declares its interfaces.
typedef struct _GUID
{
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t Data4[8];
} GUID;
typedef GUID IID;
typedef GUID CLSID;
typedef const GUID &REFGUID;
typedef const IID &REFIID;
typedef const CLSID &REFCLSID;

#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    inline constexpr GUID name = {l, w1, w2, {b1, b2, b3, b4, b5, b6, b7, b8}}

#define interface struct
#define PURE = 0
#define THIS_
#define THIS void
#define STDMETHOD(method) virtual HRESULT STDMETHODCALLTYPE method
#define STDMETHOD_(type, method) virtual type STDMETHODCALLTYPE method
#define DECLARE_INTERFACE(iface) interface iface
#define DECLARE_INTERFACE_(iface, base) interface iface : public base

DECLARE_INTERFACE(IUnknown)
{
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **ppvObject) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
};
typedef IUnknown *LPUNKNOWN;

// ---- Exceptions (winnt.h). CONTEXT is the Windows one of the target architecture.
#define EXCEPTION_MAXIMUM_PARAMETERS 15
typedef struct _EXCEPTION_RECORD
{
    DWORD ExceptionCode;
    DWORD ExceptionFlags;
    struct _EXCEPTION_RECORD *ExceptionRecord;
    PVOID ExceptionAddress;
    DWORD NumberParameters;
    ULONG_PTR ExceptionInformation[EXCEPTION_MAXIMUM_PARAMETERS];
} EXCEPTION_RECORD, *PEXCEPTION_RECORD;

typedef struct alignas(16) _M128A
{
    ULONGLONG Low;
    LONGLONG High;
} M128A;

#if defined(__x86_64__)
typedef struct alignas(16) _XSAVE_FORMAT
{
    WORD ControlWord;
    WORD StatusWord;
    BYTE TagWord;
    BYTE Reserved1;
    WORD ErrorOpcode;
    DWORD ErrorOffset;
    WORD ErrorSelector;
    WORD Reserved2;
    DWORD DataOffset;
    WORD DataSelector;
    WORD Reserved3;
    DWORD MxCsr;
    DWORD MxCsr_Mask;
    M128A FloatRegisters[8];
    M128A XmmRegisters[16];
    BYTE Reserved4[96];
} XSAVE_FORMAT, XMM_SAVE_AREA32;

typedef struct alignas(16) _CONTEXT
{
    DWORD64 P1Home;
    DWORD64 P2Home;
    DWORD64 P3Home;
    DWORD64 P4Home;
    DWORD64 P5Home;
    DWORD64 P6Home;
    DWORD ContextFlags;
    DWORD MxCsr;
    WORD SegCs;
    WORD SegDs;
    WORD SegEs;
    WORD SegFs;
    WORD SegGs;
    WORD SegSs;
    DWORD EFlags;
    DWORD64 Dr0;
    DWORD64 Dr1;
    DWORD64 Dr2;
    DWORD64 Dr3;
    DWORD64 Dr6;
    DWORD64 Dr7;
    DWORD64 Rax;
    DWORD64 Rcx;
    DWORD64 Rdx;
    DWORD64 Rbx;
    DWORD64 Rsp;
    DWORD64 Rbp;
    DWORD64 Rsi;
    DWORD64 Rdi;
    DWORD64 R8;
    DWORD64 R9;
    DWORD64 R10;
    DWORD64 R11;
    DWORD64 R12;
    DWORD64 R13;
    DWORD64 R14;
    DWORD64 R15;
    DWORD64 Rip;
    XMM_SAVE_AREA32 FltSave;
    M128A VectorRegister[26];
    DWORD64 VectorControl;
    DWORD64 DebugControl;
    DWORD64 LastBranchToRip;
    DWORD64 LastBranchFromRip;
    DWORD64 LastExceptionToRip;
    DWORD64 LastExceptionFromRip;
} CONTEXT;
static_assert(sizeof(CONTEXT) == 1232, "CONTEXT must keep the Windows x64 layout");
#define CONTEXT_AMD64 0x00100000L
#define CONTEXT_CONTROL (CONTEXT_AMD64 | 0x00000001L)
#define CONTEXT_INTEGER (CONTEXT_AMD64 | 0x00000002L)
#define CONTEXT_SEGMENTS (CONTEXT_AMD64 | 0x00000004L)
#elif defined(__aarch64__)
typedef struct alignas(16) _CONTEXT
{
    DWORD ContextFlags;
    DWORD Cpsr;
    union
    {
        struct
        {
            DWORD64 X0, X1, X2, X3, X4, X5, X6, X7, X8, X9, X10, X11, X12, X13, X14;
            DWORD64 X15, X16, X17, X18, X19, X20, X21, X22, X23, X24, X25, X26, X27, X28;
            DWORD64 Fp;
            DWORD64 Lr;
        };
        DWORD64 X[31];
    };
    DWORD64 Sp;
    DWORD64 Pc;
    M128A V[32];
    DWORD Fpcr;
    DWORD Fpsr;
    DWORD Bcr[8];
    DWORD64 Bvr[8];
    DWORD Wcr[2];
    DWORD64 Wvr[2];
} CONTEXT;
static_assert(sizeof(CONTEXT) == 912, "CONTEXT must keep the Windows ARM64 layout");
#define CONTEXT_ARM64 0x00400000L
#define CONTEXT_CONTROL (CONTEXT_ARM64 | 0x00000001L)
#define CONTEXT_INTEGER (CONTEXT_ARM64 | 0x00000002L)
#else
#error "windows.h shim: no CONTEXT for this architecture"
#endif
typedef CONTEXT *PCONTEXT, *LPCONTEXT;

typedef struct _EXCEPTION_POINTERS
{
    PEXCEPTION_RECORD ExceptionRecord;
    PCONTEXT ContextRecord;
} EXCEPTION_POINTERS, *PEXCEPTION_POINTERS;

typedef LONG(WINAPI *PTOP_LEVEL_EXCEPTION_FILTER)(struct _EXCEPTION_POINTERS *ExceptionInfo);
typedef PTOP_LEVEL_EXCEPTION_FILTER LPTOP_LEVEL_EXCEPTION_FILTER;
#define EXCEPTION_EXECUTE_HANDLER 1
#define EXCEPTION_CONTINUE_SEARCH 0
#define EXCEPTION_CONTINUE_EXECUTION (-1)

typedef struct _OSVERSIONINFOA
{
    DWORD dwOSVersionInfoSize;
    DWORD dwMajorVersion;
    DWORD dwMinorVersion;
    DWORD dwBuildNumber;
    DWORD dwPlatformId;
    CHAR szCSDVersion[128];
} OSVERSIONINFOA, *LPOSVERSIONINFOA;

// ---- Processes and threads (processthreadsapi.h, winbase.h, winnt.h)
typedef struct _SECURITY_ATTRIBUTES
{
    DWORD nLength;
    LPVOID lpSecurityDescriptor;
    BOOL bInheritHandle;
} SECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;

typedef DWORD(WINAPI *PTHREAD_START_ROUTINE)(LPVOID lpThreadParameter);
typedef PTHREAD_START_ROUTINE LPTHREAD_START_ROUTINE;

#define CREATE_SUSPENDED 0x00000004
#define THREAD_SUSPEND_RESUME 0x0002
#define THREAD_GET_CONTEXT 0x0008
#define THREAD_QUERY_INFORMATION 0x0040
#define THREAD_QUERY_LIMITED_INFORMATION 0x0800
#define THREAD_PRIORITY_NORMAL 0
#define THREAD_PRIORITY_ABOVE_NORMAL 1
#define NORMAL_PRIORITY_CLASS 0x00000020
#define HIGH_PRIORITY_CLASS 0x00000080
#define STILL_ACTIVE 259
#define ABOVE_NORMAL_PRIORITY_CLASS 0x00008000

// ---- Synchronisation (synchapi.h). Each lock object is the pthread object the platform layer implements it with
// (CRITICAL_SECTION: a recursive mutex), so it is big enough on every target by construction: 40 and 56 bytes on
// x86-64 glibc, against Windows' 40 and 8. The engine never reads their fields nor puts them in a pinned layout.
typedef struct _RTL_CRITICAL_SECTION
{
    pthread_mutex_t Mutex;
} CRITICAL_SECTION, *LPCRITICAL_SECTION;

typedef struct _RTL_SRWLOCK
{
    pthread_rwlock_t Lock;
} SRWLOCK, *PSRWLOCK;
#define SRWLOCK_INIT {PTHREAD_RWLOCK_INITIALIZER}

// ---- Time (minwinbase.h)
typedef struct _FILETIME
{
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
} FILETIME, *PFILETIME, *LPFILETIME;

typedef struct _SYSTEMTIME
{
    WORD wYear;
    WORD wMonth;
    WORD wDayOfWeek;
    WORD wDay;
    WORD wHour;
    WORD wMinute;
    WORD wSecond;
    WORD wMilliseconds;
} SYSTEMTIME, *PSYSTEMTIME, *LPSYSTEMTIME;

// ---- Memory (memoryapi.h, sysinfoapi.h, winbase.h)
typedef struct _MEMORY_BASIC_INFORMATION
{
    PVOID BaseAddress;
    PVOID AllocationBase;
    DWORD AllocationProtect;
    SIZE_T RegionSize;
    DWORD State;
    DWORD Protect;
    DWORD Type;
} MEMORY_BASIC_INFORMATION, *PMEMORY_BASIC_INFORMATION;

typedef struct _MEMORYSTATUS
{
    DWORD dwLength;
    DWORD dwMemoryLoad;
    SIZE_T dwTotalPhys;
    SIZE_T dwAvailPhys;
    SIZE_T dwTotalPageFile;
    SIZE_T dwAvailPageFile;
    SIZE_T dwTotalVirtual;
    SIZE_T dwAvailVirtual;
} MEMORYSTATUS, *LPMEMORYSTATUS;

typedef struct _MEMORYSTATUSEX
{
    DWORD dwLength;
    DWORD dwMemoryLoad;
    DWORDLONG ullTotalPhys;
    DWORDLONG ullAvailPhys;
    DWORDLONG ullTotalPageFile;
    DWORDLONG ullAvailPageFile;
    DWORDLONG ullTotalVirtual;
    DWORDLONG ullAvailVirtual;
    DWORDLONG ullAvailExtendedVirtual;
} MEMORYSTATUSEX, *LPMEMORYSTATUSEX;

typedef enum _CPU_SET_INFORMATION_TYPE
{
    CpuSetInformation
} CPU_SET_INFORMATION_TYPE;

typedef struct _SYSTEM_CPU_SET_INFORMATION
{
    DWORD Size;
    CPU_SET_INFORMATION_TYPE Type;
    union
    {
        struct
        {
            DWORD Id;
            WORD Group;
            BYTE LogicalProcessorIndex;
            BYTE CoreIndex;
            BYTE LastLevelCacheIndex;
            BYTE NumaNodeIndex;
            BYTE EfficiencyClass;
            union
            {
                BYTE AllFlags;
                struct
                {
                    BYTE Parked : 1;
                    BYTE Allocated : 1;
                    BYTE AllocatedToTargetProcess : 1;
                    BYTE RealTime : 1;
                    BYTE ReservedFlags : 4;
                };
            };
            union
            {
                DWORD Reserved;
                BYTE SchedulingClass;
            };
            DWORD64 AllocationTag;
        } CpuSet;
    };
} SYSTEM_CPU_SET_INFORMATION, *PSYSTEM_CPU_SET_INFORMATION;
static_assert(sizeof(SYSTEM_CPU_SET_INFORMATION) == 32, "SYSTEM_CPU_SET_INFORMATION must keep the Windows layout");

typedef struct _SYSTEM_INFO
{
    union
    {
        DWORD dwOemId;
        struct
        {
            WORD wProcessorArchitecture;
            WORD wReserved;
        };
    };
    DWORD dwPageSize;
    LPVOID lpMinimumApplicationAddress;
    LPVOID lpMaximumApplicationAddress;
    DWORD_PTR dwActiveProcessorMask;
    DWORD dwNumberOfProcessors;
    DWORD dwProcessorType;
    DWORD dwAllocationGranularity;
    WORD wProcessorLevel;
    WORD wProcessorRevision;
} SYSTEM_INFO, *LPSYSTEM_INFO;

// ---- Files (fileapi.h, minwinbase.h)
typedef struct _OVERLAPPED
{
    ULONG_PTR Internal;
    ULONG_PTR InternalHigh;
    union
    {
        struct
        {
            DWORD Offset;
            DWORD OffsetHigh;
        };
        PVOID Pointer;
    };
    HANDLE hEvent;
} OVERLAPPED, *LPOVERLAPPED;

typedef void(WINAPI *LPOVERLAPPED_COMPLETION_ROUTINE)(DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered,
                                                      LPOVERLAPPED lpOverlapped);

typedef struct _WIN32_FIND_DATAA
{
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
    DWORD dwReserved0;
    DWORD dwReserved1;
    CHAR cFileName[MAX_PATH];
    CHAR cAlternateFileName[14];
} WIN32_FIND_DATAA, *LPWIN32_FIND_DATAA;

// ---- Modules (libloaderapi.h)
#define GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT 0x00000002
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 0x00000004

// ---- Windows, monitors, messages, clipboard, GDI (winuser.h, wingdi.h)
typedef LRESULT(CALLBACK *WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef BOOL(CALLBACK *WNDENUMPROC)(HWND, LPARAM);
typedef BOOL(CALLBACK *MONITORENUMPROC)(HMONITOR, HDC, LPRECT, LPARAM);
typedef WORD ATOM;
DECLARE_HANDLE(HHOOK);
typedef DWORD EXECUTION_STATE;

typedef struct tagWNDCLASSEXA
{
    UINT cbSize;
    UINT style;
    WNDPROC lpfnWndProc;
    int cbClsExtra;
    int cbWndExtra;
    HINSTANCE hInstance;
    HICON hIcon;
    HCURSOR hCursor;
    HBRUSH hbrBackground;
    LPCSTR lpszMenuName;
    LPCSTR lpszClassName;
    HICON hIconSm;
} WNDCLASSEXA;

typedef struct tagMSG
{
    HWND hwnd;
    UINT message;
    WPARAM wParam;
    LPARAM lParam;
    DWORD time;
    POINT pt;
} MSG, *LPMSG;

#define CCHDEVICENAME 32
#define CCHFORMNAME 32
typedef struct tagMONITORINFO
{
    DWORD cbSize;
    RECT rcMonitor;
    RECT rcWork;
    DWORD dwFlags;
} MONITORINFO, *LPMONITORINFO;

typedef struct tagMONITORINFOEXA : public tagMONITORINFO
{
    CHAR szDevice[CCHDEVICENAME];
} MONITORINFOEXA;

typedef struct _devicemodeA
{
    BYTE dmDeviceName[CCHDEVICENAME];
    WORD dmSpecVersion;
    WORD dmDriverVersion;
    WORD dmSize;
    WORD dmDriverExtra;
    DWORD dmFields;
    union
    {
        struct
        {
            short dmOrientation;
            short dmPaperSize;
            short dmPaperLength;
            short dmPaperWidth;
            short dmScale;
            short dmCopies;
            short dmDefaultSource;
            short dmPrintQuality;
        };
        struct
        {
            POINTL dmPosition;
            DWORD dmDisplayOrientation;
            DWORD dmDisplayFixedOutput;
        };
    };
    short dmColor;
    short dmDuplex;
    short dmYResolution;
    short dmTTOption;
    short dmCollate;
    BYTE dmFormName[CCHFORMNAME];
    WORD dmLogPixels;
    DWORD dmBitsPerPel;
    DWORD dmPelsWidth;
    DWORD dmPelsHeight;
    union
    {
        DWORD dmDisplayFlags;
        DWORD dmNup;
    };
    DWORD dmDisplayFrequency;
    DWORD dmICMMethod;
    DWORD dmICMIntent;
    DWORD dmMediaType;
    DWORD dmDitherType;
    DWORD dmReserved1;
    DWORD dmReserved2;
    DWORD dmPanningWidth;
    DWORD dmPanningHeight;
} DEVMODEA, *LPDEVMODEA;
static_assert(sizeof(DEVMODEA) == 156, "DEVMODEA must keep the Windows layout");

#define GWL_STYLE (-16)
#define GWL_EXSTYLE (-20)
#define WS_POPUP 0x80000000L
#define WS_VISIBLE 0x10000000L
#define HWND_NOTOPMOST ((HWND)(intptr_t)-2)
#define SM_CXSCREEN 0
#define SM_CYSCREEN 1
#define MONITOR_DEFAULTTOPRIMARY 0x00000001
#define MONITOR_DEFAULTTONEAREST 0x00000002
#define ENUM_CURRENT_SETTINGS ((DWORD)-1)
#define WM_ACTIVATE 0x0006
#define WM_SETFOCUS 0x0007
#define WM_KILLFOCUS 0x0008
#define WM_CLOSE 0x0010
#define WM_ACTIVATEAPP 0x001C
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define WM_MOUSEWHEEL 0x020A
#define MK_LBUTTON 0x0001
#define MB_OK 0x00000000L
#define MB_YESNO 0x00000004L
#define IDOK 1
#define IDYES 6
#define IDNO 7
#define MAKEINTRESOURCEA(i) ((LPSTR)((ULONG_PTR)((WORD)(i))))
#define IDC_ARROW MAKEINTRESOURCEA(32512)
#define IDC_WAIT MAKEINTRESOURCEA(32514)
#define LoadCursor LoadCursorA
#define GetMessage GetMessageA
#define DispatchMessage DispatchMessageA
#define WA_INACTIVE 0
#define WA_ACTIVE 1

// ---- Constants (winerror.h, winbase.h, winnt.h, fileapi.h)
#define ERROR_SUCCESS 0L
#define ERROR_FILE_NOT_FOUND 2L
#define ERROR_PATH_NOT_FOUND 3L
#define ERROR_ACCESS_DENIED 5L
#define ERROR_INVALID_HANDLE 6L
#define ERROR_NOT_ENOUGH_MEMORY 8L
#define ERROR_NO_MORE_FILES 18L
#define ERROR_HANDLE_EOF 38L
#define ERROR_NOT_SUPPORTED 50L
#define ERROR_FILE_EXISTS 80L
#define ERROR_INVALID_PARAMETER 87L
#define ERROR_DISK_FULL 112L
#define ERROR_INSUFFICIENT_BUFFER 122L
#define ERROR_MOD_NOT_FOUND 126L
#define ERROR_PROC_NOT_FOUND 127L
#define ERROR_DIR_NOT_EMPTY 145L
#define ERROR_ALREADY_EXISTS 183L
#define ERROR_NOT_OWNER 288L
#define ERROR_DEVICE_NOT_CONNECTED 1167L
#define ERROR_CLASS_ALREADY_EXISTS 1410L
#define ERROR_RESOURCE_TYPE_NOT_FOUND 1813L

#define WAIT_OBJECT_0 0x00000000L
#define WAIT_ABANDONED 0x00000080L
#define WAIT_IO_COMPLETION 0x000000C0L
#define WAIT_TIMEOUT 0x00000102L
#define WAIT_FAILED ((DWORD)0xFFFFFFFF)

#define MEM_COMMIT 0x00001000
#define MEM_RESERVE 0x00002000
#define MEM_DECOMMIT 0x00004000
#define MEM_RELEASE 0x00008000
#define MEM_FREE 0x00010000
#define MEM_PRIVATE 0x00020000
#define PAGE_NOACCESS 0x01
#define PAGE_READONLY 0x02
#define PAGE_READWRITE 0x04
#define PAGE_EXECUTE 0x10
#define PAGE_EXECUTE_READ 0x20
#define PAGE_EXECUTE_READWRITE 0x40

#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define GENERIC_ALL 0x10000000u
#define FILE_READ_DATA 0x0001
#define FILE_WRITE_DATA 0x0002
#define CREATE_NEW 1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define TRUNCATE_EXISTING 5
#define FILE_FLAG_OVERLAPPED 0x40000000
#define FILE_APPEND_DATA 0x0004
#define FILE_SHARE_READ 0x00000001
#define FILE_SHARE_WRITE 0x00000002
#define FILE_SHARE_DELETE 0x00000004
#define INVALID_FILE_SIZE ((DWORD)0xFFFFFFFF)
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#define FILE_ATTRIBUTE_READONLY 0x00000001
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define FILE_ATTRIBUTE_ARCHIVE 0x00000020
#define FILE_ATTRIBUTE_NORMAL 0x00000080

#define SEM_FAILCRITICALERRORS 0x0001
#define SEM_NOOPENFILEERRORBOX 0x8000
#define TIMERR_NOERROR 0
#define FORMAT_MESSAGE_ALLOCATE_BUFFER 0x00000100
#define FORMAT_MESSAGE_IGNORE_INSERTS 0x00000200
#define FORMAT_MESSAGE_FROM_SYSTEM 0x00001000

// ---- Functions, by area. Declared only: the platform layer defines them with these signatures.
extern "C" {
// errors and debugging
DWORD GetLastError(void);
void SetLastError(DWORD dwErrCode);
void OutputDebugStringA(LPCSTR lpOutputString);
BOOL IsDebuggerPresent(void);
void RaiseException(DWORD dwExceptionCode, DWORD dwExceptionFlags, DWORD nNumberOfArguments,
                    const ULONG_PTR *lpArguments);
LPTOP_LEVEL_EXCEPTION_FILTER SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter);
UINT SetErrorMode(UINT uMode);
DWORD FormatMessageA(DWORD dwFlags, LPCVOID lpSource, DWORD dwMessageId, DWORD dwLanguageId, LPSTR lpBuffer,
                     DWORD nSize, va_list *Arguments);
HLOCAL LocalFree(HLOCAL hMem);
BOOL GetVersionExA(LPOSVERSIONINFOA lpVersionInformation);

// handles, processes and threads
BOOL CloseHandle(HANDLE hObject);
BOOL DuplicateHandle(HANDLE hSourceProcessHandle, HANDLE hSourceHandle, HANDLE hTargetProcessHandle,
                     LPHANDLE lpTargetHandle, DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwOptions);
LPSTR GetCommandLineA(void);
BOOL SetEnvironmentVariableA(LPCSTR lpName, LPCSTR lpValue);
void ExitProcess(UINT uExitCode);
BOOL TerminateProcess(HANDLE hProcess, UINT uExitCode);
HANDLE GetCurrentProcess(void);
DWORD GetCurrentProcessId(void);
HANDLE OpenProcess(DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwProcessId);
DWORD GetPriorityClass(HANDLE hProcess);
BOOL SetPriorityClass(HANDLE hProcess, DWORD dwPriorityClass);
BOOL GetProcessAffinityMask(HANDLE hProcess, PDWORD_PTR lpProcessAffinityMask, PDWORD_PTR lpSystemAffinityMask);
BOOL SetProcessAffinityMask(HANDLE hProcess, DWORD_PTR dwProcessAffinityMask);
BOOL GetProcessTimes(HANDLE hProcess, LPFILETIME lpCreationTime, LPFILETIME lpExitTime, LPFILETIME lpKernelTime,
                     LPFILETIME lpUserTime);
BOOL GetProcessHandleCount(HANDLE hProcess, PDWORD pdwHandleCount);
BOOL QueryProcessCycleTime(HANDLE ProcessHandle, PULONG64 CycleTime);
HANDLE CreateThread(LPSECURITY_ATTRIBUTES lpThreadAttributes, SIZE_T dwStackSize, LPTHREAD_START_ROUTINE lpStartAddress,
                    LPVOID lpParameter, DWORD dwCreationFlags, LPDWORD lpThreadId);
HANDLE GetCurrentThread(void);
DWORD GetCurrentThreadId(void);
DWORD GetCurrentProcessorNumber(void);
HANDLE OpenThread(DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwThreadId);
DWORD SuspendThread(HANDLE hThread);
BOOL GetExitCodeThread(HANDLE hThread, LPDWORD lpExitCode);
BOOL TerminateThread(HANDLE hThread, DWORD dwExitCode);
DWORD ResumeThread(HANDLE hThread);
BOOL GetThreadContext(HANDLE hThread, LPCONTEXT lpContext);
int GetThreadPriority(HANDLE hThread);
BOOL SetThreadPriority(HANDLE hThread, int nPriority);
DWORD_PTR SetThreadAffinityMask(HANDLE hThread, DWORD_PTR dwThreadAffinityMask);
DWORD SetThreadIdealProcessor(HANDLE hThread, DWORD dwIdealProcessor);
BOOL GetThreadTimes(HANDLE hThread, LPFILETIME lpCreationTime, LPFILETIME lpExitTime, LPFILETIME lpKernelTime,
                    LPFILETIME lpUserTime);
BOOL QueryThreadCycleTime(HANDLE ThreadHandle, PULONG64 CycleTime);
void Sleep(DWORD dwMilliseconds);
DWORD SleepEx(DWORD dwMilliseconds, BOOL bAlertable);
BOOL SwitchToThread(void);

// synchronisation
DWORD WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds);
HANDLE CreateEventA(LPSECURITY_ATTRIBUTES lpEventAttributes, BOOL bManualReset, BOOL bInitialState, LPCSTR lpName);
BOOL SetEvent(HANDLE hEvent);
BOOL ResetEvent(HANDLE hEvent);
BOOL PulseEvent(HANDLE hEvent);
HANDLE CreateMutexA(LPSECURITY_ATTRIBUTES lpMutexAttributes, BOOL bInitialOwner, LPCSTR lpName);
BOOL ReleaseMutex(HANDLE hMutex);
void InitializeCriticalSection(LPCRITICAL_SECTION lpCriticalSection);
void EnterCriticalSection(LPCRITICAL_SECTION lpCriticalSection);
BOOL TryEnterCriticalSection(LPCRITICAL_SECTION lpCriticalSection);
void LeaveCriticalSection(LPCRITICAL_SECTION lpCriticalSection);
void AcquireSRWLockExclusive(PSRWLOCK SRWLock);
void ReleaseSRWLockExclusive(PSRWLOCK SRWLock);
void AcquireSRWLockShared(PSRWLOCK SRWLock);
void ReleaseSRWLockShared(PSRWLOCK SRWLock);

// time
BOOL QueryPerformanceCounter(LARGE_INTEGER *lpPerformanceCount);
BOOL QueryPerformanceFrequency(LARGE_INTEGER *lpFrequency);
void GetSystemTime(LPSYSTEMTIME lpSystemTime);
void GetSystemTimeAsFileTime(LPFILETIME lpSystemTimeAsFileTime);
BOOL SystemTimeToFileTime(const SYSTEMTIME *lpSystemTime, LPFILETIME lpFileTime);
LONG CompareFileTime(const FILETIME *lpFileTime1, const FILETIME *lpFileTime2);
BOOL GetSystemTimes(PFILETIME lpIdleTime, PFILETIME lpKernelTime, PFILETIME lpUserTime);
DWORD timeGetTime(void);
UINT timeBeginPeriod(UINT uPeriod);
UINT timeEndPeriod(UINT uPeriod);
DWORD GetTickCount(void);

// memory and system information
LPVOID VirtualAlloc(LPVOID lpAddress, SIZE_T dwSize, DWORD flAllocationType, DWORD flProtect);
BOOL VirtualFree(LPVOID lpAddress, SIZE_T dwSize, DWORD dwFreeType);
SIZE_T VirtualQuery(LPCVOID lpAddress, PMEMORY_BASIC_INFORMATION lpBuffer, SIZE_T dwLength);
void GlobalMemoryStatus(LPMEMORYSTATUS lpBuffer);
BOOL GlobalMemoryStatusEx(LPMEMORYSTATUSEX lpBuffer);
HGLOBAL GlobalAlloc(UINT uFlags, SIZE_T dwBytes);
LPVOID GlobalLock(HGLOBAL hMem);
BOOL GlobalUnlock(HGLOBAL hMem);
SIZE_T GlobalSize(HGLOBAL hMem);
HGLOBAL GlobalFree(HGLOBAL hMem);
void GetSystemInfo(LPSYSTEM_INFO lpSystemInfo);

// files
HANDLE CreateFileA(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                   LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
                   DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);
BOOL ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead,
              LPOVERLAPPED lpOverlapped);
BOOL ReadFileEx(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPOVERLAPPED lpOverlapped,
                LPOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine);
BOOL WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten,
               LPOVERLAPPED lpOverlapped);
DWORD GetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh);
BOOL DeleteFileA(LPCSTR lpFileName);
BOOL CreateDirectoryA(LPCSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes);
BOOL RemoveDirectoryA(LPCSTR lpPathName);
DWORD GetFileAttributesA(LPCSTR lpFileName);
BOOL SetFileAttributesA(LPCSTR lpFileName, DWORD dwFileAttributes);
DWORD GetFullPathNameA(LPCSTR lpFileName, DWORD nBufferLength, LPSTR lpBuffer, LPSTR *lpFilePart);
DWORD GetCurrentDirectoryA(DWORD nBufferLength, LPSTR lpBuffer);
HANDLE FindFirstFileA(LPCSTR lpFileName, LPWIN32_FIND_DATAA lpFindFileData);
BOOL FindNextFileA(HANDLE hFindFile, LPWIN32_FIND_DATAA lpFindFileData);
BOOL FindClose(HANDLE hFindFile);

// modules
HMODULE GetModuleHandleA(LPCSTR lpModuleName);
BOOL GetModuleHandleExA(DWORD dwFlags, LPCSTR lpModuleName, HMODULE *phModule);
DWORD GetModuleFileNameA(HMODULE hModule, LPSTR lpFilename, DWORD nSize);
HMODULE LoadLibraryA(LPCSTR lpLibFileName);
FARPROC GetProcAddress(HMODULE hModule, LPCSTR lpProcName);

// windows, monitors, messages, clipboard, GDI
int MessageBoxA(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType);
int MessageBoxW(HWND hWnd, LPCWSTR lpText, LPCWSTR lpCaption, UINT uType);
HCURSOR SetCursor(HCURSOR hCursor);
ATOM RegisterClassExA(const WNDCLASSEXA *lpwcx);
HICON LoadIconA(HINSTANCE hInstance, LPCSTR lpIconName);
HCURSOR LoadCursorA(HINSTANCE hInstance, LPCSTR lpCursorName);
HBRUSH CreateSolidBrush(DWORD color);
LRESULT DefWindowProcA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
LRESULT CallNextHookEx(HHOOK hhk, int nCode, WPARAM wParam, LPARAM lParam);
UINT RegisterWindowMessageA(LPCSTR lpString);
BOOL MoveWindow(HWND hWnd, int X, int Y, int nWidth, int nHeight, BOOL bRepaint);
BOOL AdjustWindowRect(LPRECT lpRect, DWORD dwStyle, BOOL bMenu);
UINT MapVirtualKeyA(UINT uCode, UINT uMapType);
SHORT GetAsyncKeyState(int vKey);
EXECUTION_STATE SetThreadExecutionState(EXECUTION_STATE esFlags);
BOOL SystemParametersInfoA(UINT uiAction, UINT uiParam, PVOID pvParam, UINT fWinIni);
HWND CreateWindowExA(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle, int X, int Y,
                     int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam);
BOOL DestroyWindow(HWND hWnd);
BOOL ShowWindow(HWND hWnd, int nCmdShow);
BOOL IsWindow(HWND hWnd);
BOOL IsIconic(HWND hWnd);
HWND GetActiveWindow(void);
HWND GetDesktopWindow(void);
HWND GetForegroundWindow(void);
HWND SetFocus(HWND hWnd);
LONG GetWindowLongA(HWND hWnd, int nIndex);
LONG SetWindowLongA(HWND hWnd, int nIndex, LONG dwNewLong);
BOOL SetWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags);
BOOL GetClientRect(HWND hWnd, LPRECT lpRect);
BOOL GetWindowRect(HWND hWnd, LPRECT lpRect);
BOOL ClientToScreen(HWND hWnd, LPPOINT lpPoint);
BOOL ScreenToClient(HWND hWnd, LPPOINT lpPoint);
BOOL GetCursorPos(LPPOINT lpPoint);
BOOL SetCursorPos(int X, int Y);
BOOL AdjustWindowRectEx(LPRECT lpRect, DWORD dwStyle, BOOL bMenu, DWORD dwExStyle);
int GetWindowTextA(HWND hWnd, LPSTR lpString, int nMaxCount);
BOOL EnumThreadWindows(DWORD dwThreadId, WNDENUMPROC lpfn, LPARAM lParam);
int GetSystemMetrics(int nIndex);
BOOL IsProcessDPIAware(void);
int ShowCursor(BOOL bShow);
BOOL EqualRect(const RECT *lprc1, const RECT *lprc2);
HMONITOR MonitorFromWindow(HWND hwnd, DWORD dwFlags);
HMONITOR MonitorFromPoint(POINT pt, DWORD dwFlags);
BOOL GetMonitorInfoA(HMONITOR hMonitor, LPMONITORINFO lpmi);
BOOL EnumDisplayMonitors(HDC hdc, LPCRECT lprcClip, MONITORENUMPROC lpfnEnum, LPARAM dwData);
BOOL EnumDisplaySettingsA(LPCSTR lpszDeviceName, DWORD iModeNum, DEVMODEA *lpDevMode);
LONG ChangeDisplaySettingsA(DEVMODEA *lpDevMode, DWORD dwFlags);
BOOL PeekMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg);
BOOL GetMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax);
BOOL TranslateMessage(const MSG *lpMsg);
LRESULT DispatchMessageA(const MSG *lpMsg);
BOOL PostMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
BOOL OpenClipboard(HWND hWndNewOwner);
BOOL EmptyClipboard(void);
HANDLE SetClipboardData(UINT uFormat, HANDLE hMem);
BOOL CloseClipboard(void);
HANDLE GetClipboardData(UINT uFormat);
HDC GetDC(HWND hWnd);
int ReleaseDC(HWND hWnd, HDC hDC);
BOOL SetDeviceGammaRamp(HDC hdc, LPVOID lpRamp);
HINSTANCE ShellExecuteA(HWND hwnd, LPCSTR lpOperation, LPCSTR lpFile, LPCSTR lpParameters, LPCSTR lpDirectory,
                        INT nShowCmd);
}

// <windows.h> brings in <winsock.h>: DemonWare's bdAddr.h relies on it for in_addr and inet_addr, win_net.cpp for the
// rest of Winsock. And <mmsystem.h>, for win_voice.cpp's mixer queries.
#include "mmsystem.h"
#include "winsock.h"
