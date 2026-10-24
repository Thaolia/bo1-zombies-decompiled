// The MSVC CRT functions the C library lacks that take more than a few lines, with MSVC's behaviour. msvc_intrinsics.h
// declares them (and defines the short ones inline).

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <atomic>
#include <string>

namespace
{
std::atomic<int> s_errorMode{_OUT_TO_DEFAULT};
std::atomic<unsigned int> s_abortBehavior{_WRITE_ABORT_MSG | _CALL_REPORTFAULT};
thread_local unsigned int t_randState = 1;

// MSVC's wide printf functions read %s and %c as wide and %S and %C as narrow (an h prefix makes them narrow, l or w
// wide); C's read %s and %c as narrow, %ls and %lc as wide. MSVC's I64 length is C's ll.
std::wstring WideFormatFromMsvc(const wchar_t *format)
{
    std::wstring out;
    for (const wchar_t *p = format; *p; ++p)
    {
        out += *p;
        if (*p != L'%')
            continue;
        ++p;
        while (*p && wcschr(L"-+ #0", *p))
            out += *p++;
        while (*p && wcschr(L"0123456789.*", *p))
            out += *p++;
        enum
        {
            DEFAULT,
            NARROW,
            WIDE
        } width = DEFAULT;
        std::wstring length;
        if (p[0] == L'I' && p[1] == L'6' && p[2] == L'4')
        {
            length = L"ll";
            p += 3;
        }
        else if (p[0] == L'I' && p[1] == L'3' && p[2] == L'2')
            p += 3;
        else if (*p == L'h' && (p[1] == L's' || p[1] == L'S' || p[1] == L'c' || p[1] == L'C'))
        {
            width = NARROW;
            ++p;
        }
        else if ((*p == L'l' || *p == L'w') && (p[1] == L's' || p[1] == L'S' || p[1] == L'c' || p[1] == L'C'))
        {
            width = WIDE;
            ++p;
        }
        else
        {
            while (*p && wcschr(L"hlLjzt", *p))
                length += *p++;
        }
        if (!*p)
            break;
        const wchar_t conversion = *p;
        if (conversion == L's' || conversion == L'S' || conversion == L'c' || conversion == L'C')
        {
            const bool narrow = width == NARROW || (width == DEFAULT && (conversion == L'S' || conversion == L'C'));
            if (!narrow)
                out += L'l';
            out += conversion == L'S' || conversion == L's' ? L's' : L'c';
        }
        else
        {
            out += length;
            out += conversion;
        }
    }
    return out;
}
} // namespace

