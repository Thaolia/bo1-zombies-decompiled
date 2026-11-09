# Black Ops II zombies maps in this BO1 port: feasibility (2026-11-09)

Question (the user): can the BO2 zombies maps of a Steam install (`Call of Duty Black Ops II\zone\all`) be brought
into this BO1, with MapKit for example? Answer so far: **the whole content of Nuketown Zombies (`zm_nuked`) can be
extracted; there is no official way to compile it into a BO1 map, so the import goes through MapKit**, which has to
learn real geometry (collision first, then drawing). Nothing of the game goes into this repository: extractions live
outside it (`~/bo2tools/` on the session's machine), as every other game file of this project.

## What a BO2 zone is

- `zone/all/*.ff` are **T6** fastfiles (header `TAff0100`, then `PHEEBs71`: Salsa20-encrypted and signed); textures
  of a map are in its `.ipak`. Asset formats, shaders (D3D11) and scripts (T6 GSC bytecode) all differ from T5: the
  engine cannot load them, and `ffconv` (T5 32-bit to 64-bit) does not apply.
- Zombies zones present: `zm_nuked` (Nuketown), `zm_transit` (+ its streamed `gump` zones), `zm_highrise`,
  `zm_prison`, `zm_buried`, `zm_tomb`, `common_zm`, and the `so_*` game-mode zones.

## Dead end: the BO1 Mod Tools

Steam's *Call of Duty: Black Ops Mod Tools* (app 42740, `steamapps\common\Call of Duty Black Ops 42740`, 3.1 GB) hold
`linker_pc`, `converter`, `asset_manager`, `AssetViewer`, `effectsed3`, the Maya export plugins and the retail map
sources (`map_source\zombie_*.map`), but **no Radiant and no map compiler** (`cod2map`, `cod2rad`): Treyarch never
released them for BO1. A BO1 map cannot be compiled from new geometry. Their `steam_launch_mods.cmd` also creates
junctions inside the retail game folder (and its `clean` mode deletes folders there): it is not used.

## What comes out of `zm_nuked`

With [OpenAssetTools](https://github.com/Laupetin/OpenAssetTools) 0.34.0 (GPL v3) and a world dumper written for it
(`tools/bo2/oat/`, the build and the commands are there):

| Part | Result |
|---|---|
| Zone contents | 6 100 assets: 1 711 xanim, 1 460 image, 1 399 material, 697 xmodel, 244 fx, 98 weapon, 42 script, one each of gfxworld, clipmap, comworld, gameworldmp, mapents |
| Entities (`mapents`) | 703 `node_pathnode`, 93 traversal pairs (`node_negotiation_begin/end`), 8 `initial_spawn_points`, 12 `info_volume` zones, 65 zone spawners (`<zone>_zone_spawners`), 8 `weapon_upgrade` wall buys, 6 `zombie_door`, 5 magic box spots, 169 script models, 33 trigger_multiple |
| World (`gfxworld`) | 195 091 vertices, 9 865 static surfaces, **131 032 triangles**, 464 materials, all 464 with their colour map extracted; 2 082 placed static models (name, origin, axes, scale) |
| Models and textures | 1 801 OBJ (all LODs) with `.mtl`, 1 419 DDS images; one HUD icon missing (`hud_mp_firerate_auto`) |
| Collision (`clipmap`) | 7 620 brushes (7 357 world, 263 of 140 submodels: doors, debris), 33 971 non-axial sides, 24 502 terrain triangles (128 of a submodel, 15 without a collision material), 766 collision materials |
| Not yet extracted | the collision of the 1 349 static models with collision (cars, fences: it comes from their xmodels), lighting (lightmaps in `gfxworld`, `comworld` lights), the T6 pathnode graph (`gameworldmp`; the nodes are also in the entities) |

The exported world and the entities share one frame: in a top-down plot the spawns, spawners, magic boxes and
pathnodes fall on the geometry (the two houses, the cul-de-sac, the garages). The world's bounding box (about 85 000
by 70 000 units) includes the distant backdrop; the playable area is about 5 000 units across.

The collision falls on the world in a top-down plot (houses, garages, the invisible walls at the edges); submodel
brushes sit around the origin, in their model's own space. Its content flags read from the collision material names are
BO1's bits: `0x1` solid, `0x10000` `clip_player`, `0x20000` `clip_ai`, `0x80` `clip_missile`, `0x2040` `clip_weapon`,
`0x200` `clip_vehicle`, `0x400` `clip_physics`, `0x800` sky, `0x8000000` detail; the 4 700 `0x2080` brushes are
`caulk` that only stops shots. BO1's player mask (`0x810011`, `g_active_mp.cpp`) and actor mask (`0x2820011`,
`actor_mp.cpp`) test the same bits, so the flags can be kept as they are.

