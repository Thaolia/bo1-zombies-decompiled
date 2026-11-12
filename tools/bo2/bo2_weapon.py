#!/usr/bin/env python3
"""BO2 weapons as an OAT Linker project for BO1 (T5): the source of a mod's mod.ff.

    tools/bo2/bo2_weapon.py --dump <T6 dump> --bo1 <T5 dump> --out <project folder> \
        --weapon hamr_zm:hk21_zm --weapon hamr_upgraded_zm:hk21_upgraded_zm \
        --string WEAPON_HAMR=HAMR ...

<T6 dump> is OAT's Unlinker output for a BO2 zone (weapons/, xmodel/, model_export/ glTF, xanim/, materials/,
images/ DDS). <T5 dump> is a BO1 zone dumped the same way plus its materials (weapons/, xanim/, materials/); it gives
each BO2 weapon a reference BO1 weapon (BO2 name:BO1 name) and the material templates. Writes <out>/zone_raw/mod/ and
<out>/zone_source/mod.zone; then build with OAT's Linker (docs/BO2_WEAPONS.md has the command).

What changes on the way:
- weapon file: T5 takes 739 of T6's 1 027 keys; the reference weapon gives the others. Values that name assets BO1
  cannot get from this zone (fx, sounds, tracers, reticles, accuracy graphs) come from the reference weapon too; the
  models, animations and HUD icons are BO2's, shipped in the zone.
- xmodels: BO2's, without their "weapon" physics preset (BO1's weapon world models have none).
- materials: a copy of BO1's mtl_t5_weapon_law (colour detail slot kept so Pack-a-Punch camo works) with BO2's
  normal and specular maps, and BO2's colour map plus part of its specular colour (write_lit_colour). BO2's normal
  maps are BC5 (X in red, Y in green); BO1 reads DXT5 with X in alpha and Y in the colour channels. The emblem and
  clan tag surfaces are dropped.
- images: IW images (images/<name>.iwi), which the Linker references and the mod ships next to mod.ff: BO1 loads
  them from file (OAT's T5 external image loader needs the patch in tools/bo2/oat).
- animations: BO2's, with each "sndnt#<alias>" notetrack renamed to the reference weapon's alias of the same action
  (fly_hamr_mag_out -> wpn_hk21_mag_out) or disabled when BO1 has none: BO1 has no BO2 sound aliases.
The output is the player's own game data: keep it out of the repository.
"""
import argparse
import base64
import glob
import json
import math
import os
import re
import shutil
import struct
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bo2_mesh import dds_to_iwi, image_name  # noqa: E402

PROJECT = "mod"
# Keys whose text value names an asset or alias (numbers in these keys are kept from BO2)
REF_KEY = re.compile(r"fx|Fx|Effect|Sound|sound|Reticle|reticle|[Tt]racer|Overlay|accuracyGraph|Graph|[Ss]hader|"
                     r"camo|Camo|[Pp]rojectile|Table|table|[Ii]con|[Mm]odel")
MODEL_KEY = re.compile(r"^(gunModel\d*|worldModel\d*|attachViewModel\d*|attachWorldModel\d*|worldClipModel|"
                       r"handModel|knifeModel|worldKnifeModel|stowedModel)$")
ANIM_KEY = re.compile(r"Anim(Left|Right)?$")
ICON_KEY = re.compile(r"^(hudIcon|killIcon|ammoCounterIcon|dpadIcon|indicatorIcon)$")
NUMBER = re.compile(r"^-?\d+(\.\d+)?$")
# The BO1 material used as the template of every opaque lit BO2 weapon material: colour, normal, specular and colour
# detail (the Pack-a-Punch camo slot) maps, no environment map (BO2's weapon materials have none in BO1's layout)
LIT_TEMPLATE = "mc/mtl_t5_weapon_law"
HUD_TEMPLATE = "hud_hk21"
# The player's emblem and clan tag: BO2 fills them only for a custom emblem, BO1's materials of the same name draw
# coloured rectangles there
EMBLEM_MATERIALS = {"mc/mtl_player_icon", "mc/mtl_player_icon_world", "mc/mtl_clan_tag", "mc/mtl_clan_tag_world"}
DXGI_BC5_UNORM = 83
# BO1's colour map = (BO2's colour + SPECULAR_TO_COLOUR x its specular colour) x COLOUR_GAIN (--specular-mix,
# --colour-gain); defaults judged against BO2's HAMR and the user's request for a brighter weapon (2026-11-12)
SPECULAR_TO_COLOUR = 1.0
COLOUR_GAIN = 1.8


