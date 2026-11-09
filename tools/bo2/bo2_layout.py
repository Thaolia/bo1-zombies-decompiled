#!/usr/bin/env python3
"""A MapKit layout (mods/mapkit/layouts/<name>.json) from a BO2 zombies map dumped by tools/bo2/oat.

    tools/bo2/bo2_layout.py <dump folder of the zone> <map name> [--out FILE] [--collision PATH]

The dump folder is OAT's output for the zone (it holds maps/mp/<map>.d3dbsp.ents and .collision.bin). The layout is
an "empty" Five base with the BO2 map's collision (a "collision" edit, PATH under mods/mapkit, default
imports/<map>/<map>.collision.bin: copy the .bin there in the run folder) and its gameplay, read from the entities:
  - zones: the info_volume player volumes (the volumes of a zone with several become their union box);
  - spawn: the initial_spawn_points structs;
  - riser: the riser_location structs (BO2 zombies climb out of the ground), in the zone their targetname names;
  - node: every node_pathnode, dropped onto the floor under it (BO2 keeps them where the designer placed them);
  - mysterybox: the start chest;
  - wallbuy: the weapon_upgrade structs whose weapon has a BO1 equivalent loaded on the Five base (WEAPONS);
  - perk / pap: the four Five perks and Pack-a-Punch on the first zm_random_machine landing spots (BO2 drops them
    at random), on the side a player can reach;
  - traverse: the node_negotiation pairs whose BO2 animscript has a BO1 one of about the same height (TRAVERSES);
  - collision "cut": the BO2 clip at each machine and box spot (MapKit's machine brings its own);
  - mesh: the drawable world, <folder of the collision>/<map>.mesh.bin and images/ (tools/bo2/bo2_mesh.py writes them).
Not converted: dog spawns, BO1-less traversals (jump ups under 222 units, the garage door), BO2-only mechanics.
docs/BO2_MAPS.md has the whole procedure.
"""
import argparse
import json
import math
import os
import re
import struct
import sys

# BO2 wall weapon -> (BO1 weapon, its world model on the Five base); tools/mapkit/catalog.mjs lists the loaded pairs.
WEAPONS = {
    "m14_zm": ("m14_zm", "t5_weapon_m14_world"),
    "mp5k_zm": ("mp5k_zm", "t5_weapon_mp5_world"),
    "870mcs_zm": ("ithaca_zm", "t5_weapon_ithaca_world"),
    "rottweil72_zm": ("ithaca_zm", "t5_weapon_ithaca_world"),  # the Olympia does not load on the Five base
    "m16_zm": ("m16_zm", "t5_weapon_m16a1_world"),
    "ak74u_zm": ("ak74u_zm", "t5_weapon_ak74u_world"),
    "beretta93r_zm": ("cz75_zm", "t5_weapon_cz75_world"),
}
# BO2 traversal animscript -> the BO1 zombie animscript of the nearest height that the Five base loads
# (zombie_pentagon.ff, common_zombie.ff). BO1 zombies have no jump up below 222 units: those pairs are left out.
TRAVERSES = {
    "zm_mantle_over_40": "zombie_mantle_over_40",
    "zm_jump_down_fast_40": "zombie_jump_down_40",
    "zm_jump_down_72": "zombie_jump_down_72",
    "zm_jump_down_96": "zombie_jump_down_72",
    "zm_jump_down_127": "zombie_jump_down_127",
    "zm_jump_down_176": "zombie_jump_down_184",
}
PERKS = ["specialty_armorvest", "specialty_quickrevive", "specialty_fastreload", "specialty_rof"]


def read_entities(path):
    with open(path, encoding="latin-1") as f:
        text = f.read()
    return [dict(re.findall(r'"([^"]+)" "([^"]*)"', block)) for block in re.findall(r"\{([^{}]*)\}", text)]