extern "C" {

// The UCRT's linear congruential generator (compat.h).
int bo1_rand(void)
{
    t_randState = t_randState * 214013u + 2531011u;
    return int((t_randState >> 16) & 0x7FFF);
}

void bo1_srand(unsigned int seed)
{
    t_randState = seed;
}

int _set_error_mode(int mode)
{
    if (mode == _REPORT_ERRMODE)
        return s_errorMode.load();
    if (mode < _OUT_TO_DEFAULT || mode > _OUT_TO_MSGBOX)
    {
        errno = EINVAL;
        return -1;
    }
    return s_errorMode.exchange(mode);
}

unsigned int _set_abort_behavior(unsigned int flags, unsigned int mask)
{
    unsigned int previous = s_abortBehavior.load();
    while (!s_abortBehavior.compare_exchange_weak(previous, (previous & ~mask) | (flags & mask)))
        ;
    return previous;
}

// "NAME=value" sets the variable, "NAME=" removes it.
int _putenv(const char *envstring)
{
    const char *equals = envstring ? strchr(envstring, '=') : nullptr;
    if (!equals || equals == envstring)
    {
        errno = EINVAL;
        return -1;
    }
    const std::string name(envstring, equals);
    return (equals[1] ? setenv(name.c_str(), equals + 1, 1) : unsetenv(name.c_str())) ? -1 : 0;
}

// The arguments after each %s, %c and %[ destination carry its buffer size: they are taken out, and the size becomes
// the conversion's field width when it has none (so the C library stops where MSVC's would).
int sscanf_s(const char *buffer, const char *format, ...)
{
    const int MAX_ARGUMENTS = 32;
    void *arguments[MAX_ARGUMENTS] = {};
    int count = 0;
    std::string converted;
    va_list args;
    va_start(args, format);
    for (const char *p = format; *p; ++p)
    {
        converted += *p;
        if (*p != '%')
            continue;
        ++p;
        if (*p == '%')
        {
            converted += *p;
            continue;
        }
        const bool suppressed = *p == '*';
        if (suppressed)
            converted += *p++;
        std::string width;
        while (*p >= '0' && *p <= '9')
            width += *p++;
        std::string length;
        while (*p && strchr("hlLjzt", *p))
            length += *p++;
        if (!*p)
            break;
        const char conversion = *p;
        std::string set;
        if (conversion == '[')
        {
            set += *p++;
            if (*p == '^')
                set += *p++;
            if (*p == ']')
                set += *p++;
            while (*p && *p != ']')
                set += *p++;
            if (!*p)
                break;
            set += *p;
        }
        if (suppressed)
        {
            converted += width + length + (conversion == '[' ? set : std::string(1, conversion));
            continue;
        }
        if (count == MAX_ARGUMENTS)
        {
            va_end(args);
            errno = EINVAL;
            return -1;
        }
        arguments[count++] = va_arg(args, void *);
        if (conversion == 's' || conversion == 'c' || conversion == '[')
        {
            const unsigned int size = va_arg(args, unsigned int);
            if (!size)
            {
                va_end(args);
                errno = EINVAL;
                return -1;
            }
            if (width.empty())
                width = std::to_string(conversion == 'c' ? 1u : size - 1);
        }
        converted += width + length + (conversion == '[' ? set : std::string(1, conversion));
    }
    va_end(args);
    return sscanf(buffer, converted.c_str(), arguments[0], arguments[1], arguments[2], arguments[3], arguments[4],
                  arguments[5], arguments[6], arguments[7], arguments[8], arguments[9], arguments[10], arguments[11],
                  arguments[12], arguments[13], arguments[14], arguments[15], arguments[16], arguments[17],
                  arguments[18], arguments[19], arguments[20], arguments[21], arguments[22], arguments[23],
                  arguments[24], arguments[25], arguments[26], arguments[27], arguments[28], arguments[29],
                  arguments[30], arguments[31]);
}

int _vsnwprintf_s(wchar_t *buffer, size_t sizeOfBuffer, size_t count, const wchar_t *format, va_list args)
{
    if (!buffer || !sizeOfBuffer || !format)
        abort();
    const std::wstring cFormat = WideFormatFromMsvc(format);
    // vswprintf only says that the text did not fit, never how long it is: format into a growing buffer.
    std::wstring text(256, L'\0');
    int needed;
    while (true)
    {
        va_list copy;
        va_copy(copy, args);
        needed = vswprintf(&text[0], text.size(), cFormat.c_str(), copy);
        va_end(copy);
        if (needed >= 0 && (size_t)needed < text.size())
            break;
        if (text.size() >= (1u << 24))
        {
            buffer[0] = 0;
            return -1;
        }
        text.resize(text.size() * 2);
    }
    const size_t limit = count < sizeOfBuffer ? count : sizeOfBuffer - 1;
    if ((size_t)needed <= limit)
    {
        wmemcpy(buffer, text.c_str(), (size_t)needed + 1);
        return needed;
    }
    if (count != _TRUNCATE && count >= sizeOfBuffer)
        abort();
    wmemcpy(buffer, text.c_str(), limit);
    buffer[limit] = 0;
    return -1;
}

int _snwprintf_s(wchar_t *buffer, size_t sizeOfBuffer, size_t count, const wchar_t *format, ...)
{
    va_list args;
    va_start(args, format);
    const int result = _vsnwprintf_s(buffer, sizeOfBuffer, count, format, args);
    va_end(args);
    return result;
}

} // extern "C"
