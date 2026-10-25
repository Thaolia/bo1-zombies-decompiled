// Win32 files and directories, and the MSVC CRT's directory search, on POSIX (Linux). Paths arrive in Windows form
// (the engine builds them with '\', FS_ReplaceSeparators) and are converted at this boundary. Share modes are not
// enforced: no other process opens the engine's files.

#if defined(__SWITCH__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE // newlib hides fopencookie behind __GNU_VISIBLE; the custom text stream needs it
#endif

#include "win32_handles.h"

#include <direct.h>
#include <io.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <vector>

// fopencookie's seek callback takes off_t* on newlib (Switch, without __LARGE64_FILES) but off64_t* on glibc; the
// custom text stream's seek matches whichever the platform's cookie_io_functions_t expects.
#if defined(__SWITCH__)
using Bo1CookieOff = off_t;
#else
using Bo1CookieOff = off64_t;
#endif

namespace
{
// Windows rules for a search pattern: '*' and '?' wildcards, case-insensitive, and "*.*" matches every name.
bool WildcardMatch(const char *pattern, const char *name)
{
    const char *star = nullptr;
    const char *resume = nullptr;
    while (*name)
    {
        if (*pattern == '*')
        {
            star = pattern++;
            resume = name;
        }
        else if (*pattern == '?' || tolower((unsigned char)*pattern) == tolower((unsigned char)*name))
        {
            ++pattern;
            ++name;
        }
        else if (star)
        {
            pattern = star + 1;
            name = ++resume;
        }
        else
            return false;
    }
    while (*pattern == '*')
        ++pattern;
    return !*pattern;
}

DWORD AttributesFromStat(const struct stat &st)
{
    DWORD attributes = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE;
    if (!(st.st_mode & S_IWUSR))
        attributes |= FILE_ATTRIBUTE_READONLY;
    return attributes;
}

off_t OverlappedOffset(const OVERLAPPED *overlapped)
{
    return (off_t)((uint64_t)overlapped->OffsetHigh << 32 | overlapped->Offset);
}

// Windows reads a disk file to the end of the request, stopping early only at end of file. offset < 0 reads at the
// file position. Returns the bytes read, or -1 with errno set.
ssize_t ReadAll(int fd, void *buffer, size_t size, off_t offset)
{
    size_t done = 0;
    while (done < size)
    {
        const ssize_t n = offset < 0 ? read(fd, (char *)buffer + done, size - done)
                                     : pread(fd, (char *)buffer + done, size - done, offset + (off_t)done);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            break;
        done += (size_t)n;
    }
    return (ssize_t)done;
}

ssize_t WriteAll(int fd, const void *buffer, size_t size, off_t offset)
{
    size_t done = 0;
    while (done < size)
    {
        const ssize_t n = offset < 0 ? write(fd, (const char *)buffer + done, size - done)
                                     : pwrite(fd, (const char *)buffer + done, size - done, offset + (off_t)done);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        done += (size_t)n;
    }
    return (ssize_t)done;
}

// Advances a search to its next entry matching the pattern. False at the end of the directory.
bool NextMatch(Bo1Find *find, std::string *name, struct stat *st)
{
    while (dirent *entry = readdir(find->dir))
    {
        if (!WildcardMatch(find->pattern.c_str(), entry->d_name))
            continue;
        const std::string path = find->directory + "/" + entry->d_name;
        if (stat(path.c_str(), st))
            continue; // a dangling link: Windows lists no such entry
        *name = entry->d_name;
        return true;
    }
    return false;
}

// Starts a search on "directory\pattern" and returns it positioned on its first match, or null with the Win32 error
// set (ERROR_PATH_NOT_FOUND without the directory, ERROR_FILE_NOT_FOUND without a match).
Bo1Find *StartFind(const char *spec, std::string *name, struct stat *st)
{
    const std::string path = Bo1_PosixPath(spec);
    const size_t slash = path.rfind('/');
    Bo1Find *find = new Bo1Find();
    find->directory = slash == std::string::npos ? "." : slash == 0 ? "/" : path.substr(0, slash);
    find->pattern = slash == std::string::npos ? path : path.substr(slash + 1);
    if (find->pattern == "*.*")
        find->pattern = "*";
    find->dir = opendir(find->directory.c_str());
    if (!find->dir)
    {
        Bo1_SetLastError(errno == ENOENT ? ERROR_PATH_NOT_FOUND : Bo1_ErrorFromErrno(errno));
        Bo1_Release(find);
        return nullptr;
    }
    if (!NextMatch(find, name, st))
    {
        Bo1_SetLastError(ERROR_FILE_NOT_FOUND);
        Bo1_Release(find);
        return nullptr;
    }
    return find;
}

void FillFindData(const std::string &name, const struct stat &st, LPWIN32_FIND_DATAA data)
{
    memset(data, 0, sizeof(*data));
    data->dwFileAttributes = AttributesFromStat(st);
    data->ftCreationTime = Bo1_FileTimeFromUnix(st.st_ctim.tv_sec, st.st_ctim.tv_nsec);
    data->ftLastAccessTime = Bo1_FileTimeFromUnix(st.st_atim.tv_sec, st.st_atim.tv_nsec);
    data->ftLastWriteTime = Bo1_FileTimeFromUnix(st.st_mtim.tv_sec, st.st_mtim.tv_nsec);
    if (!S_ISDIR(st.st_mode))
    {
        data->nFileSizeHigh = (DWORD)((uint64_t)st.st_size >> 32);
        data->nFileSizeLow = (DWORD)st.st_size;
    }
    strncpy(data->cFileName, name.c_str(), sizeof(data->cFileName) - 1);
}

// MSVC's text mode, which fopen picks unless the mode says 'b': reads turn CR LF into LF and end at Ctrl-Z, writes turn
// LF into CR LF. The stream sits on a binary stream of the file; seeks go to its byte offsets, as MSVC's do.
struct TextStream
{
    FILE *raw;
    int pending = EOF; // the byte read after a CR that turned out not to start CR LF
    bool atCtrlZ = false;
};

ssize_t TextRead(void *cookie, char *buffer, size_t size)
{
    TextStream *stream = static_cast<TextStream *>(cookie);
    size_t done = 0;
    while (done < size && !stream->atCtrlZ)
    {
        int c = stream->pending != EOF ? stream->pending : fgetc(stream->raw);
        stream->pending = EOF;
        if (c == EOF)
            break;
        if (c == 0x1A)
        {
            stream->atCtrlZ = true;
            break;
        }
        if (c == '\r')
        {
            const int next = fgetc(stream->raw);
            if (next == '\n')
                c = '\n';
            else
                stream->pending = next;
        }
        buffer[done++] = (char)c;
    }
    return !done && ferror(stream->raw) ? -1 : (ssize_t)done;
}

ssize_t TextWrite(void *cookie, const char *buffer, size_t size)
{
    TextStream *stream = static_cast<TextStream *>(cookie);
    for (size_t i = 0; i < size; ++i)
    {
        if ((buffer[i] == '\n' && fputc('\r', stream->raw) == EOF) || fputc((unsigned char)buffer[i], stream->raw) == EOF)
            return i ? (ssize_t)i : -1;
    }
    return (ssize_t)size;
}

int TextSeek(void *cookie, Bo1CookieOff *offset, int whence)
{
    TextStream *stream = static_cast<TextStream *>(cookie);
    if (stream->pending != EOF)
    {
        // That byte was read from the file but not handed out yet.
        fseeko(stream->raw, -1, SEEK_CUR);
        stream->pending = EOF;
    }
    stream->atCtrlZ = false;
    if (fseeko(stream->raw, (off_t)*offset, whence))
        return -1;
    *offset = (Bo1CookieOff)ftello(stream->raw);
    return 0;
}

int TextClose(void *cookie)
{
    TextStream *stream = static_cast<TextStream *>(cookie);
    const int result = fclose(stream->raw);
    delete stream;
    return result;
}

void FillFindInfo(const std::string &name, const struct stat &st, struct _finddata64i32_t *info)
{
    memset(info, 0, sizeof(*info));
    info->attrib = AttributesFromStat(st) & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_ARCHIVE);
    info->time_create = st.st_ctim.tv_sec;
    info->time_access = st.st_atim.tv_sec;
    info->time_write = st.st_mtim.tv_sec;
    info->size = S_ISDIR(st.st_mode) ? 0 : (_fsize_t)st.st_size;
    strncpy(info->name, name.c_str(), sizeof(info->name) - 1);
}
} // namespace

