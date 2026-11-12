# Black Ops II weapons in BO1 Zombies

Status (2026-11-12): the 36 classic box weapons of BO2's six zombie maps (Nuketown, TranZit, Die Rise, Mob of the
Dead, Buried, Origins) and their 36 Pack-a-Punch versions work on the win64 build on Five. They are in the box, and
each was given and shot in first person on D3D9 client screenshots. The HAMR, the first one (proof of concept), was
also checked firing, reloading, aiming and in third person. They run with no engine change, through a mod
(`mods/bo2weapons`) and its `mod.ff`, which is built from the player's own BO2 install. Like the maps
(`docs/BO2_MAPS.md`), nothing extracted is committed or packaged.

## Route

```
BO2 zone --OAT Unlinker (T6)--> weapon file, xmodel JSON + glTF, xanim, material JSON, images (DDS)
         --tools/bo2/bo2_weapon.py--> OAT Linker project (T5): weapons, xmodels, xanims, materials, images/*.iwi
         --OAT Linker (T5, -l the BO1 zones it references)--> mod.ff (BO1 PC, 32-bit)
         --ffconv--> IWffs mod.ff (win64, aarch64)
mods/bo2weapons/mod.ff + mods/bo2weapons/images/*.iwi + the mod's scripts --> the weapon in the box
```

The engine loads the zone `mod` at start-up (`DB_LoadGraphicsAssetsForPC`). Retail takes it from
`<fs_h>/<fs_game>/mod.ff` only; `DB_FindModZone` (`db_registry.cpp`) also takes the `mod.ff` of the first mod of the
`fs_mods` stack that has one, under `fs_h` then `fs_b`. So `+set fs_mods bo2weapons` is enough, and stacks with
horde, MapKit, nightmare and the others, whose `fs_game` stays theirs. `fs_game mods/bo2weapons` still works for one
machine, but a server's `fs_game` is sent to its clients, which then restart and reload without `mod.ff` (an assert in
the script parser, or `Weapon index mismatch`): `fs_mods` avoids that path. `fs_b` matters for the dedicated server,
whose `fs_h` is the package's `server/` folder.
`mods/bo2weapons/maps/bo2weapons/_bo2weapons.gsc` runs from the `_zombiemode_ffotd::main_start`
hook, which comes before `maps\_zombiemode_weapons::init()` on every zombie map. It reads the weapon list from
`mp/bo2weapons.csv` in `mod.ff` (name, Pack-a-Punch name, VO type; written by the converter) and adds each weapon that
a loaded zone holds (`bo1_mapkit_weaponloaded`) with `include_zombie_weapon` and `add_zombie_weapon`, the calls the
map scripts make for retail weapons, chained through `level._zombie_custom_add_weapons`. A weapon missing from the
zones is skipped, and the map then stays retail.

## Playing with them

- **The box.** `bo2weapons_box` (read when the map loads) picks what the box gives: `0` (default) the map's weapons
  and BO2's, `1` BO2's and the map's wonder weapons and special grenades (Ray Gun, Thundergun, Wunderwaffe, Winter's
  Howl, Wave Gun, 31-79 JGb215, V-R11, Scavenger, Crossbow, Cymbal Monkey, Gersh, Matryoshka, QED), `2` BO2's only.
  The map's others leave the box through `is_in_box`, which `treasure_chest_ChooseWeightedRandomWeapon` reads. Wall
  weapons stay. The weapons that spin in the box while it opens are still drawn from every weapon (retail picks them
  for show only).
