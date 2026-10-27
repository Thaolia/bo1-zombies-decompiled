#include "sv_zm_admin.h"

#include <server_mp/sv_main_mp.h>
#include <server/server.h>
#include <qcommon/cmd.h>
#include <qcommon/common.h>
#include <game_mp/g_main_mp.h>
#include <game_mp/g_cmds_mp.h>
#include <game_mp/g_spawn_mp.h>
#include <client_mp/g_client_mp.h>
#include <clientscript/cscr_stringlist.h>
#include <clientscript/cscr_vm.h>
#include <universal/dvar.h>
#include <universal/q_shared.h>
#include <win32/win_main.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
// state bits: the first three are the entity flags the retail god / demigod / notarget cheats toggle (Cmd_God_f, ...)
constexpr unsigned int ZM_GOD = 1;
constexpr unsigned int ZM_DEMIGOD = 2;
constexpr unsigned int ZM_NOTARGET = 4;
constexpr unsigned int ZM_ENT_FLAGS = ZM_GOD | ZM_DEMIGOD | ZM_NOTARGET;
constexpr unsigned int ZM_NOCLIP = 0x100; // the client's flag 1 (Cmd_Noclip_f)
constexpr int ZM_MAX_CLIENTS = 64;

struct ZmAdminState
{
    unsigned int on;
    unsigned int clearOnce; // switched off: cleared once, so the retail cheats keep working afterwards
};

struct ZmAdminCommand
{
    int client;
    char cmd[16];
    char arg[128];
};

ZmAdminState s_state[ZM_MAX_CLIENTS];
ZmAdminCommand s_queue[128];
int s_queueCount;

int ZmMaxClients()
{
    const int max = com_maxclients->current.integer;
    return max < ZM_MAX_CLIENTS ? max : ZM_MAX_CLIENTS;
}

bool ZmIsNumber(const char *s, bool allowSign)
{
    if ( allowSign && (*s == '-' || *s == '+') )
        ++s;
    if ( !*s )
        return false;
    for ( ; *s; ++s )
    {
        if ( *s < '0' || *s > '9' )
            return false;
    }
    return true;
}

// <who>: a client slot, "all", or a player's name; minState: CS_CONNECTED for the engine states, CS_ACTIVE for a
// command the script runs on the player's entity
int ZmTargets(const char *who, int minState, int *out)
{
    int count = 0;
    if ( !com_sv_running->current.enabled )
    {
        Com_Printf(0, "zm_admin: no server running\n");
        return 0;
    }
    if ( !I_stricmp(who, "all") )
    {
        for ( int i = 0; i < ZmMaxClients(); ++i )
        {
            if ( svs.clients[i].header.state >= minState )
                out[count++] = i;
        }
        if ( !count )
            Com_Printf(0, "zm_admin: no player\n");
        return count;
    }
    int slot = -1;
    if ( ZmIsNumber(who, false) )
    {
        slot = atoi(who);
    }
    else
    {
        for ( int i = 0; i < ZmMaxClients(); ++i )
        {
            if ( svs.clients[i].header.state >= CS_CONNECTED && !I_stricmp(svs.clients[i].name, who) )
                slot = i;
        }
    }
    if ( slot < 0 || slot >= ZmMaxClients() || svs.clients[slot].header.state < CS_CONNECTED )
    {
        Com_Printf(0, "zm_admin: no player '%s'\n", who);
        return 0;
    }
    if ( svs.clients[slot].header.state < minState )
    {
        Com_Printf(0, "zm_admin: player %d (%s) is not in the game yet\n", slot, svs.clients[slot].name);
        return 0;
    }
    out[0] = slot;
    return 1;
}

void ZmQueue(int client, const char *cmd, const char *arg)
{
    if ( s_queueCount == (int)ARRAY_COUNT(s_queue) )
    {
        Com_Printf(0, "zm_admin: queue full, '%s' for player %d dropped\n", cmd, client);
        return;
    }
    ZmAdminCommand &c = s_queue[s_queueCount++];
    c.client = client;
    I_strncpyz(c.cmd, cmd, sizeof(c.cmd));
    I_strncpyz(c.arg, arg, sizeof(c.arg));
    Com_Printf(0, "zm_admin: %s%s%s -> player %d (%s)\n", cmd, *arg ? " " : "", arg, client, svs.clients[client].name);
}

void ZmUsage(const char *usage)
{
    Com_Printf(0, "usage: %s\n", usage);
}

