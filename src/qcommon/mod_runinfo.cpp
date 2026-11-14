#include "mod_runinfo.h"

#include <Windows.h>
#include <libs/libtomcrypt-1.17/src/headers/tomcrypt.h>
#include <universal/dvar.h>

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <vector>

namespace
{
struct ZoneSum
{
    std::string name;
    unsigned int crc;
    unsigned long long size;
};

std::mutex s_lock;
bool s_exeRead;
std::vector<ZoneSum> s_zones;
bool s_exeDone;
unsigned char s_exeDigest[64]; // all zero when the executable could not be read
std::string s_hash;
bool s_hashDirty = true;

// The executable as the OS loaded it (GetModuleFileNameA: the .exe, /proc/self/exe, the Switch's .nro), read once.
void HashExecutable()
{
    char path[1024];
    const DWORD n = GetModuleFileNameA(nullptr, path, sizeof(path));
    if (!n || n >= sizeof(path))
        return;
    // CreateFileA, not fopen: on posix the path comes back with '\\' separators, which the compat layer takes back.
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return;
    hash_state md;
    sha512_init(&md);
    static unsigned char buffer[1 << 16];
    DWORD got;
    while (ReadFile(f, buffer, sizeof(buffer), &got, nullptr) && got > 0)
        sha512_process(&md, buffer, (unsigned long)got);
    CloseHandle(f);
    sha512_done(&md, s_exeDigest);
    s_exeRead = true;
}

std::string Hex(const unsigned char *bytes, size_t count)
{
    static const char hex[] = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < count; ++i)
    {
        out += hex[bytes[i] >> 4];
        out += hex[bytes[i] & 15];
    }
    return out;
}

void HashExecutableOnce()
{
    if (!s_exeDone)
    {
        s_exeDone = true;
        HashExecutable();
    }
}

void AddInt(std::string *out, const char *label, const char *dvar)
{
    if (Dvar_FindVar(dvar) && Dvar_GetInt(dvar) != 0)
    {
        char text[64];
        std::snprintf(text, sizeof(text), label, Dvar_GetInt(dvar));
        *out += out->empty() ? "" : " | ";
        *out += text;
    }
}

void AddText(std::string *out, const char *label, const char *dvar)
{
    if (!Dvar_FindVar(dvar))
        return;
    const char *value = Dvar_GetString(dvar);
    if (value && *value)
    {
        *out += out->empty() ? "" : " | ";
        *out += label;
        *out += value;
    }
}
} // namespace

void RunInfo_ZoneLoaded(const char *zoneName, unsigned int crc32, unsigned long long size)
{
    std::lock_guard<std::mutex> hold(s_lock);
    for (ZoneSum &z : s_zones)
        if (z.name == zoneName)
        {
            z.crc = crc32;
            z.size = size;
            s_hashDirty = true;
            return;
        }
    s_zones.push_back({zoneName, crc32, size});
    s_hashDirty = true;
}

std::string RunInfo_Hash(int *zoneCount)
{
    std::lock_guard<std::mutex> hold(s_lock);
    HashExecutableOnce();
    if (s_hashDirty)
    {
        std::vector<ZoneSum> zones = s_zones;
        std::sort(zones.begin(), zones.end(), [](const ZoneSum &a, const ZoneSum &b) { return a.name < b.name; });
        hash_state md;
        sha512_init(&md);
        sha512_process(&md, s_exeDigest, sizeof(s_exeDigest));
        for (const ZoneSum &z : zones)
        {
            unsigned char sums[12];
            for (int i = 0; i < 4; ++i)
                sums[i] = (unsigned char)(z.crc >> (8 * i));
            for (int i = 0; i < 8; ++i)
                sums[4 + i] = (unsigned char)(z.size >> (8 * i));
            sha512_process(&md, (const unsigned char *)z.name.c_str(), (unsigned long)z.name.size() + 1);
            sha512_process(&md, sums, sizeof(sums));
        }
        unsigned char digest[64];
        sha512_done(&md, digest);
        s_hash = Hex(digest, sizeof(digest));
        s_hashDirty = false;
    }
    if (zoneCount)
        *zoneCount = (int)s_zones.size();
    return s_hash;
}

std::string RunInfo_ProgramHash()
{
    std::lock_guard<std::mutex> hold(s_lock);
    HashExecutableOnce();
    return s_exeRead ? Hex(s_exeDigest, sizeof(s_exeDigest)) : std::string();
}

std::string RunInfo_Options()
{
    std::string out;
    AddText(&out, "", "fs_game");
    AddText(&out, "mods: ", "fs_mods");
    AddText(&out, "layout: ", "bo1_mod_mapkit");
    if (Dvar_FindVar("bo1_testclient_god") && Dvar_GetBool("bo1_testclient_god"))
        out += out.empty() ? "god" : " | god";
    if (Dvar_FindVar("bo1_aimassist") && Dvar_GetBool("bo1_aimassist"))
    {
        std::string aim = "aim assist";
        if (Dvar_FindVar("aim_autoaim_enabled") && Dvar_GetBool("aim_autoaim_enabled"))
            aim += " + auto";
        out += out.empty() ? aim : " | " + aim;
    }
    AddInt(&out, "%d points", "zm_startpoints");
    AddText(&out, "perks: ", "zm_startperks");
    AddInt(&out, "power %d", "zm_startpower");
    AddInt(&out, "pap %d", "zm_startpap");
    AddInt(&out, "far zombies %d", "zm_farrespawn");
    if (const dvar_s *scale = Dvar_FindVar("timescale"))
        if (scale->current.value != 1.0f)
        {
            char text[32];
            std::snprintf(text, sizeof(text), "timescale %.2f", scale->current.value);
            out += out.empty() ? text : std::string(" | ") + text;
        }
    return out.empty() ? "standard" : out;
}
