"""The placed props of a BO2 map for bo2_bsp.py: BO1 xmodels for the OAT Linker (T5) and their collision in the BSP.

Drawing: each BO2 xmodel (OAT's Unlinker output with --model-format GLTF) becomes a BO1 xmodel of the same LODs, its
materials copies of BO1 model templates (mc/, colour map only) with BO2's colour maps; each placement of
<map>.d3dbsp.smodels.csv becomes a misc_model entity, which the engine draws as a static model (culled, with its LODs).

Collision: the engine only gives a static model collision from its xmodel's collision surfaces, which OAT's Linker does
not build, so the props collide through the BSP itself (as bo2_props_collision.py does for MapKit): a small box-like
model as an oriented box brush; a larger one as its collision LOD's triangles, while the BSP's 16-bit collision
vertices last (largest models last), then as an oriented box. Only the props inside the map's zones collide (the rest
lies past its invisible walls); lights do not (they caught a player's head).
"""
import csv
import json
import math
import os
import re
import shutil

from bo2_layout import Collision, read_entities, zone_edits
from bo2_mesh import FLAG_ALPHA_TEST, FLAG_BLEND, FLAG_DECAL, blend_flags, dds_to_iwi, image_name, material_colour, \
    skipped
from bo2_props_collision import BOX_MAX, LIGHTS, MIN_HALF, PLAY_PAD, box_like, collision_lod

MODEL_TEMPLATES = {0: "mtl_dest_glo_paper_01", FLAG_ALPHA_TEST: "mtl_filament", FLAG_DECAL: "mtl_theater_paper_scrawl",
                   FLAG_BLEND: "mtl_theater_paper_scrawl"}
SKY_TEMPLATE = "mtl_skybox_zombie_theater"  # mc_sky_cubemap_hdr: a cube map, BO2's sky models draw theirs the same way
COLLISION_VERTS = 0x10000
MODEL_FALLBACK = "mc/" + MODEL_TEMPLATES[0]  # a name for a glTF material entry no surface uses any more


COLOUR_SAMPLER = re.compile(r"colou?r_?map|diffuse", re.I)


def colour_dds(dump, material, images):
    """A material's colour map DDS: bo2_mesh's choice, else the first sampler named like a colour map (BO2's blended
    tile techsets: Micro_1_ColorMap on the desert rocks)."""
    dds = material_colour(dump, material, images)
    if dds:
        return dds
    try:
        with open(os.path.join(dump, "materials", material + ".json"), encoding="latin-1") as f:
            textures = json.load(f).get("textures", [])
    except (OSError, ValueError):
        return None
    for t in textures:
        image = t.get("image")
        if image and COLOUR_SAMPLER.search(t.get("name") or "") and os.path.exists(os.path.join(images, image + ".dds")):
            return os.path.join(images, image + ".dds")
    return None


def model_name(bo2):
    """A BO1 asset name for a BO2 xmodel ("mlv/x" -> "mlv_x")."""
    return bo2.replace("/", "_")


def axes_to_angles(ax, ay, az):
    """The entity angles (pitch, yaw, roll in degrees) of a model's x, y and z axes in the world (inverse of the
    game's AnglesToAxis, bo2_mesh.angles_to_axes)."""
    pitch = math.degrees(math.atan2(-ax[2], math.hypot(ax[0], ax[1])))
    yaw = math.degrees(math.atan2(ax[1], ax[0]))
    roll = math.degrees(math.atan2(ay[2], az[2]))
    return pitch, yaw, roll


