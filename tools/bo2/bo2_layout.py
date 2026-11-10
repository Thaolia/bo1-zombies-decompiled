#!/usr/bin/env python3
"""A MapKit layout (mods/mapkit/layouts/<name>.json) from a BO2 zombies map dumped by tools/bo2/oat.

    tools/bo2/bo2_layout.py <dump folder of the zone> <map name> [--out FILE] [--collision PATH] [--links-out FILE]

The dump folder is OAT's output for the zone (it holds maps/mp/<map>.d3dbsp.ents and .collision.bin). The layout is
an "empty" Five base with the BO2 map's collision (a "collision" edit, PATH under mods/mapkit, default
imports/<map>/<map>.collision.bin: copy the .bin there in the run folder) and its gameplay, read from the entities:
  - zones: the info_volume player volumes (the volumes of a zone with several become their union box);
  - spawn: the initial_spawn_points structs;
  - riser: the riser_location structs (BO2 zombies climb out of the ground), in the zone their targetname names;
  - node: every node_pathnode, dropped onto the floor under it (BO2 keeps them where the designer placed them);
  - links: BO2's own links between those nodes (the dump's <map>.d3dbsp.paths.csv, from the gameworldmp asset), as a
    text file of node pairs (--links-out, default <map>.links.txt; copy it to imports/<map>/ in the run folder). They
    are the graph between these nodes, instead of BO1's link test (g_mapkit_path.cpp: BO1 actors walk no facet steeper
    than 45.6 degrees, BO2's walk Nuketown's north ditch; and BO1's test crossed the sill clip of a window);
  - mysterybox: every chest location (zbarrier_*MagicBox*), "start" on BO2's start chests: with more than one,
    retail's treasure_chest_init picks a start among them, moves the box and lights its beam (one chest stays put
    and gets no beam);
  - door / debris: each zombie_door / zombie_debris trigger group, with BO2's cost; its box is the targeted brush
    models' (the door panel, the debris clip), its zones come from its script_flag ("culdesac_2_openhouse2_f1"), the
    one farther from the player start being the one it unlocks, and its "meshGroup" names the group of BO2 script
    models (door, couch, chairs) that bo2_mesh.py draws until the purchase;
  - wallbuy: the weapon_upgrade structs whose weapon has a BO1 equivalent loaded on the Five base (WEAPONS);
  - perk / pap: the four Five perks and Pack-a-Punch on the first zm_random_machine landing spots (BO2 drops them
    at random), on the side a player can reach;
  - traverse: the node_negotiation pairs whose BO2 animscript has a BO1 one of about the same height (TRAVERSES);
  - collision "cut": the BO2 clip at each machine and box spot (MapKit's machine brings its own);
  - mesh: the drawable world, <folder of the collision>/<map>.mesh.bin and images/ (tools/bo2/bo2_mesh.py writes them).
Not converted: dog spawns, BO2-only mechanics (the garage-door crawl becomes a mantle).
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
# (zombie_pentagon.ff, common_zombie.ff). BO1 zombies have no jump up below 222 units: the jump-ups take MapKit's
# mapkit_jump_up (mods/mapkit/animscripts/traverse), the 222 one brought down to the end node's height. The mantles
# take mapkit_mantle, Five's wall hop carried on to the end node (BO2's obstacles are deeper than a window sill). BO1
# has no crawl under a garage door: that flat pair takes the mantle too.
TRAVERSES = {
    "zm_mantle_over_40": "mapkit_mantle",
    "zm_traverse_garage_door": "mapkit_mantle",
    "zm_jump_down_fast_40": "zombie_jump_down_40",
    "zm_jump_down_72": "zombie_jump_down_72",
    "zm_jump_down_96": "zombie_jump_down_72",
    "zm_jump_down_127": "zombie_jump_down_127",
    "zm_jump_down_176": "zombie_jump_down_184",
    "zm_jump_up_72": "mapkit_jump_up",
    "zm_jump_up_96": "mapkit_jump_up",
    "zm_jump_up_127": "mapkit_jump_up",
    "zm_jump_up_154": "mapkit_jump_up",
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
MONSTER_CLIP = 0x20000
BARRIER_RAISE = 72.0  # a mantled barrier's clip goes this far over its top: an actor cannot stand on it
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
        self.barriers = []  # (mins, maxs, planes): thin world monster clips, 30 to 80 units tall (fences, sills)
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
            if (owner == 0 and contents & MONSTER_CLIP and not contents & 1 and 30 <= maxs[2] - mins[2] < 80
                    and min(maxs[0] - mins[0], maxs[1] - mins[1]) <= 48):
                self.barriers.append((mins, maxs, planes))
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


def blocker_edits(ents, submodels, zones, start_zones):
    """Door and debris edits for BO2's zombie_door / zombie_debris triggers, one per group of triggers sharing a target."""
    by_name = {}
    for e in ents:
        by_name.setdefault(e.get("targetname"), []).append(e)
    bases = {z["name"][: -len("_zone")] if z["name"].endswith("_zone") else z["name"]: z for z in zones}

    def box_of(entity):
        model = entity.get("model", "")
        if not model.startswith("*") or int(model[1:]) >= len(submodels):
            return None
        b, o = submodels[int(model[1:])], vec(entity.get("origin", "0 0 0"))
        return [o[k] + b[k] for k in range(3)], [o[k] + b[3 + k] for k in range(3)]

    def zone_for(token, centre):
        # a flag names a zone by its name less "_zone", or by a prefix of several ("culdesac": yellow and green)
        found = [z for base, z in bases.items() if base == token or base.startswith(token + "_")]
        return min(found, key=lambda z: sum(max(z["mins"][k] - centre[k], 0, centre[k] - z["maxs"][k]) ** 2
                                            for k in range(3)))["name"] if found else None

    def split_flag(flag, centre):
        if "_2_" in flag:
            a, b = flag.split("_2_", 1)
            return zone_for(a, centre), zone_for(b, centre)
        parts = flag.split("_")
        for cut in range(1, len(parts)):
            a, b = zone_for("_".join(parts[:cut]), centre), zone_for("_".join(parts[cut:]), centre)
            if a and b and "_".join(parts[:cut]) in bases and "_".join(parts[cut:]) in bases:
                return a, b
        return None, None

    groups = []
    for trigger in ents:
        kind = {"zombie_door": "door", "zombie_debris": "debris"}.get(trigger.get("targetname"))
        target = trigger.get("target")
        if not kind or not target or any(g["meshGroup"] == target for g in groups):
            continue
        boxes = [box_of(e) for e in by_name.get(target, []) if e.get("classname") == "script_brushmodel"]
        boxes = [b for b in boxes if b] or [box_of(trigger)]
        if not boxes[0]:
            continue
        mins = [round(min(b[0][k] for b in boxes), 2) for k in range(3)]
        maxs = [round(max(b[1][k] for b in boxes), 2) for k in range(3)]
        centre = [(mins[k] + maxs[k]) / 2 for k in range(3)]
        a, b = split_flag(trigger.get("script_flag", ""), centre)
        if not a or not b:
            print(f"blocker {target}: no zones for flag {trigger.get('script_flag')}", file=sys.stderr)
            continue
        groups.append({"kind": kind, "name": target, "mins": mins, "maxs": maxs, "yaw": 0,
                       "zones": (a, b), "cost": int(float(trigger.get("zombie_cost", 1000))), "meshGroup": target})

    # each blocker unlocks the zone the player reaches later: a breadth-first walk from the start zones through them
    reached, order, pending = set(start_zones), {}, list(groups)
    while pending:
        progress = False
        for g in list(pending):
            a, b = g["zones"]
            if a in reached or b in reached:
                g["from"], g["to"] = (a, b) if a in reached else (b, a)
                pending.remove(g)
                progress = True
        for g in groups:
            if "to" in g:
                reached.add(g["to"])
        if not progress:
            for g in pending:  # cut off from the start: the flag's order
                g["from"], g["to"] = g["zones"]
            break
    for g in groups:
        del g["zones"]
    return groups


