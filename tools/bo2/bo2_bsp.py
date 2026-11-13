#!/usr/bin/env python3
"""A BO2 map as a native BO1 map: its world BSP and the OAT Linker (T5) project of its zone.

    tools/bo2/bo2_bsp.py <dump folder of the zone> <map name> --templates <BO1 wc/ material JSONs> --out <folder>
        [--collision <BO2COL2 file>] [--name zombie_bo2nuked] [--layout <layout> [--blocker-mesh <path>]]
    tools/bo2/bo2_bsp.py <dump> <map> --out <folder> --layout <layout> [--blocker-mesh <path>] --layout-only

Reads OAT's dump of the BO2 zone (tools/bo2/oat: the world OBJ and .mtl, the BO2COL2 collision, the entities) and
writes:
- <out>/<name>.d3dbsp: the world (tools/bo2/d3dbsp.py): every static surface of the world OBJ as world surfaces, one
  BO1 material per BO2 material; the clipMap's world brushes with their own sides, its terrain triangles as BO1's
  collision triangles (partitions); the worldspawn and the BO2 player start;
- <out>/linker/: an OAT Linker project (zone_source/<name>.zone, zone_raw/<name>/...) holding one BO1 world material
  per BO2 material (wc/bo2w_<name>), each a copy of a BO1 template from --templates (OAT's Unlinker output of a BO1
  map zone's materials/wc/) with BO2's colour map, the map's script, and the colour maps as images/<name>.iwi;
- <out>/images/: the same .iwi files, which the game loads from main/images/ (they are not embedded in the zone).

Templates: global_white (opaque, wc_l_sm_r0c0), mtl_theater_poster_1 (alpha tested, wc_l_sm_t0c0x0), decal_glue
(blended, wc_l_sm_b0c0x0); all draw the colour map with an identity normal map. --collision takes a BO2COL2 file with
the props added (tools/bo2/bo2_props_collision.py); by default the dump's own.

The output is the player's own game data: keep it out of the repository. docs/BO2_MAPS.md has the procedure.
"""
import argparse
import hashlib
import json
import math
import os
import re
import shutil
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import d3dbsp  # noqa: E402
from bo2_layout import Collision, read_entities, zone_edits  # noqa: E402
from bo2_bsp_props import PropModels, place_props, prop_collision  # noqa: E402
from bo2_mesh import (FLAG_ALPHA_TEST, FLAG_BLEND, FLAG_DECAL, blend_flags, dds_to_iwi, image_name,  # noqa: E402
                      read_mtl, read_obj, skipped)
from bo2_props_collision import PROP_CONTENTS  # noqa: E402
from bo2_worldlight import NO_LIGHTMAP, WorldLight, read_world_light  # noqa: E402
import bo2_bsp_light  # noqa: E402

TEMPLATES = {0: "global_white", FLAG_ALPHA_TEST: "mtl_theater_poster_1", FLAG_DECAL: "decal_glue",
             FLAG_BLEND: "decal_glue"}
GSC = """main()
{
\tlevel thread maps\\_callbacksetup::SetupCallbacks();
\tmaps\\_zombiemode_load::main();
}
"""


SURFACE_CELL = 1024.0       # world surfaces are cut into cells this wide in the play area,
FAR_SURFACE_CELL = 8192.0   # and this wide in the distant backdrop
PLAY_HALF = 4096.0


def surface_cell(p):
    if abs(p[0]) < PLAY_HALF and abs(p[1]) < PLAY_HALF:
        return (0, math.floor(p[0] / SURFACE_CELL), math.floor(p[1] / SURFACE_CELL))
    return (1, math.floor(p[0] / FAR_SURFACE_CELL), math.floor(p[1] / FAR_SURFACE_CELL))


def bsp_material_name(bo2):
    clean = re.sub(r"[^a-z0-9_]", "_", bo2.lower()).strip("_")[:40]
    return f"bo2w_{clean}_{hashlib.sha1(bo2.encode('latin-1')).hexdigest()[:6]}"