ACTOR_MASK = 0x2820011  # BO1's actor trace mask (actor_mp.cpp): what a zombie stands on
SOLID = 0x1
FOOTPRINT_REACH = 64.0
CLIP_KEEP = 0x1 | 0x2000  # solid or stopping shots: real geometry, never a machine's placeholder clip
NODE_REACH = 8.0  # a node may sit this far under the surface it belongs to
CELL = 256.0


class Collision:
    """What the generator needs of a BO2COL2 file (tools/bo2/oat/.../ClipMapCollisionDumperT6.cpp): submodel bounds
    (model space), and the world's brushes (with their planes) and terrain triangles an actor stands on, to find the
    floor under a point. Both are filed in a 2D grid of CELL units."""

    def __init__(self, path):
        with open(path, "rb") as f:
            data = f.read()
        if data[:8] != b"BO2COL2\0":
            sys.exit(f"{path}: not a BO2COL2 file (redump it with tools/bo2/oat)")
        at = 8
        (count,) = struct.unpack_from("<I", data, at)
        materials = [struct.unpack_from("<ii", data, at + 4 + m * 72 + 64)[1] for m in range(count)]
        at += 4 + count * 72
        (count,) = struct.unpack_from("<I", data, at)
        at += 4
        self.grid = {}
        for _ in range(count):
            mins = struct.unpack_from("<3f", data, at)
            maxs = struct.unpack_from("<3f", data, at + 12)
            contents, owner = struct.unpack_from("<ii", data, at + 24)
            at += 24 + 4 + 4 + 48
            (sides,) = struct.unpack_from("<I", data, at)
            planes = [struct.unpack_from("<4f", data, at + 4 + 24 * s) for s in range(sides)]
            at += 4 + sides * 24
            if owner == 0 and contents & ACTOR_MASK:
                self._file(mins, maxs, ("brush", contents, mins, maxs, planes))
        (count,) = struct.unpack_from("<I", data, at)
        verts = [struct.unpack_from("<3f", data, at + 4 + 12 * i) for i in range(count)]
        at += 4 + count * 12
        (count,) = struct.unpack_from("<I", data, at)
        for i in range(count):
            a, b, c, m, owner = struct.unpack_from("<5H", data, at + 4 + 10 * i)
            contents = 1 if m == 0xFFFF else materials[m] if m < len(materials) else 0
            if owner == 0 and contents & ACTOR_MASK:
                tri = (verts[a], verts[b], verts[c])
                self._file([min(v[k] for v in tri) for k in range(3)], [max(v[k] for v in tri) for k in range(3)],
                           ("tri", contents) + tri)
        at += 4 + count * 10
        (count,) = struct.unpack_from("<I", data, at)
        at += 4
        self.submodels = [struct.unpack_from("<6f", data, at + 24 * i) for i in range(count)]

    def _file(self, mins, maxs, item):
        for cx in range(math.floor(mins[0] / CELL), math.floor(maxs[0] / CELL) + 1):
            for cy in range(math.floor(mins[1] / CELL), math.floor(maxs[1] / CELL) + 1):
                self.grid.setdefault((cx, cy), []).append(item)

    @staticmethod
    def _top(item, x, y):
        """The highest z of the item on the vertical line through (x, y), None when the line misses it."""
        if item[0] == "brush":
            _, _, mins, maxs, planes = item
            if not (mins[0] <= x <= maxs[0] and mins[1] <= y <= maxs[1]):
                return None
            lo, hi = mins[2], maxs[2]
            for nx, ny, nz, d in planes:
                r = d - nx * x - ny * y
                if abs(nz) < 1e-6:
                    if r < 0:
                        return None
                elif nz > 0:
                    hi = min(hi, r / nz)
                else:
                    lo = max(lo, r / nz)
            return hi if lo <= hi else None
        _, _, a, b, c = item
        d = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(d) < 1e-6:
            return None
        l1 = ((b[1] - c[1]) * (x - c[0]) + (c[0] - b[0]) * (y - c[1])) / d
        l2 = ((c[1] - a[1]) * (x - c[0]) + (a[0] - c[0]) * (y - c[1])) / d
        l3 = 1 - l1 - l2
        if min(l1, l2, l3) < 0:
            return None
        return l1 * a[2] + l2 * b[2] + l3 * c[2]

    def floor(self, point, mask=ACTOR_MASK):
        """The highest surface whose contents meet mask under point (x, y), at or below its z (default: what an actor
        stands on, clips included; 1 for the solid, visible floor); None when there is none."""
        x, y, z = point
        best = None
        for item in self.grid.get((math.floor(x / CELL), math.floor(y / CELL)), ()):
            if not item[1] & mask:
                continue
            h = self._top(item, x, y)
            if h is not None and h <= z and (best is None or h > best):
                best = h
        return best

    def _brushes_near(self, x, y, reach):
        seen = set()
        for cx in range(math.floor((x - reach) / CELL), math.floor((x + reach) / CELL) + 1):
            for cy in range(math.floor((y - reach) / CELL), math.floor((y + reach) / CELL) + 1):
                for item in self.grid.get((cx, cy), ()):
                    if item[0] == "brush" and id(item) not in seen:
                        seen.add(id(item))
                        yield item

    def footprint(self, origin, reach=FOOTPRINT_REACH):
        """The bounds (1 unit larger) and contents of each small clip-only brush standing on the floor at origin (centre within
        reach, at most 2 * reach across, neither solid nor stopping shots): the clip BO2 gives a machine or box spot,
        which a MapKit machine replaces with its own."""
        x, y, z = origin
        found = []
        for _, contents, mins, maxs, _ in self._brushes_near(x, y, reach):
            centre = ((mins[0] + maxs[0]) / 2, (mins[1] + maxs[1]) / 2)
            if (not contents & CLIP_KEEP and abs(centre[0] - x) <= reach and abs(centre[1] - y) <= reach
                    and maxs[0] - mins[0] <= 2 * reach and maxs[1] - mins[1] <= 2 * reach and abs(mins[2] - z) <= 24):
                found.append(([round(v - 1, 1) for v in mins], [round(v + 1, 1) for v in maxs], contents))
        return found

    def solid_at(self, p, skip=()):
        """Whether p is inside a world brush an actor collides with, the brushes skip cuts left out (as the engine
        leaves out the brushes of a collision edit's cuts: inside the box, with the same contents)."""
        for _, contents, mins, maxs, planes in self._brushes_near(p[0], p[1], 0):
            if any(c == contents and all(lo[k] <= mins[k] and maxs[k] <= hi[k] for k in range(3)) for lo, hi, c in skip):
                continue
            if all(mins[k] <= p[k] <= maxs[k] for k in range(3)) and all(
                    nx * p[0] + ny * p[1] + nz * p[2] <= d for nx, ny, nz, d in planes):
                return True
        return False

    def free_side(self, origin, yaws, skip=()):
        """The first of yaws whose front (24 to 56 units out, at knee and chest height) is free and has a floor
        within 24 units of origin's height: where a player can stand to use the thing; yaws[0] when none is."""
        for angle in yaws:
            f = (math.cos(math.radians(angle)), math.sin(math.radians(angle)))
            clear = all(not self.solid_at((origin[0] + f[0] * d, origin[1] + f[1] * d, origin[2] + h), skip)
                        for d in range(24, 57, 8) for h in (16, 48))
            stand = (origin[0] + f[0] * 40, origin[1] + f[1] * 40, origin[2] + 24)
            ground = self.floor(stand)
            if clear and ground is not None and abs(ground - origin[2]) <= 24:
                return angle % 360
        return yaws[0] % 360


