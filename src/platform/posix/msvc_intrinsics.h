#pragma once

// MSVC compiler intrinsics and CRT extensions the decompiled engine calls, for the posix and switch builds.
// Included by compat.h. Each one keeps MSVC's semantics, including its integer widths: MSVC's long is 4 bytes, so the
// Interlocked and bit-scan forms refuse an 8-byte long instead of corrupting the word next to it. The CRT functions
// that only compute are defined here on the C library; _putenv and sscanf_s (and io.h / direct.h) are declared only,
// for the platform layer to implement.

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>
#include <malloc.h>
#include <math.h>
#include <time.h>
#include <signal.h>

#if defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#endif

#define _countof(a) (sizeof(a) / sizeof((a)[0]))
#define _ReturnAddress() __builtin_return_address(0)

static inline int *_errno(void)
{
    return &errno;
}

// The decompiled code names MSVC's FILE by its struct tag.
typedef FILE _iobuf;

static inline void __debugbreak(void)
{
#if defined(__clang__)
    __builtin_debugtrap();
#else
    raise(SIGTRAP);
#endif
}

#if defined(__cplusplus)
// MSVC accepts any 4-byte integer object for the 32-bit forms (the engine passes long, int and unsigned int fields)
// and any 8-byte one for the 64-bit forms. The templates do the same and refuse every other width, so a field declared
// long, 8 bytes here, fails to compile. All are full barriers, as the lock-prefixed x86 instructions are.
#include <type_traits>

template <typename T, size_t N> constexpr bool bo1_interlocked_operand =
    sizeof(T) == N && (std::is_integral_v<T> || std::is_enum_v<T>);

