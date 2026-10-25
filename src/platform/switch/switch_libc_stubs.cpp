// POSIX functions the engine and the shared posix layer call that devkitA64 newlib declares but does not implement
// (so they compile, then fail to link). Small, honest implementations for the console; the headers give the exact
// prototypes so these match newlib's declarations (all extern "C").

#include <errno.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" {

// Only the keys the engine asks for (win32_system.cpp, win32_threads.cpp): page size, core count, clock ticks.
long sysconf(int name)
{
    switch (name)
    {
    case _SC_PAGESIZE:
        return 0x1000; // 4 KiB
    case _SC_NPROCESSORS_CONF:
    case _SC_NPROCESSORS_ONLN:
        return 3; // applications run on cores 0-2 (core 3 is the system's)
    case _SC_CLK_TCK:
        return 100;
    default:
        errno = EINVAL;
        return -1;
    }
}

// newlib has no pread/pwrite on this target. The engine's file I/O is not concurrent on one fd, so save-seek-restore
// is sufficient (it is not atomic with other I/O on the same descriptor, which the console never does).
// libnx's fsdev rejects an lseek(SEEK_SET) past the end of a file with EINVAL, whereas POSIX pread at or past the end
// reads nothing and returns 0 (EOF). The database loader relies on that: it issues a 0x40000 read-ahead past the end
// of a small fastfile, and treats a short/zero read as EOF. So report EOF (0) for an offset at or past the file size
// rather than letting the rejected seek surface as a read error.
ssize_t pread(int fd, void *buf, size_t count, off_t offset)
{
    struct stat st;
    if (fstat(fd, &st) == 0 && offset >= st.st_size)
        return 0;
    const off_t saved = lseek(fd, 0, SEEK_CUR);
    if (saved == (off_t)-1 || lseek(fd, offset, SEEK_SET) == (off_t)-1)
        return -1;
    const ssize_t n = read(fd, buf, count);
    lseek(fd, saved, SEEK_SET);
    return n;
}

ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset)
{
    const off_t saved = lseek(fd, 0, SEEK_CUR);
    if (saved == (off_t)-1 || lseek(fd, offset, SEEK_SET) == (off_t)-1)
        return -1;
    const ssize_t n = write(fd, buf, count);
    lseek(fd, saved, SEEK_SET);
    return n;
}

int posix_memalign(void **memptr, size_t alignment, size_t size)
{
    void *p = memalign(alignment, size);
    if (!p)
        return ENOMEM;
    *memptr = p;
    return 0;
}

// No signal-driven wake on the console; nothing the engine does on this path blocks on pause() there.
int pause(void)
{
    errno = EINTR;
    return -1;
}

int getrusage(int /*who*/, struct rusage *usage)
{
    if (usage)
        memset(usage, 0, sizeof(*usage)); // no per-process resource accounting; report zeros
    return 0;
}

} // extern "C"
