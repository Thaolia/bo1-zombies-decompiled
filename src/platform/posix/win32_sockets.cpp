// Winsock on BSD sockets (posix/include/winsock.h).

#include <winsock.h>

#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace
{
const uint32_t WINSOCK_SOL_SOCKET = 0xFFFF;
const uint32_t WINSOCK_FIONBIO = 0x8004667E;
const uint32_t WINSOCK_FIONREAD = 0x4004667F;

// Winsock's SOL_SOCKET option numbers, by their BSD counterpart.
int SocketOptionFromWinsock(int optname)
{
    switch (optname)
    {
    case 0x0004:
        return SO_REUSEADDR;
    case 0x0008:
        return SO_KEEPALIVE;
    case 0x0020:
        return SO_BROADCAST;
    case 0x1001:
        return SO_SNDBUF;
    case 0x1002:
        return SO_RCVBUF;
    case 0x1007:
        return SO_ERROR;
    case 0x1008:
        return SO_TYPE;
    default:
        return -1;
    }
}
} // namespace

extern "C" {

int WSAStartup(WORD wVersionRequested, LPWSADATA lpWSAData)
{
    memset(lpWSAData, 0, sizeof(*lpWSAData));
    lpWSAData->wVersion = wVersionRequested;
    lpWSAData->wHighVersion = 0x0202;
    strcpy(lpWSAData->szDescription, "BSD sockets");
    strcpy(lpWSAData->szSystemStatus, "Running");
    return 0;
}

int WSACleanup(void)
{
    return 0;
}

int WSAGetLastError(void)
{
    switch (errno)
    {
    case EINTR:
        return WSAEINTR;
    case EBADF:
        return WSAEBADF;
    case EACCES:
        return WSAEACCES;
    case EFAULT:
        return WSAEFAULT;
    case EINVAL:
        return WSAEINVAL;
    case EMFILE:
    case ENFILE:
        return WSAEMFILE;
    case EWOULDBLOCK:
        return WSAEWOULDBLOCK;
    case EINPROGRESS:
        return WSAEINPROGRESS;
    case EALREADY:
        return WSAEALREADY;
    case ENOTSOCK:
        return WSAENOTSOCK;
    case EDESTADDRREQ:
        return WSAEDESTADDRREQ;
    case EMSGSIZE:
        return WSAEMSGSIZE;
    case EPROTOTYPE:
        return WSAEPROTOTYPE;
    case ENOPROTOOPT:
        return WSAENOPROTOOPT;
    case EPROTONOSUPPORT:
        return WSAEPROTONOSUPPORT;
#ifdef ESOCKTNOSUPPORT // aarch64 newlib (Switch) does not define it; the error never arises there
    case ESOCKTNOSUPPORT:
        return WSAESOCKTNOSUPPORT;
#endif
    case EOPNOTSUPP:
        return WSAEOPNOTSUPP;
    case EPFNOSUPPORT:
        return WSAEPFNOSUPPORT;
    case EAFNOSUPPORT:
        return WSAEAFNOSUPPORT;
    case EADDRINUSE:
        return WSAEADDRINUSE;
    case EADDRNOTAVAIL:
        return WSAEADDRNOTAVAIL;
    case ENETDOWN:
        return WSAENETDOWN;
    case ENETUNREACH:
        return WSAENETUNREACH;
    case ENETRESET:
        return WSAENETRESET;
    case ECONNABORTED:
        return WSAECONNABORTED;
    case ECONNRESET:
        return WSAECONNRESET;
    case ENOBUFS:
    case ENOMEM:
        return WSAENOBUFS;
    case EISCONN:
        return WSAEISCONN;
    case ENOTCONN:
        return WSAENOTCONN;
#ifdef ESHUTDOWN // aarch64 newlib (Switch) does not define it; EPIPE still maps to WSAESHUTDOWN
    case ESHUTDOWN:
#endif
    case EPIPE:
        return WSAESHUTDOWN;
    case ETOOMANYREFS:
        return WSAETOOMANYREFS;
    case ETIMEDOUT:
        return WSAETIMEDOUT;
    case ECONNREFUSED:
        return WSAECONNREFUSED;
    case ELOOP:
        return WSAELOOP;
    case ENAMETOOLONG:
        return WSAENAMETOOLONG;
    case EHOSTDOWN:
        return WSAEHOSTDOWN;
    case EHOSTUNREACH:
        return WSAEHOSTUNREACH;
    case ENOTEMPTY:
        return WSAENOTEMPTY;
    default:
        return errno;
    }
}

int closesocket(SOCKET s)
{
    return close((int)s);
}

} // extern "C"

int ioctlsocket(SOCKET s, long cmd, u_long *argp)
{
    uint32_t *value = reinterpret_cast<uint32_t *>(argp);
    if ((uint32_t)cmd == WINSOCK_FIONBIO || cmd == FIONBIO)
    {
        int nonBlocking = *value != 0;
        return ioctl((int)s, FIONBIO, &nonBlocking);
    }
    if ((uint32_t)cmd == WINSOCK_FIONREAD || cmd == FIONREAD)
    {
        int available = 0;
        const int result = ioctl((int)s, FIONREAD, &available);
        if (!result)
            *value = (uint32_t)available;
        return result;
    }
    errno = EINVAL;
    return SOCKET_ERROR;
}

int setsockopt(int s, int level, int optname, const char *optval, int optlen)
{
    if ((uint32_t)level == WINSOCK_SOL_SOCKET)
    {
        level = SOL_SOCKET;
        optname = SocketOptionFromWinsock(optname);
        if (optname < 0)
        {
            errno = ENOPROTOOPT;
            return SOCKET_ERROR;
        }
    }
    return ::setsockopt(s, level, optname, (const void *)optval, (socklen_t)optlen);
}

int recvfrom(int s, char *buf, int len, int flags, struct sockaddr *from, int *fromlen)
{
    socklen_t length = fromlen ? (socklen_t)*fromlen : 0;
    const ssize_t received = ::recvfrom(s, buf, (size_t)len, flags, from, fromlen ? &length : nullptr);
    if (received >= 0 && fromlen)
        *fromlen = (int)length;
    return (int)received;
}