def read_collision(path):
    """BO2COL2 (tools/bo2/oat/.../ClipMapCollisionDumperT6.cpp): materials, brushes, terrain vertices and triangles."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"BO2COL2\0":
        sys.exit(f"{path}: not a BO2COL2 file (redump it with tools/bo2/oat)")
    at = 8
    (count,) = struct.unpack_from("<I", data, at)
    at += 4
    materials = []
    for _ in range(count):
        name = data[at:at + 64].split(b"\0")[0].decode("latin-1")
        sflags, cflags = struct.unpack_from("<ii", data, at + 64)
        materials.append((name, sflags, cflags))
        at += 72
    (count,) = struct.unpack_from("<I", data, at)
    at += 4
    brushes = []
    for _ in range(count):
        mins = struct.unpack_from("<3f", data, at)
        maxs = struct.unpack_from("<3f", data, at + 12)
        contents, owner = struct.unpack_from("<ii", data, at + 24)
        acf = struct.unpack_from("<6i", data, at + 32)
        asf = struct.unpack_from("<6i", data, at + 56)
        (sides,) = struct.unpack_from("<I", data, at + 80)
        at += 84
        side_list = []
        for _ in range(sides):
            n = struct.unpack_from("<3f", data, at)
            d, cf, sf = struct.unpack_from("<fii", data, at + 12)
            side_list.append((n, d, cf, sf))
            at += 24
        # axial flags as [side][axis] (index 0: the min side)
        axial = [(acf[s * 3 + a], asf[s * 3 + a]) for a in range(3) for s in (0, 1)]
        brushes.append((mins, maxs, contents, owner, axial, side_list))
    (count,) = struct.unpack_from("<I", data, at)
    at += 4
    verts = [struct.unpack_from("<3f", data, at + 12 * i) for i in range(count)]
    at += 12 * count
    (count,) = struct.unpack_from("<I", data, at)
    at += 4
    tris = [struct.unpack_from("<5H", data, at + 10 * i) for i in range(count)]
    return materials, brushes, verts, tris


def write_bsp_layout(layout, layout_path, out, blocker_mesh):
    """<out>/<layout>_bsp.json: the layout for this BSP. The BSP draws and collides the world, so the layout keeps its
    gameplay edits without its collision and mesh edits. Its door and debris edits keep their meshGroup when
    blocker_mesh (a path under mods/mapkit) names bo2_mesh.py --blockers-only's file: a mesh edit of that file, with
    the world mesh edit's other keys, draws BO2's doors and furniture until the purchase; without it each blocker
    shows MapKit's box."""
    world_mesh = next((e for e in layout["edits"] if e["kind"] == "mesh"), None)
    edits = []
    for e in layout["edits"]:
        if e["kind"] == "mesh" and blocker_mesh:
            edits.append(dict(e, file=blocker_mesh))
        elif e["kind"] not in ("collision", "mesh"):
            edits.append(e)
    if not (blocker_mesh and world_mesh):
        for e in edits:
            e.pop("meshGroup", None)
    name = os.path.splitext(os.path.basename(layout_path))[0] + "_bsp.json"
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, name), "w", encoding="utf-8") as f:
        json.dump(dict(layout, edits=edits), f, indent=1)
    return name


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dump", help="OAT's output folder for the zone")
    parser.add_argument("map", help="the BO2 map's name, e.g. zm_nuked")
    parser.add_argument("--templates", help="OAT's dump of a BO1 map zone's materials/wc folder (needed but with "
                                            "--layout-only)")
    parser.add_argument("--out", required=True)
    parser.add_argument("--collision", help="a BO2COL2 file (default: the dump's)")
    parser.add_argument("--name", default="zombie_bo2nuked", help="the BO1 map's name (a zombie_ name)")
    parser.add_argument("--layout", help="the map's MapKit layout (tools/bo2/bo2_layout.py): its collision edit's cut "
                                         "boxes leave the same brushes out, and <out>/<layout>_bsp.json is written for "
                                         "this BSP (without the collision and mesh edits it replaces)")
    parser.add_argument("--models", help="OAT's dump of the zone's xmodels and materials with --model-format GLTF: "
                                         "adds the placed props (bo2_bsp_props.py)")
    parser.add_argument("--blocker-mesh", help="the doors' and debris' props for the layout's mesh edit, a path "
                                               "under mods/mapkit (bo2_mesh.py --blockers-only: imports/<map>/"
                                               "<map>.blockers.mesh.bin); without it the blockers show MapKit's box")
    parser.add_argument("--layout-only", action="store_true", help="write <out>/<layout>_bsp.json only (--layout)")
    args = parser.parse_args(argv)
    if args.layout_only:
        if not args.layout:
            sys.exit("--layout-only needs --layout")
        with open(args.layout, encoding="utf-8") as f:
            name = write_bsp_layout(json.load(f), args.layout, args.out, args.blocker_mesh)
        print(f"{os.path.join(args.out, name)} written")
        return 0
    if not args.templates:
        sys.exit("--templates is needed (OAT's dump of a BO1 map zone's materials/wc folder)")
    if not args.name.startswith("zombie_"):
        sys.exit("--name must start with zombie_ (a zombies level)")
    base = os.path.join(args.dump, "maps", "mp", f"{args.map}.d3dbsp")
    images = os.path.join(args.dump, "images")
    project = os.path.join(args.out, "linker")
    raw = os.path.join(project, "zone_raw", args.name)
    out_images = os.path.join(args.out, "images")
    for d in (os.path.join(raw, "materials", "wc"), os.path.join(raw, "images"), os.path.join(raw, "maps"),
              os.path.join(project, "zone_source"), out_images):
        os.makedirs(d, exist_ok=True)
    templates = {}
    for flag, name in TEMPLATES.items():
        with open(os.path.join(args.templates, name + ".json"), encoding="utf-8") as f:
            templates[flag] = json.load(f)

    bsp = d3dbsp.Bsp()

    # Render: one BO1 material per BO2 material that has a colour map
    colour = read_mtl(base + ".mtl", images)
    positions, uvs, world = read_obj(base + ".obj")
    groups = {}
    for material, faces in world:
        groups.setdefault(material, []).extend(faces)
    # BO2's baked light: each OBJ vertex's lightmap uv, lightmap and primary light (bo2_worldlight.py)
    world_light = read_world_light(base)
    if world_light:
        lit_corners, light_info = world_light
        if len(lit_corners) != len(positions):
            sys.exit(f"{base}.worldlight.bin: {len(lit_corners)} records for {len(positions)} OBJ vertices")
        world_light = WorldLight(args.dump, light_info)
    bake_tris = []
    lightmap_of = {}  # BO2 lightmap index -> BSP lightmap index (NO_LIGHTMAP: a white one)
    zone_materials, skipped_names, no_map, bad_image = [], 0, [], []
    tri_total = 0
    for material in sorted(groups):
        if skipped(material):
            skipped_names += 1
            continue
        dds = colour.get(material)
        if not dds:
            no_map.append(material)
            continue
        flags, multiply = blend_flags(args.dump, material)
        flag = flags if flags in templates else 0
        image = image_name(dds) + ("_mul" if multiply else "")
        iwi = os.path.join(out_images, image + ".iwi")
        if not os.path.exists(iwi) and not dds_to_iwi(dds, iwi, multiply):
            bad_image.append(material)
            continue
        shutil.copyfile(iwi, os.path.join(raw, "images", image + ".iwi"))
        name = bsp_material_name(material)
        mat = json.loads(json.dumps(templates[flag]))
        for t in mat["textures"]:
            if t["name"] == "colorMap":
                t["image"] = image
        with open(os.path.join(raw, "materials", "wc", name + ".json"), "w", encoding="utf-8") as f:
            json.dump(mat, f, indent=4)
        zone_materials.append(name)
        index = bsp.add_material(name, 0, 0)
        # One surface per material and cell (the renderer culls whole surfaces). The OBJ's own vertices, welded by
        # (position, uv). The OBJ winds as the BSP does (front normal (c - a) x (b - a)): in the play area 87 % of
        # the near-horizontal area then faces up.
        cells = {}
        for face in groups[material]:
            centre = [sum(positions[p][k] for p, _ in face) / 3.0 for k in range(3)]
            lmap = lit_corners[face[0][0]][2] if world_light else NO_LIGHTMAP
            if world_light and lmap in world_light.lightmaps and not flags:
                bake_tris.append((lmap, lit_corners[face[0][0]][3],
                                  [(positions[p], lit_corners[p][:2]) for p, _ in face]))
            cells.setdefault((surface_cell(centre), lmap), []).append(face)
        for (_, lmap), faces in cells.items():
            if lmap not in lightmap_of:
                lightmap_of[lmap] = len(lightmap_of)
            remap, pos, uv, luv, idx = {}, [], [], [], []
            for face in faces:
                ids = []
                for p, t in face:
                    key = (p, t)
                    if key not in remap:
                        remap[key] = len(pos)
                        pos.append(positions[p])
                        uv.append(uvs[t] if t >= 0 else (0.0, 0.0))
                        luv.append(lit_corners[p][:2] if world_light and lmap != NO_LIGHTMAP else (0.5, 0.5))
                    ids.append(remap[key])
                idx.extend(ids)
            tri_total += len(idx) // 3
            bsp.add_mesh(index, pos, idx, uvs=uv, lightmap=lightmap_of[lmap], lmap_uvs=luv)

    # The lightmaps, in the order the surfaces named them: BO2's baked again (bo2_bsp_light.py), white for the rest
    baked = bo2_bsp_light.bake(world_light, bake_tris) if world_light else {}
    for lmap, _ in sorted(lightmap_of.items(), key=lambda kv: kv[1]):
        bsp.add_lightmap(bo2_bsp_light.lightmap_bytes(baked[lmap]) if lmap in baked
                         else bo2_bsp_light.white_lightmap_bytes())

    # Model lighting: a light grid over the play area (its zones) from BO2's light, and the sun on the same scale
    grid = None
    if world_light and baked:
        bsp.sun_color = bo2_bsp_light.bake_sun_colour(world_light)
        light_index = {world_light.sun_index: d3dbsp.SUN_PRIMARY_LIGHT}
        light_index.update(bo2_bsp_light.add_lamps(bsp, world_light))
        zones = zone_edits(read_entities(base + ".ents"), Collision(args.collision or base + ".collision.bin").submodels)
        if zones:
            grid = bo2_bsp_light.light_grid(world_light, bake_tris, [min(z["mins"][k] for z in zones) for k in range(3)],
                                            [max(z["maxs"][k] for z in zones) for k in range(3)],
                                            light_index, d3dbsp.NO_PRIMARY_LIGHT)
        if grid:
            bsp.set_light_grid(*grid)

    # Collision: the world's brushes and terrain (owner 0); submodels (doors) wait for the gameplay step
    col_path = args.collision or base + ".collision.bin"
    cmats, brushes, verts, tris = read_collision(col_path)
    flag_material = {}

    def col_material(cflags, sflags):
        key = (cflags, sflags)
        if key not in flag_material:
            flag_material[key] = bsp.add_material(f"bo2col_{cflags & 0xFFFFFFFF:08x}_{sflags & 0xFFFFFFFF:08x}",
                                                   sflags, cflags)
        return flag_material[key]

    layout, cuts = None, []
    if args.layout:
        with open(args.layout, encoding="utf-8") as f:
            layout = json.load(f)
        for e in layout["edits"]:
            if e["kind"] == "collision":
                cuts += e.get("cut", [])
    world_brushes = submodel_brushes = cut_brushes = 0
    for mins, maxs, contents, owner, axial, sides in brushes:
        if owner != 0:
            submodel_brushes += 1
            continue
        # a brush wholly inside a cut box, with its exact contents when the box gives them (cm_mapkit_import.cpp)
        if any(all(c["mins"][k] <= mins[k] and maxs[k] <= c["maxs"][k] for k in range(3))
               and (not c.get("contents") or contents == c["contents"]) for c in cuts):
            cut_brushes += 1
            continue
        if any(a >= b for a, b in zip(mins, maxs)):
            continue
        surface = next((sf for cf, sf in axial if sf), 0)
        bsp.add_collision_brush(mins, maxs, col_material(contents, surface), [col_material(cf, sf) for cf, sf in axial],
                                [(n, d, col_material(cf, sf)) for n, d, cf, sf in sides])
        world_brushes += 1
    # Terrain: BO2's collision triangles as BO1's (the same one-sided triangles, same winding), one partition material
    # per collision material
    used, coll_tris, no_contents = {}, [], 0
    for a, b, c, m, owner in tris:
        if owner != 0:
            continue
        if m == 0xFFFF or not cmats[m][2]:
            no_contents += 1
            continue
        ids = []
        for v in (a, b, c):
            if v not in used:
                used[v] = len(used)
            ids.append(used[v])
        _, sflags, cflags = cmats[m]
        coll_tris.append((ids[0], ids[1], ids[2], col_material(cflags, sflags)))
    coll_verts = [None] * len(used)
    for v, i in used.items():
        coll_verts[i] = verts[v]
    bsp.add_collision_triangles(coll_verts, coll_tris)
    prop_stats = None
    if args.models:
        prop_stats = prop_collision(bsp, args.dump, base, col_material(PROP_CONTENTS, 0), len(coll_verts))

    # Entities: the worldspawn (its sun) and the player start; the gameplay step adds the rest
    ents = read_entities(base + ".ents")
    ws = next(e for e in ents if e.get("classname") == "worldspawn")
    props = None
    if args.models:
        props = PropModels(args.models, images, os.path.join(os.path.dirname(os.path.normpath(args.templates)), "mc"),
                           raw, out_images)
    world_keys = {"classname": "worldspawn", "sundirection": ws.get("sundirection", "-45 45 0"), "suncolor": "1 1 1",
                  "sunlight": "1"}
    # BO2's sky is a model drawn around the viewer, as BO1's skyboxmodel is (R_RegisterSkyboxModel)
    sky = props.model(ws["skyboxmodel"]) if props and ws.get("skyboxmodel") else None
    if sky:
        world_keys["skyboxmodel"] = sky
    bsp.add_entity(**world_keys)
    for e in ents:
        if e.get("classname") == "info_player_start":
            bsp.add_entity(classname="info_player_start", origin=e["origin"], angles=e.get("angles", "0 0 0"))
            break
    placed = place_props(bsp, base, props) if props else (0, 0)

    bsp_path = os.path.join(args.out, args.name + ".d3dbsp")
    bsp.write(bsp_path)

    if layout:
        write_bsp_layout(layout, args.layout, args.out, args.blocker_mesh)

    with open(os.path.join(raw, "maps", args.name + ".gsc"), "w", encoding="latin-1", newline="\n") as f:
        f.write(GSC)
    open(os.path.join(raw, args.name), "wb").close()
    with open(os.path.join(project, "zone_source", args.name + ".zone"), "w", encoding="latin-1",
              newline="\n") as f:
        f.write(">game,T5\n\n")
        # The zone's build report, which Treyarch's linker always adds (empty: no errors) and G_PrintFastFileErrors
        # asserts on
        f.write(f"rawfile,{args.name}\n")
        f.write(f"rawfile,maps/{args.name}.gsc\n")
        for name in zone_materials:
            f.write(f"material,wc/{name}\n")
        for line in props.zone_lines() if props else []:
            f.write(line + "\n")

    nodes, leafs, leafbrushes, partitions, aabbs = bsp.tree_stats
    print(f"lightmaps: {len(lightmap_of)} ({len(baked)} baked from BO2's, reference "
          f"{', '.join(f'{r:.2f}' for r in world_light.reference) if world_light else '-'})")
    if grid:
        g_mins, g_maxs, g_entries, g_colours = grid
        print(f"light grid: {g_maxs[0] - g_mins[0] + 1} x {g_maxs[1] - g_mins[1] + 1} x {g_maxs[2] - g_mins[2] + 1} "
              f"points, {len(g_colours)} colours, {sum(1 for e in g_entries if e[1] == d3dbsp.SUN_PRIMARY_LIGHT)} "
              f"in the sun, {sum(1 for e in g_entries if e[1] > d3dbsp.SUN_PRIMARY_LIGHT)} under a lamp ({len(bsp.lights)} "
              f"lamps); sun colour {', '.join(f'{c:.2f}' for c in bsp.sun_color)}")
    print(f"{bsp_path}: {len(zone_materials)} materials, {tri_total} triangles in {len(bsp.soups)} surfaces "
          f"({skipped_names} materials left out, {len(no_map)} without a colour map, {len(bad_image)} with an image "
          f"the engine cannot take); {world_brushes} brushes, {len(coll_tris)} terrain triangles in {partitions} "
          f"partitions ({submodel_brushes} submodel brushes left for later, {cut_brushes} in the layout's cut boxes, "
          f"{no_contents} contentless triangles); "
          f"tree {nodes} nodes, {leafs} leaves, {leafbrushes} leaf brushes, {aabbs} leaf partitions")
    if props:
        boxes, piles, rocks, tri_props, tri_count, fallback = prop_stats
        print(f"props: {placed[0]} placements as static models ({placed[1]} left out), "
              f"{sum(1 for m in props.models.values() if m)} xmodels, "
              f"{sum(1 for m in props.materials.values() if m)} materials; collision: {boxes} boxes, "
              f"{piles} piles as {rocks} rocks, {tri_props} meshes as {tri_count} triangles, {fallback} meshes as boxes "
              f"(no collision vertices left)")
        for note in props.notes[:20]:
            print("  " + note)
    if no_map:
        print("without a colour map: " + ", ".join(no_map[:20]) + (" ..." if len(no_map) > 20 else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
