#include "sv_tui.h"

#include <server_mp/sv_main_mp.h>
#include <server/server.h>
#include <qcommon/common.h>
#include <qcommon/net_chan_mp.h>
#include <game_mp/g_main_mp.h>
#include <universal/dvar.h>
#include <universal/q_shared.h>
#include <win32/win_shared.h>

const char *Sys_ServerTuiFrame()
{
    static unsigned int startMs = Sys_Milliseconds();
    static ServerTuiStatus status;

    I_strncpyz(status.hostname, Dvar_GetString("sv_hostname"), sizeof(status.hostname));
    I_strncpyz(status.mapname, com_sv_running->current.enabled ? Dvar_GetString("mapname") : "",
               sizeof(status.mapname));
    status.port = Dvar_GetInt("net_port");
    // the server's slots (sv_maxclients), not the engine's client array (com_maxclients, 32 on a dedicated server)
    status.maxClients = Dvar_GetInt("sv_maxclients");
    if (status.maxClients > com_maxclients->current.integer)
        status.maxClients = com_maxclients->current.integer;
    if (status.maxClients > (int)ARRAY_COUNT(status.players))
        status.maxClients = ARRAY_COUNT(status.players);
    status.uptimeSeconds = (int)((Sys_Milliseconds() - startMs) / 1000);
    status.playerCount = 0;
    if (com_sv_running->current.enabled)
    {
        for (int i = 0; i < status.maxClients; ++i)
        {
            const client_t *cl = &svs.clients[i];
            if (cl->header.state < CS_CONNECTED)
                continue;
            ServerTuiPlayer &p = status.players[status.playerCount++];
            p.num = i;
            p.state = cl->header.state;
            p.ping = cl->ping;
            // the score lives in the game's client, which exists once the player is in the level
            p.score = cl->header.state == CS_ACTIVE ? G_GetClientScore(i) : 0;
            I_strncpyz(p.name, cl->name, sizeof(p.name));
            I_strncpyz(p.address, NET_AdrToString(cl->header.netchan.remoteAddress), sizeof(p.address));
        }
    }
    return ServerTui_Frame(status);
}
