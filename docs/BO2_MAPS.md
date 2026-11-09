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
   box), 8 spawns, 36 risers, 703 nodes and BO2's 5 254 links between them, 93 traversals, 102 barrier clips, the
   box, 7 wall buys, 4 perks and Pack-a-Punch. Findings it
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
   - Traversals (`traverse` edit, `g_mapkit_path.cpp`): jump-downs map to the BO1 zombie animscript of the nearest
     height that the Five base loads (`zombie_jump_down_40/72/127/184`), mantles to MapKit's `mapkit_mantle` (below); the level
     loader compiles each one (`g_sp_levelstart.cpp`: a traversal's script is only loaded when a node names it). BO1
     zombies have one jump up, 222 units (`ai_zombie_jump_up_222`); BO2's 16 jump-ups (72 to 154 units: from the
     yards to both houses' upper floors) take MapKit's own `mapkit_jump_up` (`mods/mapkit/animscripts/traverse`),
     which plays it and takes the rise beyond the end node off during its first half (`teleportThreadEx`, as retail
     corrects a traverse's height): in one run, 17 of 19 jumps ended on the upper floor (z 45 to 82, the end nodes'
     floors), not 222 units up; the other 2, at house 1's 96-unit pair (-945, 894), were back near the ground a second
     later. BO2's mantles cross obstacles up to 84 units deep, where Five's wall hop (`zombie_mantle_over_40`'s
     animations) carries a zombie 63 to 70 units: `mapkit_mantle` plays the same hop and spreads the gap between where
     it ends and the end node over its frames (the actor is noclip while it traverses). BO1 has no crawl under a
     garage door: BO2's 2 garage-door pairs (flat) take the mantle.
   - **BO2's node graph** (`links` edit). The dump's `gameworldmp` asset holds the links BO2's compiler made
     (`GameWorldPathDumperT6`, `<map>.d3dbsp.paths.csv`: the 703 pathnodes in their entity order, at their placed
     origins, 5 254 links between them). The generator writes them as node pairs (`zm_nuked.links.txt`), and between
     two layout `node`s they are the graph instead of BO1's link test (which made 7 422 links and refused 624 of BO2's).
     BO1's test refused the terrain behind the houses: an actor walks no facet steeper than 45.6 degrees (normal z
     0.7, hard-coded for players and actors alike), and the north ditch and the slope behind house 1 have many at 46
     to 49. It also linked over a window: house 1's front window has a 37-unit sill of monster clip with debris in
     front, which BO1's test climbs as steps; zombies then walked onto the sill without the mantle and stuck in the
     opening.
   - **Barrier clips** (`box` edits, `"contents":"monsterclip"`: hidden, they stop actors only). Each thin monster
     clip that a mantle pair crosses (30 to 80 units tall, at most 48 deep: the fences and that sill) gets a clip
     72 units over its top, so that no zombie stands on it; a slanted one becomes 8-unit strips along its shape, and
     a strip within an actor's radius of a node is left out (102 boxes over 19 barriers, 3 strips left out).
