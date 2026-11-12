# Black Ops II weapons in BO1 Zombies

Status (2026-11-12): the HAMR (`hamr_zm`, Pack-a-Punch `hamr_upgraded_zm`) works on the win64 build on Five. It is in
the box, and the view model, firing, reloading, ADS, world model and Pack-a-Punch weapon were checked with D3D9 client
screenshots. It runs with no engine change, through a mod (`mods/bo2weapons`) and its `mod.ff`, which is built from
the player's own BO2 install. Like the maps (`docs/BO2_MAPS.md`), nothing extracted is committed or packaged.

## Route

```
BO2 zone --OAT Unlinker (T6)--> weapon file, xmodel JSON + glTF, xanim, material JSON, images (DDS)
         --tools/bo2/bo2_weapon.py--> OAT Linker project (T5): weapons, xmodels, xanims, materials, images/*.iwi
         --OAT Linker (T5, -l the BO1 zones it references)--> mod.ff (BO1 PC, 32-bit)
         --ffconv--> IWffs mod.ff (win64, aarch64)
mods/bo2weapons/mod.ff + mods/bo2weapons/images/*.iwi + the mod's scripts --> the weapon in the box
```

The engine loads `<fs_h>/<fs_game>/mod.ff` at start-up (`DB_ModFileExists`, the retail mod route), so `fs_game` must be
`mods/bo2weapons`. `mods/bo2weapons/maps/bo2weapons/_bo2weapons.gsc` runs from the `_zombiemode_ffotd::main_start`
hook, which comes before `maps\_zombiemode_weapons::init()` on every zombie map. It adds each weapon that a loaded
zone holds (`bo1_mapkit_weaponloaded`) with `include_zombie_weapon` and `add_zombie_weapon`, the calls the map scripts
make for retail weapons, chained through `level._zombie_custom_add_weapons`. A weapon missing from the zones is
skipped, and the map then stays retail.

## What the converter does (`tools/bo2/bo2_weapon.py`)

- **Weapon file.** T5 takes 739 of T6's 1 027 keys, and the BO1 reference weapon (`hk21_zm` for the HAMR) gives the
  others. The text values that name assets BO1 cannot get (fx, sounds, tracers, reticles, accuracy graphs) also come
  from the reference: 36 values for the HAMR. BO2's models, animations and HUD icons are shipped. The keys only T6
  has (288: attachments, camo, crawl, anti-quickscope...) are dropped.
- **Attached models.** BO2 attaches the HAMR's magazine as a separate model (`attachViewModel7` /
  `attachWorldModel7`). BO1 has no attached models, so each LOD of the magazine is merged into the gun's glTF. Its
  bones become children of the gun's root bone at the slot's offset. BO2's reload animations move `tag_clip`, which
  BO1 also keeps under `j_gun`. The magazine's two surfaces share one vertex buffer, so each accessor is moved once.
- **Materials.** Each opaque lit BO2 material becomes a copy of BO1's `mc/mtl_t5_weapon_law`
  (`mc_l_sm_r0c0n0s0sc0x0_clrdtl_hero`: colour, normal, specular and colour detail, with no environment map), filled
  with BO2's colour, normal and specular maps. The colour detail slot keeps BO1's `weapon_camo_off`, the slot that
  Pack-a-Punch camo uses. BO2's normal maps are BC5 (X red, Y green), while BO1's are DXT5 with X in alpha and Y in
  red, green and blue (measured on `t5_weapon_hk21_n`). They are decoded and recompressed with ImageMagick. The HUD
  icon is a copy of `hud_hk21`.
