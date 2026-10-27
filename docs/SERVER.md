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

### Settings and their order

1. Server defaults: `bo1_zombies 1`, `sv_maxclients 4`.
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

Put a `launch.cfg` next to the client program (`BO1Zombies.exe`, or the Linux `BO1Zombies`). Its text is added before
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

## Not done yet

The curses console, admin commands (god, revive, points, perks, reset, mods), client logs sent to the server, joining
a game in progress and more than 4 players: see the plan in `docs/PORTING.md`, phase 8.