def segment_crosses_box(a, b, lo, hi):
    """Whether the segment a-b crosses the box [lo, hi] seen from above."""
    t0, t1 = 0.0, 1.0
    for k in range(2):
        d = b[k] - a[k]
        if abs(d) < 1e-6:
            if not lo[k] <= a[k] <= hi[k]:
                return False
            continue
        u, v = (lo[k] - a[k]) / d, (hi[k] - a[k]) / d
        t0, t1 = max(t0, min(u, v)), min(t1, max(u, v))
    return t0 <= t1


def barrier_clips(edits, col):
    """Hidden monster clip boxes over each thin BO2 barrier (fence, window sill: a monster clip 30 to 80 units tall)
    that a mantle pair crosses, BARRIER_RAISE units over its top. BO2 bars a window of house 1 with a 37-unit sill clip
    over debris: BO1 zombies walked up the debris onto it, without the traversal, and stuck in the opening. The mantle
    is noclip and goes through. A barrier with slanted sides becomes strips 8 units deep along its shape; a strip that
    would come within an actor's radius of a node is left out."""
    nodes = [e["origin"] for e in edits if e["kind"] == "node"]
    nodes += [e[k] for e in edits if e["kind"] == "traverse" for k in ("begin", "end")]
    done, boxes, left_out = set(), [], 0
    for e in edits:
        if e["kind"] != "traverse" or e["anim"] != "mapkit_mantle":
            continue
        a, b = e["begin"], e["end"]
        floor = min(a[2], b[2])
        for lo, hi, planes in col.barriers:
            key = (tuple(lo), tuple(hi))
            if key in done or not (lo[2] < floor + 8 and hi[2] > floor) or not segment_crosses_box(a, b, lo, hi):
                continue
            done.add(key)
            top = hi[2] + BARRIER_RAISE
            mid = (lo[2] + hi[2]) / 2

            def inside(x, y):
                return all(nx * x + ny * y + nz * mid <= d + 0.01 for nx, ny, nz, d in planes)

            strips = []
            if not planes:
                strips.append((lo[0], lo[1], hi[0], hi[1]))
            else:
                y = lo[1]
                while y < hi[1]:
                    y1 = min(y + 8, hi[1])
                    # the row's edges as well as its middle, so that the strips of a slanted barrier overlap
                    xs = [x for x in frange(lo[0], hi[0], 4) if any(inside(x + 2, v) for v in (y, (y + y1) / 2, y1))]
                    if xs:
                        strips.append((xs[0], y, min(xs[-1] + 4, hi[0]), y1))
                    y = y1
            for x0, y0, x1, y1 in strips:
                if any(x0 - 15 < n[0] < x1 + 15 and y0 - 15 < n[1] < y1 + 15 and lo[2] - 72 < n[2] < top for n in nodes):
                    left_out += 1
                    continue
                boxes.append({"kind": "box", "name": f"barrier{len(boxes) + 1}", "contents": "monsterclip",
                              "mins": [round(x0, 1), round(y0, 1), round(lo[2], 1)],
                              "maxs": [round(x1, 1), round(y1, 1), round(top, 1)]})
    print(f"barrier clips: {len(boxes)} boxes over {len(done)} mantled barriers, {left_out} strips left out by nodes")
    return boxes