def read_weapon(path):
    with open(path, encoding="latin-1") as f:
        parts = f.read().split("\\")
    if parts[0] != "WEAPONFILE":
        raise SystemExit(f"{path}: not a WEAPONFILE")
    return list(zip(parts[1::2], parts[2::2]))


def write_weapon(path, pairs):
    with open(path, "w", encoding="latin-1", newline="") as f:
        f.write("WEAPONFILE\\" + "\\".join(f"{k}\\{v}" for k, v in pairs))


def load_json(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def save_json(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=4)


def material_file(dump, name):
    return os.path.join(dump, "materials", name + ".json")


def bc4_decode(blocks):
    """BC4 blocks (n, 8) uint8 -> (n, 16) uint8 values, texel order row by row."""
    r0 = blocks[:, 0].astype(np.int32)
    r1 = blocks[:, 1].astype(np.int32)
    bits = np.zeros(len(blocks), dtype=np.uint64)
    for i in range(6):
        bits |= blocks[:, 2 + i].astype(np.uint64) << np.uint64(8 * i)
    idx = np.stack([(bits >> np.uint64(3 * t)) & np.uint64(7) for t in range(16)], axis=1).astype(np.int32)
    pal = np.zeros((len(blocks), 8), dtype=np.int32)
    pal[:, 0], pal[:, 1] = r0, r1
    eight = r0 > r1
    for k in range(1, 7):
        pal[:, 1 + k] = np.where(eight, ((7 - k) * r0 + k * r1) // 7, pal[:, 1 + k])
    for k in range(1, 5):
        pal[:, 1 + k] = np.where(eight, pal[:, 1 + k], ((5 - k) * r0 + k * r1) // 5)
    pal[:, 6] = np.where(eight, pal[:, 6], 0)
    pal[:, 7] = np.where(eight, pal[:, 7], 255)
    return np.take_along_axis(pal, idx, axis=1).astype(np.uint8)


def bc5_top_level(path):
    """The largest mip of a DX10 BC5 DDS as (height, width, 2) uint8 (X, Y)."""
    with open(path, "rb") as f:
        data = f.read()
    height, width = struct.unpack_from("<II", data, 12)
    if data[84:88] != b"DX10" or struct.unpack_from("<I", data, 128)[0] != DXGI_BC5_UNORM:
        return None
    bw, bh = (width + 3) // 4, (height + 3) // 4
    raw = np.frombuffer(data, dtype=np.uint8, count=bw * bh * 16, offset=148).reshape(-1, 16)
    out = []
    for half in (raw[:, :8], raw[:, 8:]):
        texels = bc4_decode(half).reshape(bh, bw, 4, 4).transpose(0, 2, 1, 3).reshape(bh * 4, bw * 4)
        out.append(texels[:height, :width])
    return np.stack(out, axis=2)


def read_rgba(path, size=None):
    """A DDS (DXT1/3/5) as (height, width, 4) uint8 through ImageMagick, resized to size (width, height) if given."""
    cmd = ["convert", path]
    if size:
        cmd += ["-resize", f"{size[0]}x{size[1]}!"]
    out = subprocess.run(cmd + ["-depth", "8", "rgba:-"], check=True, capture_output=True).stdout
    w, h = size or tuple(int(v) for v in subprocess.run(["identify", "-format", "%w %h", path + "[0]"], check=True,
                                                        capture_output=True, text=True).stdout.split())
    return np.frombuffer(out[:w * h * 4], dtype=np.uint8).reshape(h, w, 4)


def write_lit_colour(colour, specular, dst):
    """BO1's colour map for a BO2 weapon material: (BO2's colour + SPECULAR_TO_COLOUR x its specular colour) x
    COLOUR_GAIN (DXT5, full mip chain, BO2's alpha kept). BO2 lights weapons as metals whose look is mostly their specular colour (the HAMR's
    tan receiver is in its specular map, its colour map is dark grey); BO1's weapon shading shows mostly the colour."""
    col = read_rgba(colour).astype(np.float32)
    spec = read_rgba(specular, (col.shape[1], col.shape[0])).astype(np.float32)
    out = col.copy()
    out[..., :3] = np.clip((col[..., :3] + SPECULAR_TO_COLOUR * spec[..., :3]) * COLOUR_GAIN, 0, 255)
    h, w = out.shape[:2]
    tmp = dst + ".rgba"
    out.astype(np.uint8).tofile(tmp)
    try:
        subprocess.run(["convert", "-size", f"{w}x{h}", "-depth", "8", f"rgba:{tmp}",
                        "-define", "dds:compression=dxt5", "-define", "dds:cluster-fit=true", f"DDS:{dst}"],
                       check=True)
    finally:
        os.remove(tmp)


def write_normal_dxt5(src, dst):
    """BO2's BC5 normal map as BO1's DXT5 layout (X in alpha, Y in red, green and blue), full mip chain."""
    xy = bc5_top_level(src)
    if xy is None:
        return False
    h, w = xy.shape[:2]
    rgba = np.empty((h, w, 4), dtype=np.uint8)
    rgba[..., 0] = rgba[..., 1] = rgba[..., 2] = xy[..., 1]
    rgba[..., 3] = xy[..., 0]
    tmp = dst + ".rgba"
    rgba.tofile(tmp)
    try:
        subprocess.run(["convert", "-size", f"{w}x{h}", "-depth", "8", f"rgba:{tmp}",
                        "-define", "dds:compression=dxt5", "-define", "dds:cluster-fit=true", f"DDS:{dst}"],
                       check=True)
    finally:
        os.remove(tmp)
    return True


class Project:
    def __init__(self, args):
        self.dump = args.dump
        self.bo1 = args.bo1
        self.raw = os.path.join(args.out, "zone_raw", PROJECT)
        self.zone_source = os.path.join(args.out, "zone_source")
        self.images = {}       # (BO2 image name, specular mixed in) -> zone image name
        self.materials = {}    # BO2 material name -> zone material name
        self.bo1_materials = {os.path.relpath(p, os.path.join(self.bo1, "materials"))[:-5].replace(os.sep, "/")
                              for p in glob.glob(os.path.join(self.bo1, "materials", "**", "*.json"), recursive=True)}
        self.xmodels = set()
        self.xanims = set()
        self.lines = []        # zone file entries
        self.notes = []

    # images ----------------------------------------------------------------------------------------------------------
    def image(self, bo2_name, normal=False, specular=None):
        """The zone's image for a BO2 image: images/<name>.iwi, read by the Linker and shipped beside mod.ff. BO1 drops
        the pixels a zone embeds when the client restarts after loading mod.ff (fs_game): only images loaded from
        file come back (DB_LoadDelayedImages)."""
        key = (bo2_name, specular)
        if key in self.images:
            return self.images[key]
        src = os.path.join(self.dump, "images", bo2_name + ".dds")
        if not os.path.exists(src):
            raise SystemExit(f"image {bo2_name}: no {src}")
        name = image_name(src) + ("_lit" if specular else "")
        dst = os.path.join(self.raw, "images", name + ".iwi")
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(src, "rb") as f:
            head = f.read(132)
        dds = src
        if specular:
            dds = dst[:-4] + ".dds"
            write_lit_colour(src, os.path.join(self.dump, "images", specular + ".dds"), dds)
        elif head[84:88] == b"DX10":
            if not normal:
                raise SystemExit(f"image {bo2_name}: DX10 format {struct.unpack_from('<I', head, 128)[0]} not handled")
            dds = dst[:-4] + ".dds"
            if not write_normal_dxt5(src, dds):
                raise SystemExit(f"image {bo2_name}: normal map not BC5")
        if not dds_to_iwi(dds, dst):
            raise SystemExit(f"image {bo2_name}: not a DXT1/3/5 DDS")
        if dds != src:
            os.remove(dds)
        self.images[key] = name
        return name

    # materials -------------------------------------------------------------------------------------------------------
    def material(self, bo2_name):
        """The zone's material for a BO2 material name used by a model."""
        if bo2_name in self.materials:
            return self.materials[bo2_name]
        if bo2_name in self.bo1_materials:
            self.materials[bo2_name] = bo2_name  # BO1 has it (player icon, clan tag): no copy
            return bo2_name
        stripped = re.sub(r"_world$", "", bo2_name)
        if stripped in self.bo1_materials:
            self.materials[bo2_name] = stripped
            return stripped
        t6 = load_json(material_file(self.dump, bo2_name))
        state = t6["stateBits"][min(2, len(t6["stateBits"]) - 1)]
        if not t6["techniqueSet"].startswith("mc_lit") or state.get("dstBlendRgb") != "zero":
            raise SystemExit(f"material {bo2_name}: techset {t6['techniqueSet']} (blended or unlit) not handled yet")
        maps = {t["name"]: t["image"] for t in t6["textures"]}
        out = load_json(material_file(self.bo1, LIT_TEMPLATE))
        for t in out["textures"]:
            slot = t["name"]
            if slot in ("colorMap", "specularMap", "normalMap"):
                if slot not in maps:
                    raise SystemExit(f"material {bo2_name}: no {slot}")
                t["image"] = self.image(maps[slot], normal=slot == "normalMap",
                                        specular=maps.get("specularMap") if slot == "colorMap" else None)
        save_json(os.path.join(self.raw, "materials", bo2_name + ".json"), out)
        self.materials[bo2_name] = bo2_name
        return bo2_name

    def hud_material(self, bo2_name):
        if bo2_name in self.materials:
            return self.materials[bo2_name]
        t6 = load_json(material_file(self.dump, bo2_name))
        colour = next(t["image"] for t in t6["textures"] if t["name"] == "colorMap")
        out = load_json(material_file(self.bo1, HUD_TEMPLATE))
        for t in out["textures"]:
            if t["name"] == "colorMap":
                t["image"] = self.image(colour)
        save_json(os.path.join(self.raw, "materials", bo2_name + ".json"), out)
        self.materials[bo2_name] = bo2_name
        return bo2_name

    # models ----------------------------------------------------------------------------------------------------------
    def xmodel(self, name, attachments=()):
        """BO2's xmodel; attachments: (xmodel name, IW offset) merged into each LOD (BO1 has no attached models)."""
        if name in self.xmodels:
            return True
        src = os.path.join(self.dump, "xmodel", name + ".json")
        if not os.path.exists(src):
            return False
        model = load_json(src)
        model.pop("physPreset", None)
        extra = []
        for attach, offset in attachments:
            lods = load_json(os.path.join(self.dump, "xmodel", attach + ".json"))["lods"]
            extra.append(([os.path.join(self.dump, lod["file"]) for lod in lods], offset))
        for i, lod in enumerate(model["lods"]):
            gltf = load_json(os.path.join(self.dump, lod["file"]))
            for files, offset in extra:
                gltf = merge_attachment(gltf, load_json(files[min(i, len(files) - 1)]), offset)
            drop_surfaces(gltf, EMBLEM_MATERIALS)
            for m in gltf.get("materials", []):
                m["name"] = self.material(m["name"])
            save_json(os.path.join(self.raw, lod["file"]), gltf)
        save_json(os.path.join(self.raw, "xmodel", name + ".json"), model)
        self.xmodels.add(name)
        return True

    # animations ------------------------------------------------------------------------------------------------------
    def xanim(self, name, aliases):
        if name in self.xanims:
            return True
        src = os.path.join(self.dump, "xanim", name)
        if not os.path.exists(src):
            return False
        with open(src, "rb") as f:
            data = f.read()
        at = notetrack_table(data)
        if at is None:
            if data[-1] != 0:
                raise SystemExit(f"xanim {name}: notetrack table not found")
            at = len(data) - 1  # no notetracks: the count byte 0 ends the file
        tracks = []
        p = at + 1
        for _ in range(data[at]):
            end = data.index(b"\0", p)
            tracks.append((data[p:end].decode("latin-1"), data[end + 1:end + 3]))
            p = end + 3
        out = bytearray(data[:at + 1])
        for track, frame in tracks:
            if track.startswith("sndnt#"):
                alias = sound_for(track[6:], aliases)
                if alias is None:
                    track = "bo2nt#" + track[6:]  # a plain notify: no sound
                    self.notes.append(f"{name}: no BO1 sound for {track[6:]}")
                else:
                    track = "sndnt#" + alias
            out += track.encode("latin-1") + b"\0" + frame
        os.makedirs(os.path.join(self.raw, "xanim"), exist_ok=True)
        with open(os.path.join(self.raw, "xanim", name), "wb") as f:
            f.write(out)
        self.xanims.add(name)
        return True

    # weapons ---------------------------------------------------------------------------------------------------------
    def weapon(self, bo2_name, bo1_name):
        t6 = read_weapon(os.path.join(self.dump, "weapons", bo2_name))
        ref = read_weapon(os.path.join(self.bo1, "weapons", bo1_name))
        t6d = dict(t6)
        aliases = reference_aliases(self.bo1, dict(ref))
        attachments = attached_models(t6d)
        out, kept, from_ref = [], 0, 0
        for key, ref_value in ref:
            value = t6d.get(key)
            if value is None:
                out.append((key, ref_value))
                from_ref += 1
                continue
            if key == "hideTags":
                # BO2 hides these (the folding iron sights) only when an optic is mounted; BO1, with no attachments,
                # would always hide them
                value = ""
            elif MODEL_KEY.match(key) and value:
                if not self.xmodel(value, attachments.get(key, ())):
                    value = ref_value
            elif value and (ANIM_KEY.search(key) or os.path.exists(os.path.join(self.dump, "xanim", value))):
                if not self.xanim(value, aliases):
                    value = ref_value
            elif ICON_KEY.match(key) and value and os.path.exists(material_file(self.dump, value)):
                value = self.hud_material(value)
            elif value and not NUMBER.match(value) and REF_KEY.search(key):
                value = ref_value
                from_ref += 1
                out.append((key, value))
                continue
            kept += 1
            out.append((key, value))
        os.makedirs(os.path.join(self.raw, "weapons"), exist_ok=True)
        write_weapon(os.path.join(self.raw, "weapons", bo2_name), out)
        self.lines.append(f"weapon,{bo2_name}")
        print(f"{bo2_name}: {kept} values from BO2, {from_ref} from {bo1_name}, "
              f"{len(t6) - len([k for k in t6d if k in dict(ref)])} BO2-only keys dropped")

    def localize(self, strings):
        if not strings:
            return
        path = os.path.join(self.raw, "english", "localizedstrings", "bo2weapons.str")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="latin-1", newline="\r\n") as f:
            f.write('VERSION             "1"\nCONFIG              "C:/projects/cod/t5/bin/StringEd.cfg"\n'
                    'FILENOTES           ""\n\n')
            for key, text in strings:
                f.write(f'REFERENCE           {key}\nLANG_ENGLISH        "{text}"\n\n')
            f.write("ENDMARKER\n")
        self.lines.insert(0, "localize,bo2weapons")

    def write_zone(self):
        os.makedirs(self.zone_source, exist_ok=True)
        with open(os.path.join(self.zone_source, PROJECT + ".zone"), "w", encoding="ascii", newline="\n") as f:
            f.write(">game,T5\n\n" + "\n".join(self.lines) + "\n")


def drop_surfaces(gltf, materials):
    """Removes the primitives drawn with these materials; a node left with no primitive keeps no mesh."""
    names = [m.get("name") for m in gltf.get("materials", [])]
    for mesh in gltf["meshes"]:
        mesh["primitives"] = [p for p in mesh["primitives"] if names[p["material"]] not in materials]
    for node in gltf["nodes"]:
        if "mesh" in node and not gltf["meshes"][node["mesh"]]["primitives"]:
            del node["mesh"]
            node.pop("skin", None)


def attached_models(t6):
    """BO2's attachViewModelN / attachWorldModelN (models the engine attaches to the weapon, a magazine here) by the
    T5 key of the model they join: gunModel / worldModel, with their IW-space offset."""
    out = {}
    for kind, base in (("View", "gunModel"), ("World", "worldModel")):
        for n in range(1, 17):
            model = t6.get(f"attach{kind}Model{n}", "")
            if not model:
                continue
            offset = [float(t6.get(f"attach{kind}ModelOffset{a}{n}", "0") or 0) for a in "XYZ"]
            out.setdefault(base, []).append((model, offset))
    return out


def quat_matrix(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), 0],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w), 0],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y), 0],
                     [0, 0, 0, 1]], dtype=np.float64)


