# ZmClient - launcher for the BO1 zombies client (PC)

A curses program (Python 3, standard library only; `windows-curses` on Windows), built like ZmAdmin
(`tools/zmadmin`) and using its package as a library (widgets, the `getstatus` query, the local server). It sits in a
`make_dist.sh` package folder, next to `BO1Zombies` / `BO1Zombies.exe`, and starts the game three ways:

- **Solo** (offline): a map, the solo mods (horde, nightmare/hell, noperks, mapkit maps, zinfo, quick restart) and god
  mode. The mod flags are those of `tools/mod-launcher.hta`, rule for rule;
- **Join a server**: a saved favourite or a server found on the LAN, with its player password (`g_password`);
- **Local server + join it**: starts the folder's dedicated server as ZmAdmin does (same state file and rcon password,
  so ZmAdmin administers it), then the game, which joins it once its map is loaded.

It also edits the player's `players/config.cfg`, adds free dvars to the command line, and installs the game files
from the player's own Black Ops (as `setup.ps1` does for the development tree). The texts it shows are in French, the
players' language.

    python3 tools/zmclient/main.py --dist build/dist/BO1Zombies_linux   # the interface, from the sources
    ./zmclient                                                          # the binary, in a package folder
    ./zmclient --print-cmd        # the command line of the saved profile
    ./zmclient --play             # start the game with the saved profile (solo or join), then exit
    ./zmclient --setup [DIR]      # install the game files from DIR (default: the saved or detected install)

`--dist DIR` the package folder (default: its own folder, then the current one), `--config DIR` its settings folder
(`profile.json`; `%APPDATA%\zmclient` or `~/.config/zmclient`), `--admin-config DIR` ZmAdmin's settings folder (where
the local server's rcon password lives), `--no-mouse` keyboard only.

## Views

