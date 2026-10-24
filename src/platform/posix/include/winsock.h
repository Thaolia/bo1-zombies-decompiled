#pragma once

// <winsock.h> on BSD sockets, for the posix build; <windows.h> includes it, as the Windows SDK's does. Besides the
// Winsock names, it accepts the socket level, options and ioctl command that code written for Winsock passes as raw
// numbers (win32/win_net.cpp: SOL_SOCKET 0xFFFF, SO_BROADCAST 0x20, FIONBIO 0x8004667E), whose BSD values differ.
// The platform layer defines the functions (win32_sockets.cpp).

#include "windows.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>

typedef UINT_PTR SOCKET;

#define INVALID_SOCKET (~(SOCKET)0)
#define SOCKET_ERROR (-1)

#define WSADESCRIPTION_LEN 256
#define WSASYS_STATUS_LEN 128
typedef struct WSAData
{
    WORD wVersion;
    WORD wHighVersion;
    unsigned short iMaxSockets;
    unsigned short iMaxUdpDg;
    char *lpVendorInfo;
    char szDescription[WSADESCRIPTION_LEN + 1];
    char szSystemStatus[WSASYS_STATUS_LEN + 1];
} WSADATA, *LPWSADATA;

#define WSABASEERR 10000
#define WSAEINTR 10004
#define WSAEBADF 10009
#define WSAEACCES 10013
#define WSAEFAULT 10014
#define WSAEINVAL 10022
#define WSAEMFILE 10024
#define WSAEWOULDBLOCK 10035
#define WSAEINPROGRESS 10036
#define WSAEALREADY 10037
#define WSAENOTSOCK 10038
#define WSAEDESTADDRREQ 10039
#define WSAEMSGSIZE 10040
#define WSAEPROTOTYPE 10041
#define WSAENOPROTOOPT 10042
#define WSAEPROTONOSUPPORT 10043
#define WSAESOCKTNOSUPPORT 10044
#define WSAEOPNOTSUPP 10045
#define WSAEPFNOSUPPORT 10046
#define WSAEAFNOSUPPORT 10047
#define WSAEADDRINUSE 10048
#define WSAEADDRNOTAVAIL 10049
#define WSAENETDOWN 10050
#define WSAENETUNREACH 10051
#define WSAENETRESET 10052
#define WSAECONNABORTED 10053
#define WSAECONNRESET 10054
#define WSAENOBUFS 10055
#define WSAEISCONN 10056
#define WSAENOTCONN 10057
#define WSAESHUTDOWN 10058
#define WSAETOOMANYREFS 10059
#define WSAETIMEDOUT 10060
#define WSAECONNREFUSED 10061
#define WSAELOOP 10062
#define WSAENAMETOOLONG 10063
#define WSAEHOSTDOWN 10064
#define WSAEHOSTUNREACH 10065
#define WSAENOTEMPTY 10066

extern "C" {
int WSAStartup(WORD wVersionRequested, LPWSADATA lpWSAData);
int WSACleanup(void);
// The Winsock code of the last socket call's errno; an errno with no Winsock counterpart is returned as is (below
// WSABASEERR, so it matches no WSAE code).
int WSAGetLastError(void);
int closesocket(SOCKET s);
}

// argp is Winsock's u_long, 32 bits (LLP64); glibc's u_long is 64, so only its first 32 bits are read or written.
int ioctlsocket(SOCKET s, long cmd, u_long *argp);
// Overloads of the BSD functions for Winsock's argument types (const char * option values, int lengths). Overload
// resolution prefers them only for calls written that way; they hand anything that is not a Winsock number on unchanged.
int setsockopt(int s, int level, int optname, const char *optval, int optlen);
int recvfrom(int s, char *buf, int len, int flags, struct sockaddr *from, int *fromlen);
