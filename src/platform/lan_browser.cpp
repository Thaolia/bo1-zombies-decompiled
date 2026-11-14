#include "lan_browser.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
const char kOob[4] = {'\xff', '\xff', '\xff', '\xff'};
const char kStatusHead[] = "statusResponse\n";
const size_t kIpText = 16; // "255.255.255.255" and its 0 (newlib's netinet/in.h has no INET_ADDRSTRLEN)

// "^1Name" -> "Name": the colour escapes the game's names and host names carry
std::string StripColours(const std::string &text)
{
    std::string out;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '^' && i + 1 < text.size() && text[i + 1] >= '0' && text[i + 1] <= '9')
        {
            ++i;
            continue;
        }
        out += text[i];
    }
    return out;
}

std::string InfoValue(const std::string &info, const char *key)
{
    // "\key\value\key\value..."
    size_t at = 0;
    while (at < info.size() && info[at] == '\\')
    {
        const size_t keyEnd = info.find('\\', at + 1);
        if (keyEnd == std::string::npos)
            break;
        const size_t valueEnd = info.find('\\', keyEnd + 1);
        const std::string k = info.substr(at + 1, keyEnd - at - 1);
        const std::string v = info.substr(keyEnd + 1, (valueEnd == std::string::npos ? info.size() : valueEnd) - keyEnd - 1);
        if (k == key)
            return v;
        if (valueEnd == std::string::npos)
            break;
        at = valueEnd;
    }
    return "";
}

long long NowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
} // namespace

std::string LanBrowser_Request()
{
    return std::string(kOob, 4) + "getstatus";
}

bool LanBrowser_ParseStatus(const char *data, size_t size, LanServer *out)
{
    const size_t head = 4 + sizeof(kStatusHead) - 1;
    if (size < head || std::memcmp(data, kOob, 4) != 0 || std::memcmp(data + 4, kStatusHead, head - 4) != 0)
        return false;
    std::string body(data + head, size - head);
    while (!body.empty() && body.back() == '\0')
        body.pop_back();
    const size_t lineEnd = body.find('\n');
    const std::string info = body.substr(0, lineEnd);
    out->hostname = StripColours(InfoValue(info, "sv_hostname"));
    out->map = InfoValue(info, "mapname");
    out->maxPlayers = std::atoi(InfoValue(info, "sv_maxclients").c_str());
    out->players = 0;
    // then one '<slot> <score> <ping> <state> "<name>"' line per client
    for (size_t at = lineEnd; at != std::string::npos && at + 1 < body.size();)
    {
        const size_t next = body.find('\n', at + 1);
        const std::string line = body.substr(at + 1, (next == std::string::npos ? body.size() : next) - at - 1);
        if (line.find('"') != std::string::npos)
            ++out->players;
        at = next;
    }
    return true;
}

std::vector<std::string> LanBrowser_Broadcasts(uint32_t address, uint32_t mask)
{
    std::vector<std::string> out = {"255.255.255.255"};
    if (address != 0 && mask != 0)
    {
        in_addr subnet;
        subnet.s_addr = (address & mask) | ~mask;
        char text[kIpText];
        if (inet_ntop(AF_INET, &subnet, text, sizeof(text)) && out[0] != text)
            out.push_back(text);
    }
    return out;
}

std::vector<LanServer> LanBrowser_Scan(const std::vector<std::string> &targets, int firstPort, int portCount,
                                       int listenMs)
{
    std::vector<LanServer> found;
    const int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        return found;
    const int on = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
    const std::string request = LanBrowser_Request();
    const long long sentAt = NowMs();
    for (const std::string &target : targets)
    {
        sockaddr_in to = {};
        to.sin_family = AF_INET;
        if (inet_pton(AF_INET, target.c_str(), &to.sin_addr) != 1)
            continue;
        for (int p = 0; p < portCount; ++p)
        {
            to.sin_port = htons(uint16_t(firstPort + p));
            sendto(sock, request.data(), request.size(), 0, reinterpret_cast<sockaddr *>(&to), sizeof(to));
        }
    }
    static char buffer[16384];
    for (;;)
    {
        const long long left = sentAt + listenMs - NowMs();
        if (left <= 0)
            break;
        pollfd fd = {sock, POLLIN, 0};
        if (poll(&fd, 1, int(left)) <= 0)
            break;
        sockaddr_in from = {};
        socklen_t fromSize = sizeof(from);
        const ssize_t n = recvfrom(sock, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr *>(&from), &fromSize);
        if (n <= 0)
            continue;
        LanServer server;
        if (!LanBrowser_ParseStatus(buffer, size_t(n), &server))
            continue;
        char text[kIpText] = "";
        inet_ntop(AF_INET, &from.sin_addr, text, sizeof(text));
        server.address = text;
        server.port = ntohs(from.sin_port);
        server.pingMs = int(NowMs() - sentAt);
        // a server reached by both broadcasts answers twice: keep the first
        const bool known = std::any_of(found.begin(), found.end(), [&](const LanServer &s) {
            return s.address == server.address && s.port == server.port;
        });
        if (!known)
            found.push_back(server);
    }
    close(sock);
    std::sort(found.begin(), found.end(), [](const LanServer &a, const LanServer &b) {
        return a.address != b.address ? a.address < b.address : a.port < b.port;
    });
    return found;
}