| key | view | what it does |
| --- | --- | --- |
| F1 | Jouer | the mode, a summary, Launch / Stop the game / Stop the local server, the state of the game files, and the exact command line |
| F2 | Serveurs | favourites and LAN servers with their live status (name, map, players, ping); add, edit, remove, join |
| F3 | Solo | map (base maps, Dead Ops Arcade, the package's mapkit layouts, Black Ops II's Nuketown and TranZit when `make_dist.sh --bo2` packaged them; TranZit also loads Ascension's zone for Stamin-Up), god mode, zinfo, noperks, quick restart, start points, power and perks (mods/admin's `zm_startpoints` / `zm_startpower` / `zm_startperks`, stacked on the line's `fs_mods`), horde settings, difficulty; rows a map does not use are greyed with the reason |
| F4 | Lancement | player name, window mode and size, the port's FPS counter, the models drawn per frame (1024, 2048 or 4096: `r_mod_sceneModels` with a model lighting pool to match, for maps imported by MapKit), gamepad aim assist (each "game setting" / on / off), the local server's map, port and rcon password |
| F5 | Config du jeu | a form of `players/config.cfg`: mouse, gamepad, FOV, resolution, vsync, FPS cap, gamma, MSAA, anisotropy, texture quality, shadows, depth of field, blood, subtitles; any other dvar; key binds |
| F6 | Dvars libres | free command lines, each one a `+line` before the map or the connection, enabled one by one |
| F7 | Installation | the Black Ops install (found in Steam's libraries, picked in a folder browser, or typed), the state of each part, Install, progress |
| F8 | Journal | the running game's `console_mp.log`, live, with a filter |

Mouse and keyboard work as in ZmAdmin: a click picks, a double click edits or runs, the wheel scrolls; `?` (or F9)
explains the view, each row's help shows below the list.

## The command line

The base is `Play.bat` / `play.sh`'s: `bo1_zombies 1`, `fs_b` and `fs_h` the folder, `logfile 2`, plus
`bo1_mod_sharedconfig 1` (one `players/config.cfg` for every mod, as the `.hta` does). Solo adds `net_ip 127.0.0.1`
(nobody else can join, no firewall prompt) and ends with `+devmap <map>`; the two network modes end with
`cl_allowWanConnect 1` and `cl_autoConnect <address>` (it retries while a local server is still loading), and never
set god mode or mods: the server decides.

Two engine limits are checked before a launch: `Com_ParseCommandLine` cuts the line at **every** `+` (so no value
may contain one, the folder's path included) and keeps 64 pieces, the text before the first `+` being one: ZmClient
refuses a line of more than 63 commands, counting those of a `launch.cfg` next to the program, which the engine puts
first.

## config.cfg

The game writes `players/config.cfg` when it quits, so ZmClient refuses to save while the game runs. Edits are
marked `*` and written with **Sauvegarder** (Ctrl+S): an existing `seta` or `bind` line is rewritten in place, a new
one goes after the last of its kind, every other line is kept, and the file keeps its line ends. `bo1_mod_fov` is
also written to `players/bo1_shared.cfg`, which the engine reads after `config.cfg`. "Qualité des textures" writes
`r_picmip_manual` and the `r_picmip*` family together (`r_picmip` alone is read-only while `r_picmip_manual` is 0).

## Installing the game files

The install is found in Steam's libraries, typed, or picked in a folder browser (`dirbrowser.py`): arrows and Enter go
into a folder, Backspace or Left up to its parent (on Windows, above a drive's root, the list of drives), a letter
jumps to the next folder starting with it, Space picks the folder shown. Each folder that is a Black Ops install is
marked, and the browser opens on the chosen install, else on a Steam library's `steamapps/common`. Hidden and system
folders (`.*`, `$*`, `System Volume Information`) are left out; the install marks are not looked up in a folder of
more than 300 subfolders.

From the player's Call of Duty: Black Ops install (Steam's `libraryfolders.vdf` is read to find it), the Installation
view copies `localization.txt`, `main/*.iwd` and, optionally, `main/video/`, then converts the zombies zones with the
package's `ffconv.exe --verify --game <install> --out <temporary folder>`. The new `zone/` (and `shaders.pack`, which
the Linux GL renderer reads) replace the old ones only when ffconv succeeded. A file already copied (same size and
date) is skipped. `zone/zmclient_setup.json` records the ffconv that made the zones: a package with another ffconv
shows them as to be converted again. The install is only read. The log is `main/zmclient_setup.log`.

ffconv is a Windows program: on Linux ZmClient runs it through WSL's interop when it runs in WSL, else with `wine`
(paths through `wslpath -w` / `winepath -w`), or asks to convert on a Windows PC and copy `zone/` and `shaders.pack`.
An install without the DLCs has only Kino, Five and Dead Ops: ZmClient marks the other maps "non installée" (no
`zone/*/<map>.ff`) and refuses to launch them, solo or as the local server's map. `tools/make_dist.sh --no-game-data` makes packages
(`BO1Zombies_<platform>_nodata/`) with no game file at all, for players who install from their own copy.

## Building (Nuitka)

    tools/zmclient/build.sh [linux] [windows]     # build/zmclient/linux/zmclient, build/zmclient/windows/zmclient.exe

As ZmAdmin's `build.sh`, with ZmAdmin's package compiled in. `tools/make_dist.sh` rebuilds it when a `.py` file of
`tools/zmclient` or `tools/zmadmin/zmadmin`, or this `build.sh`, is newer than the binary.

## Tests

    python3 -m unittest discover -s tests/zmclient -v

The solo flags are compared with `tools/mod-launcher.hta`'s own `refresh()` and `args()`, run under node with a
stubbed DOM (`tests/zmclient/hta_args.js`), on every map of the `.hta` and several mod combinations; the horde and
nightmare flags with `horde_flags.js` and `nightmare_flags.js` on a matrix of inputs. The other tests cover the
command line of each mode and its refusals, the profile file, `config.cfg` rewriting, the server browser against fake
servers (broadcast, favourites, the one query per round), the installation with a fake install and a fake ffconv
(success, failure keeping the old zones, a new ffconv), and the client process (start, found again, stop).
