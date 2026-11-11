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
| Not yet extracted | the collision of the 1 349 static models with collision (cars, fences: it comes from their xmodels), `comworld` lights (the primary spot lights: the houses' lamps), the T6 pathnode graph (`gameworldmp`; the nodes are also in the entities) |

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
     which plays it and moves the zombie each frame so that it follows the animation's own path. The rise is scaled
     to the end node's height, and the gap to the end node's spot is spread over the first 80% of the animation. The
     animation goes 32 units forward and 186 up, so the zombie lands on the end node, not in the ceiling. A first
     version corrected only the height, linearly over the first half (`teleportThreadEx`, as retail corrects a
     traverse's height). The jump into house 1's attic, `(-828, 398, 88) -> (-870, 301, 160)`, is 105 units long:
     its zombies landed 70 units short, in the attic's edge, and stayed there. The same correction also sank them
     under the floor while the animation crouched before its jump. Logged on that jump, every zombie now ends on the
     end node to a thousandth of a unit. BO2's mantles cross obstacles up to 84 units deep, where Five's wall hop (`zombie_mantle_over_40`'s
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

   Rerun at 6472db7: `pass 36 fail 26`, the client stuck by and on house 2. The test client's walk
   (`SP_HeadlessWalk`, `g_sp_client.cpp`) then took these fixes:
   - `Path_FindPath` without negotiation links fails as soon as its best route has one; it does not look for another.
     The client then walked straight at the goal into a wall. It now plans with the traversals when it must.
   - Across a traversal (or the plain link the graph also holds back from its end to its begin, the fence by
     Juggernog) the client walks at the end node and jumps, which takes it over low fences and off ledges. After 3
     seconds short of the end (a zombie's jump up to an upper floor) the harness carries it there.
   - A client no node sees within 192 units gets no path. It plans from the last node it reached instead, or heads
     back to that node. Back there with still no path (a carry over a jump up left it in house 1's attic), it is
     carried to the goal after 10 seconds.
   - A walk that has not covered 32 units in 3 seconds sidesteps. After 10 seconds heading for one node, the harness
     carries the client to the node after it. Zombies climb debris piles a player cannot, since props collide with
     players only.
   - A point counts as reached only within 48 units of height, and purchases wait 35 seconds longer when walking
     (`mapkit_test_wait`): the box to Juggernog is about 3 500 units.
   - A riser check starts its 90 seconds once the walking client reaches its stand point (60 seconds at most).

   The carries need `bo1_testclient_carry 1`, which the self-test sets on another game's map only: a MapKit layout's
   walk must make it on its own. Every carry is logged (`bo1_testclient: ... carried over traversal`,
   `... carried past a link it could not walk`, `... carried to the goal with no path`), and a blocked client every 2
   seconds (`... stuck N ms at`).

   With these fixes and the jump-up fix (Traversals, above: house 1's attic), a full walking run gives
   `mapkit_selftest DONE pass 62 fail 0`. The client was carried 31 times over traversals, 50 times past links and 9
   times to a goal. Before the attic fix the walk gave `pass 58` to `pass 60`, with fails that changed from run to
   run: risers, Pack-a-Punch (bought too late) or two wall buys. The default mode places the client and stays the
   reference: `pass 62 fail 0` on the same build with no carry, and `entrances_example` `pass 6 fail 0` with no
   carry.

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
  `mc/t5_foliage_tree_sorbus_leaf01_static` for the BO2 materials that test or blend alpha (from OAT's material
  JSON; decals and glass since 2026-11-10, below), with that colour map, the flat normal map and a black specular map, linked into the database
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
  - **Windows (2026-11-10).** The first Windows client crashed at Nuketown's first frame
    (`R_StreamUpdate_AddDynamicXModelDistance`). The image streamer indexes each scene model by its place in the
    asset database's XModel pool (`DB_GetXModelIndex`), and these models are outside it: the pointer difference was
    taken as an index, which wrote far outside the streamer's arrays. Linux had shown nothing, because its runs were
    headless with no renderer. The streamer now skips a model outside the pool (`DB_IsXModelInPool`), since their
    images are loaded whole, not streamed. A Windows client (`tools/headless.ps1 -Client`, D3D9) then draws the map and
    plays on to its scheduled quit (219 s with the load), with a normal exit.
  - The scene takes 1 024 models a frame (`GfxScene::sceneModel`). The chunks and the visible props go past it, and
    `R_AllocSceneModel` drops the rest ("too many existing models (more than 1024)"). The props are added nearest
    first, sorted each frame by the distance to their bounding sphere, so the dropped ones are the farthest. The sort
    uses the sphere, not the origin, because a prop's origin can lie far from its geometry. The clock tower's origin is
    1 000 units off, and the tower was dropped in front of the viewer, leaving its clock hanging in the sky. A first
    version added the props within 1 024 units first but in file order, and in front of a house the budget ran out
    before the doors, which `bo2_mesh.py` writes last.
  - **A larger scene (2026-11-10, asked by the user).** `r_mod_sceneModels` (1024 to 4096, default 1024, ZmClient's
    Lancement view: "Modèles affichés par image") raises that budget. The renderer frustum-culls before it counts
    (camera and sun-shadow views), so the 1 024 were all in view: from the street, 2 092 to 2 099 models are wanted a
    frame, and at 4096 the clock and the distant mountain come back.
    - The next limit was the per-frame surface buffer (`GfxBackEndData::surfsBuffer`). A draw surface's 16-bit
      `objectId` addresses it in 4-byte units, so it holds 256 KB. At 1 024 models, Nuketown already filled 220 to
      231 KB of it; at 4096 it wanted 305 to 313 KB a frame and dropped the rest, zombies included, since DObjs share
      the buffer.
    - On 64-bit every entry of that buffer is a multiple of 8 bytes, so the unit is now `R_SURF_UNIT` (8 on 64-bit,
      retail's 4 on 32-bit) and the buffer 512 KB. The hidden-surface marker (`-3`) takes one unit too.
    - Each model past retail's also needs a model lighting entry: ZmClient passes `r_mod_modelLightingEntries` at the
      count + 1 024 (5 120 at 4096: none refused; with the retail 1 024, the test run logged one `MODEL_LIGHTING_EXHAUSTED`).
  - **Props drawn inside out (fixed 2026-11-10).** The engine draws a triangle whose right-hand normal points away
    from the viewer, BO2's own winding, which the world OBJ keeps. `bo2_mesh.py` wound the props the other way, with
    their right-hand normals out, as `bo2_props_collision.py` needs for its prisms. Every prop then showed its back
    faces. The clock's face turned to the tower with its numbers mirrored, signs and the garage door were missing,
    and inner faces showed through walls. `read_lods` now reverses the props' triangles for drawing only.
  - **Wall buys.** Retail `init_weapon_upgrade` hides a wall buy's weapon model until its first purchase, since a BO1
    map shows the weapon with a chalk drawing. Nuketown has no such drawing (BO2 draws its chalk by script), so on
    another game's map MapKit shows the models from the start (`mapkit_show_wallbuys`, `_mapkit.gsc`).
  - **Doors and debris (2026-11-10).** A first version left them out, wrongly. BO2 has 6 doors and 5 debris on
    Nuketown, each a group of `zombie_door` / `zombie_debris` triggers that share a target: the house fronts (750),
    the back doors and the upper floor door (1000), the stairs' debris (1000 and 1250), the truck (3000) and the ammo
    room (3000).
    - `bo2_layout.py` makes each group a MapKit `door` or `debris` with BO2's cost. Its box is the targeted brush
      models' (the door panel, the debris clip), placed by entity origin plus submodel bounds from the collision
      dump. Its zones come from the `script_flag` (`culdesac_2_openhouse2_f1`; `culdesac` is the nearer of the
      yellow and green zones). A walk from the start zones gives the one it unlocks.
    - `bo2_mesh.py` writes the group's script models (BO2's white and brown doors, couches, chairs, armoires) as
      props, plus a table of groups at the end of the file. The engine draws them until the purchase
      (`CM_Mapkit_EditShown`), in place of the kit's box.
    - The self-test buys all 11 and checks each zone's unlock. Its `door_graph` check counts the active links through
      the doorway on an imported map: the houses also join the street through their windows.
    - A cost retail has no hint for (its `default_buy_debris_<cost>` and `default_buy_door_<cost>` stop at 2000) showed
      "UNDEFINED!! PLEASE SET THE SCRIPT_HINT ON THE ENT PROPERLY!" on Nuketown's two 3000 debris (the truck and the
      ammo room). No localized string takes the cost as a parameter, so `_mapkit.gsc` (`mapkit_blocker_hints`)
      registers the missing ones with retail's text, unlocalized: "Hold [use] to Clear Debris [Cost: 3000]".
  - **Mystery box (2026-11-10, reported by the user).** The first layout kept only the start chest. With one chest,
    retail's `treasure_chest_init` never moves the box and never shows its beam. `bo2_layout.py` now writes BO2's five
    chest locations, "start" on its two start chests (retail picks one of them). Retail's beam (`lght_marker`, Five's
    `fx_zombie_coast_marker`) is a faint additive column made for a dark sky, and hardly shows over Nuketown's
    daylight sky. So on an imported map `_mapkit.gsc` (`mapkit_pandora_fx`, as `level.pandora_fx_func`) stacks the
    same effect, two on the chest and two every 1 000 units up to 5 000, deleted with the retail light when the box
    moves. Checked on D3D9 client screenshots: the beam stands over the box from across the map (about 2 900 units
    away).
  - **Machine clips (2026-11-10, reported by the user: "I walk through the mystery box").** MapKit puts a hidden
    player and monster clip around each box, perk machine and Pack-a-Punch (`CM_Mapkit_AddMachineClips`), but its
    table held 256 edits: on Nuketown (about 980 edits) the boxes, perks and Pack-a-Punch come past edit 850 and had
    none. It now holds one per edit (`MK_MAX_SPOTS` = `MK_MAX_EDITS`, 1 024).
  - **Baked light on the props (2026-11-10).** BO2 bakes the light of a static model placement into one colour per
    vertex (`GfxStaticModelDrawInst::lmapVertexInfo`, per LOD). The OAT dumper writes them to
    `<map>.d3dbsp.smodellight.bin` (`BO2SMLT1`: per placement, its LODs' colours; per model, its LODs' vertex
    positions in the same order, since the OBJ export reorders and splits vertices). `bo2_mesh.py` finds each OBJ
    vertex's colour by its position (exact to 1/100 unit for 97 %, else the nearest within 1 unit), gives each lit
    placement its own copy of the model, and writes `BO2MESH5`: a `u32` colour after each vertex. The engine puts it in
    the packed vertex colour, which BO1's model shaders multiply with the colour map.
    - The first version read the stored colour as `0xAARRGGBB`, doubled RGB by eye and dropped the alpha. BO2's lit
      static model shaders (`lprobe` techniques, read with `fxc /dumpbin`) say otherwise, and it was corrected on
      2026-11-10 (below).
    - 716 of the 2 082 placements carry colours. The other 1 366 have none (BO2 lights them from its light grid) and
      stay white.
    - Side by side with the unlit mesh (D3D9, same views): the truck and the couch are no longer washed out, the
      street's rubble stands out, and the freezer in house 1 is green again.
    - Cost: 333 prop models become 898 (a model whose placements are all lit copies is left out), the mesh's vertices
      go from 0.93 M to 1.84 M (+29 MB at 32 bytes each) and the file from 35 to 78 MB. The number of props drawn, and
      so the 1 024-model scene budget, does not change.
  - **The world's lightmaps (2026-11-10, asked by the user).** The world surfaces get BO2's own light as vertex
    colours (`tools/bo2/bo2_worldlight.py`, called by `bo2_mesh.py`).
    - The pixels are in the fastfile after all: three uncompressed RGBA images (`*lightmap0..2_secondary`, 2048, 1024
      and 512 wide, 3072 high), each three slices of 1 024 rows stacked. A world vertex is 36 bytes: xyz, binormal
      sign, colour, texCoord (2 halves), two packed 10:10:10 vectors, then lmapCoord as two 16-bit unorms (u low,
      v high), over one slice. The OAT dumper writes them per OBJ vertex (`worldlight.bin`) with each surface's
      lightmap and primary light, and the sun (`worldlight.txt`).
    - The decoding is BO2's own: its `lmap` pixel shaders, disassembled with `fxc /dumpbin`, light a pixel with
      `A + B * saturate(dot(dir, N)) + vis * sunDiffuse * saturate(dot(sunDir, N))`, where `A` and `B` are slices 0
      and 1 as `rgb / a`, `dir` is slice 2's `rgb * 2 - 1` and `vis` its alpha (the sun's baked visibility, the shadow
      map taking over near the viewer), then output `sqrt(albedo^2 * light * exposure)`. On Nuketown the sun is
      `(4.78, 11.64, 15)` from `(-0.41, -0.35, 0.84)`, and sunlit ground gets about `(8.9, 15.5, 18.8)`.
    - BO1's model shaders multiply the colour map by the vertex colour, so the colour is `sqrt(light / reference)`,
      the reference being the 75th percentile of the sunlit corners: sunlit ground looks as before, the shade
      darkens and takes BO2's bounce (green in the green house's kitchen). `LIGHT_FLOOR` (0.12) keeps the darkest
      surfaces from black.
    - A vertex colour only follows the lightmap at the vertices, so a triangle is split where the light at its edge
      midpoints or centre differs from the corners' by more than 0.06, down to 16 units (about one texel): 123 099
      splits, 131 032 triangles become 252 000 (175 000 vertices become 303 000).
    - Checked on D3D9 client screenshots before and after, same views: shaded façades, eaves, sidewalks and rocks,
      the houses' interiors darker and tinted; no crack along the split edges.
    - **The lamps (2026-11-10, asked by the user).** A surface's primary light can also be one of `comworld`'s:
      Nuketown has 4 spots over the street (type 2, `white_light`) and 12 lamps in and around the houses (type 5,
      `white_light_cube`, a cube shadow map: omni lights), green, orange and blue. OAT's new
      `ComWorldLightsDumperT6` writes them (`<map>.d3dbsp.lights.csv`), and the bake adds, for a surface whose
      primary light is one of them, `vis * colour * saturate(dot(L, N)) * min(dAttenuation / d^2, 1) * cone *
      window` (L toward the light, `window` = `(1 - (d / radius)^2)^2`, a cone only for the spots).
    - A spot's `dir` points from the lit area to the light, as the sun's does: every spot's surfaces lie at cos
      -0.97 to -0.82 of it. Read the other way, the cone cut off almost every lamp. The lamps' surfaces lie on every
      side of theirs, so they get no cone.
    - BO2 shapes the cone and the end of the range with constants (`lightFallOffA/B`, an attenuation texture) the
      bake approximates with those curves. Checked on D3D9 screenshots before and after: house 1's living room and
      stairs lit, its kitchen ceiling green from the green lamps, house 2's upper floor warm.
  - **The props' light as BO2 computes it (2026-11-10, asked by the user).** The vertex shader of a lit static model
    takes the baked colour `c` as an indirect light `32 * c.rgb^2` (divided by the light grid's value at the vertex
    normal, which the pixel shader multiplies back at the pixel's normal, so about 1), and passes `c.a` to the pixel
    shader as the sun's visibility: `L = 32 * c.rgb^2 + c.a * sunDiffuse * saturate(dot(sunDir, N))`, then
    `sqrt(albedo^2 * L * exposure)` as the world. `bo2_mesh.py` (`prop_light`) now computes that `L` with each
    vertex's normal turned by its placement, and turns it into a colour with the world's reference and floor
    (`WorldLight.colour`): props and world share one exposure, and `LIGHT_SCALE` is gone.
    - The bytes are R, G, B, A in memory (`0xAABBGGRR` as a `u32`), not `0xAARRGGBB`: the first version swapped red
      and blue. Measured against the world's lightmaps, whose indirect light in the shade is red-poor (R / G 0.75,
      B / G 1.03): the props' low byte gives R / G 0.73 and the third byte B / G 1.03. Sunlit props (alpha above 220)
      get an indirect `(4.5, 5.5, 5.9)` against the world's `(2.6, 3.5, 3.6)`: the same scale.
    - What stays approximate: the reference itself (the 75th percentile of the sunlit ground maps to the colour map
      as it is). BO2 then applies its exposure, tone curve and colour grading, which MapKit does not reproduce.
  - **Decals and glass (2026-11-10, asked by the user).** `bo2_mesh.py` (`blend_flags`) reads the state of each
    material's lit pass. BO2 blends almost all its see-through materials (fences, leaves, overlays), after a depth
    prepass that writes their depth first; BO1 has no such pass for these models, and a hedge blended without it
    would draw its leaves out of order. So:
    - a layer that lies on another surface (a depth offset on the lit pass and a decal sort below BO2's 40, or a decal
      or overlay name: `decal_`, `_dec`, `_blend`) is a decal, flag 4: 45 materials on Nuketown (cracks, rugs,
      burnt floors, radiation puddles, dirt and ash overlays). The engine draws it with a copy of the blended base
      (`blendBase`, default `mc/global_glass`: premultiplied blend, alpha above 0, no depth write) with the decal
      depth offset added to its states (`loadBits[1]` 0x10, `R_ForceSetPolygonOffset`) and sort key 29, the sort of
      Five's own blended model decal (`mc/mtl_veh_jet_f4_decals`), between the opaque materials and the glass (43).
      Five has no model decal to copy as it is: its decals either test alpha or carry samplers of their own;
    - six decals multiply the colour under them (`zero / srccolor`: burn stains, the blast crater, a wall fillet, red
      numbers). No base material multiplies, so the tool converts their colour map to DXT5 black with alpha
      `1 - luminance`: blended over the surface it gives `dst * luminance`, the same darkening for grey maps (the red
      numbers lose their red);
    - glass, blended but not a decal, is flag 8 and draws with the blended base: Nuketown's broken glass and two
      character materials;
    - every other blended or alpha-tested material stays alpha tested (36 drawn on Nuketown).
    Decals that overlap draw in the order of their materials, not BO2's sort keys.
    - Checked on D3D9 client screenshots before and after, same views: the props lose their yellow cast (the truck is
      grey, the couch beige, the kitchen's stove and freezer green), the green radiation puddles lie on house 1's
      floors, the street keeps its cracks, the garage door its burns, the mountains of the backdrop (a decal-sorted
      BO2 material, `mc/mtl_p6_zm_vista_mountain`, whose colour map the first versions left out) stand behind the
      clock, and the burnt walls and floors drawn as decals show no holes. All 274 colour maps load.
  - **`vid_restart` (2026-11-10).** The mesh's vertex and index buffers are `D3DPOOL_DEFAULT` ones, which a device
    Reset needs released: `vid_restart` after a mode change, or a full-screen window losing its device. The engine
    releases the database's models (`R_ReleaseLostDeviceAssets2`) and rebuilds them (`R_RecoverLostDevice`), but
    these models are outside the database, so the Reset was attempted with them alive. Both functions now call
    `R_Mapkit_ReleaseMeshBuffers` / `R_Mapkit_RestoreMeshBuffers` (`mapkit: mesh buffers rebuilt on the reset device`
    in the console). Checked with two `vid_restart`s after a `r_vsync` change on the D3D9 client: both recover the
    device, rebuild the buffers (0 surfaces without) and draw the map as before. That test client runs a D3D9Ex device
    (`bo1_headless_client`), whose Reset does not require the default pool released, so it could not show the
    failure a plain D3D9 device would have had: the fix follows D3D9's rule, not an observed crash.
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
  and animations (T6 skeletons and xanims), its sounds and its dynamic lights. The game is BO1's zombies on Nuketown's
  geometry.

### TranZit (`zm_transit`, 2026-11-10)

TranZit goes through the same tools, with what its size and its bus needed:

- **Dump.** 449 of its 511 prop models live in the streamed `zm_transit_gump_*` zones, and the map's XModels are
  references named `,<name>`. The OAT dumper drops the comma (also in `smodelcoll.csv` and `smodellight.bin`), and the
  gump dumps are merged into the map's (`rsync -a --ignore-existing zm_transit_gump_*/{xmodel,model_export,images,materials}/`).
  Its vertex buffer ends 20 bytes past a whole vertex: only the counted vertices must fit.
- **Scale.** 26 000 x 18 000 units, 5 350 pathnodes, 40 703 links, about 45 zones, 424 000 collision brushes with
  70 000 terrain prisms, 690 000 world triangles and 10 866 props. MapKit's tables grew (JSON values 65 536, strings
  1 MB, edits 8 192, boxes 2 048, brush models 512, marks 2 048, extra links 65 536). BO1's node visibility tests
  every node pair at load: on 5 350 nodes it never finished. A layout with more than 2 000 nodes gets
  `"nodeVisRange": 2048`, which skips the pairs farther apart (`Path_BuildNodeVis`), and the map loads in about
  4.5 minutes on the Linux headless build. MapKit's node link pool held 32 768 links: TranZit needs 42 255, and the
  nodes past the limit silently got none (1 466, found through two doors whose doorway no link crossed). It holds
  131 072 now and logs its use (`mapkit: link pool N of 131072 used`) or its overflow.
- **A crash.** A player among the power station's dense terrain prisms touched more than 128 planes at once:
  `gjk_push_out` wrote through the NULL of the full plane array (segfault on the Linux server). The extra contacts are
  left out now (`bg_slidemove.cpp`, `add_hit_info`).
- **Game modes.** One map holds TranZit's classic mode and the Survival and Grief locations (town, farm, depot). Its
  `zm_perk_machine` structs and power switch carry the gametype in `script_string` / `script_gameobjectname`, its
  wall buys in `script_noteworthy` (`zrace_town`, `zstandard_farm, zgrief_farm`...): `bo2_layout.py` keeps the
  classic ones (and the wall buys without one). The wall buy on the bus (`bus_buyable_weapon1`) is left out: it would
  stay behind when the bus drives off. Double Tap stands in the barn's loft, whose floor is a clip brush only: a
  machine stands on the walkable floor when BO2's spot is on it and the solid floor is far below.
- **Doors.** 15 doors: their `script_flag` often names no zone, so a blocker takes the two zones beside its box.
  `local_electric_door` doors are power doors (MapKit `"power": 1`, cost 0): they open when the power comes on.
  Two (`pf1766_auto2357`, `pf1766_auto2358`) have no zone beside them and are left out.
- **Machines and power.** Juggernog, Quick Revive, Speed Cola, Double Tap, Pack-a-Punch (the buildable one's spot)
  and the power switch at the classic spot, no building needed. Stamin-Up's machine and bottle come with another
  map's zone (`bo1_mod_zones zombie_cosmodrome`): MapKit places it only when a loaded zone holds
  `zombie_perk_bottle_marathon` (`bo1_mapkit_weaponloaded`); ZmClient sets that zone for TranZit. Tombstone has no
  BO1 machine. BO1 sells a player 4 perks: with Stamin-Up, a fifth machine stays refused until a perk is lost (the
  self-test drops one before buying the next).
- **The bus.** BO2's bus is a `script_vehicle` driven along `info_vehicle_node` chains. Here it is a MapKit `bus`
  edit (`mods/mapkit/MAPKIT.md`): the loop is the chain through the depot's stop node (278 points), the stops are the
  nodes whose `script_notify` is `reached_stop_point` (depot, diner, farm, power station, town), with BO2's
  `script_float` as the wait. Its collision is ten layout brush models (floor, a step outside each door, walls with
  the door openings, roof) that `_mapkit.gsc` moves point to point at 440 units a second; a player standing on the
  floor rides as on Five's elevators. The mesh edit draws the bus model (`bo2_mesh.py --mover`) on the floor's
  brush model (`R_Mapkit_SetMoverPose`, from `CG_ScriptMover`). The 23 pathnodes inside and on the parked bus are
  left out (they would be a floating island), and the layout's brush models are off while MapKit drops its nodes,
  so the nodes beside the parked bus are not left in solid. The loop's points stand on what an actor walks on:
  on the solid brushes alone, five points by the diner fell 2 400 units under the road (terrain is prisms) and the
  bus dove with them, leaving its rider behind. The front wall stands behind the windshield (model x 368), not at the
  bumper (449): a rider pushed forward had the eye past the glass, where the one-sided faces showed no bus.
  The engine carries a rider along a mover's translation but not around its turns: the rider slides until a wall
  holds it, then stays there. The self-test's `bus` check boards the client at the depot, on the floor's centre, and
  finds it on the floor at the diner. On a Windows client (D3D9, `mapkit_bus_log 1` logs the floor and the players
  at every point) the player rode a whole loop and on, 8 units over the floor at every point, against the rear wall
  after the first sharp turn, the bus drawn around them. Zombies do not board it, it has no doors, horn, lights or
  driver, and it does not wait for the players: a player who misses it waits a loop (4 minutes 55).
- **Self-test** (Linux headless, `bo1_mod_zones zombie_cosmodrome`, `mapkit_test_risers riser1,riser9`, the run
  folder holding Ascension's converted zone): `pass 113 fail 0`: the 15 doors (10 bought, their zones unlocked, 5
  opened by the power), the box, the five machines, Pack-a-Punch, the six wall buys, the power switch, two risers
  and the bus ride. `pow_door_rr` is a SKIP: BO2's graph reaches its room by stairs below the doorway, so no link crosses
  it.
- **Not done.** The props are unlit (TranZit's placements carry no `smodellight` colours), the fog and the lava,
  the buildables, Denizens, the Avogadro, the bank, the Jet Gun and the other BO2 weapons.

#### The user's first TranZit run (2026-11-11)

- **A bridge walked on but not drawn, doors not drawn.** They are BO2 brush models (`script_brushmodel`: the two
  `bridge_section`s, the panels of `auto2389`, `pow_door_rr`, `pf1766_auto2355`...), whose surfaces the world OBJ
  never held (it has the static surfaces only). The OAT dumper writes `<map>.d3dbsp.bmodels.obj` (one group per
  `GfxWorld::models[m]`, positions relative to the entity's origin) and `bo2_mesh.py` makes each a prop model: in
  its blocker's group for a door, standing for the bridge (`STANDING_BMODELS`).
- **Huts a player could not enter, stairs that only went down.** `bo2_props_collision.py` made every prop up to 160
  units across one oriented box: a door frame (`p_cub_door_wood_frame01`) was a solid box across its doorway, the
  power station's stairs (126 x 151 x 82, ten 8-unit steps) one box 82 units high that a player dropped off and never
  climbed. A small prop keeps its triangles when a player could pass through it (a quarter of the lines across its
  thinnest axis hit nothing) or climb it (4 upward faces each within a step of the next): TranZit 527 000 prisms
  instead of 400 000, Nuketown 76 000 instead of 31 000.
- **The bus drove backwards, its doors shut.** Its brush models spawn with angles 0 while the loop starts at 179.5
  degrees: the mesh edit's `moverYaw` turns the model by that heading. The doors are folding panels in the model's
  exterior surfaces (the OBJ has no bone weights): `bo2_mesh.py` cuts the triangles inside the two door openings
  (`MOVER_HOLES`).
- **Barricades not rebuildable.** BO2's 38 `zbarrier_zmcore_BasicWoodBarrier` become MapKit windows (Five's prefab:
  six boards zombies tear off and players rebuild). A zbarrier stands about 40 units in front of its wall: the window
  goes on the wall, found by walking in along its yaw (placed at the zbarrier, its inner traversal node fell into the
  wall), the mantle BO2's barricade traversal was is left out, and a window no BO2 spawner feeds gets a spawner 128
  units outside it (7). Five's boards are narrower than BO2's openings and washed out by the lighting there. Self-test:
  `pass 185 fail 12`: 33 of the 38 windows had a zombie tear the boards, climb in and reach the player; the others
  are an upper-floor window of the farmhouse (its outside is a roof), windows a spawner's path does not reach, and
  zones opened by a door whose windows saw no zombie within 90 s.
- **A very long load.** Timings now logged (`mapkit: collision edit N loaded in`, `node load times`, `mesh load
  times`). Measured on Windows: the node visibility at 2 048 units took 28 s (55 s on Linux), the mesh's models 31 s
  (BO1's `Vec3PackUnitVec` tries 256 scales for each of 3 million vectors), and a listen server imported the
  collision twice (server and client, 10 s each). Now: visibility within 1 024 units (7.6 s on Linux), each distinct
  normal packed once (524 000 of 1.47 million: 18 s), the client reuses the server's import when the file and cuts
  are the same (0.45 s). A Windows client's load went from about 150 to 120 s, most of the rest being the zones.

### Running it

On the session's machine (paths to adapt), with OAT built as `tools/bo2/oat/README.md` says:

    Z="/mnt/c/Program Files (x86)/Steam/steamapps/common/Call of Duty Black Ops II/zone/all"
    Unlinker --include-assets gfxworld,clipmap,comworld,gameworldmp,mapents -o out/?zone? "$Z/zm_nuked.ff"
    Unlinker --include-assets xmodel,image,material --model-format OBJ --image-format DDS -o out/?zone? "$Z/zm_nuked.ff"
    I=<run>/mods/mapkit/imports/zm_nuked
    tools/bo2/bo2_props_collision.py out/zm_nuked zm_nuked --out $I/zm_nuked.collision.bin
    tools/bo2/bo2_layout.py out/zm_nuked zm_nuked --collision-file $I/zm_nuked.collision.bin \
        --out <run>/mods/mapkit/layouts/bo2_zm_nuked.json --links-out $I/zm_nuked.links.txt
    tools/bo2/bo2_mesh.py out/zm_nuked zm_nuked --out $I
    BO1Zombies +set fs_game mods/mapkit +set bo1_mod_mapkit bo2_zm_nuked +devmap zombie_pentagon

The run folder is a normal game folder (the player's converted Five zones); the layout, the collision, links and mesh files
and the images are the player's own extraction and stay out of the repository.

`tools/make_dist.sh --bo2 <run> windows` puts that extraction into a package, the layout under `mods/mapkit/layouts/`
and the files under `mods/mapkit/imports/`. ZmClient's Solo view then offers "Mapkit : Nuketown (Black Ops II)", which
starts `+set fs_game mods/mapkit +set bo1_mod_mapkit bo2_zm_nuked +devmap zombie_pentagon`.