def node_matrix(node):
    m = quat_matrix(node.get("rotation", [0, 0, 0, 1]))
    m[:3, 3] = node.get("translation", [0, 0, 0])
    return m


def world_matrices(gltf):
    """Each node's world matrix (scene roots at identity)."""
    nodes = gltf["nodes"]
    parent = {c: i for i, n in enumerate(nodes) for c in n.get("children", [])}
    out = {}

    def world(i):
        if i not in out:
            out[i] = (world(parent[i]) if i in parent else np.eye(4)) @ node_matrix(nodes[i])
        return out[i]
    for i in range(len(nodes)):
        world(i)
    return out


def matrix_trs(m):
    """A rigid matrix as glTF translation + rotation (x, y, z, w)."""
    r = m[:3, :3]
    w = math.sqrt(max(0.0, 1 + r[0, 0] + r[1, 1] + r[2, 2])) / 2
    if w > 1e-6:
        q = [(r[2, 1] - r[1, 2]) / (4 * w), (r[0, 2] - r[2, 0]) / (4 * w), (r[1, 0] - r[0, 1]) / (4 * w), w]
    else:
        i = int(np.argmax(np.diag(r)))
        j, k = (i + 1) % 3, (i + 2) % 3
        t = math.sqrt(max(0.0, 1 + r[i, i] - r[j, j] - r[k, k])) / 2
        q = [0.0, 0.0, 0.0, (r[k, j] - r[j, k]) / (4 * t)]
        q[i], q[j], q[k] = t, (r[j, i] + r[i, j]) / (4 * t), (r[k, i] + r[i, k]) / (4 * t)
    return [float(v) for v in m[:3, 3]], [float(v) for v in q]


