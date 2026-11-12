#!/usr/bin/env python3
"""BO2 weapons as an OAT Linker project for BO1 (T5): the source of a mod's mod.ff.

    tools/bo2/bo2_weapon.py --dump <T6 dump> --bo1 <T5 dump> --out <project folder> \
        --weapon hamr_zm:hk21_zm --weapon hamr_upgraded_zm:hk21_upgraded_zm \
        --string WEAPON_HAMR=HAMR ...
    tools/bo2/bo2_weapon.py --all --dump <T6 dump> --dump <T6 dump> ... --bo1 <T5 dump> --localize <T6 localize dump> \
        --out <project folder>

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
  clan tag surfaces are dropped. Blended materials (scope lenses) become BO1's lens glass, emissive ones (tritium
  dots, reticles) BO1's additive sight material.
- images: IW images (images/<name>.iwi), which the Linker references and the mod ships next to mod.ff: BO1 loads
  them from file (OAT's T5 external image loader needs the patch in tools/bo2/oat).
- animations: BO2's, with each "sndnt#<alias>" notetrack renamed to the reference weapon's alias of the same action
  (fly_hamr_mag_out -> wpn_hk21_mag_out) or disabled when BO1 has none: BO1 has no BO2 sound aliases.
--all takes every BO2 box weapon of the dumps with its Pack-a-Punch version, picks the BO1 reference by class
(reference_for) and writes mp/bo2weapons.csv, the list mods/bo2weapons registers.
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
# Every enum value T5's weapon fields accept (OAT's ObjCommon/Game/T5/Weapon/WeaponStrings.h). A BO2 value outside
# them in a field whose BO1 value is one of them ("dwlefthand", "pistol spread") takes the reference weapon's.
T5_ENUM_VALUES = {"1:1", "2-Round Burst", "2:1", "3-Round Burst", "4-Round Burst", "4:1", "AltWeapon", "Ballistic",
    "Beltfed", "Bouncing diamond", "Don't stick", "Equipment", "Flash Grenade", "Frag Grenade", "Full Auto", "Gear",
    "Hellfire", "Javelin", "Killstreak Alt Stored Weapon", "Lethal grenade", "Magazine", "Minigun", "None",
    "Pip-On-A-Stick", "Rocket", "Rotate both axis, grenade style", "Rotate like a cylinder",
    "Rotate one axis, blade style", "ShortMagazine", "Shotgun", "Sidewinder", "Single Shot", "Smoke Grenade",
    "Specific use", "Stacked Fire", "Stick to all", "Stick to all, except ai and clients", "Stick to flesh",
    "Stick to ground", "Stick to ground, maintain yaw", "TVGuided", "Tactical grenade", "Turret Scope", "WireGuided",
    "altmode", "ballisticknife", "beltfed", "binoculars", "blade", "bolt", "bomb", "bottom", "briefcase", "bullet",
    "bullet_ap", "bullet_large", "bullet_small", "bullet_xtreme", "crossbow", "crosshair", "default", "dp28",
    "dualwield", "duck", "dud", "fire", "flashbang", "g11", "gas", "grenade", "grenade_bounce", "grenade_explode",
    "grimreaper", "hatchet", "heavy explosive", "hold", "item", "large", "left", "lmg", "m203", "medium", "melee",
    "mg", "mine", "minigun", "mortar_shell", "napalmblob", "non-player", "none", "nopump", "offhand", "other",
    "pistol", "primary", "projectile", "projectile_dud", "prone", "ptrs", "radio", "rearclip", "rearclipsniper",
    "remotecontrol", "reviver", "rifle", "rifle_grenade", "rocket", "rocket_explode", "rocket_explode_xtreme",
    "rocketlauncher", "shotgun", "small", "smg", "smoke", "sniper", "spread", "stand", "tank_shell", "top", "turret",
    "zipline"}
# The BO1 material used as the template of every opaque lit BO2 weapon material: colour, normal, specular and colour
# detail (the Pack-a-Punch camo slot) maps, no environment map (BO2's weapon materials have none in BO1's layout)
LIT_TEMPLATE = "mc/mtl_t5_weapon_law"
HUD_TEMPLATE = "hud_hk21"
# BO1 templates for BO2's see-through materials: scope glass (blended over the scene, lit) and glowing sight parts
# (mc_effect_hero adds colour x 1 + scene x (1 - alpha): with alpha 0 it adds, as BO2's "one one" emissive passes do)
GLASS_TEMPLATE = "mc/mtl_t5_weapon_longscope_lens"
EMISSIVE_TEMPLATE = "mc/mtl_weapon_scope_colt3x20_crosshair"
SLOT_NAMES = {"colorMap": ("colorMap", "Color_Map"), "normalMap": ("normalMap", "Normal_Map"),
              "specularMap": ("specularMap", "SpecularAndGloss", "Specular_Color_Map")}
DEFAULT_ACCURACY_GRAPH = "smg.accu"
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
    write_rgba_dxt5(out.astype(np.uint8), dst)


def write_rgba_dxt5(rgba, dst):
    """(height, width, 4) uint8 as a DXT5 DDS with its full mip chain (ImageMagick)."""
    h, w = rgba.shape[:2]
    tmp = dst + ".rgba"
    np.ascontiguousarray(rgba).tofile(tmp)
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
    rgba = np.empty(xy.shape[:2] + (4,), dtype=np.uint8)
    rgba[..., 0] = rgba[..., 1] = rgba[..., 2] = xy[..., 1]
    rgba[..., 3] = xy[..., 0]
    write_rgba_dxt5(rgba, dst)
    return True


class Project:
    def __init__(self, args):
        self.dumps = args.dump
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
        self.weapons = []      # converted BO2 weapons, in order

    def find(self, kind, name):
        """The first dump holding <kind>/<name> (BO2 zones share most weapon assets), or None."""
        for dump in self.dumps:
            path = os.path.join(dump, kind, name)
            if os.path.exists(path):
                return path
        return None

    # images ----------------------------------------------------------------------------------------------------------
    def image(self, bo2_name, normal=False, specular=None, additive=False):
        """The zone's image for a BO2 image: images/<name>.iwi, read by the Linker and shipped beside mod.ff. BO1 drops
        the pixels a zone embeds when the client restarts after loading mod.ff (fs_game): only images loaded from
        file come back (DB_LoadDelayedImages)."""
        key = (bo2_name, specular, additive)
        if key in self.images:
            return self.images[key]
        src = self.find("images", bo2_name + ".dds")
        if src is None:
            # streamed from an .ipak the dump could not read: the caller keeps the template's image
            self.notes.append(f"image {bo2_name}: not in the dumps")
            return None
        spec_src = self.find("images", specular + ".dds") if specular else None
        specular = specular if spec_src else None
        name = image_name(src) + ("_lit" if specular else "") + ("_add" if additive else "")
        dst = os.path.join(self.raw, "images", name + ".iwi")
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(src, "rb") as f:
            head = f.read(132)
        dds = src
        if specular:
            dds = dst[:-4] + ".dds"
            write_lit_colour(src, spec_src, dds)
        elif additive:
            dds = dst[:-4] + ".dds"
            rgba = read_rgba(src).copy()
            rgba[..., 3] = 0  # mc_effect_hero then adds the colour, as BO2's "one one" pass does
            write_rgba_dxt5(rgba, dds)
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
        src = self.find("materials", bo2_name + ".json")
        if src is None:
            self.notes.append(f"material {bo2_name}: not in the dumps, its surfaces dropped")
            self.materials[bo2_name] = None
            return None
        t6 = load_json(src)
        # the main lit pass (state 2; 4 is the extra-light pass, additive on every material on both games)
        main = t6["stateBits"][min(2, len(t6["stateBits"]) - 1)]
        blended = main.get("dstBlendRgb") != "zero"
        additive = main.get("srcBlendRgb") == "one" and main.get("dstBlendRgb") == "one"
        # BO2's slot names vary with the techset ("colorMap" / "Color_Map", "SpecularAndGloss"...)
        maps = {}
        for t in t6["textures"]:
            for slot, names in SLOT_NAMES.items():
                if t["name"] in names:
                    maps.setdefault(slot, t["image"])
        techset, region = t6["techniqueSet"], t6.get("cameraRegion", "")
        if "colorMap" not in maps or "digits" in techset:
            # no colour, or BO2's ammo counter digits (drawn from the ammo count: a fixed texture shows every digit)
            kind = None
        elif region.startswith("emissive") or "unlit" in techset:
            kind = "emissive"   # tritium dots, sight glows, reticles
        elif blended:
            kind = "glass"      # scope lenses, the Type 25's and the RPG's see-through parts
        else:
            kind = "lit"
        if kind is None:
            self.notes.append(f"material {bo2_name}: techset {techset}, surfaces dropped")
            self.materials[bo2_name] = None
            return None
        out = load_json(material_file(self.bo1, {"lit": LIT_TEMPLATE, "glass": GLASS_TEMPLATE,
                                                 "emissive": EMISSIVE_TEMPLATE}[kind]))
        for t in out["textures"]:
            slot = t["name"]
            if slot == "normalMap" and slot not in maps:
                t["image"] = "$identitynormalmap"  # flat: BO1's own neutral normal map
            elif slot in ("colorMap", "specularMap", "normalMap") and slot in maps:
                image = self.image(maps[slot], normal=slot == "normalMap",
                                   specular=maps.get("specularMap") if slot == "colorMap" and kind == "lit" else None,
                                   additive=slot == "colorMap" and kind == "emissive" and additive)
                if image:
                    t["image"] = image
        save_json(os.path.join(self.raw, "materials", bo2_name + ".json"), out)
        self.materials[bo2_name] = bo2_name
        return bo2_name

    def hud_material(self, bo2_name):
        if bo2_name in self.materials:
            return self.materials[bo2_name]
        t6 = load_json(self.find("materials", bo2_name + ".json"))
        colour = next(t["image"] for t in t6["textures"] if t["name"] == "colorMap")
        out = load_json(material_file(self.bo1, HUD_TEMPLATE))
        for t in out["textures"]:
            if t["name"] == "colorMap":
                t["image"] = self.image(colour) or t["image"]
        save_json(os.path.join(self.raw, "materials", bo2_name + ".json"), out)
        self.materials[bo2_name] = bo2_name
        return bo2_name

    # models ----------------------------------------------------------------------------------------------------------
    def xmodel(self, name, attachments=()):
        """BO2's xmodel; attachments: (xmodel name, IW offset) merged into each LOD (BO1 has no attached models)."""
        if name in self.xmodels:
            return True
        src = self.find("xmodel", name + ".json")
        if src is None:
            return False
        root = os.path.dirname(os.path.dirname(src))
        model = load_json(src)
        model.pop("physPreset", None)
        extra = []
        for attach, offset, tag in attachments:
            attach_src = self.find("xmodel", attach + ".json")
            if attach_src is None:
                self.notes.append(f"{name}: attached model {attach} not in the dumps")
                continue
            attach_root = os.path.dirname(os.path.dirname(attach_src))
            extra.append(([os.path.join(attach_root, lod["file"]) for lod in load_json(attach_src)["lods"]], offset,
                          tag))
        for i, lod in enumerate(model["lods"]):
            gltf = load_json(os.path.join(root, lod["file"]))
            for files, offset, tag in extra:
                gltf = merge_attachment(gltf, load_json(files[min(i, len(files) - 1)]), offset, tag)
            drop_surfaces(gltf, EMBLEM_MATERIALS)
            drop_surfaces(gltf, {m["name"] for m in gltf.get("materials", []) if self.material(m["name"]) is None})
            for m in gltf.get("materials", []):
                # a dropped material keeps an entry no surface uses: OAT wants a name there, one BO1 has
                m["name"] = self.material(m["name"]) or LIT_TEMPLATE
            save_json(os.path.join(self.raw, lod["file"]), gltf)
        save_json(os.path.join(self.raw, "xmodel", name + ".json"), model)
        self.xmodels.add(name)
        return True

    # animations ------------------------------------------------------------------------------------------------------
    def xanim(self, name, aliases):
        if name in self.xanims:
            return True
        src = self.find("xanim", name)
        if src is None:
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
        t6 = read_weapon(self.find("weapons", bo2_name))
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
            if ref_value in T5_ENUM_VALUES and value not in T5_ENUM_VALUES and not NUMBER.match(value):
                value = ref_value
            elif key == "hideTags":
                # BO2 hides these (the folding iron sights) only when an optic is mounted; BO1, with no attachments,
                # would always hide them
                value = ""
            elif MODEL_KEY.match(key) and value:
                if not self.xmodel(value, attachments.get(key, ())):
                    value = ref_value
            elif value and (ANIM_KEY.search(key) or self.find("xanim", value)
                            or os.path.exists(os.path.join(self.bo1, "xanim", ref_value or "-"))):
                if not self.xanim(value, aliases):
                    value = ref_value  # BO2's anim is not in the dumps (the Mark II's dive to prone)
            elif ICON_KEY.match(key) and value and self.find("materials", value + ".json"):
                value = self.hud_material(value)
            elif value and not NUMBER.match(value) and REF_KEY.search(key):
                value = ref_value
                from_ref += 1
                out.append((key, value))
                continue
            kept += 1
            out.append((key, value))
        # the reference's accuracy graphs are files the Linker reads next to the weapon. An AI firing a weapon without
        # one ends the game (Actor_GetWeaponAccuracyFromGraph_SP: "No 'AI vs AI' accuracy graph"); BO1's box weapons
        # have none, so an empty one takes BO1's SMG graph
        for key, folder in (("aiVsAiAccuracyGraph", "aivsai"), ("aiVsPlayerAccuracyGraph", "aivsplayer")):
            if not dict(out).get(key):
                out = [(k, DEFAULT_ACCURACY_GRAPH if k == key else v) for k, v in out]
            graph = dict(out).get(key, "")
            src = os.path.join(self.bo1, "accuracy", folder, graph)
            if graph and os.path.exists(src):
                os.makedirs(os.path.join(self.raw, "accuracy", folder), exist_ok=True)
                shutil.copyfile(src, os.path.join(self.raw, "accuracy", folder, graph))
        os.makedirs(os.path.join(self.raw, "weapons"), exist_ok=True)
        write_weapon(os.path.join(self.raw, "weapons", bo2_name), out)
        self.lines.append(f"weapon,{bo2_name}")
        self.weapons.append(bo2_name)
        print(f"{bo2_name}: {kept} values from BO2, {from_ref} from {bo1_name}, "
              f"{len(t6) - len([k for k in t6d if k in dict(ref)])} BO2-only keys dropped")

    def bo1_reference(self, name):
        """name if the BO1 dump has it, else its base weapon (no _upgraded)."""
        for candidate in (name, name.replace("_upgraded", "", 1)):
            if os.path.exists(os.path.join(self.bo1, "weapons", candidate)):
                return candidate
        raise SystemExit(f"BO1 reference {name}: not in {self.bo1}/weapons")

    def weapon_tree(self, bo2_name, bo1_name):
        """bo2_name and the weapons it switches to (its underbarrel launcher, its left-hand twin): BO1 links them by
        name too (altWeapon, DualWieldWeapon), so they ship together."""
        if bo2_name in self.weapons:
            return
        self.weapon(bo2_name, self.bo1_reference(bo1_name))
        t6 = dict(read_weapon(self.find("weapons", bo2_name)))
        for key in ("altWeapon", "DualWieldWeapon"):
            linked = t6.get(key, "")
            if linked and linked not in self.weapons and self.find("weapons", linked):
                ref, _ = reference_for(linked, dict(read_weapon(self.find("weapons", linked))))
                if "_upgraded" in linked:
                    ref = UPGRADED_REF.get(ref, ref.replace("_zm", "_upgraded_zm"))
                self.weapon_tree(linked, ref)

    def weapon_table(self, rows):
        """mp/bo2weapons.csv: name, Pack-a-Punch name, VO type of each box weapon, read by mods/bo2weapons."""
        if not rows:
            return
        path = os.path.join(self.raw, "mp", "bo2weapons.csv")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="ascii", newline="\n") as f:
            for row in rows:
                f.write(",".join(row) + "\n")
        self.lines.append("stringtable,mp/bo2weapons.csv")

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
    T5 key of the model they join: gunModel / worldModel, with their IW-space offset from the bone they hang on
    (attach*ModelTagN, the root bone when empty). An "_ads" model (the M32's optic seen in ADS) only replaces its
    hip twin while aiming in BO2: it is left out."""
    out = {}
    for kind, base in (("View", "gunModel"), ("World", "worldModel")):
        for n in range(1, 17):
            model = t6.get(f"attach{kind}Model{n}", "")
            if not model or model.endswith("_ads"):
                continue
            offset = [float(t6.get(f"attach{kind}ModelOffset{a}{n}", "0") or 0) for a in "XYZ"]
            out.setdefault(base, []).append((model, offset, t6.get(f"attach{kind}ModelTag{n}", "")))
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


def merge_attachment(base, add, offset, tag=""):
    """add's meshes merged into base: add's model space placed at base's bone `tag` (its root bone when absent) +
    offset (IW units), its bones (if any) children of that bone, a rigid add bound to it."""
    base = json.loads(json.dumps(base))
    data, extra = gltf_bytes(base), gltf_bytes(add)
    skin = base["skins"][0]
    named = [j for j in skin["joints"] if base["nodes"][j].get("name") == tag]
    root = named[0] if named else skin["joints"][0]
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


# --all: BO2 weapons left out. Scripted ones need their BO2 logic in GSC; Origins' extended-clip / stalker copies of a
# box weapon and the items, turrets, hands and powerups are not box weapons.
SKIP = re.compile(r"^(blunder|slipgun|slowgun|staff_|tower_trap|minigun|knife_ballistic|equip_|zombie_|falling_hands|"
                  r"electrocuted|defaultweapon|quadrotor|jetgun|riotshield|tomb_shield|alcatraz_shield)|_extclip|_stalker")
# The BO1 reference of a BO2 weapon by class (sounds, effects, tracer, accuracy graph, notetrack sounds)
UPGRADED_REF = {"m16_zm": "m16_gl_upgraded_zm", "gl_m16_upgraded_zm": "gl_m16_upgraded_zm"}
VO = {"mg": "mg", "smg": "smg", "spread": "shotgun", "pistol": "pistol", "rocketlauncher": "launcher",
      "grenade": "launcher"}


def reference_for(name, d):
    """(BO1 reference weapon, BO1 VO type) of a BO2 weapon file d."""
    cls, fire = d.get("weaponClass", ""), d.get("fireType", "")
    if d.get("inventoryType") == "dwlefthand":
        return "cz75lh_zm", "dualwield"
    if d.get("dualWield") == "1":
        return "cz75dw_zm", "dualwield"
    if d.get("weaponType") == "projectile" and cls == "pistol":
        return "ray_gun_zm", "raygun"
    if cls == "mg":
        return "hk21_zm", "mg"
    if cls == "smg":
        return "mp5k_zm", "smg"
    if cls == "rifle":
        if d.get("adsOverlayShader"):
            return "l96a1_zm", "sniper"
        if fire == "Full Auto":
            return "commando_zm", "assault"
        if "Burst" in fire:
            return "m16_zm", "burstrifle"
        return "m14_zm", "rifle"
    if cls == "pistol spread" or re.match(r"(rnma|python)", name):
        return "python_zm", "pistol"
    if cls == "spread":
        return ("ithaca_zm" if re.match(r"(870|ksg)", name) else "spas_zm"), "shotgun"
    if cls == "pistol":
        return "cz75_zm", "pistol"
    if cls == "rocketlauncher":
        return "m72_law_zm", "launcher"
    if cls == "grenade":
        return ("gl_m16_upgraded_zm" if d.get("inventoryType") == "altmode" else "china_lake_zm"), "launcher"
    return "commando_zm", VO.get(cls, "assault")


def read_strings(folder):
    """REFERENCE -> text from the .str files of an OAT localize dump (the player's BO2 language)."""
    out = {}
    for path in glob.glob(os.path.join(folder, "**", "*.str"), recursive=True):
        key = None
        with open(path, encoding="latin-1") as f:
            for line in f:
                m = re.match(r"REFERENCE\s+(\S+)", line)
                if m:
                    key = m.group(1)
                    continue
                m = re.match(r'LANG_\w+\s+"(.*)"\s*$', line)
                if m and key:
                    out.setdefault(key, m.group(1))
                    key = None
    return out


def select_all(p):
    """--all: (base, upgrade or None) for each BO2 box weapon of the dumps, minus BO1's own names and SKIP."""
    bo1 = set(os.listdir(os.path.join(p.bo1, "weapons")))
    names = []
    for dump in p.dumps:
        for name in sorted(os.listdir(os.path.join(dump, "weapons"))):
            if name not in names:
                names.append(name)
    out = []
    for name in names:
        if not name.endswith("_zm") or name.endswith("_upgraded_zm") or SKIP.search(name) or name in bo1:
            continue
        d = dict(read_weapon(p.find("weapons", name)))
        if d.get("inventoryType") != "primary":
            continue
        upgrade = name[:-3] + "_upgraded_zm"
        out.append((name, upgrade if p.find("weapons", upgrade) else None))
    return out


def main():
    global SPECULAR_TO_COLOUR, COLOUR_GAIN
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dump", action="append", required=True,
                    help="OAT dump of a BO2 zone holding weapons (repeat; searched in this order)")
    ap.add_argument("--bo1", required=True, help="OAT dump of a BO1 zone with the reference weapons and materials")
    ap.add_argument("--out", required=True, help="OAT Linker project folder (base folder)")
    ap.add_argument("--weapon", action="append", default=[], help="BO2 weapon:BO1 reference weapon")
    ap.add_argument("--all", action="store_true", help="every BO2 box weapon of the dumps (references by class)")
    ap.add_argument("--localize", help="OAT localize dump of BO2 (the weapons' names)")
    ap.add_argument("--string", action="append", default=[], help="KEY=text (localized string)")
    ap.add_argument("--specular-mix", type=float, default=SPECULAR_TO_COLOUR,
                    help="share of BO2's specular colour added to the colour map")
    ap.add_argument("--colour-gain", type=float, default=COLOUR_GAIN, help="brightness factor of the colour map")
    args = ap.parse_args()
    SPECULAR_TO_COLOUR, COLOUR_GAIN = args.specular_mix, args.colour_gain
    if os.path.exists(os.path.join(args.out, "zone_raw", PROJECT)):
        shutil.rmtree(os.path.join(args.out, "zone_raw", PROJECT))
    p = Project(args)
    # the hint of a BO2 weapon on a wall (they are box weapons: retail add_zombie_weapon still precaches one)
    strings = {"ZOMBIE_WEAPON_BO2": "Hold ^3[{+activate}]^7 to buy weapon [Cost: &&1]"}
    strings.update(s.split("=", 1) for s in args.string)
    bo2_strings = read_strings(args.localize) if args.localize else {}
    table = []
    for pair in args.weapon:
        bo2_name, _, bo1_name = pair.partition(":")
        p.weapon(bo2_name, bo1_name)
    for base, upgrade in (select_all(p) if args.all else []):
        d = dict(read_weapon(p.find("weapons", base)))
        ref, vo = reference_for(base, d)
        p.weapon_tree(base, ref)
        if upgrade:
            p.weapon_tree(upgrade, UPGRADED_REF.get(ref, ref.replace("_zm", "_upgraded_zm")))
        table.append((base, upgrade or "", vo))
    for name in p.weapons:
        key = dict(read_weapon(p.find("weapons", name))).get("displayName", "")
        if key and key not in strings and key in bo2_strings:
            strings[key] = bo2_strings[key]
    p.localize(sorted(strings.items()))
    p.weapon_table(table)
    p.write_zone()
    for note in sorted(set(p.notes)):
        print("note:", note)
    print(f"{len(p.xmodels)} xmodels, {len(p.xanims)} xanims, {len(p.materials)} materials, {len(p.images)} images")


if __name__ == "__main__":
    main()