The world vertex format is not described anywhere: it was worked out from the buffer (36 bytes, half-float UVs, indices
relative to each surface's `vertexDataOffset0`; details in `tools/bo2/oat/README.md`). A first guess (44 bytes,
global indices) gave coordinates of 3e38; the check that caught it (bounding box, edge lengths, UV ranges) is the one
to rerun on another map.

## The import through MapKit (state on 2026-11-09)

MapKit (`mods/mapkit/MAPKIT.md`) already had every gameplay piece Nuketown needs on an `"empty": true` base: `spawn`,
`zone`, `riser`, `node`, `mysterybox`, `wallbuy`, `perk`, `pap`. What it lacked was real geometry and traversals; its
own boxes are axial brushes (at most 512), drawn through the code mesh (16-bit indices: 65 536 vertices at most, when
Nuketown has about 195 000 world vertices before its props).

### Done: collision, layout, traversals (headless, Linux)

1. **Collision** (`collision` edit, `src/qcommon/cm_mapkit_import.cpp`). The BO2 clipMap dumped as `BO2COL2` (brushes
   with their planes, terrain triangles, each with its owner: world or submodel) is loaded into `cbrush_t`s like the
   retail ones: the world brushes (7 344 of 7 357: 13 cut, see 2.), and each world terrain triangle as a 16-unit prism
   under it (24 374). Every
   brush gets its corners (the points where three of its planes meet), filed in a 2D grid (583 x 523 cells). They join
   the world in `CM_Mapkit_TraceThrough`/`TestIn`/`PointContents`/`SightTrace` (traces) **and** in the physics'
   `colgeom_visitor_t::intersect_box` (`phys_traverse.cpp`), which serves player and actor movement, `Path_CanLinkNodes`
   and ragdolls through GJK (the reason for the corners). Without that second hook the player fell through the floor
   (z -131 000): player movement never runs the trace hooks. Submodel geometry is left out: it sits in its model's
   own space (BO2's 128-triangle `nodraw` sheet at z 0.2 over the whole map is one, which the player stood on until the
   owner was exported).