def vec(text):
    return [round(float(x), 2) for x in text.split()]


def yaw(entity):
    return round(vec(entity.get("angles", "0 0 0"))[1], 1)


def inside(point, zone):
    return all(zone["mins"][k] <= point[k] <= zone["maxs"][k] for k in range(3))


def zone_edits(ents, submodels):
    """The zone edits: one per info_volume player volume. A zone edit is one box and its name must be unique (the
    script finds its volume by targetname): the volumes of a zone with several become their union."""
    zones = []
    by_name = {}
    for e in ents:
        if e.get("classname") == "info_volume" and e.get("script_noteworthy") == "player_volume":
            model = int(e["model"].lstrip("*"))
            o = vec(e["origin"])
            b = submodels[model]
            mins = [round(o[k] + b[k], 1) for k in range(3)]
            maxs = [round(o[k] + b[3 + k], 1) for k in range(3)]
            zone = by_name.get(e["targetname"])
            if zone:
                zone["mins"] = [min(zone["mins"][k], mins[k]) for k in range(3)]
                zone["maxs"] = [max(zone["maxs"][k], maxs[k]) for k in range(3)]
                continue
            by_name[e["targetname"]] = {"kind": "zone", "name": e["targetname"], "mins": mins, "maxs": maxs}
            zones.append(by_name[e["targetname"]])
    return zones


