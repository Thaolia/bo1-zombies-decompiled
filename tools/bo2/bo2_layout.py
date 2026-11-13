#!/usr/bin/env python3
"""A MapKit layout (mods/mapkit/layouts/<name>.json) from a BO2 zombies map dumped by tools/bo2/oat.

    tools/bo2/bo2_layout.py <dump folder of the zone> <map name> [--out FILE] [--collision PATH] [--links-out FILE]

The dump folder is OAT's output for the zone (it holds maps/mp/<map>.d3dbsp.ents and .collision.bin). The layout is
an "empty" Five base with the BO2 map's collision (a "collision" edit, PATH under mods/mapkit, default
imports/<map>/<map>.collision.bin: copy the .bin there in the run folder) and its gameplay, read from the entities:
  - zones: the info_volume player volumes (the volumes of a zone with several become their union box);
  - spawn: one, at the centre of the initial_spawn_points structs of the first one's zone when they face one way,
    else on the first (MapKit spreads it over the four players);
  - riser: the riser_location structs (BO2 zombies climb out of the ground), in the zone their targetname names;
  - spawner: the spawn_location structs (TranZit's zombies behind its barricades), in the same way;
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
    # the dual CZ75: Five's English zone has no ZOMBIE_WEAPON_CZ75 (the single CZ75 is never a wall weapon in BO1), so
    # its wall showed the raw key and no "hold to buy" until the first purchase; ZOMBIE_WEAPON_CZ75DW is there
    "beretta93r_zm": ("cz75dw_zm", "t5_weapon_cz75_world"),
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
# machines MapKit also places when their assets are loaded (Stamin-Up: with bo1_mod_zones zombie_cosmodrome)
EXTRA_PERKS = ["specialty_longersprint"]
BESIDE_REACH = 64.0   # a blocker's zones: those within this of its box when its flag names none
CLASSIC = "zclassic"  # the gametype whose machines, power switch and wall buys a map with several modes (TranZit) keeps
# MapKit rebuilds the node visibility at load, one sight trace per node pair: past BIG_MAP_NODES nodes (TranZit: 5 350)
# that takes hours, so the layout limits it to node pairs within NODE_VIS_RANGE ("nodeVisRange"); a smaller map keeps
# every pair, as BO1 does
BIG_MAP_NODES = 2000
NODE_VIS_RANGE = 1024  # 2048 cost 55 s of TranZit's load on Linux (actor navigation looks at near node pairs)
# TranZit's bus (script_vehicle "zombie_bus", model veh_t6_civ_bus_zombie): model space x -96..449 rear to front
# bumper, the windshield at x 368, y -74..81, z 0..156, its two doors on the right side (-y) at x 58 and 334, the floor
# inside at z 36 with a step at 20. The front wall stands behind the windshield: at the bumper, a rider pushed forward
# in a turn had the eye past the glass and saw no bus (the model's faces are one-sided).
# MapKit carries players on layout brush models moved by _mapkit.gsc: the floor, a step outside each door (the floor is
# two player steps up) and walls and roof, with the door openings left in the right wall.
BUS_MODEL = "veh_t6_civ_bus_zombie"
BUS_FLOOR = ((-90.0, -70.0, 20.0), (366.0, 77.0, 36.0))
BUS_PARTS = [((34.0, -94.0, 0.0), (82.0, -70.0, 18.0)), ((310.0, -94.0, 0.0), (358.0, -70.0, 18.0)),
             ((-96.0, -76.0, 36.0), (34.0, -70.0, 148.0)), ((82.0, -76.0, 36.0), (310.0, -70.0, 148.0)),
             ((358.0, -76.0, 36.0), (372.0, -70.0, 148.0)), ((-96.0, 77.0, 36.0), (372.0, 81.0, 148.0)),
             ((-96.0, -70.0, 36.0), (-90.0, 77.0, 148.0)), ((366.0, -70.0, 36.0), (372.0, 77.0, 148.0)),
             ((-96.0, -76.0, 148.0), (449.0, 81.0, 156.0))]
BUS_SPEED = 440.0  # units a second: BO2's speed 30 is mph (528 units/s), slowed down at its corners
BUS_STOP = "reached_stop_point"  # the script_notify of the vehicle nodes where the bus stops


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
WINDOW_MANTLE = 64.0  # a mantle whose middle lies this close to a boarded window is that window's traversal
WINDOW_WALL_REACH = 80  # a zbarrier's wall lies within this many units in front of it
WINDOW_SPAWNER = 128.0  # a spawner added for a window stands this far outside it
SPAWN_AGREE = 0.9       # start points whose yaws' mean resultant length reaches this face one way (within ~25 deg)
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
        within 24 units of origin's height: where a player can stand to use the thing; yaws[0] when none is. Tested
        every 4 units: TranZit's depot walls are 3 units thick, and every 8 Quick Revive faced one."""
        for angle in yaws:
            f = (math.cos(math.radians(angle)), math.sin(math.radians(angle)))
            clear = all(not self.solid_at((origin[0] + f[0] * d, origin[1] + f[1] * d, origin[2] + h), skip)
                        for d in range(24, 57, 4) for h in (16, 48))
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


