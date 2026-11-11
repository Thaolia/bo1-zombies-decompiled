#!/usr/bin/env python3
"""The drawable world of a BO2 map for MapKit's "mesh" edit: its world OBJ and its placed props (tools/bo2/oat) cut into
chunks, and their colour maps as IW images.

    tools/bo2/bo2_mesh.py <dump folder of the zone> <map name> --out <run>/mods/mapkit/imports/<map>

The props are the static models of <map>.d3dbsp.smodels.csv: one model per BO2 xmodel, with its LODs (model_export/
OBJs, distances from xmodel/<name>.json), drawn once per placement (a placement BO2 lit gets its own copy with the
baked colours). The world's vertices take BO2's lightmaps (bo2_worldlight.py). Writes <out>/<map>.mesh.bin (format below) and <out>/images/<name>.iwi (one
per colour map). The engine
(src/gfx_d3d/r_mapkit_mesh.cpp) turns each chunk into an XModel: one surface per material, so a chunk holds at most
MAX_SURFS surfaces of at most MAX_VERTS vertices (16-bit indices). Chunks are CELL units across in the playable area
and FAR_CELL beyond it (the distant backdrop), so the view culls them and the light sample at each chunk is local.

<map>.mesh.bin, little-endian:
  char[8] "BO2MESH5"
  u32 materialCount, then per material: char[64] BO2 material name, char[64] colour map image name (images/<it>.iwi),
      u32 flags (1: alpha tested in BO2: drawn with the engine's alpha-tested base material; 2: the sky's, a cube map
      drawn with the engine's skybox base material; 4: a decal, blended over the surface it lies on: drawn with the
      engine's decal base material (alpha blended, depth offset); 8: blended, not a decal (glass, grates): drawn with
      the engine's blended base material; the engine takes the alpha-tested base when it has no such base)
  u32 chunkCount, then per chunk: f32[3] origin, f32 radius, u32 surfaceCount, then per surface:
      u16 material, u16 pad, u32 vertCount, u32 triCount,
      vertCount x (f32[3] position relative to origin, f32[3] normal, f32[2] uv, u32 colour 0xAARRGGBB), triCount x u16[3]
  u32 modelCount, then per model: char[64] name, f32 radius, u32 lodCount, then per LOD: f32 distance (0: any),
      u32 surfaceCount, then its surfaces as a chunk's (positions in model space)
  u32 instanceCount, then per instance: u32 model, f32[3] origin, f32[3][3] axes (model x, y, z in the world), f32 scale
  i32 sky model (the worldspawn's skyboxmodel, one of the models above, drawn around the viewer; -1: none)
  u32 groupCount, then per group: char[64] name, u32 count, count x u32 instance: the props a door or debris removes
      when bought (BO2's blockers are script models: their doors and furniture, written as instances above); a group
      is named after its triggers' target, the "meshGroup" of the layout's door or debris edit (bo2_layout.py)
The output is the player's own game data: keep it out of the repository. docs/BO2_MAPS.md has the procedure.
"""
import argparse
import csv
import hashlib
import json
import math
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bo2_layout import read_entities  # noqa: E402  (the worldspawn names the sky model)
from bo2_worldlight import WorldLight, light_triangles, pack, read_world_light  # noqa: E402

CELL = 512.0
FAR_CELL = 8192.0
PLAY_HALF = 4096.0  # the playable area is about 5 000 units across, around the origin
MAX_VERTS = 65535   # XSurface::vertCount and triCount are 16-bit
MAX_TRIS = 65535
MAX_SURFS = 120     # the renderer's rigid path keeps at most 128 surfaces per model on its stack (R_SkinXModel)

# Materials not drawn: shadow casters and caulk (never visible)
SKIP = re.compile(r"caulk|(?<!no_)shadow|nodraw")
FLAG_ALPHA_TEST, FLAG_SKY, FLAG_DECAL, FLAG_BLEND = 1, 2, 4, 8
DECAL_NAME = re.compile(r"(^|/)decal_|_decal|_dec(_|\d|$)|_blend$")

WHITE = 0xFFFFFFFF  # the vertex colour of a surface with no baked light: the model shaders multiply by it
# BO2's lit static models (lprobe techniques, vs/ps_4_0 read with fxc /dumpbin) take the baked vertex light c (bytes
# R, G, B, A in memory, so 0xAABBGGRR as a u32) as an indirect light of 32 * c.rgb^2 (scaled by the light grid's ratio
# at the pixel's normal, about 1) and c.a as the sun's visibility: L = 32 * c.rgb^2 + c.a * sunDiffuse *
# saturate(dot(sunDir, N)), then sqrt(albedo^2 * L * exposure) as the world. The world's reference turns L into the
# vertex colour (WorldLight.colour), so props and world share one exposure.
PROP_INDIRECT = 32.0

DDS_FORMATS = {b"DXT1": (0x0B, 8), b"DXT3": (0x0C, 16), b"DXT5": (0x0D, 16)}


