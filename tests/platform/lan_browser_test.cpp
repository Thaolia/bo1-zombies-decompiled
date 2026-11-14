// src/platform/lan_browser.cpp on the host: the statusResponse parser, the broadcast targets, and a real scan of
// 127.0.0.1 answered by a stand-in server on a thread (the reply a dedicated server sends, as tests/zmadmin's).
//   ./lan_browser_test
#include <platform/lan_browser.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

namespace
{
int failures;

void Check(bool ok, const char *what)
{
    if (!ok)
    {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

const std::string kReply = std::string("\xff\xff\xff\xff", 4) +
                           "statusResponse\n\\sv_hostname\\^1Chez ^7Toto\\mapname\\zombie_theater\\sv_maxclients\\4\n"
                           "0 500 31 5 \"Alpha\"\n1 1200 40 5 \"Bravo the second\"\n";

// A server on 127.0.0.1:port that answers one getstatus, as SV_ConnectionlessPacket does.
void Serve(int sock, int timeoutMs)
{
    pollfd fd = {sock, POLLIN, 0};
    if (poll(&fd, 1, timeoutMs) <= 0)
        return;
    char buffer[256];
    sockaddr_in from = {};
    socklen_t size = sizeof(from);
    const ssize_t n = recvfrom(sock, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr *>(&from), &size);
    if (n == ssize_t(LanBrowser_Request().size()) && std::memcmp(buffer, LanBrowser_Request().data(), size_t(n)) == 0)
        sendto(sock, kReply.data(), kReply.size(), 0, reinterpret_cast<sockaddr *>(&from), size);
}
} // namespace

int main()
{
    LanServer s;
    Check(LanBrowser_ParseStatus(kReply.data(), kReply.size(), &s), "a statusResponse parses");
    Check(s.hostname == "Chez Toto" && s.map == "zombie_theater" && s.maxPlayers == 4 && s.players == 2,
          "host name without colour codes, map, 2 of 4 players");
    const std::string empty = std::string("\xff\xff\xff\xff", 4) + "statusResponse\n\\sv_hostname\\X\n";
    Check(LanBrowser_ParseStatus(empty.data(), empty.size(), &s) && s.players == 0 && s.maxPlayers == 0 &&
              s.map.empty(),
          "a server with no player and no sv_maxclients");
    const std::string other = std::string("\xff\xff\xff\xff", 4) + "infoResponse\n\\x\\y";
    Check(!LanBrowser_ParseStatus(other.data(), other.size(), &s) && !LanBrowser_ParseStatus("abc", 3, &s),
          "anything else is refused");

    in_addr ip, mask;
    inet_pton(AF_INET, "192.168.1.20", &ip);
    inet_pton(AF_INET, "255.255.255.0", &mask);
    const std::vector<std::string> targets = LanBrowser_Broadcasts(ip.s_addr, mask.s_addr);
    Check(targets.size() == 2 && targets[0] == "255.255.255.255" && targets[1] == "192.168.1.255",
          "the limited broadcast and the subnet's");
    Check(LanBrowser_Broadcasts(0, 0).size() == 1, "without an address, the limited broadcast only");

    // a real scan: the stand-in listens on a free port of 127.0.0.1, the scan sends to three ports from it
    const int server = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in at = {};
    at.sin_family = AF_INET;
    at.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(server, reinterpret_cast<sockaddr *>(&at), sizeof(at));
    socklen_t size = sizeof(at);
    getsockname(server, reinterpret_cast<sockaddr *>(&at), &size);
    const int port = ntohs(at.sin_port);
    std::thread serving(Serve, server, 2000);
    const std::vector<LanServer> found = LanBrowser_Scan({"127.0.0.1"}, port - 1, 3, 500);
    serving.join();
    close(server);
    Check(found.size() == 1 && found[0].address == "127.0.0.1" && found[0].port == port &&
              found[0].hostname == "Chez Toto" && found[0].players == 2,
          "the scan finds the stand-in server with its address, port and status");
    Check(LanBrowser_Scan({"127.0.0.1"}, port, 1, 200).empty(), "nothing answers: an empty list after the wait");

    if (failures)
        return 1;
    std::printf("lan_browser_test: all checks passed\n");
    return 0;
}