2. **Layout generator** (`tools/bo2/bo2_layout.py <dump> zm_nuked`): 10 zones (a zone's several volumes become one
   box), 8 spawns, 36 risers, 703 nodes, 75 traversals, the box, 7 wall buys, 4 perks and Pack-a-Punch. Findings it
   encodes:
   - BO2's entities are **not on the floor**: its compiler drops pathnodes into its own path data, and the entities
     keep them where the designer put them, 0 to 175 units above it. BO1 links nodes on the floor (`Path_CanLinkNodes`
     ends within 18 units of the target's height): with the raw heights, 0 of 703 nodes linked. Dropped onto the floor
     under them (brush planes and terrain, actor mask), they make 6 724 links.
   - A machine's `zm_random_machine` struct floats at z 2204 and targets its real spot (on the ground, with a yaw).
     Each spot holds a clip brush (`0x8030200`, the crate pile's footprint, larger than a BO1 machine), so the
     `collision` edit takes `cut` boxes (each with the brush's exact contents) and MapKit's machine brings its own clip.
     The machine and box face the first side with room in front of them.
   - BO2 lays a wall weapon along the wall: a BO1 wall buy faces BO2's yaw + 90 (true for all 7).
   - Traversals (`traverse` edit, `g_mapkit_path.cpp`): mantles and jump-downs map to the BO1 zombie animscript of the
     nearest height that the Five base loads (`zombie_mantle_over_40`, `zombie_jump_down_40/72/127/184`); the level
     loader compiles each one (`g_sp_levelstart.cpp`: a traversal's script is only loaded when a node names it). BO1
     zombies have no jump up under 222 units: BO2's 16 jump-ups and its 2 garage-door traversals are left out.
3. **Headless self-test** (Linux build, `mapkit_selftest 1`, a test client in god mode, timescale 4):

   | Check | Result |
   |---|---|
   | Zones | 10/10 PASS |
   | Pathnodes | 692/692 found by the actors' goal lookup; 15 bad nodes (11 against a wall after the drop, 4 traversal ends) |
   | Mystery box | PASS (Monkey Bomb, Winter's Howl, CZ75 on three runs) |
   | Perks | 3/4 PASS (Quick Revive, Speed Cola, Double Tap); the test client never reaches Juggernog's spot, in house 2's backyard (not investigated: BO2 has mantles there, which a player walking the node graph does not take) |
   | Wall buys | 5/7 PASS, one of them upstairs in house 1; the M16 (upstairs in house 2: the client stays on the floor below it) and the AK-74u (not investigated) fail |
   | Pack-a-Punch | FAIL: its spot is upstairs in house 2 too |
   | Risers | 10/36 PASS (a zombie climbs out and reaches the player); 17 rose but none reached the player (most stuck in the ditch north of house 2, around (700..1200, 1200..1300, z -250): no way out of it in the node graph yet), 9 not picked in 90 s |
| Round | PASS; whole run: `mapkit_selftest DONE pass 31 fail 31` |

   BO2's zombies reach both houses' upper floors from outside with `zm_jump_up_127` (12 pairs), which BO1 has no
   animation for. In house 1 the node graph has another way up (the MP5K passes); in house 2 it finds none, so the
   zombies cannot go up there either until it gets one (a jump up for BO1's zombies, or nodes on the stairs).

### Not done

- **Drawing.** Nothing of Nuketown is drawn yet: the game runs on its collision only (headless tests do not need the
  world drawn). Next step: a static mesh path in the renderer (one vertex and index buffer built at load, materials
  cloned from a lit BO1 world material with the BO2 colour map loaded from its DDS, the neutral lightmap MapKit's
  brush models use), with the placed props baked into the same mesh from their OBJ and placement.
- The collision of the 1 349 static models (cars, fences: it comes from their xmodels): today a player walks through
  them.
- Windows checks (`tools/headless.ps1`), and the Switch.
- Dog rounds (Five's dogs are not converted), BO2's own mechanics (the bus, buildables, Who's Who...), its characters
  and animations (T6 skeletons and xanims), its sounds and its baked lighting. The game is BO1's zombies on Nuketown's
  geometry.

### Running it

On the session's machine (paths to adapt), with OAT built as `tools/bo2/oat/README.md` says:

    Z="/mnt/c/Program Files (x86)/Steam/steamapps/common/Call of Duty Black Ops II/zone/all"
    Unlinker --include-assets clipmap,mapents -o out/?zone? "$Z/zm_nuked.ff"
    tools/bo2/bo2_layout.py out/zm_nuked zm_nuked --out <run>/mods/mapkit/layouts/bo2_zm_nuked.json
    cp out/zm_nuked/maps/mp/zm_nuked.d3dbsp.collision.bin <run>/mods/mapkit/imports/zm_nuked/zm_nuked.collision.bin
    BO1Zombies +set fs_game mods/mapkit +set bo1_mod_mapkit bo2_zm_nuked +devmap zombie_pentagon

The run folder is a normal game folder (the player's converted Five zones); the layout and the collision file are the
player's own extraction and stay out of the repository.
