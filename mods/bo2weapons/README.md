# bo2weapons - Black Ops II weapons in the box (NOT part of the original game)

A mod folder, kept apart from the base game. It adds Black Ops II weapons to the mystery box and to Pack-a-Punch on
every zombie map. The weapons come from **your own** Black Ops II install: you build them into `mod.ff` yourself, and
this folder ships only the scripts. Nothing extracted from either game belongs in the repository.

Proof of concept: the HAMR (`hamr_zm`, Pack-a-Punch `hamr_upgraded_zm`, 9 500 points in the box).

## Build mod.ff

The procedure is in `docs/BO2_WEAPONS.md`. In short:
1. dump the BO2 zone and a BO1 reference zone with OAT's Unlinker;
2. run `tools/bo2/bo2_weapon.py`;
3. build with OAT's Linker (T5).

That gives `mod.ff` for the 32-bit build. Convert it with `ffconv` for the 64-bit builds (Windows x64, Linux, Switch),
then put it at `mods/bo2weapons/mod.ff` next to the game, and the weapon's images (`images/*.iwi` from the Linker
project) in `mods/bo2weapons/images/`. The game loads the images from there, not from `mod.ff`.

## Start it

`+set fs_game mods/bo2weapons +set fs_mods bo2weapons`. The engine loads `mod.ff` from the `fs_game` folder only
(`DB_ModFileExists`), so `fs_game` must be this folder. Other mods stack after it in `fs_mods`.

Without `mod.ff` the scripts skip each weapon and the map stays retail: `games_mp.log` then says
`bo2weapons: hamr_zm not loaded`. With it, `games_mp.log` says `bo2weapons: hamr_zm in the box`.

## What differs from Black Ops II

- **Sounds and effects** are those of a close BO1 weapon (the HK21 for the HAMR): firing, reloads, muzzle flash,
  shell ejection. BO1 cannot take BO2's sound banks.
- **The magazine** is a separate model in BO2, attached by the engine. BO1 has no attached models, so the converter
  merges the magazine into the gun model on the `tag_clip` bone that BO2's animations move.
- **Pack-a-Punch** shows BO1's camo, through the weapon materials' colour-detail slot, as on BO1's own weapons
  (not yet checked on screen).
- **Emblem and clan tag** surfaces are left out of the models.
