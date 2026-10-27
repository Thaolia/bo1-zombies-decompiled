#pragma once

// phase 8 step 5: the dedicated server's receiver of the clients' console logs (src/platform/netlog.cpp sends them),
// on UDP net_port + 1. Each client run gets <fs_h>/logs/<name>_<date>_<session>.log; lost packets are marked in it.

#include <string>
#include <vector>

// server thread, each frame: opens the socket the first time, then reads what has arrived
void SV_NetLog_Frame();
// any thread (the TUI): the last lines of the newest log from this address ("ip" or "ip:port"), preferring the one
// with this player name; false when that address has sent none
bool SV_NetLog_Snapshot(const char *address, const char *name, std::vector<std::string> &lines, std::string &title);