// zm_god / zm_demigod / zm_notarget / zm_noclip <who> [0|1]: without a value, toggles
void ZmStateCommand(unsigned int bit, const char *name)
{
    int targets[ZM_MAX_CLIENTS];
    if ( SV_Cmd_Argc() < 2 || SV_Cmd_Argc() > 3 )
    {
        Com_Printf(0, "usage: %s <player|all> [0|1]\n", name);
        return;
    }
    const int count = ZmTargets(SV_Cmd_Argv(1), CS_CONNECTED, targets);
    for ( int t = 0; t < count; ++t )
    {
        ZmAdminState &s = s_state[targets[t]];
        const bool on = SV_Cmd_Argc() == 3 ? atoi(SV_Cmd_Argv(2)) != 0 : !(s.on & bit);
        if ( on )
        {
            s.on |= bit;
            s.clearOnce &= ~bit;
        }
        else
        {
            if ( s.on & bit )
                s.clearOnce |= bit;
            s.on &= ~bit;
        }
        Com_Printf(0, "zm_admin: %s %s for player %d (%s)\n", name, on ? "on" : "off", targets[t],
                   svs.clients[targets[t]].name);
    }
}

void ZmGod_f()
{
    ZmStateCommand(ZM_GOD, "zm_god");
}

void ZmDemigod_f()
{
    ZmStateCommand(ZM_DEMIGOD, "zm_demigod");
}

void ZmNotarget_f()
{
    ZmStateCommand(ZM_NOTARGET, "zm_notarget");
}

void ZmNoclip_f()
{
    ZmStateCommand(ZM_NOCLIP, "zm_noclip");
}

// a command mods/admin runs on each target player: <who> [argument]; restArgs: the argument is every word after <who>
void ZmScriptCommand(const char *name, const char *script, bool needArg, bool numberArg, const char *usage,
                     bool restArgs = false)
{
    int targets[ZM_MAX_CLIENTS];
    if ( needArg ? SV_Cmd_Argc() < 3 || (!restArgs && SV_Cmd_Argc() > 3) : SV_Cmd_Argc() != 2 )
    {
        ZmUsage(usage);
        return;
    }
    char arg[128] = "";
    for ( int i = 2; needArg && i < SV_Cmd_Argc(); ++i )
    {
        if ( i > 2 )
            I_strncat(arg, sizeof(arg), " ");
        I_strncat(arg, sizeof(arg), SV_Cmd_Argv(i));
    }
    if ( numberArg && !ZmIsNumber(arg, true) )
    {
        Com_Printf(0, "%s: '%s' is not a number\n", name, arg);
        return;
    }
    const int count = ZmTargets(SV_Cmd_Argv(1), CS_ACTIVE, targets);
    for ( int t = 0; t < count; ++t )
        ZmQueue(targets[t], script, arg);
}

void ZmPoints_f()
{
    ZmScriptCommand("zm_points", "points", true, true, "zm_points <player|all> <amount> (negative takes points)");
}

void ZmSetPoints_f()
{
    ZmScriptCommand("zm_setpoints", "setpoints", true, true, "zm_setpoints <player|all> <amount>");
}

void ZmPerk_f()
{
    ZmScriptCommand("zm_perk", "perk", true, false,
                    "zm_perk <player|all> <perk ...|all> (jugg, revive, speed, doubletap, staminup, phd, deadshot, "
                    "mulekick, or a specialty_ name)",
                    true);
}

void ZmRevive_f()
{
    ZmScriptCommand("zm_revive", "revive", false, false, "zm_revive <player|all>");
}

void ZmRespawn_f()
{
    ZmScriptCommand("zm_respawn", "respawn", false, false, "zm_respawn <player|all>");
}

// zm_kill / zm_down: the script's DoDamage cannot reach a god player, so god and demigod go off first (the frame
// clears the flags before it hands the command over)
void ZmDamageCommand(const char *name, const char *script)
{
    int targets[ZM_MAX_CLIENTS];
    if ( SV_Cmd_Argc() != 2 )
    {
        Com_Printf(0, "usage: %s <player|all>\n", name);
        return;
    }
    const int count = ZmTargets(SV_Cmd_Argv(1), CS_ACTIVE, targets);
    for ( int t = 0; t < count; ++t )
    {
        ZmAdminState &s = s_state[targets[t]];
        s.clearOnce |= s.on & (ZM_GOD | ZM_DEMIGOD);
        s.on &= ~(ZM_GOD | ZM_DEMIGOD);
        ZmQueue(targets[t], script, "");
    }
}

void ZmKill_f()
{
    ZmDamageCommand("zm_kill", "kill");
}

void ZmDown_f()
{
    ZmDamageCommand("zm_down", "down");
}

// the map's power switch, used by any player in the game
void ZmPower_f()
{
    int targets[ZM_MAX_CLIENTS];
    if ( ZmTargets("all", CS_ACTIVE, targets) )
        ZmQueue(targets[0], "power", "");
}