- **ZmClient** (`tools/zmclient`): Solo view, row "Armes de Black Ops II" (no, with the map's, + specials, alone) adds
  `bo2weapons` to the line's `fs_mods` and sets `bo2weapons_box`. Lancement view (local server): the same choice for
  the server it starts. Play view, Join mode: "Armes de Black Ops II" loads `mod.ff` to join a server that has them.
  The rows are greyed without `mods/bo2weapons/mod.ff`, and on Dead Ops Arcade.
- **ZmAdmin** (`tools/zmadmin`): starting the local server asks for the same choice when the folder has `mod.ff`
  (`+set fs_mods bo2weapons +set bo2weapons_box N`); the Settings view edits `bo2weapons_box` (next map load, Save
  offers `zm_reset`). `mod.ff` loads at start-up only: adding `bo2weapons` later with `zm_mods` gives no weapon, and a
  `server.cfg` that sets `fs_mods` replaces the line's.
- **Clients of a server** that has them must load the same `mod.ff` (`+set fs_mods bo2weapons`), or the game stops
  when it enters (`Weapon index mismatch`, the weapons' indices come from the server).
- **Packages**: `tools/make_dist.sh --bo2 DIR` copies `DIR/mods/bo2weapons/mod.ff` (ffconv's 64-bit one) and `images/`
  into the windows and linux packages.

## What the converter does (`tools/bo2/bo2_weapon.py`)

- **Weapon file.** T5 takes 739 of T6's 1 027 keys, and the BO1 reference weapon gives the others. With `--all` the
  reference comes from the weapon's class and fire type (`reference_for`): machine gun → `hk21_zm`, SMG → `mp5k_zm`,
  automatic rifle → `commando_zm`, burst → `m16_zm`, semi-automatic → `m14_zm`, rifle with a scope overlay →
  `l96a1_zm`, shotguns → `spas_zm` / `ithaca_zm`, revolvers → `python_zm`, pistols → `cz75_zm`, dual wield →
  `cz75dw_zm` / `cz75lh_zm`, projectile pistol (Ray Gun Mark II) → `ray_gun_zm`, launchers → `m72_law_zm` /
  `china_lake_zm`; the Pack-a-Punch version takes the reference's `_upgraded_zm`. A weapon's linked weapons
  (`altWeapon`, `DualWieldWeapon`: the Five-seven's left-hand twin) ship with it. The text values that name assets BO1
  cannot get (fx, sounds, tracers, reticles, accuracy graphs) also come from the reference: 36 values for the HAMR.
  BO2's models, animations and HUD icons are shipped. The keys only T6 has (288: attachments, camo, crawl,
  anti-quickscope...) are dropped.
- **Attached models.** BO2 attaches the HAMR's magazine as a separate model (`attachViewModel7` /
  `attachWorldModel7`). BO1 has no attached models, so each LOD of the magazine is merged into the gun's glTF. Its
  bones become children of the gun's root bone at the slot's offset. BO2's reload animations move `tag_clip`, which
  BO1 also keeps under `j_gun`. The magazine's two surfaces share one vertex buffer, so each accessor is moved once.
- **Enumerations.** A BO2 value that T5 does not know in a field whose BO1 value is an enumeration (`dwlefthand`,
  `pistol spread`) takes the reference's value (`T5_ENUM_VALUES`, from OAT's `WeaponStrings.h`): the Linker refuses it.
- **Accuracy graphs.** An actor firing a weapon with no "AI vs AI" graph ends the game (`No 'AI vs AI' accuracy graph
  for weapon 'ak47_zm'`); BO1's box weapons have none, so an empty one takes BO1's `smg.accu`, copied into the project.
- **Materials.** Each opaque lit BO2 material becomes a copy of BO1's `mc/mtl_t5_weapon_law`
  (`mc_l_sm_r0c0n0s0sc0x0_clrdtl_hero`: colour, normal, specular and colour detail, with no environment map), filled
  with BO2's colour, normal and specular maps. The colour detail slot keeps BO1's `weapon_camo_off`, the slot that
  Pack-a-Punch camo uses. BO2's normal maps are BC5 (X red, Y green), while BO1's are DXT5 with X in alpha and Y in
  red, green and blue (measured on `t5_weapon_hk21_n`). They are decoded and recompressed with ImageMagick. The HUD
  icon is a copy of `hud_hk21`.
- **Scope glass and glowing sight parts.** A BO2 material whose main pass blends (scope lenses, the Type 25's and the
  RPG's see-through parts) becomes a copy of BO1's `mc/mtl_t5_weapon_longscope_lens` (`mc_l_sm_b0c0n0s0_hero`: lit,
  blended over the scene). An emissive one (camera region `emissive*` or an `unlit` techset: tritium dots, the M32's
  reticle, sight glows) becomes a copy of `mc/mtl_weapon_scope_colt3x20_crosshair` (`mc_effect_hero`, colour + scene
  x (1 - alpha)); when BO2 draws it "one one" (additive), its colour map's alpha is set to 0 so BO1 adds it too
  (`*_add` images). Before this, both were dropped and the scopes were empty tubes. BO2's slot names vary with the
  techset (`Color_Map`, `SpecularAndGloss`, `Specular_Color_Map`); a material with no normal map takes
  `$identitynormalmap`. The LSAT's ammo counter (`mc_sw4_3d_digits_ammo`) is still dropped: BO2 draws its digits from
  the ammo count, and a fixed texture would show every digit.
- **Several zones.** `--dump` is repeated, one per BO2 zone, and each asset comes from the first dump holding it.
  An image streamed from an `.ipak` is not in OAT's dump (24 of them): its material keeps the template's image.
- **Colour.** BO2 shades weapons as metals: the HAMR's tan receiver is in its specular map, and its colour map is dark
  grey (mean 47). BO1 shows mostly the colour map, so the HAMR came out black, even with `r_fullbright 1`. The colour
  map BO1 gets is (BO2's colour + its specular colour) x 1.8 (`write_lit_colour`; `--specular-mix`, `--colour-gain`),
  judged against the user's BO2 screenshot and brightened at their request (x1.8: +23 % on the weapon in Five's war
  room). The rail's ridges stay dark: they are dark in both of BO2's maps, and their tan look in BO2 comes from its
  reflections, which BO1's shading does not have.
- **Iron sights.** The weapon file's `hideTags` is cleared. BO2 hides `tag_sights_off` (the folding front post and
  the rear drum sight) only when an optic is mounted, while BO1 hid them always: no sights in hip fire or ADS.
- **Emblem surfaces.** The player's emblem and the clan tag are dropped. BO2 fills them only for a custom emblem, while
  BO1's materials of the same name draw coloured rectangles there.
- **Images** are written as IW images (`images/<name>.iwi`, `dds_to_iwi` from `bo2_mesh.py`): the Linker references
  them, and the mod ships them next to `mod.ff`. Pixels embedded in `mod.ff` draw black. The client restarts after
  loading `mod.ff` (the `fs_game` change, a second `R_Init` in the log), and only images loaded from file come back
  (`DB_LoadDelayedImages`). OAT's T5 external image loader left the category, the format and the card memory at 0,
  which BO1 stops on (`unhandled case: 0`); `tools/bo2/oat/ImageLoaderExternalT5.patch` fills them in.
- **Animations.** BO2's anims use BO1's hand skeleton (same bone names, BO1's `j_*_3` finger tips included), and OAT
  writes them in v19's T5 form. Their `sndnt#<alias>` notetracks name BO2 sound aliases, which BO1 does not have. Each
  one becomes the reference weapon's alias for the same action (`fly_hamr_mag_out` → `wpn_hk21_mag_out`). A notetrack
  with no match is renamed `bo2nt#...`: it still notifies but plays no sound (the HAMR's `tap`).

## Build it

Paths are those of this machine. OAT must be built with both patches (`tools/bo2/oat/README.md`):

    D=~/bo2tools/dump; Z="/mnt/c/Program Files (x86)/Steam/steamapps/common/Call of Duty Black Ops II/zone/all"
    U=~/bo2tools/oat-src/build/bin/Release_x64/Unlinker; L=~/bo2tools/oat-src/build/bin/Release_x64/Linker
    ZM="zm_nuked zm_transit zm_highrise zm_prison zm_buried zm_tomb"
    # BO2: the weapons, their models, anims, materials and images, then the weapons' names (the install's language)
    for z in $ZM; do
      $U --include-assets weapon,xmodel,xanim,material --model-format GLTF -o $D/weapons/?zone? "$Z/$z.ff"
      $U --include-assets image --image-format DDS -o $D/weapons/?zone? "$Z/$z.ff"
    done
    $U --include-assets localize -o $D/loc/?zone? "$Z"/fr_*.ff        # en_*.ff for English
    # BO1: the reference weapons, their anims and accuracy graphs, the material templates (one folder)
    B=<BO1 install>/zone/Common
    $U --include-assets weapon,xanim -o $D/bo1/?zone? "$B/common_zombie.ff"
    $U --include-assets material -o $D/bo1mat/?zone? "$B/common_zombie.ff"
    mkdir -p $D/bo1ref; ln -s $D/bo1/common_zombie/{weapons,xanim,accuracy} $D/bo1mat/common_zombie/materials $D/bo1ref/
    # convert every box weapon (about an hour, mostly ImageMagick), then link
    tools/bo2/bo2_weapon.py --all $(for z in $ZM; do echo --dump $D/weapons/$z; done) \
        --bo1 $D/bo1ref --localize $D/loc --out ~/bo2tools/wmod
    cd ~/bo2tools/wmod && $L -b . -l $B/code_post_gfx.ff -l $B/common.ff -l $B/common_zombie.ff \
        -l $B/common_zombie_patch.ff -l $B/zombie_pentagon.ff mod      # zone_out/mod/mod.ff

One weapon at a time still works: `--weapon hamr_zm:hk21_zm --weapon hamr_upgraded_zm:hk21_upgraded_zm` and
`--string KEY=text` for its names, without `--all`.

Then:
- copy `zone_out/mod/mod.ff` to `mods/bo2weapons/mod.ff` for the 32-bit build;
- for 64-bit, write it there through `ffconv [--abi aarch64] --verify mod.ff <mods/bo2weapons>/mod.ff`;
- copy `zone_raw/mod/images/*.iwi` to `mods/bo2weapons/images/` in both cases;
- start with `+set fs_game mods/bo2weapons +set fs_mods bo2weapons`.

## Verified (win64-msvc, `tools/headless.ps1 -Client`, Five)

All weapons (`--all`):
- Converter: 74 weapon files (36 box weapons, their 36 Pack-a-Punch versions, the Five-seven's two left-hand twins),
  93 xmodels, 739 xanims, 183 materials (136 lit, 6 glass, 6 emissive, 35 HUD), 193 images (116 MB of `.iwi`). Linker:
  0 errors; `mod.ff` 9.5 MB; `ffconv --verify`: the narrowed zone gives the 32-bit body back.
- `games_mp.log`: `bo2weapons: 36 weapons in the box`.
- `give` then a screenshot for each of the 72 (six batches of 12 through `bo1_clientCommands`, which keeps only about
  4 096 characters): every weapon drawn in first person with its textures, the lenses of the DSR-50, Barrett, SVU and
  Ballista, the Type 25's and M32's sights. The last screenshot of a batch was sometimes lost (taken again).

The HAMR (proof of concept):

- `games_mp.log`: `bo2weapons: hamr_zm in the box`.
- `give hamr_zm` + `give ammo`: 125/375. Firing with a muzzle flash, then a reload (122 → 125/372, magazine out, the
  spare one brought in by the left hand), ADS through the iron sight.
- Third person: the world model with its merged magazine. `hamr_upgraded_zm`: 125/625.
- Compared with the user's BO2 screenshot of the HAMR (`build/run/bo2w64/mods/bo2weapons/hamr_vs_ref.jpg`): same
  hold, tan receiver with its markings, the front post and the "3 2 1" drum sight, ADS through the rear aperture.
- `r_fullbright 1` shows BO2's textures (the strap's weave, the receiver's "EL PASO" marking), in place of the black
  silhouette that the embedded images gave.
- The HK21 next to it for comparison: the HAMR is darker, as its colour maps are (mean 47 against 62).
- `win32-msvc` (fresh build before the merge, 32-bit `mod.ff` as linked): the same hip, ADS, zoom and reload shots,
  `hamr_zm in the box`, no assert.
- `ffconv --verify`: 104 assets, the narrowed zone gives the 32-bit body back.

## Not done yet

- Joining, with `+set fs_mods bo2weapons` on the client, checked once (win64 dedicated server and client, Five,
  box mode 2): the client entered and was given the upgraded DSR-50. Not checked: a client that loads `mod.ff` and
  joins a server without the weapons; the box opened in play.
- Linux and the Switch: not run. On the Switch, `mod.ff` needs `ffconv --abi aarch64`, and `mods/` reaches the SD
  card with the package. The 193 images (116 MB) may weigh on the console's memory.
- The box and the Pack-a-Punch machine in play: checked only through `give`. PaP camo on the models is not seen yet.
- Sounds are those of the BO1 reference weapon. BO2's sound banks cannot be linked for T5, and a BO2 notetrack sound
  with no BO1 equivalent stays silent (249 notes in the converter's output).
- The LSAT's ammo counter is dropped; 24 images streamed from `.ipak`s keep the template's image.
- The Five-seven's left gun, in dual wield, is barely visible.
- Scripted weapons (Jet Gun, staffs, Blundergat, Slowgun, Sliquifier, tomahawk, ballistic knife), equipment, the
  shields and Origins' extended-clip / stalker copies are left out (`SKIP`, or not a primary weapon). Weapons BO1
  already has under the same name (`m14_zm`, `mp5k_zm`, `ray_gun_zm`...) keep BO1's.
- One `mod.ff` loads: fs_game's, else the first stacked mod's. Two mods each shipping one would need distinct zone
  names.

## Inventory

BO2 weapons per zone (`Unlinker --list`): 98 in `zm_nuked`, the same set plus TranZit's in `zm_transit`, Origins'
(`zm_tomb`: staffs, MP40, STG-44, Thompson, Ballista, Mark II...), Mob's (`zm_prison`: Blundergat, tomahawk...), and
Buried's (`zm_buried`: AN-94, SVU, Slowgun...).

Classic weapons BO1 lacks: HAMR, AN-94, Type 25 (`type95`), SMR (`saritch`), MTAR (`tar21`), Chicom (`qcw05`),
Executioner (`judge`), KAP-40 (`kard`), Five-seven (and dual wield), DSR-50, Barrett M82, M1216, LSAT, PDW-57,
Ballista, SVU, plus Origins' MP40, STG-44, Thompson, MG08, and Ray Gun Mark II.
