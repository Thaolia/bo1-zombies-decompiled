#pragma once

// The dedicated servers of the local network, found the way ZmClient's browser finds them
// (tools/zmclient/zmclient/browser.py): a broadcast "getstatus" to the game's ports, then every "statusResponse" that
// comes back within a short wait. BSD sockets only, so the Switch launcher (libnx) and the host test
// (tests/platform/lan_browser_test.cpp) run the same code.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct LanServer
{
    std::string address; // dotted IPv4 of the reply's sender
    int port = 0;
    std::string hostname; // sv_hostname, colour codes removed
    std::string map;      // mapname
    int players = 0;      // one status line per connected client
    int maxPlayers = 0;   // sv_maxclients, 0 if the server does not say
    int pingMs = 0;       // from the broadcast to this reply
};

// The game's server ports: net_port 3074, and the next ones a second server on one machine takes (+1..9).
constexpr int kLanFirstPort = 3074;
constexpr int kLanPortCount = 10;

// The out-of-band request the servers answer (SV_ConnectionlessPacket's getstatus).
std::string LanBrowser_Request();

// Fills hostname, map, players and maxPlayers from one datagram; false if it is not a statusResponse.
bool LanBrowser_ParseStatus(const char *data, size_t size, LanServer *out);

// Where to broadcast: 255.255.255.255 and, with an address and its mask (network byte order, as in_addr's s_addr;
// 0 when unknown), the subnet's own broadcast address, which some access points forward when they drop the limited one.
std::vector<std::string> LanBrowser_Broadcasts(uint32_t address, uint32_t mask);

// Sends the request to every target on every port in [firstPort, firstPort + portCount), then collects replies for
// listenMs. One entry per (address, port), sorted by address then port. Empty when the socket cannot be made; never
// blocks longer than listenMs (plus the sends).
std::vector<LanServer> LanBrowser_Scan(const std::vector<std::string> &targets, int firstPort, int portCount,
                                       int listenMs);