// the level's round: run by mods/admin from any player in the game
void ZmRound_f()
{
    if ( SV_Cmd_Argc() != 2 || !ZmIsNumber(SV_Cmd_Argv(1), false) || atoi(SV_Cmd_Argv(1)) < 1 )
    {
        ZmUsage("zm_round <round number, 1 or more>");
        return;
    }
    int targets[ZM_MAX_CLIENTS];
    if ( ZmTargets("all", CS_ACTIVE, targets) )
        ZmQueue(targets[0], "round", SV_Cmd_Argv(1));
}

// a fresh load of the map: devmap, which the dedicated server's zombies levels are started with
void ZmLoadMap(const char *map)
{
    Cbuf_AddText(0, va("devmap %s\n", map));
}

void ZmReset_f()
{
    const char *map = Dvar_GetString("mapname");
    if ( !com_sv_running->current.enabled || !*map )
    {
        Com_Printf(0, "zm_reset: no map running\n");
        return;
    }
    Com_Printf(0, "zm_admin: reset, %s loads again\n", map);
    ZmLoadMap(map);
}

void ZmMap_f()
{
    if ( SV_Cmd_Argc() != 2 )
    {
        ZmUsage("zm_map <map>, e.g. zm_map zombie_cosmodrome");
        return;
    }
    ZmLoadMap(SV_Cmd_Argv(1));
}

// fs_mods is read again by the FS_Restart of every map load (SV_SpawnServer), and the scripts compile at load
void ZmMods_f()
{
    if ( SV_Cmd_Argc() > 2 )
    {
        ZmUsage("zm_mods \"<mod mod ...>\" (quoted; \"\" for none; mods/admin is always added)");
        return;
    }
    if ( SV_Cmd_Argc() == 1 )
    {
        Com_Printf(0, "zm_mods: fs_mods is \"%s\"\n", Dvar_GetString("fs_mods"));
        return;
    }
    char mods[256];
    I_strncpyz(mods, SV_Cmd_Argv(1), sizeof(mods));
    Dvar_SetStringByName("fs_mods", mods);
    const char *map = Dvar_GetString("mapname");
    if ( com_sv_running->current.enabled && *map )
    {
        Com_Printf(0, "zm_admin: fs_mods \"%s\", %s loads again\n", mods, map);
        ZmLoadMap(map);
    }
    else
    {
        Com_Printf(0, "zm_admin: fs_mods \"%s\", used by the next map load\n", mods);
    }
}

void ZmAdmin_f()
{
    Com_Printf(0, "zm_admin: start points %d, start perks \"%s\", fs_mods \"%s\"\n", Dvar_GetInt("zm_startpoints"),
               Dvar_GetString("zm_startperks"), Dvar_GetString("fs_mods"));
    if ( !com_sv_running->current.enabled )
        return;
    for ( int i = 0; i < ZmMaxClients(); ++i )
    {
        const client_t *cl = &svs.clients[i];
        if ( cl->header.state < CS_CONNECTED )
            continue;
        const unsigned int on = s_state[i].on;
        const gentity_s *ent = &g_entities[i];
        Com_Printf(0, "  %2d %-24s %-10s health %4d%s%s%s%s\n", i, cl->name,
                   cl->header.state == CS_ACTIVE ? SV_ZmAdmin_GameState(i) : "loading", ent->client ? ent->health : 0,
                   on & ZM_GOD ? " god" : "", on & ZM_DEMIGOD ? " demigod" : "", on & ZM_NOTARGET ? " notarget" : "",
                   on & ZM_NOCLIP ? " noclip" : "");
    }
}

struct ZmCommandDef
{
    const char *name;
    void (__cdecl *function)();
    cmd_function_s console;
    cmd_function_s server;
};

ZmCommandDef s_commands[] = {
    { "zm_god", ZmGod_f },       { "zm_demigod", ZmDemigod_f }, { "zm_notarget", ZmNotarget_f },
    { "zm_noclip", ZmNoclip_f }, { "zm_points", ZmPoints_f },   { "zm_setpoints", ZmSetPoints_f },
    { "zm_perk", ZmPerk_f },     { "zm_revive", ZmRevive_f },   { "zm_respawn", ZmRespawn_f },
    { "zm_kill", ZmKill_f },     { "zm_down", ZmDown_f },       { "zm_round", ZmRound_f },
    { "zm_reset", ZmReset_f },   { "zm_map", ZmMap_f },         { "zm_mods", ZmMods_f },
    { "zm_power", ZmPower_f },   { "zm_admin", ZmAdmin_f },
};

