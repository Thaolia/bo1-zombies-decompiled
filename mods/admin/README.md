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
  - respawn: `[[level.spawnPlayer]]` with `maps\_zombiemode::spectator_respawn`, as `spectators_respawn` between rounds;
  - round: the devgui's `zombie_devgui_goto_round`, as mods/sandbox does it.
- Each reply is a `LogPrint` line starting with `admin: `; the server also prints those to its console (and so to its
  curses console).

## Dvars

- `zm_startpoints <n>`: every player's points once they first spawn on a map (0, the default, keeps the game's own).
- `zm_startperks "<perks>"`: perks every player gets at that time, e.g. `"jugg revive"` or `"all"`.

Perk names: `jugg`, `revive`, `speed`, `doubletap`, `staminup`, `phd`, `deadshot`, `mulekick`, or the game's own
`specialty_...` name.