def zones_beside(zones, mins, maxs, reach=BESIDE_REACH):
    """The two zones nearest a blocker's box (a zone and the box touch or come within reach), or (None, None)."""
    def gap(z):
        return sum(max(z["mins"][k] - maxs[k], 0, mins[k] - z["maxs"][k]) ** 2 for k in range(3)) ** 0.5
    near = sorted((gap(z), z["name"]) for z in zones)
    near = [name for d, name in near if d <= reach]
    return (near[0], near[1]) if len(near) >= 2 else (None, None)


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
            # TranZit's flags name doors, not zones ("OnTowDoorBarber"): the two zones that touch the blocker's box
            a, b = zones_beside(zones, mins, maxs)
        if not a or not b:
            print(f"blocker {target}: no zones for flag {trigger.get('script_flag')} nor beside it", file=sys.stderr)
            continue
        group = {"kind": kind, "name": target, "mins": mins, "maxs": maxs, "yaw": 0,
                 "zones": (a, b), "cost": int(float(trigger.get("zombie_cost", 1000))), "meshGroup": target}
        if kind == "door" and trigger.get("script_noteworthy") == "local_electric_door":
            # TranZit's doors that the power opens by itself (MapKit's electric_door)
            group["power"], group["cost"] = 1, 0
        groups.append(group)

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


def bus_route(ents):
    """TranZit's bus loop: (start node entity, [points], [(point index, wait seconds, stop name)]), or None. The loop
    is the info_vehicle_node chain through the depot's stop node (script_notify reached_stop_point); its stops are
    the nodes with that notify, and their script_float is BO2's wait there."""
    nodes = {e["targetname"]: e for e in ents if e.get("classname", "").startswith("info_vehicle_node")
             and "targetname" in e}
    start = next((n for n, e in nodes.items()
                  if e.get("script_notify") == BUS_STOP and e.get("script_noteworthy") == "depot"), None)
    if not start:
        return None
    points, stops, cur = [], [], start
    while cur in nodes and len(points) < len(nodes):
        e = nodes[cur]
        if e.get("script_notify") == BUS_STOP:
            stops.append((len(points), float(e.get("script_float", 10)), e.get("script_noteworthy", "")))
        points.append(vec(e["origin"]))
        cur = e.get("target")
        if cur == start:
            return nodes[start], points, stops
    return None


