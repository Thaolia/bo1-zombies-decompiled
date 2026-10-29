# ZmAdmin - admin console for the BO1 zombies dedicated server

A curses program (Python 3, standard library only; `windows-curses` on Windows) that drives a dedicated zombies
server (`docs/SERVER.md`) over its UDP queries: `getstatus` for the players, rcon for every command. The texts it
shows are in French, the players' language.

    python3 tools/zmadmin/main.py                          # the interface
    python3 tools/zmadmin/main.py 192.168.1.20 -p secret   # connect at once (port 3074 unless host:port)
    python3 tools/zmadmin/main.py --status 192.168.1.20    # status only, no curses, no password
    python3 tools/zmadmin/main.py --rcon zm_admin 192.168.1.20 -p secret

`--admin-poll SECONDS` sets how often the Players and Game views read `zm_admin` (5 by default, 0 never),
`--dist DIR` the folder whose server it may start, `--config DIR` its settings folder, `--no-mouse` keyboard only.

## Views

| key | view | what it does |
| --- | --- | --- |
| F1 | Servers | saved servers (`servers.json`), and the local server when ZmAdmin sits in a `make_dist.sh` folder: start it (map, port, rcon password), connect, stop |
| F2 | Players | each slot: state, health, points, ping, god/demigod/notarget/noclip/frozen; one key per `zm_*` action on the selected player, `a` for all |
| F3 | Game | map, mods, start settings; go to a round, power on, reset, change map, change mods |
| F4 | Settings | a form of the live settings (`sv_hostname`, `g_password`, `rcon_password`, `zm_startpoints`, `zm_startperks`, `zm_jip_mode`, `zm_debug_revive`, `g_inactivity`, `sv_maxclients`, `fs_mods`) |
| F5 | Console | any command over rcon, with its reply and a saved history |
| F6 | Log | the local server's `console_mp.log`, live, with a filter |

**Mouse or keyboard.** A click picks a tab, a row, a button or an action of the Players legend, a double click opens
or runs the row (connect, launch, edit a setting, run a game action), the wheel scrolls; each dialog has its buttons
(`[ OK ]`, `[ Annuler ]`, ...) and the message line `[ Aide ]` / `[ Quitter ]`. Clicks act on release (a slow click
never acts twice). The terminal then keeps its own text selection behind Shift; `--no-mouse` leaves the mouse to it.

`?` (or F9) explains every key of the view; the Settings view explains the selected setting below the form (what it
does, its values and default, when it takes effect).

**Settings are saved with a button.** An edited line is marked `*` and nothing is sent until **[ Sauvegarder ]**
(or Ctrl+S): it lists the commands, sends the `set`s in one rcon command (`a;b;c`), reads every edited dvar back and
marks each line applied or refused, with the server's output. `rcon_password` goes last, and ZmAdmin switches to the
new one once the server answered. `sv_maxclients` takes effect at the next map load (Save offers `zm_reset`);
`fs_mods` is applied with `zm_mods`, which reloads the map. For the local server, "Écrire aussi dans server.cfg"
writes the applied values into `server/main/server.cfg` too (an existing `set` line is rewritten in place, a new one
goes before the first map load, other lines are kept).

## The server's limits it works with

- **One rcon command every 500 ms**, all senders together (`SVC_RemoteCommand`): a command sent sooner is dropped
  without a reply. ZmAdmin spaces its commands by 550 ms, retries once on silence, and shows how many wait. Several
  commands in one rcon (`;`) count as one.
- **rcon returns only the command's immediate output.** The `admin: ...` replies of mods/admin come one frame later,
  in the server's log: ZmAdmin shows them only for the local server, whose `console_mp.log` it follows. For a remote
  server, the Players view shows the result at the next `zm_admin` read.
- **Every rcon command is logged by the server** (`Rcon from ...`): the `zm_admin` reads add one line every few
  seconds while the Players or Game view is open (`--admin-poll` to change or stop them).

## The local server

ZmAdmin finds a `make_dist.sh` folder (`BO1Zombies` or `BO1Zombies.exe`, `server.sh` or `Server.bat`, `zone/`) in
its own folder or the current one. It starts the server as that folder's script does (`fs_b` the folder, `fs_h`
`server/`, `bo1_server 1`, `net_ip 0.0.0.0`, `+devmap <map>`), with `bo1_tui 0` (no window; output in
`server/main/zmadmin_server_output.log`) and the rcon password twice: `+set rcon_password` is applied before
`server.cfg` runs, and the `+rcon_password <pw>` command after it, so a `server.cfg` password never locks ZmAdmin out.
The password is generated once per folder and kept in `servers.json`; the PID, port and map in
`server/main/zmadmin_server.json`, so a later ZmAdmin finds a server that kept running (it checks the PID's program
is that folder's). Quitting asks whether to stop it; a stop sends `quit` and kills the process after 15 s.
On Windows the server runs without a window (`CREATE_NO_WINDOW`); the first start from a new folder showed Windows'
firewall question for `BO1Zombies.exe`: allow it for players on other machines.

## Building (Nuitka)

    tools/zmadmin/build.sh [linux] [windows]      # build/zmadmin/linux/zmadmin, build/zmadmin/windows/zmadmin.exe

One file per platform, with Python inside. Linux: a venv and a copy of the sources on ext4 (`$ZMADMIN_WORK`, default
`~/.cache/zmadmin`), gcc and patchelf. Windows, from WSL: a venv of the Windows Python 3.13 in
`build\zmadmin\venv-win` with `nuitka` and `windows-curses`, compiled with MSVC. `tools/make_dist.sh` runs it when a
`.py` file or `build.sh` is newer than the binary, and puts the binaries in `BO1Zombies_linux/` and
`BO1Zombies_windows/`. The Linux binary needs the glibc of the machine that built it (2.38 here; the game itself
needs 2.43).

## Tests

    python3 -m unittest discover -s tests/zmadmin -v

A fake UDP server answers as the real one does (formats checked on a win64 server): getstatus, rcon replies split
over several packets, `a;b` batches, the 500 ms limit, a refused password. The tests cover the parsers, the Settings
form and its commands, `server.cfg` rewriting, the network thread (events, offline then back), the log follower
(truncation) and the launcher's command line.