def frange(start, stop, step):
    x = start
    while x < stop:
        yield x
        x += step


def read_paths(path):
    """BO2's compiled path data (GameWorldPathDumperT6): [(type, (x, y, z), [(to, negotiation)])] by node index."""
    nodes = []
    with open(path, encoding="utf-8") as f:
        next(f)
        for line in f:
            index, kind, x, y, z, links = line.rstrip("\n").split(",")
            if int(index) != len(nodes):
                sys.exit(f"{path}: node {index} out of order")
            out = []
            for link in filter(None, links.split(";")):
                to, _, negotiation = link.split(":")
                out.append((int(to), int(negotiation)))
            nodes.append((int(kind), (float(x), float(y), float(z)), out))
    return nodes


def node_links(ents, paths):
    """BO2's links between plain pathnodes as pairs of layout node numbers (the n-th "node" edit), each direction on
    its own; traversal links come from the traverse edits. The compiled pathnodes are the node_pathnode entities in
    their order, at their placed origins (zm_nuked: all 703; the compiler does not drop them)."""
    placed = [vec(e["origin"]) for e in ents if e.get("classname") == "node_pathnode"]
    number = {}
    for index, (kind, origin, _) in enumerate(paths):
        if kind != 1:  # NODE_PATHNODE
            continue
        n = len(number)
        if n >= len(placed) or max(abs(origin[k] - placed[n][k]) for k in range(3)) > 0.1:
            sys.exit(f"compiled pathnode {index} at {origin} is not node_pathnode {n}: redump the paths")
        number[index] = n
    if len(number) != len(placed):
        sys.exit(f"{len(number)} compiled pathnodes for {len(placed)} node_pathnode entities: redump the paths")
    pairs = []
    for index, (_, _, links) in enumerate(paths):
        for to, negotiation in links:
            if not negotiation and index in number and to in number:
                pairs.append((number[index], number[to]))
    return pairs