3. **Headless self-test** (Linux build, `mapkit_selftest 1`, a test client in god mode, timescale 4; with the prop
   collision, the client put on the floor at each purchase, see below):

   | Check | Result |
   |---|---|
   | Zones | 10/10 PASS |
   | Pathnodes | 703/703 found by the actors' goal lookup; 0 bad nodes (10 moved 8 to 16 units out of solid, see below) |
   | Mystery box | PASS (FN FAL; RPK, Monkey Bomb, Winter's Howl, CZ75 on earlier runs) |
   | Perks | 4/4 PASS |
   | Wall buys | 7/7 PASS, upstairs in both houses included |
   | Pack-a-Punch | PASS (AK-74u upgraded, 5 000 points), upstairs in house 2 |
   | Risers | 36/36 PASS (a zombie climbs out and reaches the player), the north ditch's included |
   | Round | PASS; whole run: `mapkit_selftest DONE pass 62 fail 0` |

   The test client walks straight from node to node along `Path_FindPath`'s route. BO2's node graph goes round the
   props, but not by much: with the props solid for players, the client caught on house 2's back ramp
   (`p6_zm_nuked_zombie_ramp`) and its handrail posts and stayed on the deck, and every check after it failed. On a layout with a `collision` edit the self-test therefore puts the client on the floor at each machine
   and wall buy (`mapkit_test_place`) and lets it walk only the last step: the checks test the purchases and the
   zombies, not the walk. MapKit's own layouts keep walking, and `+set mapkit_test_walk 1` keeps it on an imported
   map. The test client also steers round what the node graph does not know (`SP_HeadlessAvoid`, `g_sp_client.cpp`:
   a player hull probed 32 units ahead from step height; when it hits, the nearest clear heading in 20-degree steps,
   keeping the side it found while blocked) and counts a goal it cannot reach exactly as reached once it stands still
   within 48 units. Walking all the way (`mapkit_test_walk 1`, before the wall fix below) it bought the box, 3 of 4
   perks and 6 of 7 wall buys; Juggernog (house 2's backyard), the M16 and Pack-a-Punch (house 2's upper floor) had
   no route in the node graph (BO2 reaches them with mantles and `zm_jump_up` traversals).

   Rerun at 6472db7 (BO2's node graph, jump-ups, barrier clips): `mapkit_selftest DONE pass 36 fail 26`. Passing:
   the box, Quick Revive, Speed Cola, the CZ75, the AK-74u, Pack-a-Punch (house 2's upper floor, reached this time),
   17 risers and the round. Juggernog, Double Tap and 5 of 7 wall buys fail with the client stopped on the ground
   next to house 2 (x 600-850, y 160-280). After Pack-a-Punch it never leaves house 2's upper floor: 2 073
   `SP_HeadlessAvoid` turns, nearly all at z 80 there. 19 of 36 risers then fail with "no zombie rose there", since
   zombies rise only in the active zones round the player's. The default mode places the client, so the zombie and
   purchase checks do not depend on the walk (`pass 62 fail 0` on the same commit). Getting the client round house 2
   remains open.

   Pack-a-Punch took three fixes, each found by logging the engine's use search (`Player_GetUseList`):
   - BO2's machine yaws are rounded to right angles (`bo2_layout.py`, at most 15 degrees on Nuketown). MapKit's
     machine clip is the axial box around the turned model; at Pack-a-Punch's 105 degrees it reached 37 units in
     front, over the spot a player stands to use it, and the client put there was stuck in it.
   - Wall prisms go behind the wall (`cm_mapkit_import.cpp`). BO2 winds its triangles clockwise (10 983 of 13 089
     near-horizontal ones give a downward cross product); the loader turned only near-horizontal ones up, so a wall's
     16-unit prism came out of its visible face: house 2's walls stood 16 units into its rooms, and the use search's
     sight trace (mask `0x11`) from the player's eye to the trigger hit the wall behind Pack-a-Punch. Steep triangles
     now take BO2's front; the bad nodes went from 15 to 9 and the goal lookup from 692 to 694 nodes.
   - The check looks at the trigger's origin: Pack-a-Punch's trigger requires the look-at
     (`UseTriggerRequireLookAt`, a dot of 0.76 with the bounds centre, which for a `trigger_radius_use` is its
     origin).

   Risers went from 12 to 36 of 36, in four steps:
   - **Where the client stands.** A riser rises only while its zone is active, and with no adjacent zones (Nuketown
     has no doors, the layout none) that means occupied: `do_zombie_rise` takes a random spot of the spawner's own
     zone (`zone_init`: `rise_locations`). BO2 scatters each zone's risers round the map edge: 30 of the 36 lie in no
     zone volume (`riser17` belongs to house 1's backyard and stands in the cul-de-sac). The check stood 192 units in
     front of the riser, mostly outside every volume, where the zone manager falls back to the first zone; and at the
     riser's height, under a slope, `riser1`'s zombie reached the client's spot 74 units above it. On an imported map
     the client now stands on a plain pathnode of the riser's zone: the nearest one (height counted 4 times: a zone
     box, the union of BO2's volumes, also holds the upper floors) that `FindPath` reaches from the riser; nearer
     nodes without a path are logged (house 1's backyard pocket at (-921, 1024), house 2's upper floor from
     `riser8`).
   - **One riser at a time.** With 4 to 6 risers a zone, one could go unpicked for 90 s (`riser10`: its zone's 5
     zombies came out of `riser11` 4 times, `riser3` once). For its check the zone offers only that riser, restored
     afterwards.
   - **Spots that hold the client.** House 1's stairs carry actors but not players: the client put on their nodes
     (z 107 to 131) fell to the ground floor, out of the upper floor's zone. Once put down it must still be in the
     zone, or a node 96 units from every spot tried is taken (6 tries).
   - **Jump-ups** (above): the node graph led from the upper floors to the yards only (one-way jump-downs): from the
     east yard (`riser8`), 272 nodes could not be reached, the upper floors among them; with the jump-ups 226, the
     rest lying behind mantles that BO2 crosses one way only, inwards.

   The north ditch (around (700..1500, 1150..1450), `riser28` and `riser29`) had no node graph: `Path_CanLinkNodes`
   walks an actor capsule from each node, and every step hit a terrain facet steeper than an actor walks (hit entity
   1022, the world). Those two checks passed only when a zombie happened to climb the slope without a path. With
   BO2's graph (above) the ditch is linked, and in the last run 4 zombies of `riser28` and the one of `riser29` walked
   up it to the player. With BO2's graph `riser7` failed instead: its zombies stood on house 1's window sill (the
   barrier clips fixed it).

   **Nodes in solid.** 10 layout nodes failed the engine's drop (`node_droptofloor`): the actor capsule, put down on
   the floor, overlapped the world by a fraction of a unit (the GJK trace reports a negative distance) at terrain
   creases, slopes and walls whose 16-unit prisms stand out of a thin wall's far side; an offline box test found
   nothing at 6 of them. `G_Mapkit_LinkPathnodes` now tries a bad layout node again 8 then 16 units around (8
   directions each), as a designer would nudge it, and rebuilds the empty base's node tree if one moved: all 10 found
   a spot (`mapkit: node N in solid at (...), moved to (...)`).

### Not done

- **Drawing (2026-11-09).** `tools/bo2/bo2_mesh.py` cuts the world OBJ into 258 chunks (512 units in the playable
  area, 8 192 beyond), one surface per material (2 088 surfaces, 128 847 triangles, normals smoothed over shared
  vertices), writes each prop model with its LODs (331 models, 700 515 triangles over all LODs, distances from OAT's
  xmodel JSON) and its 2 081 placements, and converts the 254 colour maps used to IW images (DXT1/3/5 kept as they
  are, mips smallest first). The `mesh` edit (`src/gfx_d3d/r_mapkit_mesh.cpp`) builds one rigid XModel per chunk and
  per prop model at the end of `R_FinishLoadingModels` (buffers made by `_XModelOptimize` on the render thread) and
  adds them every frame with `R_FilterXModelIntoScene`, as the skybox model is added (a prop past its last LOD's
  distance x 1.5 is not added); none of them is in the asset database. Each colour map gets one material: a copy of
  `mc/rus_metal_panel03` (a model material, the 32-byte packed vertex), or of the alpha-tested leaf
  `mc/t5_foliage_tree_sorbus_leaf01_static` for the 62 BO2 materials that test or blend alpha (from OAT's material
  JSON), with that colour map, the flat normal map and a black specular map, linked into the database
  (`DB_AddXAsset`) so that the material sort gives it a sorted index. Screenshots (GL backend, llvmpipe) show the
  cul-de-sac, the burnt sidings, fences, hedges, rocks, cars, power poles and the mushroom cloud of the backdrop.
  - The colour map is the base layer's: a BO2 material can hold several colour-map samplers (embers, thermal,
    reveal, specular), so OAT's dumper takes the sampler named `colorMap`, `Diffuse_Map`, `Color_Map` or
    `radiantDiffuseMap` first (the first version took the first one: red ember maps on the walls).
  - **Sky.** The worldspawn's `skyboxmodel` (`skybox_dlc0_zm_nuketown`, a 448-triangle dome) is written as one more
    prop model, its materials flagged as sky; its cube map (`skybox_zm_nuked_ft`, DXT5, 512 pixels, six faces, IWI flag
    4) gets a copy of Five's `mc/mtl_skybox_sp_frontend` (technique `skycubemaphdr`: the cube map sampled by view
    direction, the `skyBase` of the edit). The engine draws it centred on the viewer, as `R_AddSkyboxModel` places a
    map's own skybox. The dump's winding faces outwards, so the dome was culled from inside: the tool writes its
    triangles both ways round.
  - Not done yet: BO2's baked lighting (the light is the empty base's flat grey and the sun), standalone decals (left
    out with caulk and shadow casters), blending (glass is alpha tested), and a `vid_restart` (the mesh is not drawn
    again until the next map load: its buffers belonged to the old device).
- **Prop collision (2026-11-09).** BO2's clipMap lists the 1 349 static models that collide (`smodelcoll.csv`, written
  by the OAT dumper: model, contents, origin, inverse scaled axes; every row matches a drawn placement by name and
  origin). `tools/bo2/bo2_props_collision.py` adds them to the `BO2COL2` file as world brushes, from each model's
  collision LOD: a model at most 160 units across becomes an oriented box around it (865), a larger one keeps its
  triangles as 16-unit prisms (31 465: ramps, roofs, rock piles stay their shape). Only props inside the zones (64
  units around them) are kept (380 lie past the invisible walls), and lights are left out (27: hanging lamps caught
  the player's head). Their contents are BO1's player clip plus the shot-stopping bits (`0x10000|0x2000|0x80`): they
  stop players and bullets but not actors, whose mask (`0x2820011`) leaves the player clip out. Blocking actors too
  put 43 nodes "in solid" (on rock piles, under lamps), changed the node links and dropped the self-test from 31 to 16
  passes; with player clip the graph is the clipMap's again (6 722 links, 15 bad nodes), and BO2's own nodes already
  go round its props.
- Windows checks (`tools/headless.ps1`), and the Switch.
- Dog rounds (Five's dogs are not converted), BO2's own mechanics (the bus, buildables, Who's Who...), its characters
  and animations (T6 skeletons and xanims), its sounds and its baked lighting. The game is BO1's zombies on Nuketown's
  geometry.

### Running it

On the session's machine (paths to adapt), with OAT built as `tools/bo2/oat/README.md` says:

    Z="/mnt/c/Program Files (x86)/Steam/steamapps/common/Call of Duty Black Ops II/zone/all"
    Unlinker --include-assets gfxworld,clipmap,gameworldmp,mapents -o out/?zone? "$Z/zm_nuked.ff"
    Unlinker --include-assets xmodel,image,material --model-format OBJ --image-format DDS -o out/?zone? "$Z/zm_nuked.ff"
    I=<run>/mods/mapkit/imports/zm_nuked
    tools/bo2/bo2_props_collision.py out/zm_nuked zm_nuked --out $I/zm_nuked.collision.bin
    tools/bo2/bo2_layout.py out/zm_nuked zm_nuked --collision-file $I/zm_nuked.collision.bin \
        --out <run>/mods/mapkit/layouts/bo2_zm_nuked.json --links-out $I/zm_nuked.links.txt
    tools/bo2/bo2_mesh.py out/zm_nuked zm_nuked --out $I
    BO1Zombies +set fs_game mods/mapkit +set bo1_mod_mapkit bo2_zm_nuked +devmap zombie_pentagon

The run folder is a normal game folder (the player's converted Five zones); the layout, the collision, links and mesh files
and the images are the player's own extraction and stay out of the repository.