Bo1File::~Bo1File()
{
    close(fd);
}

Bo1Find::~Bo1Find()
{
    if (dir)
        closedir(dir);
}

// A directory entry of `dir` whose name equals `name` ignoring case, or empty when there is none.
static std::string MatchIgnoringCase(const std::string &dir, const std::string &name)
{
    DIR *d = opendir(dir.empty() ? "." : dir.c_str());
    if (!d)
        return std::string();
    std::string found;
    for (dirent *e = readdir(d); e; e = readdir(d))
    {
        if (!strcasecmp(e->d_name, name.c_str()))
        {
            found = e->d_name;
            break;
        }
    }
    closedir(d);
    return found;
}

// The engine spells paths as Windows' case-insensitive file system accepts them; a case-sensitive one (ext4, the
// Switch SD) does not. Convert '\\' to '/', then resolve each existing component to its real case as Windows would
// (e.g. the engine's `zone/Common` to the `zone/common` on disk). A component that does not exist is kept as given,
// so a new file or directory keeps its intended case. The exact path is tried first, so a matching case costs one
// stat and nothing lists a directory.
std::string Bo1_PosixPath(const char *path)
{
    std::string converted = path;
    for (char &c : converted)
    {
        if (c == '\\')
            c = '/';
    }
    struct stat st;
    if (converted.empty() || lstat(converted.c_str(), &st) == 0)
        return converted;
    // The root to resolve from: a POSIX-absolute path starts with '/', and a devoptab device ("sdmc:/...",
    // "romfs:/...", libnx/newlib) with its "name:" prefix, which never starts with '/'. Everything else is relative
    // to the working directory. Without the device case a prefixed path resolved from "." (e.g. the wildcard
    // Sys_ListFiles builds for the iwd scan), mangling "sdmc:" and leaving SD directory listings empty.
    std::string result;
    size_t i = 0;
    const size_t colon = converted.find(':');
    if (colon != std::string::npos && colon < converted.find('/'))
    {
        result = converted.substr(0, colon + 1);
        i = colon + 1;
    }
    else
    {
        result = converted[0] == '/' ? "/" : ".";
    }
    bool stopped = false;
    for (; i < converted.size();)
    {
        while (i < converted.size() && converted[i] == '/')
            ++i;
        if (i >= converted.size())
            break;
        const size_t slash = converted.find('/', i);
        const std::string comp = converted.substr(i, slash == std::string::npos ? std::string::npos : slash - i);
        i = slash == std::string::npos ? converted.size() : slash;
        const std::string sep = result.back() == '/' ? "" : "/";
        if (!stopped)
        {
            const std::string candidate = result + sep + comp;
            if (lstat(candidate.c_str(), &st) == 0)
            {
                result = candidate;
                continue;
            }
            const std::string match = MatchIgnoringCase(result, comp);
            if (!match.empty())
            {
                result = result + sep + match;
                continue;
            }
            stopped = true;
        }
        result = result + sep + comp;
    }
    return result;
}

