#pragma once

// The dedicated server's console on curses (docs/SERVER.md, docs/PORTING.md phase 8): a status line, the player slots,
// the console log and a command line. sv_tui.cpp is the curses side and includes no engine header (curses.h defines
// move, clear, refresh, timeout, scroll, ...); sv_tui_engine.cpp feeds it the server's state. Not on the Switch.

struct ServerTuiPlayer
{
    int num;
    int state; // client_t header.state: 3 connected, 4 loading, 5 active
    int ping;
    int score;
    char name[32];
    char address[48];
    char game[12];   // in the game (state 5): playing, down, spectator, dead
    char admin[8];   // zm_* states: G god, D demigod, N notarget, C noclip
};

struct ServerTuiStatus
{
    char hostname[64];
    char mapname[64];
    int port;
    int maxClients;
    int uptimeSeconds;
    int playerCount;
    ServerTuiPlayer players[64];
};

// false when there is no interactive terminal (output redirected, no console): the plain console stays in use
bool ServerTui_Start();
// restores the terminal; safe to call twice and from every exit path
void ServerTui_Stop();
bool ServerTui_Active();
// console text from any thread; colour codes are dropped
void ServerTui_Print(const char *text);
// main thread: reads the keys, redraws at most 10 times a second, returns a command line to run or nullptr
const char *ServerTui_Frame(const ServerTuiStatus &status);

// sv_tui_engine.cpp: fills the status from the server and runs ServerTui_Frame
const char *Sys_ServerTuiFrame();
