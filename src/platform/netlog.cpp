#include "netlog.h"

#include <qcommon/common.h>
#include <universal/dvar.h>
#include <universal/q_shared.h>
#include <win32/win_main.h>

#include <windows.h> // Winsock: the SDK's on Windows, BSD sockets through posix/include/winsock.h elsewhere

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <thread>

#if defined(__SWITCH__)
extern "C" void Switch_PinCurrentThread(int core);
#endif

namespace
{
// a map load prints about a megabyte within seconds (dvar dumps, asset lines)
constexpr size_t kRingSize = 1024 * 1024;
constexpr int kDefaultPort = 3074;
constexpr int kPacketsPerTick = 40; // per 50 ms: ~960 KB/s, under the server's per-address limit
// Winsock's FIONBIO; the posix shim takes the same number (posix/include/winsock.h)
constexpr long kWinsockFionbio = (long)0x8004667E;

// the buffer: a byte ring, written by any thread under s_lock, read by the sending thread
char s_ring[kRingSize];
size_t s_read;
size_t s_used;
unsigned long long s_dropped;

std::mutex s_lock;
std::condition_variable s_wake;
std::thread s_thread;
bool s_started;
bool s_stop;

// where to send, from the main thread
char s_target[128];
char s_name[netlog::kMaxName + 1];
bool s_targetChanged;

const dvar_t *bo1_netlog;
const dvar_t *bo1_logserver;

void PutU32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

// "host[:port]" of the game server -> its log port (port + 1); false when the host does not resolve
bool ResolveTarget(const char *target, sockaddr_in *out)
{
    char host[128];
    I_strncpyz(host, target, sizeof(host));
    int port = kDefaultPort;
    char *colon = strrchr(host, ':');
    if ( colon )
    {
        *colon = 0;
        port = atoi(colon + 1);
        if ( port <= 0 || port >= 65535 )
            return false;
    }
    memset(out, 0, sizeof(*out));
    out->sin_family = AF_INET;
    out->sin_port = htons((unsigned short)(port + 1));
    const unsigned long ip = inet_addr(host);
    if ( ip != INADDR_NONE )
    {
        out->sin_addr.s_addr = ip;
        return true;
    }
    const hostent *h = gethostbyname(host);
    if ( !h || h->h_addrtype != AF_INET || !h->h_addr_list[0] )
        return false;
    memcpy(&out->sin_addr, h->h_addr_list[0], sizeof(out->sin_addr));
    return true;
}

// one packet from the buffer, under s_lock; 0 when there is nothing to send
int TakePacket(unsigned char *packet, uint32_t session, uint32_t seq)
{
    const int nameLen = (int)strlen(s_name);
    memcpy(packet, netlog::kMagic, 4);
    packet[4] = netlog::kVersion;
    packet[5] = (unsigned char)nameLen;
    packet[6] = packet[7] = 0;
    PutU32(packet + 8, session);
    PutU32(packet + 12, seq);
    memcpy(packet + netlog::kHeaderSize, s_name, nameLen);
    unsigned char *payload = packet + netlog::kHeaderSize + nameLen;
    int len = 0;
    if ( s_dropped )
    {
        len = snprintf((char *)payload, netlog::kMaxPayload, "\n[netlog: %llu bytes dropped, the buffer was full]\n",
                       s_dropped);
        s_dropped = 0;
    }
    while ( len < netlog::kMaxPayload && s_used )
    {
        const size_t contiguous = kRingSize - s_read < s_used ? kRingSize - s_read : s_used;
        const size_t n = contiguous < (size_t)(netlog::kMaxPayload - len) ? contiguous : netlog::kMaxPayload - len;
        memcpy(payload + len, s_ring + s_read, n);
        len += (int)n;
        s_read = (s_read + n) % kRingSize;
        s_used -= n;
    }
    return len ? netlog::kHeaderSize + nameLen + len : 0;
}

void SendThread()
{
#if defined(__SWITCH__)
    // the default core is the creator's (the main thread's): keep it off the game's cores
    Switch_PinCurrentThread(2);
#endif
#if defined(_WIN32)
    WSADATA wsa;
    WSAStartup(MAKEWORD(1, 1), &wsa);
#endif
    const SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    // a session id per process, so the server keeps one file per run and tells two clients of one address apart
    const uint32_t session = (uint32_t)time(nullptr) * 2654435761u ^ (uint32_t)(uintptr_t)&session
        ^ (uint32_t)std::chrono::steady_clock::now().time_since_epoch().count();
    uint32_t seq = 0;
    sockaddr_in to;
    bool resolved = false;
    unsigned char packet[netlog::kPacketMax];
    char target[sizeof(s_target)] = "";
    std::chrono::steady_clock::time_point stopBy;
    std::unique_lock<std::mutex> lock(s_lock);
    for ( ;; )
    {
        if ( s_stop && stopBy == std::chrono::steady_clock::time_point() )
            stopBy = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        if ( s_targetChanged )
        {
            s_targetChanged = false;
            I_strncpyz(target, s_target, sizeof(target));
            lock.unlock();
            resolved = *target && ResolveTarget(target, &to);
            lock.lock();
        }
        for ( int i = 0; resolved && sock != INVALID_SOCKET && i < kPacketsPerTick; ++i )
        {
            const int len = TakePacket(packet, session, seq);
            if ( !len )
                break;
            ++seq;
            lock.unlock();
            sendto(sock, (const char *)packet, len, 0, (const sockaddr *)&to, sizeof(to));
            lock.lock();
        }
        if ( s_stop && (!s_used || !resolved || std::chrono::steady_clock::now() >= stopBy) )
            break;
        s_wake.wait_for(lock, std::chrono::milliseconds(50));
    }
    lock.unlock();
    if ( sock != INVALID_SOCKET )
        closesocket(sock);
}
} // namespace