def bus_box(ents):
    """The parked bus's box in the world (its origin, model bounds, 32 more above for the nodes on its roof)."""
    vehicle = next((e for e in ents if e.get("classname") == "script_vehicle"
                    and e.get("model") == BUS_MODEL), None)
    if not vehicle:
        return None
    o, a = vec(vehicle["origin"]), math.radians(yaw(vehicle))
    corners = [(x, y) for x in (-96.0, 449.0) for y in (-74.0, 81.0)]
    xs = [o[0] + x * math.cos(a) - y * math.sin(a) for x, y in corners]
    ys = [o[1] + x * math.sin(a) + y * math.cos(a) for x, y in corners]
    return [min(xs), min(ys), o[2] - 16], [max(xs), max(ys), o[2] + 188]


def bus_edits(ents, col):
    """The bus's brush models at its start (the depot stop), its "bus" edit and the mesh edit's mover fields."""
    route = bus_route(ents)
    if not route:
        return [], {}
    start, points, stops = route
    # the bus stands on the road: each point dropped onto the floor under it, what an actor walks on (terrain and clips
    # included: the solid brushes alone dropped five points by the diner 2 400 units, under the road); BO2's vehicle
    # nodes float about 40 units over the road, so a point with no floor within 128 units stays 48 under its node
    ground = []
    for p in points:
        z = col.floor([p[0], p[1], p[2] + 64])
        if z is None or p[2] - z > 128:
            z = p[2] - 48
        ground.append([round(p[0], 2), round(p[1], 2), round(z, 2)])
    heading = math.atan2(ground[1][1] - ground[0][1], ground[1][0] - ground[0][0])
    c, s_ = math.cos(heading), math.sin(heading)

    def world_box(lo, hi):
        corners = [(x, y) for x in (lo[0], hi[0]) for y in (lo[1], hi[1])]
        xs = [ground[0][0] + x * c - y * s_ for x, y in corners]
        ys = [ground[0][1] + x * s_ + y * c for x, y in corners]
        return ([round(min(xs), 2), round(min(ys), 2), round(ground[0][2] + lo[2], 2)],
                [round(max(xs), 2), round(max(ys), 2), round(ground[0][2] + hi[2], 2)])

    edits = []
    lo, hi = world_box(*BUS_FLOOR)
    edits.append({"kind": "ent", "keys": {"classname": "script_brushmodel", "targetname": "mapkit_bus_floor"},
                  "bmodel": {"mins": lo, "maxs": hi}})
    for part in BUS_PARTS:
        lo, hi = world_box(*part)
        edits.append({"kind": "ent", "keys": {"classname": "script_brushmodel", "targetname": "mapkit_bus_part"},
                      "bmodel": {"mins": lo, "maxs": hi}})
    pivot = [round((BUS_FLOOR[0][k] + BUS_FLOOR[1][k]) / 2, 2) for k in range(3)]
    edits.append({"kind": "bus", "name": "bus", "floor": "mapkit_bus_floor", "parts": "mapkit_bus_part",
                  "pivot": pivot, "speed": BUS_SPEED, "path": ground,
                  "stops": [[index, wait, 0] for index, wait, _ in stops]})
    # the brush models spawn with angles 0: the model is drawn turned by the loop's first heading
    mover = {"moverModel": BUS_MODEL, "moverEntity": "mapkit_bus_floor", "moverOffset": pivot,
             "moverYaw": round(math.degrees(heading), 2)}
    return edits, mover


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

    # One spawn edit: MapKit spreads it over the four players, 40 units apart across its facing, and _zombiemode puts
    # player i on the i-th initial_spawn_points struct, so only the first edit's points were ever used. BO2's points
    # are one per player already; an edit on each spread four more around it, the outer ones into the fences and
    # house walls beside them ("Spawn point entity ... is in solid"). The edit stands at the centre of the points in
    # the first point's zone (Nuketown: the cul-de-sac street between them), facing their mean yaw, when they face
    # about the same way (SPAWN_AGREE); points around a room (TranZit's depot) keep the first point, the one used so far.
    starts = [e for e in ents if e.get("targetname") == "initial_spawn_points"]
    start_zones = {zone_of(vec(e["origin"])) for e in starts}  # every BO2 start zone: the doors' unlocked side
    if starts:
        zone = zone_of(vec(starts[0]["origin"]))
        group = [e for e in starts if zone_of(vec(e["origin"])) == zone]
        sx = sum(math.sin(math.radians(yaw(e))) for e in group)
        cx = sum(math.cos(math.radians(yaw(e))) for e in group)
        if math.hypot(sx, cx) / len(group) >= SPAWN_AGREE:
            origin = [round(sum(vec(e["origin"])[k] for e in group) / len(group), 2) for k in range(3)]
            facing = round(math.degrees(math.atan2(sx, cx)) % 360.0, 1)
        else:
            origin, facing = vec(starts[0]["origin"]), yaw(starts[0])
        edits.append({"kind": "spawn", "origin": origin, "yaw": facing, "zone": zone})

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
        elif e.get("script_noteworthy") == "spawn_location" and target.endswith("_spawners"):
            # TranZit's zombies also come from behind its barricades (script_string "depot_baricade4"): a spawner
            # there, on the floor, whose zombies walk in over the barricade's traversal
            zone = target[: -len("_spawners")]
            if zone not in zone_names:
                continue
            p = vec(e["origin"])
            ground = col.floor([p[0], p[1], p[2] + NODE_REACH])
            edits.append({"kind": "spawner", "origin": [p[0], p[1], round(ground, 2) if ground is not None else p[2]],
                          "yaw": yaw(e), "zone": zone})

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

    # BO2's boarded windows (zbarrier_zmcore_BasicWoodBarrier: TranZit's 38) become MapKit windows: Five's prefab, six
    # boards zombies tear off and players rebuild, its clip, goal and traversal. The zbarrier stands outside, about 40
    # units in front of the wall, at the height of the sill; its yaw points into the building (its exterior_goal lies
    # behind it). MapKit's window goes in the middle of the wall (found by walking in along that yaw at knee height)
    # and on the inside floor: placed at the zbarrier, its inner traversal node fell into the wall. The mantle BO2's
    # barricade traversal became is left out: it would let zombies in past the boards.
    windows = []
    for e in ents:
        if not e.get("classname", "").startswith("zbarrier_zmcore_BasicWoodBarrier"):
            continue
        o = vec(e["origin"])
        angle = yaw(e)
        f = (math.cos(math.radians(angle)), math.sin(math.radians(angle)))
        ahead = [o[0] + f[0] * 48, o[1] + f[1] * 48, o[2]]
        behind = [o[0] - f[0] * 48, o[1] - f[1] * 48, o[2]]
        if not any(inside(ahead, z) for z in zones) and any(inside(behind, z) for z in zones):
            angle, f, ahead = angle + 180, (-f[0], -f[1]), behind
        knee = o[2] - 24
        hits = [d for d in range(0, WINDOW_WALL_REACH + 1, 2)
                if col.solid_at([o[0] + f[0] * d, o[1] + f[1] * d, knee], ())]
        wall = 0
        if hits:
            end = hits[0]
            for d in hits[1:]:
                if d - end > 2:
                    break
                end = d
            wall = (hits[0] + end) / 2
        o = [round(o[0] + f[0] * wall, 1), round(o[1] + f[1] * wall, 1), o[2]]
        inner = [o[0] + f[0] * 48, o[1] + f[1] * 48]
        ground = col.floor([inner[0], inner[1], o[2] + NODE_REACH])
        name = e.get("script_string") or e.get("targetname") or f"window{len(windows) + 1}"
        if any(w["name"] == name for w in windows):
            name = f"{name}_{len(windows) + 1}"
        windows.append({"kind": "window", "name": name,
                        "origin": [o[0], o[1], round(ground, 2) if ground is not None else round(o[2] - 37, 2)],
                        "yaw": round(angle % 360, 1)})
    mantles = len(edits)
    edits[:] = [t for t in edits if not (t["kind"] == "traverse" and t["anim"] == "mapkit_mantle" and any(
        math.dist(w["origin"][:2], [(t["begin"][k] + t["end"][k]) / 2 for k in range(2)]) < WINDOW_MANTLE
        for w in windows))]
    mantles -= len(edits)
    edits += windows
    # MapKit sends a spawner's zombies to the nearest window of its zone (the zone 48 units in from the window, 32
    # up): a window no BO2 spawner feeds gets one of its own, outside on the floor in front of it, so that every
    # barricade sees zombies as in BO2
    def window_zone(w):
        r = math.radians(w["yaw"])
        return zone_of([w["origin"][0] + math.cos(r) * 48, w["origin"][1] + math.sin(r) * 48, w["origin"][2] + 32])
    fed = set()
    for sp in [t for t in edits if t["kind"] == "spawner"]:
        mine = [w for w in windows if window_zone(w) == sp["zone"]]
        if mine:
            fed.add(min(mine, key=lambda w: math.dist(w["origin"], sp["origin"]))["name"])
    added = 0
    for w in windows:
        if w["name"] in fed:
            continue
        r = math.radians(w["yaw"])
        out = [w["origin"][0] - math.cos(r) * WINDOW_SPAWNER, w["origin"][1] - math.sin(r) * WINDOW_SPAWNER]
        ground = col.floor([out[0], out[1], w["origin"][2] + 64])
        if ground is None:
            continue
        edits.append({"kind": "spawner", "origin": [round(out[0], 1), round(out[1], 1), round(ground, 2)],
                      "yaw": w["yaw"], "zone": window_zone(w)})
        added += 1
    print(f"windows: {len(windows)} boarded windows, {mantles} barricade mantles they replace, {added} spawners added "
          f"for the windows no BO2 spawner feeds")

    chests = [e for e in ents if e.get("classname", "").startswith("zbarrier_") and "MagicBox" in e["classname"]]
    starts = [c for c in chests if "start" in c.get("script_noteworthy", "")] or chests[:1]
    cuts = []

    def place(origin, angle, yaws):
        """A machine's spot on the solid floor, BO2's clip there to cut, and the side a player can use it from."""
        ground = col.floor([origin[0], origin[1], origin[2] + 32], SOLID)
        # a loft floored by a clip brush only (TranZit's barn, Double Tap at z 264): the solid floor is the ground storey
        # far below, while BO2's spot stands on the clip an actor walks on
        walk = col.floor([origin[0], origin[1], origin[2] + 32])
        if walk is not None and abs(walk - origin[2]) < 4 and (ground is None or walk - ground > 32):
            ground = walk
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
    # TranZit lays out a set of wall buys per game mode and location (script_noteworthy "zrace_town",
    # "zstandard_farm, zgrief_farm"...): classic keeps those without one and those naming a classic location. The one
    # on the bus ("bus_buyable_weapon1", a trigger on the parked bus) would stay behind when the bus drives off.
    def classic_wallbuy(e):
        note = e.get("script_noteworthy")
        if note is None:
            return True
        return any(token.strip().startswith(CLASSIC) for token in note.split(","))

    for e in ents:
        if e.get("targetname") == "weapon_upgrade" and classic_wallbuy(e):
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
    machines, machines_skipped = [], []
    for e in ents:
        if e.get("script_noteworthy") == "zm_random_machine":
            land = by_targetname.get(e.get("target", ""), e)
            machines.append(place(vec(land["origin"]), yaw(land), (0, 180, 90, -90)))
    for perk, (origin, angle) in zip(PERKS, machines):
        edits.append({"kind": "perk", "perk": perk, "origin": origin, "yaw": angle})
    if len(machines) > len(PERKS):
        origin, angle = machines[len(PERKS)]
        edits.append({"kind": "pap", "name": "pap", "origin": origin, "yaw": angle})
    # TranZit places its machines per gametype (zm_perk_machine structs, script_string "zclassic_perks_transit ...")
    for e in ents:
        if e.get("targetname") != "zm_perk_machine" or CLASSIC not in e.get("script_string", ""):
            continue
        perk = e.get("script_noteworthy", "")
        origin, angle = place(vec(e["origin"]), yaw(e), (0, 180, 90, -90))
        if perk == "specialty_weapupgrade":
            edits.append({"kind": "pap", "name": "pap", "origin": origin, "yaw": angle})
        elif perk in PERKS or perk in EXTRA_PERKS:
            edits.append({"kind": "perk", "perk": perk, "origin": origin, "yaw": angle})
        else:
            machines_skipped.append(perk)
    # its power comes from a switch the players build: MapKit's switch stands where the classic one is built
    for e in ents:
        if (e.get("targetname") == "powerswitch_p6_zm_buildable_pswitch_body"
                and e.get("script_gameobjectname") == CLASSIC):
            o = vec(e["origin"])
            ground = col.floor([o[0], o[1], o[2] + 32], SOLID)
            edits.append({"kind": "powerswitch", "name": "switch",
                          "origin": [o[0], o[1], round(ground, 2) if ground is not None else o[2]], "yaw": yaw(e)})
            break
    bus, mover = bus_edits(ents, col)
    edits += bus
    edits[1].update(mover)
    if cuts:
        edits[0]["cut"] = cuts
    edits += blocker_edits(ents, submodels, zones, start_zones)
    edits += barrier_clips(edits, col)

    layout = {"name": name, "base": "zombie_pentagon", "empty": True, "edits": edits}
    if sum(1 for e in edits if e["kind"] == "node") > BIG_MAP_NODES:
        layout["nodeVisRange"] = NODE_VIS_RANGE
    counts = {}
    for edit in edits:
        counts[edit["kind"]] = counts.get(edit["kind"], 0) + 1
    return layout, counts, skipped, traverse_skipped, machines_skipped


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
    # TranZit's bus has pathnodes inside and on its roof where it is parked: they would be a floating island once
    # the bus drives off, so they are left out (with their links)
    box = bus_box(ents)
    placed = [e for e in ents if e.get("classname") == "node_pathnode"]
    dropped = {n for n, e in enumerate(placed) if box and inside(vec(e["origin"]), {"mins": box[0], "maxs": box[1]})}
    renumber = {}
    for n in range(len(placed)):
        if n not in dropped:
            renumber[n] = len(renumber)
    if dropped:
        print(f"{len(dropped)} pathnodes in the parked bus left out")
    if os.path.exists(base + ".paths.csv"):
        pairs = [(renumber[a], renumber[b]) for a, b in node_links(ents, read_paths(base + ".paths.csv"))
                 if a in renumber and b in renumber]
        links_out = args.links_out or f"{args.map}.links.txt"
        with open(links_out, "w", encoding="utf-8") as f:
            f.write("".join(f"{a} {b}\n" for a, b in pairs))
        print(f"{links_out}: {len(pairs)} links between pathnodes")
    else:
        print(f"{base}.paths.csv not found (dump the gameworldmp asset): no node links file")
    drop_ids = {id(placed[n]) for n in dropped}
    ents = [e for e in ents if id(e) not in drop_ids]
    layout, counts, skipped, traverse_skipped, machines_skipped = build(ents, col, name, collision,
                                                                        links_out and os.path.basename(links_out))
    out = args.out or f"{name}.json"
    with open(out, "w", encoding="utf-8") as f:
        json.dump(layout, f, indent=1)
    print(f"{out}: " + ", ".join(f"{v} {k}" for k, v in counts.items()))
    if skipped:
        print("wall weapons without a BO1 equivalent, left out: " + ", ".join(sorted(set(skipped))))
    if machines_skipped:
        print("machines without a BO1 equivalent on the base, left out: " + ", ".join(machines_skipped))
    if traverse_skipped:
        print("traversals without a BO1 animscript, left out: " + ", ".join(
            f"{k} x{traverse_skipped.count(k)}" for k in sorted(set(traverse_skipped))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