template <typename T> static inline int32_t _InterlockedExchangeAdd(volatile T *addend, int64_t value)
{
    static_assert(bo1_interlocked_operand<T, 4>, "_InterlockedExchangeAdd needs a 4-byte integer");
    return (int32_t)__atomic_fetch_add(addend, (T)value, __ATOMIC_SEQ_CST);
}
template <typename T> static inline int32_t _InterlockedIncrement(volatile T *addend)
{
    static_assert(bo1_interlocked_operand<T, 4>, "_InterlockedIncrement needs a 4-byte integer");
    return (int32_t)__atomic_add_fetch(addend, (T)1, __ATOMIC_SEQ_CST);
}
template <typename T> static inline int32_t _InterlockedDecrement(volatile T *addend)
{
    static_assert(bo1_interlocked_operand<T, 4>, "_InterlockedDecrement needs a 4-byte integer");
    return (int32_t)__atomic_sub_fetch(addend, (T)1, __ATOMIC_SEQ_CST);
}
template <typename T> static inline int32_t _InterlockedExchange(volatile T *target, int64_t value)
{
    static_assert(bo1_interlocked_operand<T, 4>, "_InterlockedExchange needs a 4-byte integer");
    return (int32_t)__atomic_exchange_n(target, (T)value, __ATOMIC_SEQ_CST);
}
template <typename T>
static inline int32_t _InterlockedCompareExchange(volatile T *destination, int64_t exchange, int64_t comparand)
{
    static_assert(bo1_interlocked_operand<T, 4>, "_InterlockedCompareExchange needs a 4-byte integer");
    T expected = (T)comparand;
    __atomic_compare_exchange_n(destination, &expected, (T)exchange, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return (int32_t)expected;
}
template <typename T> static inline long long _InterlockedExchange64(volatile T *target, long long value)
{
    static_assert(bo1_interlocked_operand<T, 8>, "_InterlockedExchange64 needs an 8-byte integer");
    return (long long)__atomic_exchange_n(target, (T)value, __ATOMIC_SEQ_CST);
}
template <typename T>
static inline long long _InterlockedCompareExchange64(volatile T *destination, long long exchange, long long comparand)
{
    static_assert(bo1_interlocked_operand<T, 8>, "_InterlockedCompareExchange64 needs an 8-byte integer");
    T expected = (T)comparand;
    __atomic_compare_exchange_n(destination, &expected, (T)exchange, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return (long long)expected;
}
static inline void *_InterlockedCompareExchangePointer(void *volatile *destination, void *exchange, void *comparand)
{
    __atomic_compare_exchange_n(destination, &comparand, exchange, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return comparand;
}
static inline void *_InterlockedExchangePointer(void *volatile *target, void *value)
{
    return __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST);
}
#endif
#define _ReadWriteBarrier() __atomic_signal_fence(__ATOMIC_SEQ_CST)

#if defined(__cplusplus)
// The index is an unsigned long on MSVC, 4 bytes: the engine passes DWORD and unsigned int too.
template <typename T> static inline unsigned char _BitScanReverse(T *index, uint32_t mask)
{
    static_assert(bo1_interlocked_operand<T, 4>, "_BitScanReverse needs a 4-byte index");
    if (!mask)
        return 0;
    *index = (T)(31u - (uint32_t)__builtin_clz(mask));
    return 1;
}
template <typename T> static inline unsigned char _BitScanForward(T *index, uint32_t mask)
{
    static_assert(bo1_interlocked_operand<T, 4>, "_BitScanForward needs a 4-byte index");
    if (!mask)
        return 0;
    *index = (T)__builtin_ctz(mask);
    return 1;
}
#endif

#if !defined(__x86_64__) && !defined(__i386__)
static inline uint64_t __rdtsc()
{
    uint64_t ticks;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(ticks));
    return ticks;
}
#endif

static inline int _stricmp(const char *a, const char *b)
{
    return strcasecmp(a, b);
}
static inline int _strnicmp(const char *a, const char *b, size_t n)
{
    return strncasecmp(a, b, n);
}

// MSVC's _vsnprintf writes at most count characters and, when the text does not fit, returns -1 without a
// terminator; callers rely on both (Com_sprintf checks for -1, then terminates the buffer itself).
static inline int _vsnprintf(char *buffer, size_t count, const char *format, va_list args)
{
    va_list copy;
    va_copy(copy, args);
    const int needed = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (needed < 0)
        return -1;
    if ((size_t)needed < count)
        return vsnprintf(buffer, count, format, args);
    if (count)
    {
        char *full = (char *)malloc((size_t)needed + 1);
        if (full)
        {
            vsnprintf(full, (size_t)needed + 1, format, args);
            memcpy(buffer, full, count);
            free(full);
        }
    }
    return (size_t)needed == count ? needed : -1;
}
static inline int _snprintf(char *buffer, size_t count, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    const int result = _vsnprintf(buffer, count, format, args);
    va_end(args);
    return result;
}

// _itoa / _ui64toa: lowercase digits; only base 10 prints a sign.
static inline char *bo1_utoa(unsigned long long magnitude, int negative, char *buffer, int radix)
{
    char digits[66];
    int length = 0;
    do
    {
        const unsigned int digit = (unsigned int)(magnitude % (unsigned int)radix);
        digits[length++] = (char)(digit < 10 ? '0' + digit : 'a' + digit - 10);
        magnitude /= (unsigned int)radix;
    } while (magnitude);
    char *out = buffer;
    if (negative)
        *out++ = '-';
    while (length)
        *out++ = digits[--length];
    *out = 0;
    return buffer;
}
static inline char *_itoa(int value, char *buffer, int radix)
{
    const int negative = radix == 10 && value < 0;
    return bo1_utoa(negative ? 0u - (unsigned int)value : (unsigned int)value, negative, buffer, radix);
}
static inline char *_ui64toa(unsigned long long value, char *buffer, int radix)
{
    return bo1_utoa(value, 0, buffer, radix);
}
static inline long long _atoi64(const char *s)
{
    return strtoll(s, NULL, 10);
}
static inline unsigned long long _strtoui64(const char *s, char **end, int radix)
{
    return strtoull(s, end, radix);
}

static inline uint32_t _byteswap_ulong(uint32_t value)
{
    return __builtin_bswap32(value);
}

static inline char *_strdup(const char *s)
{
    return strdup(s);
}
static inline char *_strlwr(char *s)
{
    for (char *c = s; *c; ++c)
        *c = (char)tolower((unsigned char)*c);
    return s;
}
// A macro rather than a second function: newlib (Switch) declares its own strlwr.
#define strlwr _strlwr

// The secure CRT forms. When the destination is too small MSVC calls the invalid parameter handler, which ends the
// process (the engine never installs another one); with _TRUNCATE they truncate instead.
#define _TRUNCATE ((size_t)-1)
#define STRUNCATE 80

static inline int bo1_vformat_size(const char *format, va_list args)
{
    va_list copy;
    va_copy(copy, args);
    const int needed = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    return needed;
}
static inline int vsprintf_s(char *buffer, size_t size, const char *format, va_list args)
{
    if (!buffer || !size || !format)
        abort();
    const int needed = bo1_vformat_size(format, args);
    if (needed < 0)
    {
        buffer[0] = 0;
        return -1;
    }
    if ((size_t)needed >= size)
        abort();
    return vsnprintf(buffer, size, format, args);
}
static inline int sprintf_s(char *buffer, size_t size, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    const int result = vsprintf_s(buffer, size, format, args);
    va_end(args);
    return result;
}
static inline int _vsnprintf_s(char *buffer, size_t size, size_t count, const char *format, va_list args)
{
    if (!buffer || !size || !format)
        abort();
    const int needed = bo1_vformat_size(format, args);
    if (needed < 0)
    {
        buffer[0] = 0;
        return -1;
    }
    const size_t limit = count < size ? count : size - 1;
    if ((size_t)needed <= limit)
        return vsnprintf(buffer, size, format, args);
    if (count != _TRUNCATE && count >= size)
        abort();
    vsnprintf(buffer, limit + 1, format, args);
    return -1;
}
static inline int _snprintf_s(char *buffer, size_t size, size_t count, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    const int result = _vsnprintf_s(buffer, size, count, format, args);
    va_end(args);
    return result;
}
static inline int strcpy_s(char *dest, size_t size, const char *src)
{
    if (!dest || !size || !src || strlen(src) >= size)
        abort();
    memcpy(dest, src, strlen(src) + 1);
    return 0;
}
static inline int strncpy_s(char *dest, size_t size, const char *src, size_t count)
{
    if (!dest || !size || (!src && count))
        abort();
    size_t length = 0;
    while (length < count && src[length])
        ++length;
    if (length < size)
    {
        memcpy(dest, src, length);
        dest[length] = 0;
        return 0;
    }
    if (count != _TRUNCATE)
        abort();
    memcpy(dest, src, size - 1);
    dest[size - 1] = 0;
    return STRUNCATE;
}
static inline int fopen_s(FILE **file, const char *name, const char *mode)
{
    if (!file || !name || !mode)
        abort();
    *file = fopen(name, mode);
    return *file ? 0 : errno;
}
static inline char *strtok_s(char *str, const char *delimiters, char **context)
{
    if (!delimiters || !context || (!str && !*context))
        abort();
    return strtok_r(str, delimiters, context);
}
// MSVC's time conversions refuse times before 1970 and after year 3000: localtime_s fails with EINVAL and every
// field -1, _localtime64 / _gmtime64 / _ctime64 return NULL.
#define BO1_TIME64_MAX 32535215999LL
static inline int localtime_s(struct tm *out, const time_t *t)
{
    if (!out || !t)
        abort();
    if (*t < 0 || *t > BO1_TIME64_MAX || !localtime_r(t, out))
    {
        out->tm_sec = out->tm_min = out->tm_hour = out->tm_mday = out->tm_mon = out->tm_year = -1;
        out->tm_wday = out->tm_yday = out->tm_isdst = -1;
        return EINVAL;
    }
    return 0;
}

#if defined(__cplusplus)
// The C++ overloads MSVC adds for array destinations.
template <size_t N> static inline int sprintf_s(char (&buffer)[N], const char *format, ...)
{
    va_list args;
    va_start(args, format);
    const int result = vsprintf_s(buffer, N, format, args);
    va_end(args);
    return result;
}
template <size_t N> static inline int _snprintf_s(char (&buffer)[N], size_t count, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    const int result = _vsnprintf_s(buffer, N, count, format, args);
    va_end(args);
    return result;
}
template <size_t N> static inline int strcpy_s(char (&dest)[N], const char *src)
{
    return strcpy_s(dest, N, src);
}
template <size_t N> static inline int strncpy_s(char (&dest)[N], const char *src, size_t count)
{
    return strncpy_s(dest, N, src, count);
}

extern "C" int _putenv(const char *envstring);
// fopen as MSVC's behaves: it accepts '\' separators, which the engine puts in every OS path it builds
// (FS_ReplaceSeparators), and opens in text mode (CR LF read as LF, LF written as CR LF) unless the mode says 'b'.
// Engine code opening such a path calls this instead on posix; the platform layer defines it (win32_files.cpp).
extern "C" FILE *bo1_fopen(const char *path, const char *mode);
// The MSVC CRT's error reporting switches. This C library always reports to stderr and never shows a dialog, which is
// what the engine selects with them, so they only keep and return the setting (msvc_crt.cpp).
#define _OUT_TO_DEFAULT 0
#define _OUT_TO_STDERR 1
#define _OUT_TO_MSGBOX 2
#define _REPORT_ERRMODE 3
#define _WRITE_ABORT_MSG 0x1
#define _CALL_REPORTFAULT 0x2
extern "C" int _set_error_mode(int mode);
extern "C" unsigned int _set_abort_behavior(unsigned int flags, unsigned int mask);
// MSVC's wide formatting, whose %s and %c are wide (msvc_crt.cpp).
extern "C" int _vsnwprintf_s(wchar_t *buffer, size_t sizeOfBuffer, size_t count, const wchar_t *format, va_list args);
extern "C" int _snwprintf_s(wchar_t *buffer, size_t sizeOfBuffer, size_t count, const wchar_t *format, ...);
// It takes a buffer size after each %s, %c and %[ destination, which vsscanf cannot be given.
extern "C" int sscanf_s(const char *buffer, const char *format, ...);
#endif

static inline void *_aligned_malloc(size_t size, size_t alignment)
{
    void *p = NULL;
    return posix_memalign(&p, alignment < sizeof(void *) ? sizeof(void *) : alignment, size) ? NULL : p;
}
static inline void _aligned_free(void *p)
{
    free(p);
}
// A new block, as MSVC does when the size changes; on failure the old block is left as it was.
static inline void *_aligned_realloc(void *p, size_t size, size_t alignment)
{
    if (!p)
        return _aligned_malloc(size, alignment);
    if (!size)
    {
        _aligned_free(p);
        return NULL;
    }
    void *grown = _aligned_malloc(size, alignment);
    if (!grown)
        return NULL;
    const size_t old = malloc_usable_size(p);
    memcpy(grown, p, old < size ? old : size);
    _aligned_free(p);
    return grown;
}

typedef long long __time64_t;
static inline __time64_t _time64(__time64_t *out)
{
    const __time64_t now = (__time64_t)time(NULL);
    if (out)
        *out = now;
    return now;
}
static inline struct tm *_gmtime64(const __time64_t *t)
{
    const time_t value = (time_t)*t;
    return *t < 0 || *t > BO1_TIME64_MAX ? NULL : gmtime(&value);
}
static inline struct tm *_localtime64(const __time64_t *t)
{
    const time_t value = (time_t)*t;
    return *t < 0 || *t > BO1_TIME64_MAX ? NULL : localtime(&value);
}
static inline char *_ctime64(const __time64_t *t)
{
    const time_t value = (time_t)*t;
    return *t < 0 || *t > BO1_TIME64_MAX ? NULL : ctime(&value);
}