def gltf_bytes(gltf):
    uri = gltf["buffers"][0]["uri"]
    if len(gltf["buffers"]) != 1 or not uri.startswith("data:"):
        raise SystemExit("glTF: one embedded buffer expected (OAT's dumps)")
    return bytearray(base64.b64decode(uri.split(",", 1)[1]))


# glTF is Y-up: OAT's root bones carry this -90 degree turn about X from IW's Z-up model space
IW_TO_GLTF = quat_matrix([-math.sqrt(0.5), 0.0, 0.0, math.sqrt(0.5)])
COMPONENTS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}
DTYPES = {5121: np.uint8, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}


def accessor_view(gltf, data, index):
    """(numpy view on data, accessor) of an accessor: rows of its components, written back in place."""
    acc = gltf["accessors"][index]
    view = gltf["bufferViews"][acc["bufferView"]]
    dtype = np.dtype(DTYPES[acc["componentType"]])
    comps = COMPONENTS[acc["type"]]
    stride = view.get("byteStride", dtype.itemsize * comps)
    start = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    arr = np.ndarray((acc["count"], comps), dtype=dtype, buffer=data, offset=start, strides=(stride, dtype.itemsize))
    return arr, acc


def merge_attachment(base, add, offset):
    """add's meshes merged into base: add's model space placed at base's root bone + offset (IW units), its bones
    (if any) children of that root bone, a rigid add bound to the root bone."""
    base = json.loads(json.dumps(base))
    data, extra = gltf_bytes(base), gltf_bytes(add)
    skin = base["skins"][0]
    root = skin["joints"][0]
    base_world = world_matrices(base)
    place = base_world[root] @ np.vstack([np.hstack([np.eye(3), np.array(offset)[:, None]]), [0, 0, 0, 1]]) \
        @ np.linalg.inv(IW_TO_GLTF)
    add_world = world_matrices(add)
    # the add's joints become base nodes; a rigid add's vertices take the root bone
    add_skin = add.get("skins", [None])[0]
    joint_map = {}
    if add_skin:
        for j in add_skin["joints"]:
            joint_map[j] = len(base["nodes"])
            base["nodes"].append({"name": add["nodes"][j]["name"]})
        for j in add_skin["joints"]:
            node = base["nodes"][joint_map[j]]
            children = [joint_map[c] for c in add["nodes"][j].get("children", []) if c in joint_map]
            if children:
                node["children"] = children
            parent = next((p for p in add_skin["joints"] if j in add["nodes"][p].get("children", [])), None)
            local = add_world[j] if parent is None else np.linalg.inv(add_world[parent]) @ add_world[j]
            if parent is None:
                local = np.linalg.inv(base_world[root]) @ place @ add_world[j]
                base["nodes"][root].setdefault("children", []).append(joint_map[j])
            node["translation"], node["rotation"] = matrix_trs(local)
    new_world = world_matrices(base)
    # move the add's vertices into base's space (its surfaces share vertex accessors: each one once)
    done = set()
    for mesh in add["meshes"]:
        for prim in mesh["primitives"]:
            if prim["attributes"]["POSITION"] in done:
                continue
            done.add(prim["attributes"]["POSITION"])
            pos, acc = accessor_view(add, extra, prim["attributes"]["POSITION"])
            moved = (place @ np.hstack([pos.astype(np.float64), np.ones((len(pos), 1))]).T).T[:, :3]
            pos[:] = moved
            acc["min"], acc["max"] = [float(v) for v in moved.min(0)], [float(v) for v in moved.max(0)]
            if "NORMAL" in prim["attributes"]:
                nrm, _ = accessor_view(add, extra, prim["attributes"]["NORMAL"])
                nrm[:] = (place[:3, :3] @ nrm.astype(np.float64).T).T
            if add_skin:
                joints, _ = accessor_view(add, extra, prim["attributes"]["JOINTS_0"])
                table = np.array([len(skin["joints"]) + k for k in range(len(add_skin["joints"]))] + [0] * 256)
                joints[:] = table[joints.astype(np.int64)]
    # one buffer: base's bytes, then the add's, then the new arrays
    def append(blob, align=4):
        while len(data) % align:
            data.append(0)
        at = len(data)
        data.extend(blob)
        return at
    add_at = append(bytes(extra))
    view_base, acc_base = len(base["bufferViews"]), len(base["accessors"])
    for view in add["bufferViews"]:
        view = dict(view, buffer=0, byteOffset=view.get("byteOffset", 0) + add_at)
        base["bufferViews"].append(view)
    for acc in add["accessors"]:
        base["accessors"].append(dict(acc, bufferView=acc["bufferView"] + view_base))

    def new_accessor(array, gl_type, kind, target=None):
        at = append(array.tobytes())
        view = {"buffer": 0, "byteOffset": at, "byteLength": array.nbytes}
        if target:
            view["target"] = target
        base["bufferViews"].append(view)
        base["accessors"].append({"bufferView": len(base["bufferViews"]) - 1, "componentType": gl_type,
                                  "count": len(array), "type": kind})
        return len(base["accessors"]) - 1
    if add_skin:
        skin["joints"] += [joint_map[j] for j in add_skin["joints"]]
    ibm, _ = accessor_view(base, data, skin["inverseBindMatrices"])
    ibms = [ibm.reshape(-1, 4, 4).copy()]
    if add_skin:
        # glTF matrices are column-major: store each inverse transposed
        ibms.append(np.stack([np.linalg.inv(new_world[joint_map[j]]).T for j in add_skin["joints"]]).astype(np.float32))
    skin["inverseBindMatrices"] = new_accessor(np.concatenate(ibms).reshape(-1, 16).astype(np.float32), 5126, "MAT4")
    material_index = {m["name"]: i for i, m in enumerate(base.get("materials", []))}
    scene_root = base["scenes"][base.get("scene", 0)]["nodes"][0]
    for mesh in add["meshes"]:
        prims = []
        for prim in mesh["primitives"]:
            attrs = {k: v + acc_base for k, v in prim["attributes"].items()}
            if not add_skin:
                count = add["accessors"][prim["attributes"]["POSITION"]]["count"]
                attrs["JOINTS_0"] = new_accessor(np.zeros((count, 4), np.uint8), 5121, "VEC4", 34962)
                weights = np.zeros((count, 4), np.float32)
                weights[:, 0] = 1
                attrs["WEIGHTS_0"] = new_accessor(weights, 5126, "VEC4", 34962)
            name = add["materials"][prim["material"]]["name"]
            if name not in material_index:
                material_index[name] = len(base["materials"])
                base["materials"].append({"name": name})
            prims.append(dict(prim, attributes=attrs, indices=prim["indices"] + acc_base,
                              material=material_index[name]))
        base["meshes"].append({"primitives": prims})
        base["nodes"].append({"name": f"surf{len(base['meshes']) - 1}", "mesh": len(base["meshes"]) - 1, "skin": 0})
        base["nodes"][scene_root].setdefault("children", []).insert(0, len(base["nodes"]) - 1)
    base["buffers"] = [{"byteLength": len(data),
                        "uri": "data:application/octet-stream;base64," + base64.b64encode(bytes(data)).decode()}]
    return base


