#!/usr/bin/env python3
"""A BO2 map's collision with its solid props: the clipMap's BO2COL2 file plus one brush per colliding static model.

    tools/bo2/bo2_props_collision.py <dump folder of the zone> <map name>
        --out <run>/mods/mapkit/imports/<map>/<map>.collision.bin

The clipMap lists the static models that collide (<map>.d3dbsp.smodelcoll.csv, tools/bo2/oat): model and contents.
Each is placed as the renderer places it (<map>.d3dbsp.smodels.csv: same model and origin) and shaped from its
collision LOD (the xmodel JSON's collLod, else its last LOD):
  - a model at most BOX_MAX units across becomes an oriented box around that LOD (a fence panel, a sign, a crate);
  - a larger one keeps its triangles, each a prism PRISM_DEPTH units deep into the model, as the engine builds terrain
    triangles (cm_mapkit_import.cpp): a ramp stays a ramp, a roof a roof, a rock pile a pile.
The props block players and shots, not the AI: their contents are BO1's player clip and shot-stopping bits
(PROP_CONTENTS). BO1's actor mask (0x2820011) leaves the player clip out, so the pathnodes, their links and the zombies'
movement stay what the clipMap gives (BO2's own nodes already go round the props). Blocking actors too made nodes on
uneven rock piles and under ceiling lights "in solid", and linked stairs the AI then could not come back down from.
Lights (hanging lamps, ceiling tubes: LIGHTS) do not collide: they caught a player's head.
Only the props inside the map's zones (its info_volume player volumes, PLAY_PAD units around them) collide: the rest lies past the map's invisible
walls, and their triangles would cost memory and load time for nothing. The shapes are world brushes (owner 0) added
to the clipMap's brushes; the rest of the file is copied as it is.
The output is the player's own game data: keep it out of the repository. docs/BO2_MAPS.md has the procedure.
"""
import argparse
import csv
import json
import math
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bo2_layout import Collision, read_entities, zone_edits  # noqa: E402  (the zones the layout gets)
from bo2_mesh import model_tris  # noqa: E402  (same OBJ reading as the drawn props)

MIN_HALF = 1.0     # a flat collision LOD (a sign) still gets some thickness
BOX_MAX = 160.0    # larger models keep their triangles
PRISM_DEPTH = 16.0
PLAY_PAD = 64.0
PROP_CONTENTS = 0x10000 | 0x2000 | 0x80  # player clip; stops shots (BO1 bits, docs/BO2_MAPS.md)
LIGHTS = re.compile(r"light|lamp")


def collision_lod(dump, name, cache):
    """A model's collision LOD as (triangles in model space, bounds); None when it has no OBJ."""
    if name in cache:
        return cache[name]
    lods, coll = [], None
    try:
        with open(os.path.join(dump, "xmodel", name + ".json"), encoding="latin-1") as f:
            data = json.load(f)
        lods = [l["file"] for l in data.get("lods", [])]
        coll = data.get("collLod")
    except (OSError, ValueError, KeyError):
        pass
    if not lods:
        lods = [f"model_export/{name}_lod{i}.obj" for i in range(4)
                if os.path.exists(os.path.join(dump, "model_export", f"{name}_lod{i}.obj"))]
    pick = lods[coll] if isinstance(coll, int) and 0 <= coll < len(lods) else (lods[-1] if lods else None)
    result = None
    if pick and os.path.exists(os.path.join(dump, pick)):
        tris = [[c[0] for c in tri] for group in model_tris(os.path.join(dump, pick)).values() for tri in group]
        pts = [p for tri in tris for p in tri]
        if pts:
            result = (tris, ([min(p[k] for p in pts) for k in range(3)], [max(p[k] for p in pts) for k in range(3)]))
    cache[name] = result
    return result


def brush_record(mins, maxs, contents, sides):
    record = struct.pack("<3f3fii", *mins, *maxs, contents, 0)
    record += struct.pack("<6i", *([contents] * 6)) + struct.pack("<6i", *([0] * 6))
    record += struct.pack("<I", len(sides))
    for normal, dist in sides:
        record += struct.pack("<4fii", *normal, dist, contents, 0)
    return record


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def prism_brush(v, contents):
    """A world triangle as a prism PRISM_DEPTH deep behind its face (its own winding's normal), None if degenerate."""
    n = cross([v[1][k] - v[0][k] for k in range(3)], [v[2][k] - v[0][k] for k in range(3)])
    length = math.sqrt(dot(n, n))
    if length < 1e-4:
        return None
    n = [x / length for x in n]
    d = dot(n, v[0])
    sides = [(n, d), ([-x for x in n], -(d - PRISM_DEPTH))]
    centre = [sum(p[k] for p in v) / 3 for k in range(3)]
    for e in range(3):
        a, b = v[e], v[(e + 1) % 3]
        en = cross([b[k] - a[k] for k in range(3)], n)
        el = math.sqrt(dot(en, en))
        if el < 1e-4:
            return None
        en = [x / el for x in en]
        ed = dot(en, a)
        if dot(en, centre) > ed:
            en, ed = [-x for x in en], -ed
        sides.append((en, ed))
    corners = list(v) + [[p[k] - PRISM_DEPTH * n[k] for k in range(3)] for p in v]
    return brush_record([min(c[k] for c in corners) for k in range(3)], [max(c[k] for c in corners) for k in range(3)],
                        contents, sides)