class PropModels:
    """The xmodels, materials and images of the zone's props, written into an OAT Linker project."""

    def __init__(self, models_dump, images, templates_dir, raw, out_images):
        self.dump = models_dump
        self.images = images
        self.raw = raw
        self.out_images = out_images
        self.templates = {}
        for flag, name in list(MODEL_TEMPLATES.items()) + [("sky", SKY_TEMPLATE)]:
            with open(os.path.join(templates_dir, name + ".json"), encoding="utf-8") as f:
                self.templates[flag] = json.load(f)
        self.materials = {}   # BO2 material -> zone material or None
        self.models = {}      # BO2 xmodel -> zone xmodel or None
        self.notes = []
        for d in ("images", "xmodel", "model_export"):
            os.makedirs(os.path.join(raw, d), exist_ok=True)

    def material(self, bo2):
        if bo2 in self.materials:
            return self.materials[bo2]
        result = None
        dds = None if skipped(bo2) else colour_dds(self.dump, bo2, self.images)
        if dds:
            flags, multiply = blend_flags(self.dump, bo2)
            if self.is_sky(bo2):
                flags, multiply = "sky", False
            image = image_name(dds) + ("_mul" if multiply else "")
            iwi = os.path.join(self.out_images, image + ".iwi")
            if os.path.exists(iwi) or dds_to_iwi(dds, iwi, multiply):
                shutil.copyfile(iwi, os.path.join(self.raw, "images", image + ".iwi"))
                clean = re.sub(r"[^a-z0-9_]", "_", bo2.lower()).strip("_")[:48]
                result = f"mc/bo2m_{clean}"
                mat = json.loads(json.dumps(self.templates[flags if flags in self.templates else 0]))
                for t in mat["textures"]:
                    if t.get("name") == "colorMap":
                        t["image"] = image
                path = os.path.join(self.raw, "materials", result + ".json")
                os.makedirs(os.path.dirname(path), exist_ok=True)
                with open(path, "w", encoding="utf-8") as f:
                    json.dump(mat, f, indent=4)
            else:
                self.notes.append(f"material {bo2}: image the engine cannot take")
        self.materials[bo2] = result
        return result

    def is_sky(self, bo2):
        try:
            with open(os.path.join(self.dump, "materials", bo2 + ".json"), encoding="latin-1") as f:
                return "sky" in json.load(f).get("techniqueSet", "")
        except (OSError, ValueError):
            return False

    def model(self, bo2):
        """The zone's xmodel for a BO2 xmodel, None when it has nothing left to draw."""
        if bo2 in self.models:
            return self.models[bo2]
        name = model_name(bo2)
        try:
            with open(os.path.join(self.dump, "xmodel", bo2 + ".json"), encoding="utf-8") as f:
                data = json.load(f)
        except OSError:
            self.notes.append(f"xmodel {bo2}: not in the dump")
            self.models[bo2] = None
            return None
        data.pop("physPreset", None)
        data.pop("collLod", None)  # OAT builds no collision surfaces from it; the BSP carries the props' collision
        lods, drawn = [], False
        for i, lod in enumerate(data.get("lods", [])):
            with open(os.path.join(self.dump, lod["file"]), encoding="utf-8") as f:
                gltf = json.load(f)
            names = [self.material(m.get("name", "")) for m in gltf.get("materials", [])]
            for mesh in gltf.get("meshes", []):
                mesh["primitives"] = [p for p in mesh["primitives"] if names[p["material"]]]
                drawn |= bool(mesh["primitives"])
            for node in gltf.get("nodes", []):
                if "mesh" in node and not gltf["meshes"][node["mesh"]]["primitives"]:
                    del node["mesh"]
                    node.pop("skin", None)
            for m, zone in zip(gltf.get("materials", []), names):
                # an unused entry still needs a name the Linker finds
                m["name"] = zone or next((n for n in names if n), MODEL_FALLBACK)
            rel = f"model_export/{name}_lod{i}.gltf"
            with open(os.path.join(self.raw, rel), "w", encoding="utf-8") as f:
                json.dump(gltf, f)
            lods.append(dict(lod, file=rel))
        if not drawn:
            self.notes.append(f"xmodel {bo2}: nothing to draw")
            self.models[bo2] = None
            return None
        data["lods"] = lods
        with open(os.path.join(self.raw, "xmodel", name + ".json"), "w", encoding="utf-8") as f:
            json.dump(data, f, indent=4)
        self.models[bo2] = name
        return name

    def zone_lines(self):
        lines = [f"material,{m}" for m in sorted({m for m in self.materials.values() if m})]
        lines += [f"xmodel,{m}" for m in sorted({m for m in self.models.values() if m})]
        return lines



def place_props(bsp, base, props):
    """A misc_model entity per placement whose xmodel converts. Returns (placed, skipped)."""
    placed = left = 0
    with open(base + ".smodels.csv", encoding="latin-1") as f:
        for row in csv.DictReader(f):
            name = props.model(row["model"])
            if not name:
                left += 1
                continue
            ax = [float(row[f"ax{i}"]) for i in range(3)]
            ay = [float(row[f"ay{i}"]) for i in range(3)]
            az = [float(row[f"az{i}"]) for i in range(3)]
            pitch, yaw, roll = axes_to_angles(ax, ay, az)
            bsp.add_entity(classname="misc_model", model=name, origin=f"{row['x']} {row['y']} {row['z']}",
                           angles=f"{pitch:.4f} {yaw:.4f} {roll:.4f}", modelscale=row["scale"])
            placed += 1
    return placed, left