def notetrack_table(data):
    """Offset of the xanim's notetrack table: a count byte, then count x (name, NUL, u16 frame) up to the end."""
    for at in range(len(data) - 4, max(0, len(data) - 8192), -1):
        count, p = data[at], at + 1
        if count == 0:
            continue
        ok = True
        for _ in range(count):
            end = data.find(b"\0", p)
            if end < 0 or end == p or end + 3 > len(data) or not all(32 <= c < 127 for c in data[p:end]):
                ok = False
                break
            p = end + 3
        if ok and p == len(data):
            return at
    return None


def reference_aliases(bo1, ref):
    """The sound aliases the reference weapon's animations play (sndnt# notetracks), by action: mag_out -> alias."""
    found = {}
    for key, value in ref.items():
        if not ANIM_KEY.search(key) or not value:
            continue
        path = os.path.join(bo1, "xanim", value)
        if not os.path.exists(path):
            continue
        with open(path, "rb") as f:
            for alias in re.findall(rb"sndnt#([\w]+)\0", f.read()):
                alias = alias.decode()
                found.setdefault(action(alias), alias)
    return found


def action(alias):
    """fly_hamr_mag_out / wpn_hk21_mag_out -> mag_out (the weapon's own token dropped)."""
    parts = alias.split("_")
    return "_".join(parts[2:]) if len(parts) > 2 and parts[0] in ("fly", "wpn") else alias


