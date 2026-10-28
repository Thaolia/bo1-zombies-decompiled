# admin - the dedicated server's admin commands (NOT part of the original game)

The script side of the `zm_*` console commands of the dedicated zombies server (`+set bo1_server 1`,
`docs/SERVER.md`, "Admin commands"). The server always stacks this mod on `fs_mods`, on every map
(`src/universal/com_files.cpp`, `FS_ModStackNames`); a normal game never loads it.

## How it works

- The engine's commands (`src/server_mp/sv_zm_admin.cpp`) check their arguments, pick the target players and queue
  the work. Once per server frame and player they notify `"zm_admin", <command>, <argument>` on the player's entity;
  `maps/admin/_admin.gsc` waits for it on every player and runs the command in a thread of its own.
- god, demigod, notarget and noclip stay in the engine: they are the entity / client flags of the retail cheats
  (`Cmd_God_f`, ...), set again around every server frame.
- Every command calls the retail scripts' own functions:
  - points: `maps\_zombiemode_score::add_to_player_score`, or `.score` and `set_player_score_hud`;
  - perks: `maps\_zombiemode_perks::give_perk` (free, as `give_random_perk`), only for the perks this map has a
    machine for;
  - revive: `maps\_laststand::auto_revive` (the solo Quick Revive path); the player's own revive count is kept;
  - kill: what `maps\_zombiemode` does to a player outside the playable area (no life left, down, bled out at once);
  - down: `DoDamage( health + 1000 )`, the game's own last stand;
  - respawn: `[[level.spawnPlayer]]` with `maps\_zombiemode::spectator_respawn`, as `spectators_respawn` between rounds;
  - round: the devgui's `zombie_devgui_goto_round`, as mods/sandbox does it;
  - power: Kino's `use_elec_switch` trigger notified as a player's use (`electric_switch`), else the `power_on` flag.
- Each reply is a `LogPrint` line starting with `admin: `; the server also prints those to its console (and so to its
  curses console).

## Players who join later (join in progress)

The game sets its players up when `all_players_connected` is set (`maps\_load_common`), which on a dedicated server is
as soon as the players connected at that time are in. `_admin.gsc` replaces `level.spawnClient`
(`maps\_callbackglobal::Callback_PlayerConnect` runs it once per connection). It keeps the retail one for the players
the game set up, that is those with a start point. A player without one is joining in progress, and mods/admin:

- gives them a start point (`spectator_respawn`, the struct the end-of-round respawn spawns at);
- spawns them by `zm_jip_mode`:
  - `immediate`: through the retail `maps\_zombiemode::spectator_respawn`, near the team. The game's own
    `spawnPlayer` would leave them where they connected;
  - `nextround`: as a spectator through `maps\_zombiemode::spawnSpectator`, as after a bleed-out, until the
    end-of-round respawn;
- frees their controls once the intro is over. `onPlayerSpawned` freezes every player at their first spawn, and only
  the intro's `fade_in` frees them;
- gives them the start points of the current player count, at least 1500 after round 6 as `spectators_respawn` does.

It also defines `zombie_score_start_<n>p` / `zombie_team_score_start_<n>p` for 1 to 4 players: the power-up drop reads
them for the current count. A round that started with one player and ends with more gets the retail
`maps\_zombiemode::spectators_respawn`, which `round_think` skips then. Each down, bleed-out and revive is logged.

## Dvars

- `zm_startpoints <n>`: every player's points once they first spawn on a map (0, the default, keeps the game's own).
- `zm_startperks "<perks>"`: perks every player gets at that time, e.g. `"jugg revive"` or `"all"`.
- `zm_jip_mode immediate|nextround`: a player who joins a game in progress plays at once (the default) or spectates
  until the round ends.
- `zm_debug_revive 1`: while a player is down, logs each test of `maps\_laststand::can_revive` / `is_reviving` for
  every other player whenever the result changes.

Perk names: `jugg`, `revive`, `speed`, `doubletap`, `staminup`, `phd`, `deadshot`, `mulekick`, or the game's own
`specialty_...` name.