def box_sides(origin, axes, scale, lo, hi):
    """An oriented box (model-space [lo, hi] placed at origin with axes and scale): world bounds and six planes."""
    lo, hi = list(lo), list(hi)
    for k in range(3):
        if hi[k] - lo[k] < 2 * MIN_HALF:
            mid = (lo[k] + hi[k]) / 2
            lo[k], hi[k] = mid - MIN_HALF, mid + MIN_HALF
    corners = []
    for i in range(8):
        local = [hi[k] if i >> k & 1 else lo[k] for k in range(3)]
        corners.append([origin[k] + scale * sum(local[j] * axes[j][k] for j in range(3)) for k in range(3)])
    sides = []
    for j in range(3):
        length = math.sqrt(sum(a * a for a in axes[j]))
        unit = [a / length for a in axes[j]]
        d0 = sum(origin[k] * unit[k] for k in range(3))
        sides.append((tuple(unit), d0 + scale * length * hi[j]))
        sides.append((tuple(-a for a in unit), -(d0 + scale * length * lo[j])))
    return ([min(c[k] for c in corners) for k in range(3)], [max(c[k] for c in corners) for k in range(3)], sides)


def prop_collision(bsp, dump, base, material, verts_used):
    """The props' collision (module docstring). Returns (boxes, triangle props, triangles, fallback boxes)."""
    placements = {}
    with open(base + ".smodels.csv", encoding="latin-1") as f:
        for row in csv.DictReader(f):
            key = (row["model"], round(float(row["x"]), 1), round(float(row["y"]), 1), round(float(row["z"]), 1))
            placements[key] = row
    zones = zone_edits(read_entities(base + ".ents"), Collision(base + ".collision.bin").submodels)

    def in_play(o):
        return any(all(z["mins"][k] - PLAY_PAD <= o[k] <= z["maxs"][k] + PLAY_PAD for k in range(3)) for z in zones)

    cache, boxes, meshes = {}, [], []
    with open(base + ".smodelcoll.csv", encoding="latin-1") as f:
        for row in csv.DictReader(f):
            name = row["model"]
            if LIGHTS.search(name):
                continue
            place = placements.get((name, round(float(row["x"]), 1), round(float(row["y"]), 1),
                                    round(float(row["z"]), 1)))
            if not place:
                continue
            origin = [float(place[k]) for k in ("x", "y", "z")]
            if not in_play(origin):
                continue
            lod = collision_lod(dump, name, cache)
            if not lod:
                continue
            tris, bounds = lod
            axes = [[float(place[f"a{a}{i}"]) for i in range(3)] for a in "xyz"]
            scale = float(place["scale"])
            entry = (origin, axes, scale, tris, bounds)
            if max(bounds[1][k] - bounds[0][k] for k in range(3)) * scale <= BOX_MAX and box_like(tris, bounds):
                boxes.append(entry)
            else:
                meshes.append(entry)

    def add_box(origin, axes, scale, bounds):
        mins, maxs, sides = box_sides(origin, axes, scale, bounds[0], bounds[1])
        bsp.add_collision_brush(mins, maxs, material, [material] * 6, [(n, d, material) for n, d in sides])

    for origin, axes, scale, _, bounds in boxes:
        add_box(origin, axes, scale, bounds)
    # Triangles while the 16-bit collision vertices last, smallest meshes first; the rest as boxes.
    meshes.sort(key=lambda e: len(e[3]))
    tri_props = tri_count = fallback = 0
    for origin, axes, scale, tris, bounds in meshes:
        local = {}
        for tri in tris:
            for p in tri:
                local.setdefault(tuple(p), len(local))
        if verts_used + len(local) > COLLISION_VERTS:
            add_box(origin, axes, scale, bounds)
            fallback += 1
            continue
        world = [None] * len(local)
        for p, i in local.items():
            world[i] = [origin[k] + scale * sum(p[j] * axes[j][k] for j in range(3)) for k in range(3)]
        # model_tris winds each triangle with its right-hand normal out of the surface; the BSP's front is the other
        # winding's, so each one is written reversed
        out = [(local[tuple(t[2])], local[tuple(t[1])], local[tuple(t[0])], material) for t in tris]
        bsp.add_collision_triangles(world, out)
        verts_used += len(local)
        tri_props += 1
        tri_count += len(out)
    return len(boxes), tri_props, tri_count, fallback