def box_brush(origin, axes, scale, lo, hi, contents):
    """A BO2COL2 brush record: the oriented box [lo, hi] (model space) at origin with axes and scale."""
    lo, hi = list(lo), list(hi)
    for k in range(3):
        if hi[k] - lo[k] < 2 * MIN_HALF:
            mid = (lo[k] + hi[k]) / 2
            lo[k], hi[k] = mid - MIN_HALF, mid + MIN_HALF
    corners = []
    for i in range(8):
        local = [hi[k] if i >> k & 1 else lo[k] for k in range(3)]
        corners.append([origin[k] + scale * sum(local[j] * axes[j][k] for j in range(3)) for k in range(3)])
    mins = [min(c[k] for c in corners) for k in range(3)]
    maxs = [max(c[k] for c in corners) for k in range(3)]
    sides = []
    for j in range(3):
        d0 = sum(origin[k] * axes[j][k] for k in range(3))
        sides.append((axes[j], d0 + scale * hi[j]))
        sides.append(([-a for a in axes[j]], -(d0 + scale * lo[j])))
    return brush_record(mins, maxs, contents, sides)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dump", help="OAT's output folder for the zone")
    parser.add_argument("map", help="the map's name, e.g. zm_nuked")
    parser.add_argument("--out", required=True, help="the collision file to write")
    args = parser.parse_args(argv)
    base = os.path.join(args.dump, "maps", "mp", f"{args.map}.d3dbsp")
    with open(base + ".collision.bin", "rb") as f:
        data = f.read()
    if data[:8] != b"BO2COL2\0":
        sys.exit(f"{base}.collision.bin: not a BO2COL2 file (redump it with tools/bo2/oat)")

    placements = {}
    with open(base + ".smodels.csv", encoding="latin-1") as f:
        for row in csv.DictReader(f):
            key = (row["model"], round(float(row["x"]), 1), round(float(row["y"]), 1), round(float(row["z"]), 1))
            placements[key] = row
    zones = zone_edits(read_entities(base + ".ents"), Collision(base + ".collision.bin").submodels)

    def in_play(o):
        return any(all(z["mins"][k] - PLAY_PAD <= o[k] <= z["maxs"][k] + PLAY_PAD for k in range(3)) for z in zones)

    boxes, unplaced, shapeless, cache = [], 0, set(), {}
    box_count = prism_count = outside = degenerate = lights = 0
    with open(base + ".smodelcoll.csv", encoding="latin-1") as f:
        for row in csv.DictReader(f):
            name = row["model"]
            if LIGHTS.search(name):
                lights += 1
                continue
            key = (name, round(float(row["x"]), 1), round(float(row["y"]), 1), round(float(row["z"]), 1))
            place = placements.get(key)
            if not place:
                unplaced += 1
                continue
            origin = [float(place[k]) for k in ("x", "y", "z")]
            if not in_play(origin):
                outside += 1
                continue
            lod = collision_lod(args.dump, name, cache)
            if not lod:
                shapeless.add(name)
                continue
            tris, bounds = lod
            axes = [[float(place[f"a{a}{i}"]) for i in range(3)] for a in "xyz"]
            scale = float(place["scale"])
            contents = PROP_CONTENTS
            if max(bounds[1][k] - bounds[0][k] for k in range(3)) * scale <= BOX_MAX:
                boxes.append(box_brush(origin, axes, scale, bounds[0], bounds[1], contents))
                box_count += 1
                continue
            for tri in tris:
                world = [[origin[k] + scale * sum(p[j] * axes[j][k] for j in range(3)) for k in range(3)] for p in tri]
                brush = prism_brush(world, contents)
                if brush is None:
                    degenerate += 1
                    continue
                boxes.append(brush)
                prism_count += 1

    # the brush section follows the materials: its count is patched, the boxes appended after the clipMap's brushes
    at = 8
    (materials,) = struct.unpack_from("<I", data, at)
    at += 4 + materials * 72
    count_at = at
    (brushes,) = struct.unpack_from("<I", data, at)
    at += 4
    for _ in range(brushes):
        at += 80
        (sides,) = struct.unpack_from("<I", data, at)
        at += 4 + sides * 24
    out = data[:count_at] + struct.pack("<I", brushes + len(boxes)) + data[count_at + 4:at] + b"".join(boxes) + data[at:]
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(out)
    print(f"{args.out}: {brushes} clipMap brushes + {box_count} prop boxes + {prism_count} prop triangle prisms "
          f"({degenerate} degenerate triangles, {outside} props outside the play area, {lights} lights, "
          f"{unplaced} without a placement, "
          f"{len(shapeless)} models without an OBJ: {', '.join(sorted(shapeless)) or 'none'})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