extern "C" {

FILE *bo1_fopen(const char *path, const char *mode)
{
    const std::string posixPath = Bo1_PosixPath(path);
    if (strchr(mode, 'b'))
        return fopen(posixPath.c_str(), mode);
    std::string binaryMode;
    for (const char *c = mode; *c; ++c)
    {
        if (*c != 't')
            binaryMode += *c;
    }
    binaryMode += 'b';
    FILE *raw = fopen(posixPath.c_str(), binaryMode.c_str());
    if (!raw)
        return nullptr;
    TextStream *stream = new TextStream{raw};
    FILE *text = fopencookie(stream, binaryMode.c_str(), {TextRead, TextWrite, TextSeek, TextClose});
    if (!text)
    {
        fclose(raw);
        delete stream;
    }
    return text;
}

HANDLE CreateFileA(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD, LPSECURITY_ATTRIBUTES, DWORD dwCreationDisposition,
                   DWORD, HANDLE)
{
    // FILE_APPEND_DATA without FILE_WRITE_DATA writes only at the end of the file (win_main.cpp's emergency log).
    const bool read = (dwDesiredAccess & (GENERIC_READ | GENERIC_ALL | FILE_READ_DATA)) != 0;
    const bool write = (dwDesiredAccess & (GENERIC_WRITE | GENERIC_ALL | FILE_WRITE_DATA | FILE_APPEND_DATA)) != 0;
    const bool appendOnly = (dwDesiredAccess & FILE_APPEND_DATA) &&
                            !(dwDesiredAccess & (GENERIC_WRITE | GENERIC_ALL | FILE_WRITE_DATA));
    int flags = O_CLOEXEC | (appendOnly ? O_APPEND : 0);
    if (read && write)
        flags |= O_RDWR;
    else if (write)
        flags |= O_WRONLY;
    else
        flags |= O_RDONLY;
    switch (dwCreationDisposition)
    {
    case CREATE_NEW:
        flags |= O_CREAT | O_EXCL;
        break;
    case CREATE_ALWAYS:
        flags |= O_CREAT | O_TRUNC;
        break;
    case OPEN_EXISTING:
        break;
    case OPEN_ALWAYS:
        flags |= O_CREAT;
        break;
    case TRUNCATE_EXISTING:
        flags |= O_TRUNC;
        break;
    default:
        Bo1_SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    const std::string path = Bo1_PosixPath(lpFileName);
    const bool existed = access(path.c_str(), F_OK) == 0;
    const int fd = open(path.c_str(), flags, 0666);
    if (fd < 0)
    {
        Bo1_SetLastError(Bo1_ErrorFromErrno(errno));
        return INVALID_HANDLE_VALUE;
    }
    struct stat st;
    if (fstat(fd, &st) || S_ISDIR(st.st_mode))
    {
        // CreateFile opens a directory only with FILE_FLAG_BACKUP_SEMANTICS, which the engine never passes.
        close(fd);
        Bo1_SetLastError(ERROR_ACCESS_DENIED);
        return INVALID_HANDLE_VALUE;
    }
    const bool reportsExisting = dwCreationDisposition == CREATE_ALWAYS || dwCreationDisposition == OPEN_ALWAYS;
    Bo1_SetLastError(reportsExisting && existed ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS);
    return Bo1_ToHandle(new Bo1File(fd));
}

// With an OVERLAPPED the read is positional and completes before returning; reading at or past the end of the file then
// fails with ERROR_HANDLE_EOF, as an overlapped read does on Windows.
BOOL ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead,
              LPOVERLAPPED lpOverlapped)
{
    if (lpNumberOfBytesRead)
        *lpNumberOfBytesRead = 0;
    Bo1File *file = Bo1_HandleAs<Bo1File>(hFile, BO1_OBJ_FILE);
    if (!file)
    {
        Bo1_SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    const ssize_t done = ReadAll(file->fd, lpBuffer, nNumberOfBytesToRead, lpOverlapped ? OverlappedOffset(lpOverlapped) : -1);
    if (done < 0)
    {
        Bo1_SetLastError(Bo1_ErrorFromErrno(errno));
        return FALSE;
    }
    if (lpOverlapped)
    {
        if (!done && nNumberOfBytesToRead)
        {
            Bo1_SetLastError(ERROR_HANDLE_EOF);
            return FALSE;
        }
        lpOverlapped->Internal = ERROR_SUCCESS;
        lpOverlapped->InternalHigh = (ULONG_PTR)done;
    }
    if (lpNumberOfBytesRead)
        *lpNumberOfBytesRead = (DWORD)done;
    return TRUE;
}

// The read happens now; its completion routine runs at the calling thread's next alertable wait (SleepEx), as an APC
// does on Windows. A read starting at or past the end of the file fails at once with ERROR_HANDLE_EOF.
BOOL ReadFileEx(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPOVERLAPPED lpOverlapped,
                LPOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine)
{
    Bo1File *file = Bo1_HandleAs<Bo1File>(hFile, BO1_OBJ_FILE);
    if (!file || !lpOverlapped || !lpCompletionRoutine)
    {
        Bo1_SetLastError(file ? ERROR_INVALID_PARAMETER : ERROR_INVALID_HANDLE);
        return FALSE;
    }
    const ssize_t done = ReadAll(file->fd, lpBuffer, nNumberOfBytesToRead, OverlappedOffset(lpOverlapped));
    if (done < 0)
    {
        Bo1_SetLastError(Bo1_ErrorFromErrno(errno));
        return FALSE;
    }
    if (!done && nNumberOfBytesToRead)
    {
        Bo1_SetLastError(ERROR_HANDLE_EOF);
        return FALSE;
    }
    lpOverlapped->Internal = ERROR_SUCCESS;
    lpOverlapped->InternalHigh = (ULONG_PTR)done;
    Bo1_QueueCompletion(lpCompletionRoutine, ERROR_SUCCESS, (DWORD)done, lpOverlapped);
    Bo1_SetLastError(ERROR_SUCCESS);
    return TRUE;
}

BOOL WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten,
               LPOVERLAPPED lpOverlapped)
{
    if (lpNumberOfBytesWritten)
        *lpNumberOfBytesWritten = 0;
    Bo1File *file = Bo1_HandleAs<Bo1File>(hFile, BO1_OBJ_FILE);
    if (!file)
    {
        Bo1_SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    const ssize_t done =
        WriteAll(file->fd, lpBuffer, nNumberOfBytesToWrite, lpOverlapped ? OverlappedOffset(lpOverlapped) : -1);
    if (done < 0)
    {
        Bo1_SetLastError(Bo1_ErrorFromErrno(errno));
        return FALSE;
    }
    if (lpOverlapped)
    {
        lpOverlapped->Internal = ERROR_SUCCESS;
        lpOverlapped->InternalHigh = (ULONG_PTR)done;
    }
    if (lpNumberOfBytesWritten)
        *lpNumberOfBytesWritten = (DWORD)done;
    return TRUE;
}

DWORD GetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh)
{
    Bo1File *file = Bo1_HandleAs<Bo1File>(hFile, BO1_OBJ_FILE);
    struct stat st;
    if (!file || fstat(file->fd, &st))
    {
        Bo1_SetLastError(file ? Bo1_ErrorFromErrno(errno) : ERROR_INVALID_HANDLE);
        return INVALID_FILE_SIZE;
    }
    if (lpFileSizeHigh)
        *lpFileSizeHigh = (DWORD)((uint64_t)st.st_size >> 32);
    // INVALID_FILE_SIZE is also a valid low part: callers that care tell them apart with GetLastError.
    Bo1_SetLastError(ERROR_SUCCESS);
    return (DWORD)st.st_size;
}

BOOL DeleteFileA(LPCSTR lpFileName)
{
    if (unlink(Bo1_PosixPath(lpFileName).c_str()))
    {
        Bo1_SetLastError(Bo1_ErrorFromErrno(errno));
        return FALSE;
    }
    return TRUE;
}

BOOL CreateDirectoryA(LPCSTR lpPathName, LPSECURITY_ATTRIBUTES)
{
    if (mkdir(Bo1_PosixPath(lpPathName).c_str(), 0777))
    {
        Bo1_SetLastError(errno == ENOENT ? ERROR_PATH_NOT_FOUND : Bo1_ErrorFromErrno(errno));
        return FALSE;
    }
    return TRUE;
}

BOOL RemoveDirectoryA(LPCSTR lpPathName)
{
    if (rmdir(Bo1_PosixPath(lpPathName).c_str()))
    {
        Bo1_SetLastError(Bo1_ErrorFromErrno(errno));
        return FALSE;
    }
    return TRUE;
}

DWORD GetFileAttributesA(LPCSTR lpFileName)
{
    struct stat st;
    if (stat(Bo1_PosixPath(lpFileName).c_str(), &st))
    {
        Bo1_SetLastError(Bo1_ErrorFromErrno(errno));
        return INVALID_FILE_ATTRIBUTES;
    }
    return AttributesFromStat(st);
}

// Only the read-only attribute has a POSIX counterpart: the write permission bits.
BOOL SetFileAttributesA(LPCSTR lpFileName, DWORD dwFileAttributes)
{
    const std::string path = Bo1_PosixPath(lpFileName);
    struct stat st;
    if (stat(path.c_str(), &st))
    {
        Bo1_SetLastError(Bo1_ErrorFromErrno(errno));
        return FALSE;
    }
    const mode_t mode = (dwFileAttributes & FILE_ATTRIBUTE_READONLY) ? st.st_mode & ~(mode_t)(S_IWUSR | S_IWGRP | S_IWOTH)
                                                                     : st.st_mode | S_IWUSR;
    if (chmod(path.c_str(), mode & 07777))
    {
        Bo1_SetLastError(Bo1_ErrorFromErrno(errno));
        return FALSE;
    }
    return TRUE;
}

// Like Windows, resolves "." and ".." by name, without touching the filesystem or following links.
DWORD GetFullPathNameA(LPCSTR lpFileName, DWORD nBufferLength, LPSTR lpBuffer, LPSTR *lpFilePart)
{
    std::string path = Bo1_PosixPath(lpFileName);
    if (path.empty() || path[0] != '/')
    {
        char cwd[PATH_MAX];
        if (!getcwd(cwd, sizeof(cwd)))
        {
            Bo1_SetLastError(Bo1_ErrorFromErrno(errno));
            return 0;
        }
        path = std::string(cwd) + "/" + path;
    }
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= path.size())
    {
        size_t end = path.find('/', start);
        if (end == std::string::npos)
            end = path.size();
        const std::string part = path.substr(start, end - start);
        if (part == "..")
        {
            if (!parts.empty())
                parts.pop_back();
        }
        else if (!part.empty() && part != ".")
            parts.push_back(part);
        start = end + 1;
    }
    std::string full;
    for (const std::string &part : parts)
        full += "/" + part;
    if (full.empty())
        full = "/";
    if (full.size() + 1 > nBufferLength)
        return (DWORD)full.size() + 1;
    memcpy(lpBuffer, full.c_str(), full.size() + 1);
    if (lpFilePart)
        *lpFilePart = parts.empty() ? nullptr : lpBuffer + full.size() - parts.back().size();
    return (DWORD)full.size();
}

