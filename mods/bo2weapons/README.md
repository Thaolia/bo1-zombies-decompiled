# bo2weapons - Black Ops II weapons in the box (NOT part of the original game)

A mod folder, kept apart from the base game. It adds Black Ops II weapons to the mystery box and to Pack-a-Punch on
every zombie map. The weapons come from **your own** Black Ops II install: you build them into `mod.ff` yourself, and
this folder ships only the scripts. Nothing extracted from either game belongs in the repository.

The 36 classic box weapons of BO2's zombie maps (HAMR, AN-94, Type 25, SMR, MTAR, Chicom, Executioner, KAP-40,
Five-seven, DSR-50, Barrett M82, M1216, LSAT, PDW-57, Ballista, SVU, MP40, STG-44, Thompson, MG08, Ray Gun Mark II...)
and their Pack-a-Punch versions. `mod.ff` lists them in `mp/bo2weapons.csv`, which the scripts read.

## Build mod.ff

The procedure is in `docs/BO2_WEAPONS.md`. In short:
1. dump the BO2 zone and a BO1 reference zone with OAT's Unlinker;
2. run `tools/bo2/bo2_weapon.py --all` (every box weapon of the dumps);
3. build with OAT's Linker (T5).

That gives `mod.ff` for the 32-bit build. Convert it with `ffconv` for the 64-bit builds (Windows x64, Linux, Switch),
then put it at `mods/bo2weapons/mod.ff` next to the game, and the weapon's images (`images/*.iwi` from the Linker
project) in `mods/bo2weapons/images/`. The game loads the images from there, not from `mod.ff`.

## Start it

`+set fs_mods bo2weapons` (with the other mods of the line: `+set fs_mods zinfo bo2weapons`). The engine loads the
`mod.ff` of the first stacked mod that has one, beside `fs_game`'s (`DB_FindModZone`), so the weapons stack with
horde, MapKit and nightmare. ZmClient and ZmAdmin offer them (`docs/BO2_WEAPONS.md`, "Playing with them").

`bo2weapons_box` picks what the box gives: `0` (default) the map's weapons and BO2's, `1` BO2's and the map's wonder
weapons and special grenades, `2` BO2's only. Wall weapons stay the map's.

Without `mod.ff` the scripts skip each weapon and the map stays retail: `games_mp.log` then says
`bo2weapons: 0 weapons in the box`. With it, it says `bo2weapons: 36 weapons in the box`, and with `bo2weapons_box`
1 or 2 `bo2weapons: box mode 2, 23 map weapons out of the box` (Five).

A player who joins a server that has the weapons loads the same `mod.ff` (`+set fs_mods bo2weapons`), or the game
goes back to the menu when it enters ("The server uses weapon ..., which this game does not have").

## What differs from Black Ops II

- **Sounds and effects** are those of a close BO1 weapon of the same class (the HK21 for the HAMR, the L96A1 for the
  snipers...): firing, reloads, muzzle flash, shell ejection. BO1 cannot take BO2's sound banks.
- **The magazine** is a separate model in BO2, attached by the engine. BO1 has no attached models, so the converter
  merges the magazine into the gun model on the `tag_clip` bone that BO2's animations move.
- **Pack-a-Punch** shows BO1's camo, through the weapon materials' colour-detail slot, as on BO1's own weapons
  (not yet checked on screen).
- **Scopes**: lenses are drawn with BO1's scope glass, tritium dots and reticles glow with BO1's sight material.
  The LSAT's digital ammo counter is left out.
- **Emblem and clan tag** surfaces are left out of the models.
- **Left out**: the scripted weapons (Jet Gun, staffs, Blundergat, Slowgun, Sliquifier...), the equipment, and BO2
  weapons BO1 already has under the same name.
