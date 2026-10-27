#pragma once

// phase 8 step 5: a client's console log sent to the dedicated server over UDP (docs/SERVER.md, "Client logs"), so a
// player's log reaches the operator without anyone copying files: on its own socket, apart from the netchan, from a
// 1 MiB buffer filled from the start, so the lines from before the connection, from a load or from a freeze go too.
// The server (src/server_mp/sv_netlog.cpp) listens on its game port + 1.

#include <cstdint>

namespace netlog
{
constexpr char kMagic[4] = { 'B', 'O', '1', 'L' };
constexpr uint8_t kVersion = 1;
constexpr int kHeaderSize = 16;   // magic, version, name length, flags (2), session id, sequence
constexpr int kMaxName = 31;
constexpr int kMaxPayload = 1200; // stays under the LAN's MTU with the header and the name
constexpr int kPacketMax = kHeaderSize + kMaxName + kMaxPayload;
} // namespace netlog

// Any thread, never blocks on the network: appends a console line to the buffer (dropped, and counted, when full).
void NetLog_Print(const char *text);
// Main thread, from Com_Init (once the command line's +set are dvars) and each client frame: reads where to send
// (bo1_logserver, else the cl_autoConnect server) and the player's name, and starts the sending thread the first time
// there is somewhere to send.
void NetLog_ClientFrame();
// Sends what is left (at most a second) and stops the thread; also registered with atexit.
void NetLog_Shutdown();
