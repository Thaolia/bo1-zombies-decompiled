#!/usr/bin/env python3
"""A BO2 map as a native BO1 map: its world BSP and the OAT Linker (T5) project of its zone.

    tools/bo2/bo2_bsp.py <dump folder of the zone> <map name> --templates <BO1 wc/ material JSONs> --out <folder>
        [--collision <BO2COL2 file>] [--name zombie_bo2nuked]

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
import os
import re
import shutil
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import d3dbsp  # noqa: E402
from bo2_layout import read_entities  # noqa: E402
from bo2_bsp_props import PropModels, place_props, prop_collision  # noqa: E402
from bo2_mesh import (FLAG_ALPHA_TEST, FLAG_BLEND, FLAG_DECAL, blend_flags, dds_to_iwi, image_name,  # noqa: E402
                      read_mtl, read_obj, skipped)
from bo2_props_collision import PROP_CONTENTS  # noqa: E402

TEMPLATES = {0: "global_white", FLAG_ALPHA_TEST: "mtl_theater_poster_1", FLAG_DECAL: "decal_glue",
             FLAG_BLEND: "decal_glue"}
GSC = """main()
{
\tlevel thread maps\\_callbacksetup::SetupCallbacks();
\tmaps\\_zombiemode_load::main();
}
"""


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


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dump", help="OAT's output folder for the zone")
    parser.add_argument("map", help="the BO2 map's name, e.g. zm_nuked")
    parser.add_argument("--templates", required=True, help="OAT's dump of a BO1 map zone's materials/wc folder")
    parser.add_argument("--out", required=True)
    parser.add_argument("--collision", help="a BO2COL2 file (default: the dump's)")
    parser.add_argument("--name", default="zombie_bo2nuked", help="the BO1 map's name (a zombie_ name)")
    parser.add_argument("--models", help="OAT's dump of the zone's xmodels and materials with --model-format GLTF: "
                                         "adds the placed props (bo2_bsp_props.py)")
    args = parser.parse_args(argv)
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
        # The OBJ's own vertices, welded by (position, uv). The OBJ winds as the BSP does (front normal
        # (c - a) x (b - a)): in the play area 87 % of the near-horizontal area then faces up.
        remap, pos, uv, idx = {}, [], [], []
        for face in groups[material]:
            ids = []
            for p, t in face:
                key = (p, t)
                if key not in remap:
                    remap[key] = len(pos)
                    pos.append(positions[p])
                    uv.append(uvs[t] if t >= 0 else (0.0, 0.0))
                ids.append(remap[key])
            idx.extend(ids)
        tri_total += len(idx) // 3
        bsp.add_mesh(index, pos, idx, uvs=uv)

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

    world_brushes = submodel_brushes = 0
    for mins, maxs, contents, owner, axial, sides in brushes:
        if owner != 0:
            submodel_brushes += 1
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
    bsp.add_entity(classname="worldspawn", sundirection=ws.get("sundirection", "-45 45 0"), suncolor="1 1 1",
                   sunlight="1")
    for e in ents:
        if e.get("classname") == "info_player_start":
            bsp.add_entity(classname="info_player_start", origin=e["origin"], angles=e.get("angles", "0 0 0"))
            break
    props, placed = None, (0, 0)
    if args.models:
        props = PropModels(args.models, images, os.path.join(os.path.dirname(os.path.normpath(args.templates)), "mc"),
                           raw, out_images)
        placed = place_props(bsp, base, props)

    bsp_path = os.path.join(args.out, args.name + ".d3dbsp")
    bsp.write(bsp_path)

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
    print(f"{bsp_path}: {len(zone_materials)} materials, {tri_total} triangles in {len(bsp.soups)} surfaces "
          f"({skipped_names} materials left out, {len(no_map)} without a colour map, {len(bad_image)} with an image "
          f"the engine cannot take); {world_brushes} brushes, {len(coll_tris)} terrain triangles in {partitions} "
          f"partitions ({submodel_brushes} submodel brushes left for later, {no_contents} contentless triangles); "
          f"tree {nodes} nodes, {leafs} leaves, {leafbrushes} leaf brushes, {aabbs} leaf partitions")
    if props:
        boxes, tri_props, tri_count, fallback = prop_stats
        print(f"props: {placed[0]} placements as static models ({placed[1]} left out), "
              f"{sum(1 for m in props.models.values() if m)} xmodels, "
              f"{sum(1 for m in props.materials.values() if m)} materials; collision: {boxes} boxes, {tri_props} "
              f"meshes as {tri_count} triangles, {fallback} meshes as boxes (no collision vertices left)")
        for note in props.notes[:20]:
            print("  " + note)
    if no_map:
        print("without a colour map: " + ", ".join(no_map[:20]) + (" ..." if len(no_map) > 20 else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