DWORD GetCurrentDirectoryA(DWORD nBufferLength, LPSTR lpBuffer)
{
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd)))
    {
        Bo1_SetLastError(Bo1_ErrorFromErrno(errno));
        return 0;
    }
    const size_t length = strlen(cwd);
    if (length + 1 > nBufferLength)
        return (DWORD)length + 1;
    memcpy(lpBuffer, cwd, length + 1);
    return (DWORD)length;
}

HANDLE FindFirstFileA(LPCSTR lpFileName, LPWIN32_FIND_DATAA lpFindFileData)
{
    std::string name;
    struct stat st;
    Bo1Find *find = StartFind(lpFileName, &name, &st);
    if (!find)
        return INVALID_HANDLE_VALUE;
    FillFindData(name, st, lpFindFileData);
    return Bo1_ToHandle(find);
}

BOOL FindNextFileA(HANDLE hFindFile, LPWIN32_FIND_DATAA lpFindFileData)
{
    Bo1Find *find = Bo1_HandleAs<Bo1Find>(hFindFile, BO1_OBJ_FIND);
    if (!find)
    {
        Bo1_SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    std::string name;
    struct stat st;
    if (!NextMatch(find, &name, &st))
    {
        Bo1_SetLastError(ERROR_NO_MORE_FILES);
        return FALSE;
    }
    FillFindData(name, st, lpFindFileData);
    return TRUE;
}

BOOL FindClose(HANDLE hFindFile)
{
    Bo1Find *find = Bo1_HandleAs<Bo1Find>(hFindFile, BO1_OBJ_FIND);
    if (!find)
    {
        Bo1_SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    Bo1_Release(find);
    return TRUE;
}

// ---- MSVC CRT (io.h, direct.h): the same search, with errno instead of the Win32 error.

intptr_t _findfirst64i32(const char *filespec, struct _finddata64i32_t *fileinfo)
{
    std::string name;
    struct stat st;
    Bo1Find *find = StartFind(filespec, &name, &st);
    if (!find)
    {
        errno = ENOENT;
        return -1;
    }
    FillFindInfo(name, st, fileinfo);
    return (intptr_t)Bo1_ToHandle(find);
}

int _findnext64i32(intptr_t handle, struct _finddata64i32_t *fileinfo)
{
    Bo1Find *find = Bo1_HandleAs<Bo1Find>((HANDLE)handle, BO1_OBJ_FIND);
    if (!find)
    {
        errno = EINVAL;
        return -1;
    }
    std::string name;
    struct stat st;
    if (!NextMatch(find, &name, &st))
    {
        errno = ENOENT;
        return -1;
    }
    FillFindInfo(name, st, fileinfo);
    return 0;
}

int _findclose(intptr_t handle)
{
    Bo1Find *find = Bo1_HandleAs<Bo1Find>((HANDLE)handle, BO1_OBJ_FIND);
    if (!find)
    {
        errno = EINVAL;
        return -1;
    }
    Bo1_Release(find);
    return 0;
}

int _mkdir(const char *dirname)
{
    return mkdir(Bo1_PosixPath(dirname).c_str(), 0777);
}

char *_getcwd(char *buffer, int maxlen)
{
    return getcwd(buffer, (size_t)maxlen);
}

} // extern "C"