def build(ents, col, name, collision, links_file=None):
    submodels = col.submodels
    folder = os.path.dirname(collision)
    edits = [{"kind": "collision", "file": collision},
             {"kind": "mesh", "file": f"{folder}/{name[4:]}.mesh.bin", "images": f"{folder}/images"}]
    if links_file:
        edits.append({"kind": "links", "file": f"{folder}/{links_file}"})

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
    starts = [c for c in chests if "start" in c.get("script_noteworthy", "")] or chests[:1]
    cuts = []

    def place(origin, angle, yaws):
        """A machine's spot on the solid floor, BO2's clip there to cut, and the side a player can use it from."""
        ground = col.floor([origin[0], origin[1], origin[2] + 32], SOLID)
        spot = [origin[0], origin[1], round(ground, 2) if ground is not None else origin[2]]
        cut = col.footprint(spot)
        cuts.extend({"mins": lo, "maxs": hi, "contents": c} for lo, hi, c in cut)
        # MapKit's machine clip is the axial box around the turned model (cm_mapkit.cpp CM_Mapkit_AddMachineClips): at
        # a yaw off the right angles it reaches out in front of the machine (37 units at Pack-a-Punch's 105) and a
        # player standing to use it is caught inside. A right angle keeps the clip on the model, BO2's yaw within 45.
        angle = round(angle / 90.0) * 90
        return spot, round(col.free_side(spot, [angle + d for d in yaws], cut), 1)

    # BO2's box (a zbarrier) is oriented along its length; BO1's chest faces its yaw
    for chest in chests:
        origin, angle = place(vec(chest["origin"]), yaw(chest), (90, -90, 0, 180))
        name = chest.get("script_noteworthy", "").replace("_zbarrier", "") or f"box{len(edits)}"
        edits.append({"kind": "mysterybox", "name": name, "origin": origin, "yaw": angle,
                      "start": 1 if chest in starts else 0})

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
    edits += blocker_edits(ents, submodels, zones, {e["zone"] for e in edits if e["kind"] == "spawn"})
    edits += barrier_clips(edits, col)

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
    parser.add_argument("--links-out", help="the node links file to write (default: <map>.links.txt in the current "
                                            "folder; none when the dump has no .paths.csv)")
    args = parser.parse_args(argv)
    base = os.path.join(args.dump, "maps", "mp", f"{args.map}.d3dbsp")
    ents = read_entities(base + ".ents")
    # the collision the game will load (with the props of tools/bo2/bo2_props_collision.py) places the machines and
    # drops the nodes best; the clipMap's own file otherwise
    col = Collision(args.collision_file or base + ".collision.bin")
    name = f"bo2_{args.map}"
    collision = args.collision or f"imports/{args.map}/{args.map}.collision.bin"
    links_out = None
    if os.path.exists(base + ".paths.csv"):
        pairs = node_links(ents, read_paths(base + ".paths.csv"))
        links_out = args.links_out or f"{args.map}.links.txt"
        with open(links_out, "w", encoding="utf-8") as f:
            f.write("".join(f"{a} {b}\n" for a, b in pairs))
        print(f"{links_out}: {len(pairs)} links between pathnodes")
    else:
        print(f"{base}.paths.csv not found (dump the gameworldmp asset): no node links file")
    layout, counts, skipped, traverse_skipped = build(ents, col, name, collision,
                                                      links_out and os.path.basename(links_out))
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
