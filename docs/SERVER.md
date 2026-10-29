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
  `build\server`: put `server.cfg` in `build\server\main\`. Game files or zones that live elsewhere (another
  worktree) are given with two variables, as for `Server.sh`; `Play.bat` reads the same ones:

      set BO1_INSTALL=C:\...\BO-Zombies_switch\build\Release
      set BO1_ZONES=C:\...\BO-Zombies_switch_new\build\zones64
      Server.bat zombie_cosmodrome 3074
- `Server.sh` takes its paths from the environment: `BO1_EXE` (default `build/linux64-clang/Release/BO1Zombies`),
  `BO1_INSTALL` (game files, default `build/Release`), `BO1_ZONES` (`zone/` and `shaders.pack`, default
  `build/zones64`) and `BO1_SERVER` (its folder, default `build/server-linux`). It copies the program into that folder,
  links `zone`, `shaders.pack` and `localization.txt` there, and runs it from there. Keep the game files and zones on a
  Linux file system: read from `/mnt/c` under WSL a map takes minutes to load.

      BO1_EXE=~/build/bo1-server/build/Release/BO1Zombies BO1_INSTALL=~/bo1-run/install \
      BO1_ZONES=~/bo1-run/data ./Server.sh

### Maps

The map is `Server.bat`'s first argument, `+devmap <map>`, or `zm_map <map>` while the server runs. Every map needs
its zone converted by `tools/ffconv` (`<zones>/zone/Common/<map>.ff` and `<map>_patch.ff`).

| map | name | on the dedicated server |
| --- | --- | --- |
| `zombie_theater` | Kino der Toten | played (phases 0-4) |
| `zombie_pentagon` | Five | not run yet |
| `zombie_cosmodrome` | Ascension | loads, admin mod loaded (2026-10-27); not played yet |
| `zombie_coast` | Call of the Dead | not run yet |
| `zombie_temple` | Shangri-La | not run yet |
| `zombie_moon` | Moon | not run yet |
| `zombie_cod5_prototype` | Nacht der Untoten | not run yet |
| `zombie_cod5_asylum` | Verrückt | not run yet |
| `zombie_cod5_sumpf` | Shi No Numa | not run yet |
| `zombie_cod5_factory` | Der Riese | not run yet |
| `zombietron` | Dead Ops Arcade | not run yet (another game mode) |

### The console

In a terminal (Linux) or with its own console window (Windows), the server shows a curses console:

    BO1 Zombies server | BO1 Linux TUI test | zombie_theater | 1/4 players | port 29070 | up 0:01:50
      #   name                     address                 ping  state        points admin
    > 0   Player                   172.23.0.1:29060          31  playing        500 G
      1   -
    -- console ----------------------------------------------------------------------
    ...the console log...
     F2/F3 player  F4 god  F5 revive  F6 respawn  F7 points  F8 kick  F10 reset  Tab player's log  PgUp/PgDn log ...
    > status

- The command line takes any console command (`status`, `map zombie_cosmodrome`, `set g_password x`, `quit`, ...);
  Up/Down recall the last 100.
- state: `connected` / `loading`, then, in the game, `playing`, `down` (last stand), `dead` or `spectator`. admin:
  the `zm_*` states on (G god, D demigod, N notarget, C noclip).
- F2 / F3 select the previous / next player. F4 writes `zm_god <player>`, F5 `zm_revive <player>`, F6
  `zm_respawn <player>`, F7 `zm_points <player> ` (type the amount), F8 `clientkick <player>` and F10 `zm_reset` on
  the command line: nothing runs until Enter (Esc clears it). Tab shows the selected player's own log ("Client logs").
- PgUp / PgDn scroll the log (5000 lines kept), End goes back to the newest line. Everything also goes to
  `<fs_h>/main/console_mp.log`.
- `+set bo1_tui 0` keeps the plain console (stdout and stdin), and so does a server whose output is not a terminal
  (`nohup`, a service, a redirection).

### Settings and their order

1. Server defaults: `bo1_zombies 1`, `sv_maxclients 4` (up to 32: "More than 4 players"), `g_inactivity 0` (the
   dedicated configs' 120 s idle kick is multiplayer's; zombies has none. `set g_inactivity 120` brings it back).
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
did not reach the game, up to 60 s. `set cl_autoConnect ""` in the console stops it.

## Joining from a Switch

In the start menu, **Serveur** (A) opens the server screen:
- **Connexion**: on to join a server, off for a solo game;
- **Adresse IP** and **Port** (3074 by default): typed with the console's keyboard;
- **Nom du joueur**: the name on the server; empty keeps the game's own;
- the console's own network address, or a warning when it has none (Wi-Fi off, airplane mode).

Start then writes `+set name <name> +set cl_allowWanConnect 1 +set cl_autoConnect <address>:<port>` into
`sdmc:/switch/bo1zm/launch.cfg`, with no `+devmap`: the game opens its own menu and joins as a PC client does. The
server picks the map and god mode, so those menu lines are greyed out. The menu refuses to start with no address, or
with the connection on and no network. In game, a `connect` to a remote address without a network prints `connect: the
console has no network connection` and leaves the menu as it is.

A `skip_menu` boot reads `launch.cfg` as it is, so a hand-written one can join too; several lines read as one, as on
PC. The console opens its sockets at start-up (`socketInitializeDefault`, `nifm` for its address, both logged in
`boot.log` as `network:` lines) and closes them at exit.

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
them. Checked: a Linux server with a Windows client, and Windows with Windows. A Switch client: see `docs/PORTING.md`,
phase 8, step 2.

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
| `zm_revive <player>` | stands a downed player up (a dead player spectates: `zm_respawn` brings them back) |
| `zm_down <player>` | puts a player down without killing them (god goes off first): to test revives |
| `zm_kill <player>` | kills a player (god goes off first): no last stand, the player spectates until the round ends |
| `zm_respawn <player>` | brings a spectator back now instead of at the end of the round |
| `zm_round <n>` | goes to round `n` (between rounds: once the next round starts) |
| `zm_power` | turns the map's power on, as a player using the switch (Kino's `use_elec_switch`; a map with only the `power_on` flag gets the flag) |
| `zm_reset` | loads the current map again (a new game) |
| `zm_map <map>` | loads another map |
| `zm_mods "<mods>"` | sets `fs_mods` (quoted, `""` for none) and loads the current map again with it; alone, prints it |

Settings, in `server.cfg` or on the command line: `zm_startpoints <n>` (every player's points once they first spawn on
a map; 0 keeps the game's own), `zm_startperks "<perks>"` (perks every player gets then, e.g. `"jugg revive"`),
`zm_jip_mode immediate|nextround` (a player who joins a game in progress, below) and `zm_debug_revive 1` (while a player is down, logs `admin: revive debug: ...` lines with each test of the game's
revive for every other player: distance, alive, down, touching the revive trigger, facing, sight and bullet traces,
use button, the result).

The engine checks the arguments and prints `zm_admin: ...` at once; the script side (mods/admin, which the server
always adds to `fs_mods`) replies with `admin: ...` lines once it has acted, also written to `games_mp.log`. Output
of a command sent with rcon goes back to the rcon client, not to the console log. The server also always stacks
mods/zinfo: every player, PC or Switch, sees the zombies alive and left this round beside the Tab scoreboard;
`set zinfo 0` on the server hides it for everyone. `Server.bat` and `Server.sh` link
this repository's `mods/` into the server's folder, where the server reads it; started otherwise, put `mods/` there
(`<fs_h>/mods/admin`).

## Joining a game in progress

The game sets its players up once, as soon as the players connected at that time are in. A player who connects after
that is joining in progress: a slower client, one that dropped during a map load, or a friend who comes in at round
10. `zm_jip_mode` (an engine setting, read at each join) decides what happens:

- `immediate` (the default): the player plays at once. They spawn at a free spot near the other players, as the
  game's own respawn does, with the start points of the current player count (500 on the retail table), at least
  1500 after round 6 as the game's respawn gives, or `zm_startpoints` when set. They get the starting pistol and
  their character by player slot, as at the start of a game. The log says `admin: 1 (Bravo) joined in progress:
  plays now`, then `spawned at (...), controls free`.
- `nextround`: the player spectates, as a player who bled out, until the round ends, and then comes in with the
  others (`admin: ... spectates until the round ends`). `zm_respawn` brings them in sooner.

Without mods/admin, such a player stayed frozen for good: the game freezes each player at their first spawn, and only
its intro frees them. In round 1 they also spawned at the spot where they connected, not at a start point. From round
2 on, the respawn stopped with a script error, because the player had no start point. mods/admin also defines the
start points for every player count: without them the power-up drops stopped once a second player was in. A round
that started with one player and ends with more respawns its spectators: the game skips that case (`round_think`
counts the players at the start of the round).

`zm_admin` shows `frozen` next to a player whose controls the game holds. The log has a line for each down,
bleed-out and revive (`admin: 1 (Bravo) is down`).

Known limits: a Quick Revive given while only one player is in the game takes the solo path (an extra life, the
machine hidden), as a solo purchase would, and its retail thread then logs script errors once more players are in.
A player alone at the start gets a random character (the game's solo rule); a player who joins later and whose
slot's character is that one gets another (see "More than 4 players").

## Client logs

Every client (PC and Switch) sends its console log to the server it joins, so a player's log is on the server
without anyone copying files:

- The server listens on UDP **game port + 1** (3075 for 3074; open it in the firewall too) and writes each client run
  to `<fs_h>/logs/<name>_<date>-<time>_<session>.log` (`build\server\logs\` with `Server.bat`). The console says
  `netlog: <name> (<address>) -> logs/<file>` when a run starts.
- The log starts at the client's launch: a client keeps its lines (1 MiB) until it knows the server, so the lines
  from before the connection come too, then sends as they are written, also during loads and freezes (its own
  socket and thread, not the game connection). Its last lines go before it quits. The Switch sends its whole
  `boot.log` (start menu, deko3d lines included).
- Missing packets are marked in the file: `[netlog: lost N packets]`; a full client buffer:
  `[netlog: N bytes dropped, the buffer was full]`. The server accepts 2 MB/s per address (4 MB in a burst).
- In the curses console, **Tab** shows the selected player's log instead of the server's (PgUp / PgDn / End scroll
  it, Tab again goes back); its title gives the file and the packets lost.
- On the client: `bo1_netlog 0` stops it; `bo1_logserver <host[:port]>` sends to another server than the
  `cl_autoConnect` one (the game port: the log goes to port + 1).

## More than 4 players

The server takes 4 players by default. For more, set `sv_maxclients` in `server.cfg` or on the command line, before
the map loads (`set sv_maxclients 8`); the dedicated server accepts up to 32. The game itself is made for 4: 4
characters, 4 start points, start points and difficulty tables for 1 to 4 players. mods/admin gives the 5th player
on what the game gives the first 4:

- **Character**: the 5th player gets the character fewest players have (`admin: 4 (Echo) plays character 0 (the
  game has 4)`), with that character's model, arms and voice. A player of slots 1-3 whose character is already taken
  (the random character of a player who was alone at the start, or a 5th player's) gets one the same way. With more
  than 4 players, characters repeat. The log gives each player's character and model at their first spawn
  (`admin: 4 (Echo) is character 0, model c_usa_dempsey_body`).
- **Start point**: the map's start points are repeated up to `sv_maxclients` when the map loads (`admin: targetname
  initial_spawn_points: 4 structs, repeated to 8`), so a game where everyone is in at the start (after a game over or
  `zm_reset`) places every player; a 5th player starts on the 1st one's spot. The respawn at the end of a round is the
  game's own, near the team.
- **Points and difficulty**: past 4 players, the start points (500), the team points and the game's coop difficulty
  settings (damage taken, invulnerable time after a hit, enemy accuracy) are the 4-player ones. Without mods/admin,
  5 players at the start each got 3000 points, and `maps/_gameskill.gsc` stopped with script errors. The zombie
  count per round grows with the player count, as in the game (`zombie_ai_per_player`).
- **HUD**: the score list at the bottom right shows every player (5 rows seen on two of the clients), the 5th in
  the colour of the 1st slot. In the scoreboard, a name past the 4th takes the colour of the same slot of 4.
- **Console**: with more slots than fit in the top third of the terminal, the player list shows the connected players
  only, scrolled so that the selected one stays in view.

Checked with 5 headless win64 clients on Kino (`sv_maxclients 8`): joins one by one, a `zm_reset` with all 5 in, a
death, and the round-end respawn. Not measured yet: the bandwidth per client and the frame time with many players,
on a PC server or with Switch clients (`docs/PORTING.md`, phase 8 step 7).