def read_smodel_light(path):
    """The dump's <map>.d3dbsp.smodellight.bin (tools/bo2/oat GfxWorldObjDumperT6.cpp): per static model placement
    (the rows of smodels.csv), its LODs' vertex colours; per model, its LODs' vertex positions, in the colours' order.
    None when the file is missing."""
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError:
        return None
    if data[:8] != b"BO2SMLT1":
        sys.exit(f"{path}: not a BO2SMLT1 file")
    at = 8

    def u32():
        nonlocal at
        at += 4
        return struct.unpack_from("<I", data, at - 4)[0]

    placements = []
    for _ in range(u32()):
        lods = []
        for _ in range(u32()):
            count = u32()
            lods.append(struct.unpack_from(f"<{count}I", data, at))
            at += 4 * count
        placements.append(lods)
    positions = {}
    for _ in range(u32()):
        name = data[at:at + 64].split(b"\0")[0].decode("latin-1")
        at += 64
        lods = []
        for _ in range(u32()):
            count = u32()
            lods.append([struct.unpack_from("<3f", data, at + 12 * k) for k in range(count)])
            at += 12 * count
        positions[name] = lods
    return placements, positions


def prop_light(stored, normal, world_light):
    """A BO2 baked static model vertex colour (0xAABBGGRR) at a world normal as the engine's vertex colour."""
    c = [((stored >> shift) & 0xFF) / 255.0 for shift in (0, 8, 16, 24)]
    ndl = min(max(sum(world_light.sun_dir[k] * normal[k] for k in range(3)), 0.0), 1.0)
    light = [PROP_INDIRECT * c[k] * c[k] + c[3] * world_light.sun_diffuse[k] * ndl for k in range(3)]
    return pack(world_light.colour(light))


def vertex_normals(lod, axes):
    """Each position's smooth outward normal in the world: a LOD's triangles are wound for drawing (right-hand normal
    into the surface, read_lods), turned by the placement's axes (model x, y, z in the world)."""
    sums = {}
    for tris in lod.values():
        for tri in tris:
            a, b, c = (t[0] for t in tri)
            e1 = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
            e2 = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
            n = (e1[2] * e2[1] - e1[1] * e2[2], e1[0] * e2[2] - e1[2] * e2[0], e1[1] * e2[0] - e1[0] * e2[1])
            for p in (a, b, c):
                s = sums.setdefault(p, [0.0, 0.0, 0.0])
                for k in range(3):
                    s[k] += n[k]
    out = {}
    for p, n in sums.items():
        w = [n[0] * axes[k] + n[1] * axes[3 + k] + n[2] * axes[6 + k] for k in range(3)]
        length = math.sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2])
        out[p] = tuple(v / length for v in w) if length > 1e-12 else (0.0, 0.0, 1.0)
    return out


def lit_lod(lod, positions, colours, axes, world_light):
    """A LOD's triangles (material -> [[(position, uv)] * 3]) with each vertex's baked light (prop_light), found by
    its position among the model's own vertices (exactly to 1/100 unit, else the nearest within 1 unit; white when
    none is)."""
    normals = vertex_normals(lod, axes)
    exact, cells = {}, {}
    for i, p in enumerate(positions):
        exact.setdefault((round(p[0], 2), round(p[1], 2), round(p[2], 2)), i)
        cells.setdefault((math.floor(p[0]), math.floor(p[1]), math.floor(p[2])), []).append(i)

    def colour_at(p):
        i = exact.get((round(p[0], 2), round(p[1], 2), round(p[2], 2)))
        if i is None:
            best = 1.0
            cx, cy, cz = math.floor(p[0]), math.floor(p[1]), math.floor(p[2])
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    for dz in (-1, 0, 1):
                        for j in cells.get((cx + dx, cy + dy, cz + dz), ()):
                            q = positions[j]
                            d = (q[0] - p[0]) ** 2 + (q[1] - p[1]) ** 2 + (q[2] - p[2]) ** 2
                            if d <= best:
                                best, i = d, j
        return prop_light(colours[i], normals[p], world_light) if i is not None else WHITE

    cache = {}
    out = {}
    for material, tris in lod.items():
        lit = []
        for tri in tris:
            corners = []
            for p, uv in tri:
                c = cache.get(p)
                if c is None:
                    c = cache[p] = colour_at(p)
                corners.append((p, uv, c))
            lit.append(corners)
        out[material] = lit
    return out


def read_mtl(path, images):
    """BO2 material name -> its colour map's DDS path (OAT's .mtl: newmtl, map_Kd). OAT writes the path relative to the
    .mtl as if it were one folder below the dump, which is wrong for a model in a sub-folder (mlv/...): a path that does
    not exist is looked up by its file name in images."""
    maps, name = {}, None
    base = os.path.dirname(path)
    with open(path, encoding="latin-1") as f:
        for line in f:
            line = line.rstrip("\n")
            if line.startswith("newmtl "):
                name = line[7:]
            elif line.startswith("map_Kd ") and name is not None:
                dds = os.path.normpath(os.path.join(base, line[7:]))
                if not os.path.exists(dds):
                    dds = os.path.join(images, os.path.basename(line[7:]))
                maps[name] = dds
    return maps


def image_name(dds_path):
    """A file name the engine accepts (OAT's names hold ~ $ & characters): readable part + hash."""
    stem = os.path.splitext(os.path.basename(dds_path))[0]
    clean = re.sub(r"[^a-z0-9_]", "", stem.lower())[:40]
    return f"bo2_{clean}_{hashlib.sha1(stem.encode('latin-1')).hexdigest()[:8]}"