- **Colour.** BO2 shades weapons as metals: the HAMR's tan receiver is in its specular map, and its colour map is dark
  grey (mean 47). BO1 shows mostly the colour map, so the HAMR came out black, even with `r_fullbright 1`. The colour
  map BO1 gets is (BO2's colour + its specular colour) x 1.8 (`write_lit_colour`; `--specular-mix`, `--colour-gain`),
  judged against the user's BO2 screenshot and brightened at their request (x1.8: +23 % on the weapon in Five's war room). The rail's ridges stay dark: they are dark in both of BO2's maps, and their tan look in BO2
  comes from its reflections, which BO1's shading does not have.
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
    # BO2: the weapons, their models, anims, materials and images
    $U --include-assets weapon,xmodel,xanim,material --model-format GLTF -o $D/weapons/?zone? "$Z/zm_nuked.ff"
    $U --include-assets image --image-format DDS -o $D/weapons/?zone? "$Z/zm_nuked.ff"
    # BO1: the reference weapons, their anims and the material templates (one folder holding weapons/, xanim/, materials/)
    B=<BO1 install>/zone/Common
    $U --include-assets weapon,xanim -o $D/bo1/?zone? "$B/common_zombie.ff"
    $U --include-assets material -o $D/bo1mat/?zone? "$B/common_zombie.ff"
    mkdir -p $D/bo1ref; ln -s $D/bo1/common_zombie/{weapons,xanim} $D/bo1mat/common_zombie/materials $D/bo1ref/
    # convert, then link
    tools/bo2/bo2_weapon.py --dump $D/weapons/zm_nuked --bo1 $D/bo1ref --out ~/bo2tools/wmod \
        --weapon hamr_zm:hk21_zm --weapon hamr_upgraded_zm:hk21_upgraded_zm \
        --string "WEAPON_HAMR=HAMR" --string "ZMWEAPON_HAMR_UPGRADED=SLDG HAMR" \
        --string "ZOMBIE_WEAPON_HAMR=Hold ^3[{+activate}]^7 to buy HAMR [Cost: &&1]"
    cd ~/bo2tools/wmod && $L -b . -l $B/code_post_gfx.ff -l $B/common.ff -l $B/common_zombie.ff \
        -l $B/common_zombie_patch.ff -l $B/zombie_pentagon.ff mod      # zone_out/mod/mod.ff

Then:
- copy `zone_out/mod/mod.ff` to `mods/bo2weapons/mod.ff` for the 32-bit build;
- for 64-bit, write it there through `ffconv [--abi aarch64] --verify mod.ff <mods/bo2weapons>/mod.ff`;
- copy `zone_raw/mod/images/*.iwi` to `mods/bo2weapons/images/` in both cases;
- start with `+set fs_game mods/bo2weapons +set fs_mods bo2weapons`.

## Verified (win64-msvc, `tools/headless.ps1 -Client`, Five)

- `games_mp.log`: `bo2weapons: hamr_zm in the box`.
- `give hamr_zm` + `give ammo`: 125/375. Firing with a muzzle flash, then a reload (122 → 125/372, magazine out, the
  spare one brought in by the left hand), ADS through the iron sight.
- Third person: the world model with its merged magazine. `hamr_upgraded_zm`: 125/625.
- Compared with the user's BO2 screenshot of the HAMR (`build/run/bo2w64/mods/bo2weapons/hamr_vs_ref.jpg`): same
  hold, tan receiver with its markings, the front post and the "3 2 1" drum sight, ADS through the rear aperture.
- `r_fullbright 1` shows BO2's textures (the strap's weave, the receiver's "EL PASO" marking), in place of the black
  silhouette that the embedded images gave.
- The HK21 next to it for comparison: the HAMR is darker, as its colour maps are (mean 47 against 62).
- `ffconv --verify`: 104 assets, the narrowed zone gives the 32-bit body back.

## Not done yet

- `win32-msvc`: `mod.ff` loads, but the `build/Release` exe predates `bo1_mapkit_weaponloaded` and the script stops.
  Rebuild it, then rerun.
- Linux and the Switch: not run. On the Switch, `mod.ff` needs `ffconv --abi aarch64`, and `mods/` reaches the SD
  card with the package.
- The box and the Pack-a-Punch machine in play: checked only through `give` (registration logged, upgrade weapon
  loaded). PaP camo on the merged model is not seen yet.
- Sounds are BO1's (the HK21's). BO2's sound banks cannot be linked for T5.
- The other weapons: same procedure with `--weapon <bo2>:<bo1 reference>`. Expect new cases where materials are blended
  or alpha-tested, or where a weapon carries attachments (scopes): the converter stops on them with a message.
- Scripted weapons (Jet Gun, staffs, Blundergat, Slowgun, tomahawk) need their BO2 logic rewritten in BO1 GSC.
- `mod.ff` only loads when `fs_game` is `mods/bo2weapons`. Stacking it with MapKit (whose `fs_game` is
  `mods/mapkit`) would need the engine to load the `mod.ff` of every `fs_mods` folder.

## Inventory

BO2 weapons per zone (`Unlinker --list`): 98 in `zm_nuked`, the same set plus TranZit's in `zm_transit`, Origins'
(`zm_tomb`: staffs, MP40, STG-44, Thompson, Ballista, Mark II...), Mob's (`zm_prison`: Blundergat, tomahawk...), and
Buried's (`zm_buried`: AN-94, SVU, Slowgun...).

Classic weapons BO1 lacks: HAMR, AN-94, Type 25 (`type95`), SMR (`saritch`), MTAR (`tar21`), Chicom (`qcw05`),
Executioner (`judge`), KAP-40 (`kard`), Five-seven (and dual wield), DSR-50, Barrett M82, M1216, LSAT, PDW-57,
Ballista, SVU, plus Origins' MP40, STG-44, Thompson, MG08, and Ray Gun Mark II.