void NetLog_Print(const char *text)
{
    // the server receives logs; it sends none
    if ( !text || !*text || Sys_IsDedicatedZombiesServer() )
        return;
    const size_t len = strlen(text);
    const std::lock_guard<std::mutex> guard(s_lock);
    if ( len > kRingSize - s_used )
    {
        s_dropped += len;
        return;
    }
    size_t write = (s_read + s_used) % kRingSize;
    const size_t first = kRingSize - write < len ? kRingSize - write : len;
    memcpy(s_ring + write, text, first);
    memcpy(s_ring, text + first, len - first);
    s_used += len;
}

void NetLog_ClientFrame()
{
    if ( Sys_IsDedicatedZombiesServer() )
        return;
    if ( !bo1_netlog )
    {
        bo1_netlog = _Dvar_RegisterBool("bo1_netlog", true, 0,
                                        "phase 8: send this client's console log to the server it joins (its port + 1)");
        bo1_logserver = _Dvar_RegisterString("bo1_logserver", "", 0,
                                             "phase 8: the server to send the log to, host[:port] of its game port "
                                             "(empty: the cl_autoConnect server)");
    }
    if ( !bo1_netlog->current.enabled )
        return;
    const char *target = bo1_logserver->current.string;
    if ( !*target )
    {
        const dvar_t *autoConnect = Dvar_FindVar("cl_autoConnect");
        target = autoConnect ? autoConnect->current.string : "";
    }
    const dvar_t *nameVar = Dvar_FindVar("name");
    const char *name = nameVar ? nameVar->current.string : "";
    bool start = false;
    {
        const std::lock_guard<std::mutex> guard(s_lock);
        if ( strcmp(target, s_target) || strcmp(name, s_name) )
        {
            I_strncpyz(s_target, target, sizeof(s_target));
            I_strncpyz(s_name, name, sizeof(s_name));
            s_targetChanged = true;
        }
        start = *s_target && !s_started && !s_stop;
        if ( start )
            s_started = true;
    }
    if ( start )
    {
        s_thread = std::thread(SendThread);
        atexit(NetLog_Shutdown);
        Com_Printf(0, "netlog: this console log also goes to %s (its port + 1)\n", target);
    }
}

void NetLog_Shutdown()
{
    {
        const std::lock_guard<std::mutex> guard(s_lock);
        if ( !s_started || s_stop )
            return;
        s_stop = true;
    }
    s_wake.notify_all();
    if ( s_thread.joinable() )
        s_thread.join();
}