def dds_to_iwi(src, dst, multiply=False):
    """A DXT DDS (largest mip first) as an IW image: GfxImageFileHeader, then the mips smallest first, which is the
    order Image_LoadDxtc uploads them in; multiply: as multiply_to_alpha's DXT5. Returns False for a format the engine
    cannot take."""
    try:
        with open(src, "rb") as f:
            data = f.read()
    except OSError:
        return False
    if data[:4] != b"DDS " or len(data) < 128:
        return False
    height, width = struct.unpack_from("<II", data, 12)
    mips = max(1, struct.unpack_from("<I", data, 28)[0])
    fourcc = data[84:88]
    if fourcc not in DDS_FORMATS:
        return False
    fmt, block = DDS_FORMATS[fourcc]
    full = int(math.log2(max(width, height))) + 1
    cube = struct.unpack_from("<I", data, 112)[0] & 0xFE00 == 0xFE00  # DDSCAPS2_CUBEMAP with all six faces
    faces, at = [], 128
    for _ in range(6 if cube else 1):
        levels = []
        for level in range(mips):
            w, h = max(1, width >> level), max(1, height >> level)
            size = block * ((w + 3) // 4) * ((h + 3) // 4)
            levels.append(data[at:at + size])
            at += size
        faces.append(levels)
    if at > len(data):
        return False
    if multiply:
        if cube:
            return False
        faces = [[multiply_to_alpha(level, fourcc) for level in face] for face in faces]
        fmt = DDS_FORMATS[b"DXT5"][0]
    flags = 0 if mips == full else 2  # 2: no mipmaps (Image_CountMipmaps), only the top level is uploaded
    if cube:
        flags |= 4  # a cube texture (Image_Setup); Image_LoadDxtc reads each mip's six faces in turn
    count = mips if mips == full else 1
    # DDS stores face by face, largest mip first; the engine reads mip by mip, smallest first, each mip's faces in order
    body = b"".join(faces[f][level] for level in reversed(range(count)) for f in range(len(faces)))
    total = 48 + len(body)
    header = b"IWi" + struct.pack("<BBB3hf8i", 13, fmt, flags, width, height, 1, 0.0, *([total] * 8))
    with open(dst, "wb") as f:
        f.write(header + body)
    return True


def dxt_colours(block, four):
    """The 16 (r, g, b) of a DXT colour block (8 bytes); four: always the 4-colour mode (DXT3/5)."""
    c0, c1, bits = struct.unpack("<HHI", block)
    rgb = [((c >> 11 & 31) * 255 // 31, (c >> 5 & 63) * 255 // 63, (c & 31) * 255 // 31) for c in (c0, c1)]
    if four or c0 > c1:
        rgb += [tuple((2 * a + b) // 3 for a, b in zip(rgb[0], rgb[1])),
                tuple((a + 2 * b) // 3 for a, b in zip(rgb[0], rgb[1]))]
    else:
        rgb += [tuple((a + b) // 2 for a, b in zip(rgb[0], rgb[1])), (0, 0, 0)]
    return [rgb[bits >> (2 * i) & 3] for i in range(16)]


def dxt5_alpha_block(alphas):
    """A DXT5 alpha block (8 bytes) for 16 alphas: 8-step ramp between their max and min."""
    hi, lo = max(alphas), min(alphas)
    if hi == lo:
        return struct.pack("<BB6x", hi, lo)
    ramp = [hi, lo] + [((7 - k) * hi + k * lo) // 7 for k in range(1, 7)]
    bits = 0
    for i, a in enumerate(alphas):
        bits |= min(range(8), key=lambda k: abs(ramp[k] - a)) << (3 * i)
    return struct.pack("<BB", hi, lo) + bits.to_bytes(6, "little")


def multiply_to_alpha(level, fourcc):
    """A multiply decal's DXT mip level (BO2 draws dst * src) as DXT5 black with alpha 1 - luminance: blended with
    srcalpha / invsrcalpha that gives dst * luminance, the same darkening for grey maps (a tinted map loses its tint)."""
    block, four = (8, False) if fourcc == b"DXT1" else (16, True)
    out = bytearray()
    for at in range(0, len(level), block):
        rgb = dxt_colours(level[at + block - 8:at + block], four)
        out += dxt5_alpha_block([255 - (r * 77 + g * 150 + b * 29 >> 8) for r, g, b in rgb]) + bytes(8)
    return bytes(out)


def read_obj(path):
    """Positions, uvs and the triangles of each OAT surface group with its material."""
    positions, uvs, groups = [], [], []
    material = None
    with open(path, encoding="latin-1") as f:
        for line in f:
            tag = line[:2]
            if tag == "v ":
                positions.append(tuple(float(x) for x in line.split()[1:4]))
            elif tag == "vt":
                # the OBJ's v is 1 - v (OBJ's convention, GfxWorldObjDumperT6); the engine wants the game's own
                u, v = (float(x) for x in line.split()[1:3])
                uvs.append((u, 1.0 - v))
            elif tag == "us":
                material = line.split(" ", 1)[1].rstrip("\n")
                groups.append((material, []))
            elif tag == "f ":
                corners = [tuple(int(i) - 1 for i in c.split("/")[:2]) for c in line.split()[1:4]]
                groups[-1][1].append(corners)
    return positions, uvs, groups


def read_bmodels(base):
    """BO2's brush models (<map>.d3dbsp.bmodels.obj, tools/bo2/oat's GfxWorldObjDumperT6): {index: material ->
    triangles of (position, uv)}, the positions relative to the entity's origin, wound as the world's ({} without
    the file: a dump from before it)."""
    path = base + ".bmodels.obj"
    if not os.path.exists(path):
        return {}
    positions, uvs, out = [], [], {}
    current, material = None, None
    with open(path, encoding="latin-1") as f:
        for line in f:
            tag = line[:2]
            if tag == "v ":
                positions.append(tuple(float(x) for x in line.split()[1:4]))
            elif tag == "vt":
                u, v = (float(x) for x in line.split()[1:3])
                uvs.append((u, 1.0 - v))
            elif tag == "g ":
                current = out.setdefault(int(line.split()[1][len("bmodel"):]), {})
            elif tag == "us":
                material = line.split(" ", 1)[1].rstrip("\n")
            elif tag == "f " and current is not None:
                corners = [tuple(int(i) - 1 for i in c.split("/")[:2]) for c in line.split()[1:4]]
                current.setdefault(material, []).append([(positions[c[0]], uvs[c[1]]) for c in corners])
    return out


def model_tris(path):
    """An OAT model OBJ (XModel/Obj/ObjWriter.cpp: game (x, y, z) written as (x, z, -y), v as 1 - v) as
    material -> triangles of (position, uv) in model space, each wound so that its right-hand normal points out of the
    surface, as the OBJ's normals do (a model whose faces mostly turn away from them is reversed). That is the
    collision's winding (bo2_props_collision.py puts each prism behind it); drawing takes the other one (read_lods)."""
    positions, uvs, normals, out = [], [], [], {}
    current, faces = None, []
    with open(path, encoding="latin-1") as f:
        for line in f:
            tag = line[:2]
            if tag == "v ":
                x, y, z = (float(v) for v in line.split()[1:4])
                positions.append((x, -z, y))
            elif tag == "vt":
                u, v = (float(x) for x in line.split()[1:3])
                uvs.append((u, 1.0 - v))
            elif tag == "vn":
                x, y, z = (float(v) for v in line.split()[1:4])
                normals.append((x, -z, y))
            elif tag == "us":
                current = line.split(" ", 1)[1].rstrip("\n")
            elif tag == "f " and current is not None:
                corners = [[int(i) - 1 for i in c.split("/")] for c in line.split()[1:4]]
                faces.append((current, corners))
    agree = 0.0
    for _, corners in faces:
        a, b, c = (positions[k[0]] for k in corners)
        e1 = [b[k] - a[k] for k in range(3)]
        e2 = [c[k] - a[k] for k in range(3)]
        n = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0])
        if all(len(k) > 2 for k in corners):
            vn = [sum(normals[k[2]][j] for k in corners) for j in range(3)]
            agree += n[0] * vn[0] + n[1] * vn[1] + n[2] * vn[2]
    for material, corners in faces:
        tri = [(positions[k[0]], uvs[k[1]] if len(k) > 1 else (0.0, 0.0)) for k in corners]
        out.setdefault(material, []).append(tri if agree >= 0 else tri[::-1])
    return out


def angles_to_axes(text):
    """An entity's "angles" (pitch yaw roll, degrees) as the model's x, y, z axes in the world, flattened (the game's
    AnglesToAxis: forward, left, up)."""
    pitch, yaw, roll = (math.radians(float(v)) for v in text.split())
    sp, cp, sy, cy, sr, cr = math.sin(pitch), math.cos(pitch), math.sin(yaw), math.cos(yaw), math.sin(roll), math.cos(roll)
    forward = (cp * cy, cp * sy, -sp)
    right = (-sr * sp * cy + cr * sy, -sr * sp * sy - cr * cy, -sr * cp)
    up = (cr * sp * cy + sr * sy, cr * sp * sy - sr * cy, cr * cp)
    return [*forward, *(-v for v in right), *up]


# Openings cut out of a mover model, (mins, maxs) boxes in model space (x forward, y left, z up): a triangle with its
# three corners inside one is left out. TranZit's bus has its two folding doors (bo2_layout.py BUS_PARTS: the
# openings at x 34-82 and 310-358 on the right side) shut in its bind pose, and the OBJ carries no bone weights to
# open them.
MOVER_HOLES = {
    "veh_t6_civ_bus_zombie": [((34.0, -84.0, 12.0), (82.0, -58.0, 128.0)), ((308.0, -84.0, 12.0), (358.0, -58.0, 128.0))],
}


# script_brushmodel entities drawn for good (by targetname): the others are a blocker's (drawn until bought) or come and
# go with scripts this import does not run (the depot's lava cover, blood decals hidden by the mature filter)
STANDING_BMODELS = ("bridge_section",)


def cut_holes(lod, holes):
    """lod (material -> triangles of (position, uv)) without the triangles lying inside one of holes."""
    def inside(tri, lo, hi):
        return all(lo[k] <= p[k] <= hi[k] for p, _ in tri for k in range(3))
    return {m: [t for t in tris if not any(inside(t, lo, hi) for lo, hi in holes)] for m, tris in lod.items()}


def read_lods(dump, name, with_index=False):
    """A model's LODs as [(distance, material -> triangles)]: OAT's xmodel JSON gives each LOD's OBJ and the distance
    up to which it draws (0 on the last one: drawn at any distance); without the JSON, the OBJs found, the last at 0."""
    lods = []
    try:
        with open(os.path.join(dump, "xmodel", name + ".json"), encoding="latin-1") as f:
            listed = [(float(l.get("distance", 0.0)), l["file"]) for l in json.load(f).get("lods", [])]
    except (OSError, ValueError, KeyError):
        listed = []
    if not listed:
        listed = [(0.0, f"model_export/{name}_lod{i}.obj") for i in range(4)
                  if os.path.exists(os.path.join(dump, "model_export", f"{name}_lod{i}.obj"))]
    # The engine draws a triangle whose right-hand normal points away from the viewer, BO2's own (clockwise) winding,
    # which the world OBJ keeps: a prop wound as model_tris returns it was drawn inside out, its back faces showing
    # (Nuketown's clock face turned to the tower, its numbers mirrored, signs and doors missing).
    for index, (dist, rel) in enumerate(listed):
        path = os.path.join(dump, rel)
        if os.path.exists(path):
            lod = {m: [tri[::-1] for tri in tris] for m, tris in model_tris(path).items()}
            lods.append((dist, lod, index) if with_index else (dist, lod))
    return lods


def blend_flags(dump, material):
    """How the BO2 material draws, from OAT's material JSON: (flags, multiply), judged on the state of its lit pass.
    BO2 blends nearly all its see-through materials (fences, leaves, overlays) after a depth prepass that BO1 lacks:
    blended without it, a hedge's leaves would draw out of order. So only the layers that lie on another surface
    become FLAG_DECAL (a depth offset on the lit pass and a decal sort, below BO2's 40, or a decal or overlay name);
    glass becomes FLAG_BLEND; any other blended or alpha-tested material stays FLAG_ALPHA_TEST. multiply: the decal
    multiplies the colour under it (src zero, dst srccolor: burn stains, cracks), which no base material does: its
    colour map is converted (multiply_to_alpha). A layered "*N(wpc/a:wpc/b)" material is its base layer's."""
    name = material
    if name.startswith("*"):
        inner = re.search(r"\(([^:)]+)", name)
        name = inner.group(1) if inner else name
    path = os.path.join(dump, "materials", name + ".json")
    try:
        with open(path, encoding="latin-1") as f:
            data = json.load(f)
    except (OSError, ValueError):
        return 0, False
    entries, states = data.get("stateBitsEntry", []), data.get("stateBits", [])
    # the lit pass: the first lit technique's state (T6 technique 3, else 4, the first "lit" one), else the unlit one
    lit = next((entries[t] for t in (3, 4, 2) if t < len(entries) and 0 <= entries[t] < len(states)), None)
    if lit is None:
        return 0, False
    bits = states[lit]
    src, dst = bits.get("srcBlendRgb"), bits.get("dstBlendRgb")
    multiply = src == "zero" and dst == "srccolor"
    blend = dst == "invsrcalpha" or multiply  # the lit passes blend premultiplied colour (src one)
    test = bits.get("alphaTest", "disabled") != "disabled"
    if not (blend or test):
        return 0, False
    overlay = DECAL_NAME.search(name) is not None or data.get("sortKey", 40) < 40
    if bits.get("polygonOffset", "offset0") != "offset0" and overlay:
        return FLAG_DECAL, multiply
    if blend and "glass" in name:
        return FLAG_BLEND, False
    return FLAG_ALPHA_TEST, False

def skipped(material):
    return not material.startswith("*") and SKIP.search(material) is not None


def cell_of(p):
    if abs(p[0]) < PLAY_HALF and abs(p[1]) < PLAY_HALF:
        return (0, math.floor(p[0] / CELL), math.floor(p[1] / CELL))
    return (1, math.floor(p[0] / FAR_CELL), math.floor(p[1] / FAR_CELL))


def build(groups, materials):
    """chunk key -> material index -> triangles of (position, uv), by each triangle's centre."""
    cells = {}
    for material, tris in groups.items():
        m = materials.get(material)
        if m is None:
            continue
        for tri in tris:
            centre = tuple(sum(c[0][k] for c in tri) / 3 for k in range(3))
            cells.setdefault(cell_of(centre), {}).setdefault(m, []).append(tri)
    return cells


def surfaces(tris):
    """Split one material's triangles into surfaces of at most MAX_VERTS shared vertices and MAX_TRIS triangles, with
    smooth normals over the vertices a surface shares (same position and uv)."""
    out = []
    index, verts, normals, faces = {}, [], [], []
    for tri in tris:
        a, b, c = (t[0] for t in tri)
        e1 = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
        e2 = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
        n = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0])
        if len(verts) + 3 > MAX_VERTS or len(faces) == MAX_TRIS:
            out.append((verts, normals, faces))
            index, verts, normals, faces = {}, [], [], []
        ids = []
        for key in tri:
            i = index.get(key)
            if i is None:
                i = index[key] = len(verts)
                verts.append(key)
                normals.append([0.0, 0.0, 0.0])
            for k in range(3):
                normals[i][k] += n[k]  # area-weighted (the cross product's length)
            ids.append(i)
        faces.append(ids)
    if verts:
        out.append((verts, normals, faces))
    return out


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dump", help="OAT's output folder for the zone")
    parser.add_argument("map", help="the map's name, e.g. zm_nuked")
    parser.add_argument("--out", required=True, help="the folder to write <map>.mesh.bin and images/ into")
    parser.add_argument("--mover", action="append", default=[],
                        help="an xmodel written without a placement, which the engine draws on a moving layout brush "
                             "model (the mesh edit's moverModel: TranZit's veh_t6_civ_bus_zombie); repeatable")
    args = parser.parse_args(argv)
    base = os.path.join(args.dump, "maps", "mp", f"{args.map}.d3dbsp")
    images = os.path.join(args.dump, "images")
    colour = read_mtl(base + ".mtl", images)
    positions, uvs, world = read_obj(base + ".obj")
    groups = {}
    # BO2's lightmaps as the world's vertex colours (bo2_worldlight.py); without the dump's worldlight files, white
    world_light = read_world_light(base)
    if world_light:
        corners, info = world_light
        if len(corners) != len(positions):
            sys.exit(f"{base}.worldlight.bin: {len(corners)} records for {len(positions)} OBJ vertices (rerun the "
                     "Unlinker: both come from the same dump)")
        world_light = WorldLight(args.dump, info)
        lit_tris = {}
        for material, faces in world:
            lit_tris.setdefault(material, []).extend(
                [[(positions[c[0]], uvs[c[1]] if c[1] >= 0 else (0.0, 0.0), corners[c[0]][:2], corners[c[0]][2],
                   corners[c[0]][3]) for c in face] for face in faces])
        world_light.calibrate_corners([t for tris in lit_tris.values() for t in tris])
        stats = {"splits": 0, "unlit": 0}
        for material, tris in lit_tris.items():
            groups[material] = light_triangles(world_light, tris, stats)
        print(f"world light: reference {', '.join(f'{r:.2f}' for r in world_light.reference)}; {stats['splits']} "
              f"triangle splits, {stats['unlit']} triangles without a lightmap; "
              f"{sum(len(t) for t in groups.values())} triangles")
    else:
        print("world light: no worldlight.bin in the dump (rerun tools/bo2/oat's Unlinker): world unlit")
        for material, faces in world:
            groups.setdefault(material, []).extend(
                [[(positions[c[0]], uvs[c[1]] if c[1] >= 0 else (0.0, 0.0)) for c in face] for face in faces])

    # props: one model per BO2 xmodel, with its LODs (distances from OAT's xmodel JSON), and one instance per placement
    exports = os.path.join(args.dump, "model_export")
    models, model_index, instances, missing = [], {}, [], set()

    def add_model(name):
        """The index of an xmodel in models, read on first use; -1 when it has nothing to draw."""
        if name not in model_index:
            # a LOD whose materials are all left out (decals) has nothing to draw: the engine wants surfaces
            lods = [(dist, {m: t for m, t in lod.items() if not skipped(m)}, index)
                    for dist, lod, index in read_lods(args.dump, name, with_index=True)]
            lods = [lod for lod in lods if lod[1]]
            if not lods:
                missing.add(name)
                model_index[name] = -1
                return -1
            for material, dds in read_mtl(os.path.join(exports, name + ".mtl"), images).items():
                colour.setdefault(material, dds)
            for _, tris, _ in lods:
                for material in tris:
                    groups.setdefault(material, [])  # listed with the world's materials
            model_index[name] = len(models)
            models.append((name, lods))
        return model_index[name]

    # BO2's brush models: the doors a blocker opens and TranZit's bridge sections, drawn by script_brushmodel entities
    # (the world OBJ holds the static surfaces only)
    bmodels = read_bmodels(base)
    if bmodels:
        for material, dds in read_mtl(base + ".bmodels.mtl", images).items():
            colour.setdefault(material, dds)

    def add_bmodel(entity):
        """The index of a script_brushmodel's surfaces in models, as one prop model; -1 when it has none."""
        model = entity.get("model", "")
        if not model.startswith("*") or entity.get("script_noteworthy") == "clip":
            return -1
        name = "bmodel" + model[1:]
        if name not in model_index:
            tris = {m: t for m, t in bmodels.get(int(model[1:]), {}).items() if not skipped(m)}
            if not tris:
                model_index[name] = -1
                return -1
            for material in tris:
                groups.setdefault(material, [])
            model_index[name] = len(models)
            models.append((name, [(0.0, tris, 0)]))
        return model_index[name]

    row_instance = {}
    with open(base + ".smodels.csv", encoding="latin-1") as f:
        for row_index, row in enumerate(csv.DictReader(f)):
            name = row["model"]
            if add_model(name) < 0:
                continue
            row_instance[row_index] = len(instances)
            axes = [float(row[f"a{a}{i}"]) for a in "xyz" for i in range(3)]
            instances.append((model_index[name], [float(row[k]) for k in ("x", "y", "z")], axes,
                              float(row["scale"])))

    # blockers: the script models a zombie_door or zombie_debris trigger targets (doors, couches, chairs), drawn
    # until the purchase, in groups the engine binds to the layout's door and debris edits
    ents = read_entities(base + ".ents")
    by_name = {}
    for e in ents:
        by_name.setdefault(e.get("targetname"), []).append(e)
    blocker_groups = {}
    for trigger in ents:
        if trigger.get("targetname") not in ("zombie_door", "zombie_debris") or not trigger.get("target"):
            continue
        group = trigger["target"]
        if group in blocker_groups:
            continue
        blocker_groups[group] = []
        for e in by_name.get(group, []):
            name = e.get("model", "")
            if e.get("classname") == "script_brushmodel":
                index = add_bmodel(e)
            elif e.get("classname") != "script_model" or not name or name.startswith("*"):
                continue
            else:
                index = add_model(name)
            if index < 0:
                continue
            blocker_groups[group].append(len(instances))
            instances.append((index, [float(v) for v in e["origin"].split()], angles_to_axes(e.get("angles", "0 0 0")),
                              float(e.get("modelscale", 1.0))))

    # brush models that always stand: TranZit's bridge (two sections, walked on, never drawn before)
    standing = 0
    for e in ents:
        if e.get("classname") == "script_brushmodel" and e.get("targetname") in STANDING_BMODELS:
            index = add_bmodel(e)
            if index >= 0:
                instances.append((index, [float(v) for v in e["origin"].split()],
                                  angles_to_axes(e.get("angles", "0 0 0")), 1.0))
                standing += 1
    if bmodels:
        print(f"brush models: {sum(1 for n, i in model_index.items() if n.startswith('bmodel') and i >= 0)} drawn "
              f"({standing} standing, the others with their blockers)")

    # BO2's baked light of each placed static model, one colour per vertex: a lit placement gets its own copy of its
    # model, its vertices coloured (the colour is in the vertex, and the light differs from one placement to another)
    light = read_smodel_light(base + ".smodellight.bin") if world_light else None
    lit_placements = 0
    if light:
        placements, model_positions = light
        for row, index in row_instance.items():
            colours = placements[row] if row < len(placements) else []
            model, origin, axes, scale = instances[index]
            name, lods = models[model]
            known = model_positions.get(name, [])
            new_lods, coloured = [], False
            for dist, lod, lod_index in lods:
                ok = (lod_index < len(colours) and lod_index < len(known) and colours[lod_index]
                      and len(colours[lod_index]) == len(known[lod_index]))
                new_lods.append((dist, lit_lod(lod, known[lod_index], colours[lod_index], axes, world_light) if ok else lod,
                                 lod_index))
                coloured |= bool(ok)
            if coloured:
                instances[index] = (len(models), origin, axes, scale)
                models.append((name, new_lods))
                lit_placements += 1
        # a model whose placements all got their own lit copy is no longer drawn: leave it out of the file
        used = sorted({instance[0] for instance in instances})
        remap = {old: new for new, old in enumerate(used)}
        models = [models[old] for old in used]
        instances = [(remap[model],) + tuple(rest) for model, *rest in instances]
    print(f"baked light: {lit_placements} placed models lit" if light else
          "baked light: no smodellight.bin or worldlight.bin in the dump (rerun tools/bo2/oat's Unlinker): props unlit")

    # the sky: the worldspawn's skybox model, one more model (no placement: the engine draws it around the viewer)
    sky, sky_materials = -1, set()
    worldspawn = next((e for e in read_entities(base + ".ents") if e.get("classname") == "worldspawn"), {})
    sky_name = worldspawn.get("skyboxmodel", "")
    if sky_name:
        lods = read_lods(args.dump, sky_name)[:1]
        if lods:
            for material, dds in read_mtl(os.path.join(exports, sky_name + ".mtl"), images).items():
                colour.setdefault(material, dds)
            # seen from inside, and the dump's winding is not the one the engine draws from there: both sides
            lods[0] = (lods[0][0], {m: tris + [tri[::-1] for tri in tris] for m, tris in lods[0][1].items()})
            sky_materials = set(lods[0][1])
            for material in sky_materials:
                groups.setdefault(material, [])
            sky = len(models)
            models.append((sky_name, [(0.0, lods[0][1])]))

    # the movers: models with no placement (after the lit copies' clean-up, which drops unplaced models)
    for mover in args.mover:
        lods = [(dist, cut_holes({m: t for m, t in lod.items() if not skipped(m)}, MOVER_HOLES.get(mover, [])))
                for dist, lod in read_lods(args.dump, mover)]
        lods = [lod for lod in lods if lod[1]]
        if not lods:
            sys.exit(f"--mover {mover}: no OBJ in {args.dump}/model_export")
        for material, dds in read_mtl(os.path.join(exports, mover + ".mtl"), images).items():
            colour.setdefault(material, dds)
        for _, tris in lods:
            for material in tris:
                groups.setdefault(material, [])
        models.append((mover, lods))

    os.makedirs(os.path.join(args.out, "images"), exist_ok=True)
    names = sorted(m for m in groups if not skipped(m))
    left_out = sorted(m for m in groups if skipped(m))
    materials, records, converted, failed = {}, [], {}, []
    for name in names:
        dds = colour.get(name)
        flags, multiply = (FLAG_SKY, False) if name in sky_materials else blend_flags(args.dump, name)
        image = ""
        if dds:
            image = image_name(dds) + ("_mul" if multiply else "")
            key = (dds, multiply)
            if key not in converted:
                converted[key] = dds_to_iwi(dds, os.path.join(args.out, "images", image + ".iwi"), multiply)
                if not converted[key]:
                    failed.append(os.path.basename(dds))
            if not converted[key]:
                image = ""
        materials[name] = len(records)
        records.append((name, image, flags))

    cells = build(groups, materials)
    chunks = []  # (origin, radius, [(material, verts, normals, faces)])
    for key in sorted(cells):
        surfs = []
        for m in sorted(cells[key]):
            surfs.extend((m,) + s for s in surfaces(cells[key][m]))
        for first in range(0, len(surfs), MAX_SURFS):
            part = surfs[first:first + MAX_SURFS]
            pts = [v[0] for s in part for v in s[1]]
            lo = [min(p[k] for p in pts) for k in range(3)]
            hi = [max(p[k] for p in pts) for k in range(3)]
            origin = [(lo[k] + hi[k]) / 2 for k in range(3)]
            radius = max(math.dist(origin, p) for p in pts)
            chunks.append((origin, radius, part))

    out_bin = os.path.join(args.out, f"{args.map}.mesh.bin")
    tris = verts = surf_count = 0
    model_tris_total = [0]
    with open(out_bin, "wb") as f:
        f.write(b"BO2MESH5")
        f.write(struct.pack("<I", len(records)))
        for name, image, flags in records:
            f.write(name.encode("latin-1")[:63].ljust(64, b"\0"))
            f.write(image.encode("latin-1")[:63].ljust(64, b"\0"))
            f.write(struct.pack("<I", flags))
        f.write(struct.pack("<I", len(chunks)))
        for origin, radius, part in chunks:
            f.write(struct.pack("<4fI", *origin, radius, len(part)))
            for m, vs, ns, fs in part:
                f.write(struct.pack("<HHII", m, 0, len(vs), len(fs)))
                buf = bytearray()
                for key, n in zip(vs, ns):
                    p, uv = key[0], key[1]
                    length = math.sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) or 1.0
                    buf += struct.pack("<8fI", p[0] - origin[0], p[1] - origin[1], p[2] - origin[2],
                                       n[0] / length, n[1] / length, n[2] / length, uv[0], uv[1],
                                       key[2] if len(key) > 2 else WHITE)
                for tri in fs:
                    buf += struct.pack("<3H", *tri)
                f.write(buf)
                tris += len(fs)
                surf_count += 1
                verts += len(vs)
        # props: models (each LOD's surfaces in model space), then instances
        f.write(struct.pack("<I", len(models)))
        for name, lods in models:
            lod_surfs = []
            for dist, lod, *_ in lods:
                surfs = []
                for material in sorted(lod):
                    m = materials.get(material)
                    if m is not None:
                        surfs.extend((m,) + s for s in surfaces(lod[material]))
                lod_surfs.append((dist, surfs))
            pts = [v[0] for _, surfs in lod_surfs for s in surfs for v in s[1]]
            radius = max((math.sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]) for p in pts), default=0.0)
            f.write(name.encode("latin-1")[:63].ljust(64, b"\0"))
            f.write(struct.pack("<fI", radius, len(lod_surfs)))
            for dist, surfs in lod_surfs:
                f.write(struct.pack("<fI", dist, len(surfs)))
                for m, vs, ns, fs in surfs:
                    f.write(struct.pack("<HHII", m, 0, len(vs), len(fs)))
                    buf = bytearray()
                    for key, n in zip(vs, ns):
                        p, uv = key[0], key[1]
                        length = math.sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) or 1.0
                        buf += struct.pack("<8fI", *p, n[0] / length, n[1] / length, n[2] / length, uv[0], uv[1],
                                           key[2] if len(key) > 2 else WHITE)
                    for tri in fs:
                        buf += struct.pack("<3H", *tri)
                    f.write(buf)
                    model_tris_total[0] += len(fs)
        f.write(struct.pack("<I", len(instances)))
        for model, origin, axes, scale in instances:
            f.write(struct.pack("<I3f9ff", model, *origin, *axes, scale))
        f.write(struct.pack("<i", sky))
        f.write(struct.pack("<I", len(blocker_groups)))
        for group, members in blocker_groups.items():
            f.write(group.encode("latin-1")[:63].ljust(64, b"\0"))
            f.write(struct.pack(f"<I{len(members)}I", len(members), *members))
    print(f"{out_bin}: {len(records)} materials, {len(chunks)} chunks, {surf_count} surfaces, {verts} vertices, {tris} triangles; "
          f"{sum(1 for v in converted.values() if v)} images; {len(instances)} props of {len(models)} models "
          f"({model_tris_total[0]} triangles over all their LODs), "
          f"materials: {sum(r[2] == FLAG_ALPHA_TEST for r in records)} alpha tested, "
          f"{sum(r[2] == FLAG_DECAL for r in records)} decals ({sum(i.endswith('_mul') for _, i, _ in records)} "
          f"multiplying), {sum(r[2] == FLAG_BLEND for r in records)} blended")
    if missing:
        print("props with nothing to draw (no OBJ, or only left-out materials): " + ", ".join(sorted(missing)))
    if left_out:
        count = sum(len(groups[m]) for m in left_out)
        print(f"{len(left_out)} materials not drawn ({count} triangles): " + ", ".join(left_out))
    if failed:
        print("colour maps not converted (not DXT1/3/5): " + ", ".join(sorted(set(failed))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