def build(ents, col, name, collision):
    submodels = col.submodels
    folder = os.path.dirname(collision)
    edits = [{"kind": "collision", "file": collision},
             {"kind": "mesh", "file": f"{folder}/{name[4:]}.mesh.bin", "images": f"{folder}/images"}]

    zones = zone_edits(ents, submodels)
    edits += zones
    zone_names = {z["name"] for z in zones}

    def zone_of(point):
        for z in zones:
            if inside(point, z):
                return z["name"]
        return min(zones, key=lambda z: sum((point[k] - (z["mins"][k] + z["maxs"][k]) / 2) ** 2 for k in range(3)))[
            "name"] if zones else ""

    for e in ents:
        if e.get("targetname") == "initial_spawn_points":
            p = vec(e["origin"])
            edits.append({"kind": "spawn", "origin": p, "yaw": yaw(e), "zone": zone_of(p)})

    risers = 0
    for e in ents:
        target = e.get("targetname", "")
        if e.get("script_noteworthy") == "riser_location" and target.endswith("_spawners"):
            zone = target[: -len("_spawners")]
            if zone not in zone_names:
                continue
            risers += 1
            edits.append({"kind": "riser", "name": f"riser{risers}", "origin": vec(e["origin"]), "yaw": yaw(e),
                          "zone": zone})

    # BO2's compiler drops the pathnodes into its own path data (gameworldmp); the entities keep them where they were
    # placed, up to 150 units above the floor. BO1 links nodes on the floor (Path_CanLinkNodes ends within 18 units of
    # the target's height), so each one is dropped onto the surface under it.
    for e in ents:
        if e.get("classname") == "node_pathnode":
            p = vec(e["origin"])
            ground = col.floor([p[0], p[1], p[2] + NODE_REACH])
            edits.append({"kind": "node", "origin": [p[0], p[1], round(ground, 2)] if ground is not None else p})

    # traversal pairs (node_negotiation_begin -> its target, the end), each end on its own floor
    ends = {e["targetname"]: e for e in ents if e.get("classname") == "node_negotiation_end" and "targetname" in e}
    traverse_skipped = []
    for e in ents:
        if e.get("classname") != "node_negotiation_begin":
            continue
        end = ends.get(e.get("target", ""))
        anim = TRAVERSES.get(e.get("animscript", ""))
        if not end or not anim:
            traverse_skipped.append(e.get("animscript", "?") if end else "no end")
            continue
        pair = []
        for node in (e, end):
            p = vec(node["origin"])
            ground = col.floor([p[0], p[1], p[2] + NODE_REACH])
            pair.append([p[0], p[1], round(ground, 2)] if ground is not None else p)
        edits.append({"kind": "traverse", "begin": pair[0], "end": pair[1], "yaw": yaw(e), "anim": anim})

    chests = [e for e in ents if e.get("classname", "").startswith("zbarrier_") and "MagicBox" in e["classname"]]
    start = next((c for c in chests if "start" in c.get("script_noteworthy", "")), chests[0] if chests else None)
    cuts = []

    def place(origin, angle, yaws):
        """A machine's spot on the solid floor, BO2's clip there to cut, and the side a player can use it from."""
        ground = col.floor([origin[0], origin[1], origin[2] + 32], SOLID)
        spot = [origin[0], origin[1], round(ground, 2) if ground is not None else origin[2]]
        cut = col.footprint(spot)
        cuts.extend({"mins": lo, "maxs": hi, "contents": c} for lo, hi, c in cut)
        return spot, round(col.free_side(spot, [angle + d for d in yaws], cut), 1)

    # BO2's box (a zbarrier) is oriented along its length; BO1's chest faces its yaw
    if start:
        origin, angle = place(vec(start["origin"]), yaw(start), (90, -90, 0, 180))
        edits.append({"kind": "mysterybox", "name": "box", "origin": origin, "yaw": angle})

    skipped = []
    for e in ents:
        if e.get("targetname") == "weapon_upgrade":
            bo2 = e.get("zombie_weapon_upgrade", "")
            if bo2 not in WEAPONS:
                skipped.append(bo2)
                continue
            weapon, model = WEAPONS[bo2]
            # BO2 lays the weapon along the wall (its yaw); BO1's wallbuy faces out of the wall: 90 degrees more
            edits.append({"kind": "wallbuy", "origin": vec(e["origin"]), "yaw": round((yaw(e) + 90) % 360, 1),
                          "weapon": weapon, "model": model})

    # BO2 drops these machines from the sky: each zm_random_machine struct, high above the map, targets the struct
    # where the machine lands (its yaw included)
    by_targetname = {e["targetname"]: e for e in ents if "targetname" in e}
    machines = []
    for e in ents:
        if e.get("script_noteworthy") == "zm_random_machine":
            land = by_targetname.get(e.get("target", ""), e)
            machines.append(place(vec(land["origin"]), yaw(land), (0, 180, 90, -90)))
    for perk, (origin, angle) in zip(PERKS, machines):
        edits.append({"kind": "perk", "perk": perk, "origin": origin, "yaw": angle})
    if len(machines) > len(PERKS):
        origin, angle = machines[len(PERKS)]
        edits.append({"kind": "pap", "name": "pap", "origin": origin, "yaw": angle})
    if cuts:
        edits[0]["cut"] = cuts

    layout = {"name": name, "base": "zombie_pentagon", "empty": True, "edits": edits}
    counts = {}
    for edit in edits:
        counts[edit["kind"]] = counts.get(edit["kind"], 0) + 1
    return layout, counts, skipped, traverse_skipped


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dump", help="OAT's output folder for the zone")
    parser.add_argument("map", help="the map's name, e.g. zm_nuked")
    parser.add_argument("--out", help="the layout file (default: bo2_<map>.json in the current folder)")
    parser.add_argument("--collision-file", help="the collision file to read (default: the dump's clipMap file)")
    parser.add_argument("--collision", help="the collision file's path under mods/mapkit "
                                            "(default: imports/<map>/<map>.collision.bin)")
    args = parser.parse_args(argv)
    base = os.path.join(args.dump, "maps", "mp", f"{args.map}.d3dbsp")
    ents = read_entities(base + ".ents")
    # the collision the game will load (with the props of tools/bo2/bo2_props_collision.py) places the machines and
    # drops the nodes best; the clipMap's own file otherwise
    col = Collision(args.collision_file or base + ".collision.bin")
    name = f"bo2_{args.map}"
    collision = args.collision or f"imports/{args.map}/{args.map}.collision.bin"
    layout, counts, skipped, traverse_skipped = build(ents, col, name, collision)
    out = args.out or f"{name}.json"
    with open(out, "w", encoding="utf-8") as f:
        json.dump(layout, f, indent=1)
    print(f"{out}: " + ", ".join(f"{v} {k}" for k, v in counts.items()))
    if skipped:
        print("wall weapons without a BO1 equivalent, left out: " + ", ".join(sorted(set(skipped))))
    if traverse_skipped:
        print("traversals without a BO1 animscript, left out: " + ", ".join(
            f"{k} x{traverse_skipped.count(k)}" for k in sorted(set(traverse_skipped))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
