#include "sv_netlog.h"

#include <platform/netlog.h>
#include <qcommon/common.h>
#include <win32/win_common.h>
#include <universal/dvar.h>
#include <universal/q_shared.h>
#include <win32/win_main.h>

#include <windows.h> // Winsock: the SDK's on Windows, BSD sockets through posix/include/winsock.h elsewhere

#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <mutex>

namespace
{
constexpr int kMaxSessions = 32;
constexpr size_t kKeptLines = 2000;
constexpr int kPacketsPerFrame = 128;
// per address: twice a client's sending rate, and its whole 1 MiB buffer and more in a burst
constexpr double kBytesPerSecond = 2048.0 * 1024;
constexpr double kBurstBytes = 4096.0 * 1024;
constexpr long kWinsockFionbio = (long)0x8004667E;
#if defined(_WIN32)
constexpr char kPathSep = '\\';
#else
constexpr char kPathSep = '/';
#endif

struct Session
{
    bool used;
    unsigned long ip;
    uint32_t id;
    char name[netlog::kMaxName + 1];
    char address[32];
    char file[64];
    FILE *f;
    bool started;
    uint32_t next;
    unsigned int lost;
    unsigned int late;
    long long lastMs;
    std::string partial;
    std::deque<std::string> lines;
};

struct RateLimit
{
    unsigned long ip;
    double tokens;
    long long lastMs;
    unsigned long long dropped;
};

std::mutex s_lock; // the sessions: written on the server thread, read by the TUI on the main one
Session s_sessions[kMaxSessions];
RateLimit s_rates[kMaxSessions];
SOCKET s_socket = INVALID_SOCKET;
bool s_opened;
unsigned char s_packet[2048];

long long NowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

uint32_t GetU32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

void OpenSocket()
{
    s_opened = true;
    const int port = Dvar_GetInt("net_port") + 1;
#if defined(_WIN32)
    WSADATA wsa;
    WSAStartup(MAKEWORD(1, 1), &wsa);
#endif
    s_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if ( s_socket == INVALID_SOCKET )
    {
        Com_PrintError(0, "netlog: no socket for the client logs (error %d)\n", WSAGetLastError());
        return;
    }
    sockaddr_in at;
    memset(&at, 0, sizeof(at));
    at.sin_family = AF_INET;
    at.sin_port = htons((unsigned short)port);
    // the game socket's interface when net_ip names one, else every interface
    const unsigned long ip = inet_addr(Dvar_GetString("net_ip"));
    at.sin_addr.s_addr = ip != INADDR_NONE ? ip : htonl(INADDR_ANY);
    u_long nonBlocking = 1;
    // a client's 50 ms burst is 40 packets, read once a server frame: more room than the system's default buffer.
    // Winsock's SOL_SOCKET / SO_RCVBUF numbers, which the posix shim also takes (posix/include/winsock.h)
    const int receiveBuffer = 1024 * 1024;
    setsockopt(s_socket, 0xFFFF, 0x1002, (const char *)&receiveBuffer, sizeof(receiveBuffer));
    if ( bind(s_socket, (const sockaddr *)&at, sizeof(at)) == SOCKET_ERROR
        || ioctlsocket(s_socket, kWinsockFionbio, &nonBlocking) == SOCKET_ERROR )
    {
        Com_PrintError(0, "netlog: cannot listen for the client logs on UDP port %d (error %d)\n", port,
                       WSAGetLastError());
        closesocket(s_socket);
        s_socket = INVALID_SOCKET;
        return;
    }
    Com_Printf(0, "netlog: client logs on UDP port %d, written to %s/logs\n", port, Dvar_GetString("fs_h"));
}

bool Allowed(unsigned long ip, int bytes, long long now)
{
    RateLimit *slot = nullptr;
    RateLimit *oldest = &s_rates[0];
    for ( RateLimit &r : s_rates )
    {
        if ( r.lastMs && r.ip == ip )
            slot = &r;
        if ( r.lastMs < oldest->lastMs )
            oldest = &r;
    }
    if ( !slot )
    {
        slot = oldest;
        slot->ip = ip;
        slot->tokens = kBurstBytes;
        slot->dropped = 0;
        slot->lastMs = now;
    }
    slot->tokens += (now - slot->lastMs) * kBytesPerSecond / 1000.0;
    if ( slot->tokens > kBurstBytes )
        slot->tokens = kBurstBytes;
    slot->lastMs = now;
    if ( slot->tokens < bytes )
    {
        slot->dropped += bytes;
        return false;
    }
    slot->tokens -= bytes;
    return true;
}

void AddText(Session &s, const char *text, size_t len)
{
    if ( s.f )
    {
        fwrite(text, 1, len, s.f);
        fflush(s.f);
    }
    for ( size_t i = 0; i < len; ++i )
    {
        if ( text[i] == '\n' )
        {
            s.lines.push_back(s.partial);
            s.partial.clear();
            if ( s.lines.size() > kKeptLines )
                s.lines.pop_front();
        }
        else if ( text[i] != '\r' )
        {
            s.partial += text[i] == '\t' ? ' ' : text[i];
        }
    }
}

void AddMarker(Session &s, const char *fmt, unsigned int n)
{
    char line[128];
    const int len = snprintf(line, sizeof(line), fmt, n);
    if ( len > 0 )
        AddText(s, line, (size_t)len);
}

Session &FindSession(unsigned long ip, const char *address, uint32_t id, const char *name, long long now)
{
    Session *oldest = &s_sessions[0];
    for ( Session &s : s_sessions )
    {
        if ( s.used && s.ip == ip && s.id == id )
            return s;
        if ( !s.used || (oldest->used && s.lastMs < oldest->lastMs) )
            oldest = &s;
    }
    Session &s = *oldest;
    if ( s.f )
        fclose(s.f);
    s.used = true;
    s.ip = ip;
    s.id = id;
    s.f = nullptr;
    s.started = false;
    s.next = 0;
    s.lost = s.late = 0;
    s.lastMs = now;
    s.partial.clear();
    s.lines.clear();
    I_strncpyz(s.address, address, sizeof(s.address));
    // the file name keeps the name's letters, digits, '-' and '_' only
    char safe[netlog::kMaxName + 1];
    int n = 0;
    for ( const char *p = name; *p && n < netlog::kMaxName; ++p )
    {
        if ( (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' )
            safe[n++] = *p;
    }
    safe[n] = 0;
    I_strncpyz(s.name, name, sizeof(s.name));
    const time_t t = time(nullptr);
    const tm *lt = localtime(&t);
    snprintf(s.file, sizeof(s.file), "%s_%04d%02d%02d-%02d%02d%02d_%08x.log", n ? safe : "player",
             lt ? lt->tm_year + 1900 : 0, lt ? lt->tm_mon + 1 : 0, lt ? lt->tm_mday : 0, lt ? lt->tm_hour : 0,
             lt ? lt->tm_min : 0, lt ? lt->tm_sec : 0, id);
    char dir[400];
    char path[512];
    snprintf(dir, sizeof(dir), "%s%clogs", Dvar_GetString("fs_h"), kPathSep);
    snprintf(path, sizeof(path), "%s%c%s", dir, kPathSep, s.file);
    Sys_Mkdir(dir);
    s.f = fopen(path, "ab");
    if ( s.f )
        Com_Printf(0, "netlog: %s (%s) -> logs/%s\n", s.name, s.address, s.file);
    else
        Com_PrintError(0, "netlog: cannot write %s\n", path);
    return s;
}

void Receive(const unsigned char *p, int len, const sockaddr_in &from, long long now)
{
    if ( len < netlog::kHeaderSize || memcmp(p, netlog::kMagic, 4) || p[4] != netlog::kVersion )
        return;
    const int nameLen = p[5];
    if ( nameLen > netlog::kMaxName || netlog::kHeaderSize + nameLen > len
        || len - netlog::kHeaderSize - nameLen > netlog::kMaxPayload )
        return;
    const unsigned long ip = from.sin_addr.s_addr;
    if ( !Allowed(ip, len, now) )
        return;
    char name[netlog::kMaxName + 1];
    memcpy(name, p + netlog::kHeaderSize, nameLen);
    name[nameLen] = 0;
    const uint32_t id = GetU32(p + 8);
    const uint32_t seq = GetU32(p + 12);
    char address[32];
    I_strncpyz(address, inet_ntoa(from.sin_addr), sizeof(address));

    const std::lock_guard<std::mutex> guard(s_lock);
    Session &s = FindSession(ip, address, id, name, now);
    s.lastMs = now;
    if ( !s.started )
    {
        s.started = true;
        if ( seq )
            AddMarker(s, "\n[netlog: %u packets before this one did not arrive]\n", seq);
        s.next = seq;
    }
    if ( seq < s.next )
    {
        ++s.late; // a duplicate or a packet overtaken by a later one: its gap is already marked
        return;
    }
    if ( seq > s.next )
    {
        s.lost += seq - s.next;
        AddMarker(s, "\n[netlog: lost %u packets]\n", seq - s.next);
    }
    s.next = seq + 1;
    AddText(s, (const char *)p + netlog::kHeaderSize + nameLen, (size_t)(len - netlog::kHeaderSize - nameLen));
}
} // namespace

void SV_NetLog_Frame()
{
    if ( !Sys_IsDedicatedZombiesServer() )
        return;
    if ( !s_opened )
        OpenSocket();
    if ( s_socket == INVALID_SOCKET )
        return;
    const long long now = NowMs();
    for ( int i = 0; i < kPacketsPerFrame; ++i )
    {
        sockaddr_in from;
        int fromLen = sizeof(from);
        const int len = recvfrom(s_socket, (char *)s_packet, sizeof(s_packet), 0, (sockaddr *)&from, &fromLen);
        if ( len <= 0 )
            break;
        Receive(s_packet, len, from, now);
    }
}

bool SV_NetLog_Snapshot(const char *address, const char *name, std::vector<std::string> &lines, std::string &title)
{
    char ip[48];
    I_strncpyz(ip, address, sizeof(ip));
    char *colon = strchr(ip, ':');
    if ( colon )
        *colon = 0;
    const std::lock_guard<std::mutex> guard(s_lock);
    const Session *best = nullptr;
    for ( const Session &s : s_sessions )
    {
        if ( !s.used || strcmp(s.address, ip) )
            continue;
        const bool sameName = !I_stricmp(s.name, name);
        const bool bestSameName = best && !I_stricmp(best->name, name);
        if ( !best || (sameName && !bestSameName) || (sameName == bestSameName && s.lastMs > best->lastMs) )
            best = &s;
    }
    if ( !best )
        return false;
    lines.assign(best->lines.begin(), best->lines.end());
    if ( !best->partial.empty() )
        lines.push_back(best->partial);
    char text[160];
    snprintf(text, sizeof(text), "client log: %s, logs/%s, lost %u packets", best->name, best->file, best->lost);
    title = text;
    return true;
}