def sound_for(alias, aliases):
    return aliases.get(action(alias))


def main():
    global SPECULAR_TO_COLOUR, COLOUR_GAIN
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dump", required=True, help="OAT dump of the BO2 zone holding the weapons")
    ap.add_argument("--bo1", required=True, help="OAT dump of a BO1 zone with the reference weapons and materials")
    ap.add_argument("--out", required=True, help="OAT Linker project folder (base folder)")
    ap.add_argument("--weapon", action="append", required=True, help="BO2 weapon:BO1 reference weapon")
    ap.add_argument("--string", action="append", default=[], help="KEY=English text (localized string)")
    ap.add_argument("--specular-mix", type=float, default=SPECULAR_TO_COLOUR,
                    help="share of BO2's specular colour added to the colour map")
    ap.add_argument("--colour-gain", type=float, default=COLOUR_GAIN, help="brightness factor of the colour map")
    args = ap.parse_args()
    SPECULAR_TO_COLOUR, COLOUR_GAIN = args.specular_mix, args.colour_gain
    if os.path.exists(os.path.join(args.out, "zone_raw", PROJECT)):
        shutil.rmtree(os.path.join(args.out, "zone_raw", PROJECT))
    p = Project(args)
    for pair in args.weapon:
        bo2_name, _, bo1_name = pair.partition(":")
        p.weapon(bo2_name, bo1_name)
    p.localize([s.split("=", 1) for s in args.string])
    p.write_zone()
    for note in sorted(set(p.notes)):
        print("note:", note)
    print(f"{len(p.xmodels)} xmodels, {len(p.xanims)} xanims, {len(p.materials)} materials, {len(p.images)} images")


if __name__ == "__main__":
    main()
