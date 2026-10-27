# Dedicated zombies server

Status and design decisions: `docs/PORTING.md`, phase 8. This file is the operator's guide; it grows with each step.

## Starting a server

The server is the game's own executable started with `+set bo1_server 1` (`BO1Zombies.exe` on Windows, the
`linux64-clang` build on Linux). It needs the same files as a client: the install's `localization.txt` and iwds
(`fs_b`), and the converted 64-bit zones (`zone`, from `tools/ffconv`) next to the program.

    BO1Zombies.exe +set fs_b <install> +set fs_h <server folder> +set bo1_server 1 +devmap zombie_theater
    ./BO1Zombies   +set fs_b <install> +set fs_h <server folder> +set bo1_server 1 +devmap zombie_theater

`bo1_server` turns the process into a dedicated server (`dedicated 1`) with no window, no dialog and no sound; a fatal
error ends the process instead of showing a message. Every console line goes to `<fs_h>/main/console_mp.log`
(`logfile 2`).

### Start scripts

At the repository root, `Server.bat` (Windows) and `Server.sh` (Linux) start a server listening on every interface
(`net_ip 0.0.0.0`):

    Server.bat [map] [port] [more + commands]        defaults: zombie_theater 3074
    ./Server.sh [map] [port] [more + commands]
    Server.bat zombie_cosmodrome 3074 +set g_password secret