// hands the player's oldest queued command to mods/admin's "zm_admin" waittill (one per player and frame: a second
// notify in the same frame would find the script thread still busy with the first)
void ZmDeliver()
{
    bool served[ZM_MAX_CLIENTS] = {};
    int kept = 0;
    for ( int q = 0; q < s_queueCount; ++q )
    {
        const ZmAdminCommand c = s_queue[q];
        if ( c.client >= ZmMaxClients() || svs.clients[c.client].header.state < CS_CONNECTED )
        {
            Com_Printf(0, "zm_admin: player %d left, '%s' dropped\n", c.client, c.cmd);
            continue;
        }
        const client_t *cl = &svs.clients[c.client];
        gentity_s *ent = &g_entities[c.client];
        if ( served[c.client] || cl->header.state < CS_ACTIVE || !ent->client )
        {
            s_queue[kept++] = c;
            continue;
        }
        served[c.client] = true;
        const unsigned int notify = SL_GetString((char *)"zm_admin", 0, SCRIPTINSTANCE_SERVER);
        // pushed last to first: waittill( "zm_admin", cmd, arg ) takes the last value pushed first
        Scr_AddString(c.arg, SCRIPTINSTANCE_SERVER);
        Scr_AddString(c.cmd, SCRIPTINSTANCE_SERVER);
        Scr_Notify(ent, (unsigned short)notify, 2u);
        SL_RemoveRefToString(SCRIPTINSTANCE_SERVER, notify);
    }
    s_queueCount = kept;
}
} // namespace

void SV_ZmAdmin_AddCommands()
{
    static bool added;
    if ( added || !Sys_IsDedicatedZombiesServer() )
        return;
    added = true;
    _Dvar_RegisterInt("zm_startpoints", 0, 0, 100000000, 0,
                      "zm_admin: points every player starts with (mods/admin); 0 keeps the game's own");
    _Dvar_RegisterString("zm_startperks", "", 0,
                         "zm_admin: perks every player starts with (mods/admin), e.g. \"jugg revive\" or \"all\"");
    // as the operator commands (SV_AddOperatorCommands): typed on the main thread, run on the server thread
    for ( ZmCommandDef &c : s_commands )
    {
        Cmd_AddCommandInternal(c.name, Cbuf_AddServerText_f, &c.console);
        Cmd_AddServerCommandInternal(c.name, c.function, &c.server);
    }
}

void SV_ZmAdmin_Frame(bool afterFrame)
{
    if ( !Sys_IsDedicatedZombiesServer() || sv.state != SS_GAME || level.initializing
        || !com_sv_running->current.enabled )
        return;
    // before and after G_RunFrame: a spawn inside the frame rebuilds the entity's flags (G_SP_HeadlessTestClientFrame)
    for ( int i = 0; i < ZmMaxClients(); ++i )
    {
        ZmAdminState &s = s_state[i];
        if ( svs.clients[i].header.state < CS_CONNECTED )
        {
            s = ZmAdminState();
            continue;
        }
        gentity_s *ent = &g_entities[i];
        if ( !ent->client )
            continue;
        ent->flags = (ent->flags & ~(s.clearOnce & ZM_ENT_FLAGS)) | (s.on & ZM_ENT_FLAGS);
        if ( s.clearOnce & ZM_NOCLIP )
            ent->client->flags &= ~1u;
        if ( s.on & ZM_NOCLIP )
            ent->client->flags |= 1u;
        s.clearOnce = 0;
    }
    if ( !afterFrame )
        ZmDeliver();
}

const char *SV_ZmAdmin_GameState(int client)
{
    if ( client < 0 || client >= ZmMaxClients() || !g_entities[client].client )
        return "";
    const gclient_s *gc = g_entities[client].client;
    switch ( gc->sess.sessionState )
    {
    case SESS_STATE_PLAYING:
        if ( g_entities[client].health <= 0 )
            return "dead";
        return gc->lastStand ? "down" : "playing";
    case SESS_STATE_DEAD:
        return "dead";
    case SESS_STATE_SPECTATOR:
        return "spectator";
    default:
        return "intermission";
    }
}

void SV_ZmAdmin_Flags(int client, char *out, int size)
{
    const unsigned int on = client >= 0 && client < ZM_MAX_CLIENTS ? s_state[client].on : 0;
    // the TUI calls this from the main thread: no va(), whose buffers the server thread shares
    snprintf(out, size, "%s%s%s%s", on & ZM_GOD ? "G" : "", on & ZM_DEMIGOD ? "D" : "", on & ZM_NOTARGET ? "N" : "",
             on & ZM_NOCLIP ? "C" : "");
}

void SV_ZmAdmin_LogLine(const char *line)
{
    if ( Sys_IsDedicatedZombiesServer() && !strncmp(line, "admin: ", 7) )
        Com_Printf(0, "%s", line);
}
