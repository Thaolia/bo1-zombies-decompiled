#pragma once

// phase 8 step 4: the dedicated zombies server's admin commands (zm_*, docs/SERVER.md "Admin commands"). Registered
// only with +set bo1_server 1; the script side is mods/admin, which the server adds to fs_mods.
void SV_ZmAdmin_AddCommands();
// around G_RunFrame, on the server thread: applies the god / noclip states and hands one queued script command per
// player to mods/admin
void SV_ZmAdmin_Frame(bool afterFrame);
// the player's place in the game, as the console and the TUI show it: playing, down, spectator, dead, or "" (no entity)
const char *SV_ZmAdmin_GameState(int client);
// the zm_* states on, as letters: G god, D demigod, N notarget, C noclip
void SV_ZmAdmin_Flags(int client, char *out, int size);
// a script LogPrint line (G_LogPrintf): mods/admin's "admin: ..." replies also go to the server console
void SV_ZmAdmin_LogLine(const char *line);
