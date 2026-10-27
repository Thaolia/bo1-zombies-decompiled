// libnx before anything that could pull the Win32 shim (switch_main.cpp explains why).
#include <switch.h>

#include "switch_net.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

extern "C" void Switch_BootLogWrite(const char *text);

namespace
{
bool s_socketsUp;
bool s_nifmUp;

void Log(const char *fmt, ...)
{
    char text[256];
    va_list ap;
    va_start(ap, fmt);
    const int n = std::vsnprintf(text, sizeof(text) - 1, fmt, ap);
    va_end(ap);
    size_t len = n < 0 ? 0 : size_t(n);
    if (len > sizeof(text) - 2)
        len = sizeof(text) - 2;
    text[len] = '\n';
    text[len + 1] = '\0';
    Switch_BootLogWrite(text);
}

bool CurrentIp(u32 *ip)
{
    *ip = 0;
    return s_nifmUp && R_SUCCEEDED(nifmGetCurrentIpAddress(ip)) && *ip != 0;
}

void FormatIp(u32 ip, char *text, size_t size)
{
    // nifm gives the address as in_addr's s_addr: network byte order, first octet in the low byte.
    std::snprintf(text, size, "%u.%u.%u.%u", ip & 0xff, (ip >> 8) & 0xff, (ip >> 16) & 0xff, ip >> 24);
}

void NetExit()
{
    if (s_socketsUp)
        socketExit();
    if (s_nifmUp)
        nifmExit();
    s_socketsUp = s_nifmUp = false;
}
} // namespace

void SwitchNet_Init()
{
    static bool done;
    if (done)
        return;
    done = true;

    const Result src = socketInitializeDefault();
    s_socketsUp = R_SUCCEEDED(src);
    const Result nrc = nifmInitialize(NifmServiceType_User);
    s_nifmUp = R_SUCCEEDED(nrc);
    Log("network: socketInitializeDefault -> 0x%x, nifmInitialize -> 0x%x", src, nrc);
    if (s_socketsUp || s_nifmUp)
        std::atexit(NetExit);

    char address[32];
    if (SwitchNet_Address(address, sizeof(address)))
    {
        NifmInternetConnectionType type = NifmInternetConnectionType(0);
        u32 strength = 0;
        NifmInternetConnectionStatus status = NifmInternetConnectionStatus(0);
        const Result irc = nifmGetInternetConnectionStatus(&type, &strength, &status);
        Log("network: IP %s (%s, Wi-Fi strength %u, internet status %d, rc 0x%x)", address,
            type == NifmInternetConnectionType_Ethernet ? "Ethernet" : "Wi-Fi", strength, int(status), irc);
    }
    else
        Log("network: no IP address (Wi-Fi off, airplane mode or no access point): solo only");
}

bool SwitchNet_Address(char *text, size_t size)
{
    u32 ip;
    if (!CurrentIp(&ip))
    {
        std::snprintf(text, size, "aucun");
        return false;
    }
    FormatIp(ip, text, size);
    return true;
}

extern "C" int Switch_NetworkAvailable()
{
    u32 ip;
    return s_socketsUp && CurrentIp(&ip);
}