- `Server.bat` runs `build\win64-msvc\Release\BO1Zombies.exe` (the `win64-msvc` preset) in its own console window,
  on the game files of `build\Release` (`fs_b`, also its working folder: it holds `localization.txt`) and the zones of
  `build\zones64` (linked as `zone` next to the program, as `Play.bat x64` does). Its folder (`fs_h`) is
  `build\server`: put `server.cfg` in `build\server\main\`.
- `Server.sh` takes its paths from the environment: `BO1_EXE` (default `build/linux64-clang/Release/BO1Zombies`),
  `BO1_INSTALL` (game files, default `build/Release`), `BO1_ZONES` (`zone/` and `shaders.pack`, default
  `build/zones64`) and `BO1_SERVER` (its folder, default `build/server-linux`). It copies the program into that folder,
  links `zone`, `shaders.pack` and `localization.txt` there, and runs it from there. Keep the game files and zones on a
  Linux file system: read from `/mnt/c` under WSL a map takes minutes to load.

      BO1_EXE=~/build/bo1-server/build/Release/BO1Zombies BO1_INSTALL=~/bo1-run/install \
      BO1_ZONES=~/bo1-run/data ./Server.sh

### The console

In a terminal (Linux) or with its own console window (Windows), the server shows a curses console:

    BO1 Zombies server | BO1 Linux TUI test | zombie_theater | 1/4 players | port 29070 | up 0:01:50
      #   name                     address                 ping  state        points admin
    > 0   Player                   172.23.0.1:29060          31  playing        500 G
      1   -
    -- console ----------------------------------------------------------------------
    ...the console log...
     F2/F3 player  F4 god  F5 revive  F6 respawn  F7 points  F8 kick  F10 reset  PgUp/PgDn log  Up/Down history ...
    > status

- The command line takes any console command (`status`, `map zombie_cosmodrome`, `set g_password x`, `quit`, ...);
  Up/Down recall the last 100.
- state: `connected` / `loading`, then, in the game, `playing`, `down` (last stand), `dead` or `spectator`. admin:
  the `zm_*` states on (G god, D demigod, N notarget, C noclip).
- F2 / F3 select the previous / next player. F4 writes `zm_god <player>`, F5 `zm_revive <player>`, F6
  `zm_respawn <player>`, F7 `zm_points <player> ` (type the amount), F8 `clientkick <player>` and F10 `zm_reset` on
  the command line: nothing runs until Enter (Esc clears it).
- PgUp / PgDn scroll the log (5000 lines kept), End goes back to the newest line. Everything also goes to
  `<fs_h>/main/console_mp.log`.
- `+set bo1_tui 0` keeps the plain console (stdout and stdin), and so does a server whose output is not a terminal
  (`nohup`, a service, a redirection).

### Settings and their order

1. Server defaults: `bo1_zombies 1`, `sv_maxclients 4`, `g_inactivity 0` (the dedicated configs' 120 s idle kick is
   multiplayer's; zombies has none. `set g_inactivity 120` brings it back).
2. The command line's `+set` values, which override the defaults.
3. `server.cfg`, run with `exec` from the game's search path (put it in `<fs_h>/main/`). An `exec` runs after every
   startup `+set`, so its values override both. A missing `server.cfg` only prints an error.
4. The command line's other commands (`+devmap <map>`, ...), in order.

Load the map either from `server.cfg` or from the command line, not from both (each would load it). A
`server.cfg` example:

    set sv_hostname "Zombies at home"
    set g_password ""          // non-empty: clients need "set password <it>" before connecting
    set rcon_password ""       // non-empty: enables rcon
    devmap zombie_theater

### Network

- The server listens on UDP port `net_port` (default 3074; the next free one up to +9 if it is taken: the console log
  says `Opening IP socket: ...`) on every interface. `+set net_ip <address>` binds one interface only.
- Open that UDP port in the firewall of the server's machine. On Windows the first run does not show a firewall
  prompt (the server never opens a window): add an inbound UDP rule for the port or the program yourself.
- Clients connect with `connect <server address>:<port>` from the console, or by themselves with `cl_autoConnect`
  (below). A client accepts LAN addresses only (10/8, 172.16/12, 192.168/16, 169.254/16 and its own /24) unless
  `cl_allowWanConnect 1` is set; a refused address prints `connect: ... is not a LAN address` and changes nothing.

## Joining from a client (PC)

On Windows, `Play.bat net <address[:port]> [name]` (or `Launch.bat net ...`, the same script) starts the 64-bit
client (`Play.bat x64`) with `cl_autoConnect <address>` and `name <name>` (the player's name on the server, no
spaces):

    Launch.bat net 172.23.3.156:3074 Player2

Unlike the other `Play.bat` modes it keeps `net_ip` at its default: a client bound to `127.0.0.1` cannot reach a LAN
address.

Otherwise put a `launch.cfg` next to the client program (`BO1Zombies.exe`, or the Linux `BO1Zombies`). Its text is added before
the command line (whose values win), several lines read as one, with no comments:

    +set cl_autoConnect 192.168.1.20:3074

Add `+set cl_allowWanConnect 1` for a server beyond the LAN.

`cl_autoConnect <address>` joins that server once the main menu is up (a `+connect` on the command line would be
undone by the menu's own load), and again whenever the client is back at the menu or disconnected: server restarted,
timeout (`cl_timeout`, 40 s by default), kick. It waits 2 s the first time, then 5 s, doubled after each attempt that
did not reach the game, up to 60 s. `set cl_autoConnect ""` in the console stops it. The Switch reads
`sdmc:/switch/bo1zm/launch.cfg` already; joining a server from the Switch comes later (its network start-up is not done).

## Checking a server

`tools/server_status.py <address>[:port]` sends the `getstatus` query and prints the server name, map, player slots
and, per connected player, the points, ping and state (`connected`, `loading`, `active`):

    $ python3 tools/server_status.py 192.168.1.194:29060
    sv_hostname: BO1 Zombies test server
    mapname: zombie_theater
    g_gametype: tdm
    sv_maxclients: 4
    players: 1
      #0 Player: 500 points, ping 51, active

Any tool can send it: the packet is `\xff\xff\xff\xffgetstatus`, the reply `\xff\xff\xff\xffstatusResponse\n`, the
serverinfo string, then one line per client: `<number> <points> <ping> <state> "<name>"`. The server answers at most
one query every 50 ms.

## Platforms

A server and its clients may run on different platforms: the network messages are encoded the same way on all of
them. Checked: a Linux server with a Windows client, and Windows with Windows. Switch clients come with step 2.

## Admin commands

Typed in the server's console, run from `server.cfg`, or sent over rcon once the server has a `rcon_password`:
`tools/server_rcon.py <address>[:port] <password> zm_god all 1`. `<player>` is a slot number, a player's name, or
`all`.

| command | what it does |
| --- | --- |
| `zm_admin` | lists the players (state, health, the states below) and the start settings |
| `zm_god <player> [0\|1]` | god mode (no damage); without a value it toggles. Kept until switched off or the player leaves, across map loads |
| `zm_demigod`, `zm_notarget`, `zm_noclip` | the same for the other retail cheats: damage without dying, ignored by zombies, flying through walls |
| `zm_points <player> <amount>` | gives points (a negative amount takes them, down to 0) |
| `zm_setpoints <player> <amount>` | sets the points |
| `zm_perk <player> <perk ...\|all>` | gives perks for free: `jugg`, `revive`, `speed`, `doubletap`, `staminup`, `phd`, `deadshot`, `mulekick` or a `specialty_` name; only the perks this map has a machine for |
| `zm_revive <player>` | stands a downed player up |
| `zm_kill <player>` | kills a player (god goes off first): the player spectates until the round ends |
| `zm_respawn <player>` | brings a spectator back now instead of at the end of the round |
| `zm_round <n>` | goes to round `n` (between rounds: once the next round starts) |
| `zm_reset` | loads the current map again (a new game) |
| `zm_map <map>` | loads another map |
| `zm_mods "<mods>"` | sets `fs_mods` (quoted, `""` for none) and loads the current map again with it; alone, prints it |

Settings, in `server.cfg` or on the command line: `zm_startpoints <n>` (every player's points once they first spawn on
a map; 0 keeps the game's own) and `zm_startperks "<perks>"` (perks every player gets then, e.g. `"jugg revive"`).

The engine checks the arguments and prints `zm_admin: ...` at once; the script side (mods/admin, which the server
always adds to `fs_mods`) replies with `admin: ...` lines once it has acted, also written to `games_mp.log`. Output
of a command sent with rcon goes back to the rcon client, not to the console log. `Server.bat` and `Server.sh` link
this repository's `mods/` into the server's folder, where the server reads it; started otherwise, put `mods/` there
(`<fs_h>/mods/admin`).

Known limits: a Quick Revive given while only one player is in the game takes the solo path (an extra life, the
machine hidden), as a solo purchase would, and its retail thread then logs script errors once more players are in;
a player who joins a game in progress spectates until the round ends unless `zm_respawn` brings them in (step 6).

## Not done yet

Client logs sent to the server, joining a game in progress and more than 4 players: see the plan in
`docs/PORTING.md`, phase 8.
