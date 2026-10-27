#pragma once

// The Switch's network start-up (switch_net.cpp): BSD sockets for the engine's UDP (win_net.cpp through the posix
// Winsock shim) and nifm for the console's own address.

#include <cstddef>

// Opens the sockets and nifm services once, before the start menu, and logs the result; their exit is registered with
// atexit, since the engine leaves through exit() (Sys_Quit). Without sockets the engine runs as before: its socket()
// calls fail and it plays solo.
void SwitchNet_Init();

// The console's IP address as text ("192.168.1.20") and true while it has one; "aucun" and false without a network
// (Wi-Fi off, airplane mode, no access point). Asked each time: the console may join or leave a network at any time.
bool SwitchNet_Address(char *text, size_t size);

// For the engine (cl_main_pc_mp.cpp): 1 while the console has an IP address.
extern "C" int Switch_NetworkAvailable();
