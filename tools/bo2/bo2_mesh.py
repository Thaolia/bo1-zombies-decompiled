#!/usr/bin/env python3
"""The drawable world of a BO2 map for MapKit's "mesh" edit: its world OBJ and its placed props (tools/bo2/oat) cut into
chunks, and their colour maps as IW images.

    tools/bo2/bo2_mesh.py <dump folder of the zone> <map name> --out <run>/mods/mapkit/imports/<map>

The props are the static models of <map>.d3dbsp.smodels.csv: one model per BO2 xmodel, with its LODs (model_export/
OBJs, distances from xmodel/<name>.json), drawn once per placement. Writes <out>/<map>.mesh.bin (format below) and <out>/images/<name>.iwi (one
per colour map). The engine
(src/gfx_d3d/r_mapkit_mesh.cpp) turns each chunk into an XModel: one surface per material, so a chunk holds at most
MAX_SURFS surfaces of at most MAX_VERTS vertices (16-bit indices). Chunks are CELL units across in the playable area
and FAR_CELL beyond it (the distant backdrop), so the view culls them and the light sample at each chunk is local.

<map>.mesh.bin, little-endian:
  char[8] "BO2MESH4"
  u32 materialCount, then per material: char[64] BO2 material name, char[64] colour map image name (images/<it>.iwi),
      u32 flags (1: alpha tested in BO2: drawn with the engine's alpha-tested base material; 2: the sky's, a cube map
      drawn with the engine's skybox base material)
  u32 chunkCount, then per chunk: f32[3] origin, f32 radius, u32 surfaceCount, then per surface:
      u16 material, u16 pad, u32 vertCount, u32 triCount,
      vertCount x (f32[3] position relative to origin, f32[3] normal, f32[2] uv), triCount x u16[3]
  u32 modelCount, then per model: char[64] name, f32 radius, u32 lodCount, then per LOD: f32 distance (0: any),
      u32 surfaceCount, then its surfaces as a chunk's (positions in model space)
  u32 instanceCount, then per instance: u32 model, f32[3] origin, f32[3][3] axes (model x, y, z in the world), f32 scale
  i32 sky model (the worldspawn's skyboxmodel, one of the models above, drawn around the viewer; -1: none)
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

CELL = 512.0
FAR_CELL = 8192.0
PLAY_HALF = 4096.0  # the playable area is about 5 000 units across, around the origin
MAX_VERTS = 65535   # XSurface::vertCount and triCount are 16-bit
MAX_TRIS = 65535
MAX_SURFS = 120     # the renderer's rigid path keeps at most 128 surfaces per model on its stack (R_SkinXModel)

# Materials not drawn: shadow casters and caulk (never visible), and standalone decals, which lie on another surface
# and need blending this first version does not do (a layered "*N(wpc/a:wpc/b)" material keeps its base layer)
SKIP = re.compile(r"caulk|(?<!no_)shadow|nodraw|(^|/)decal_|_decal|_dec(_|\d|$)")

DDS_FORMATS = {b"DXT1": (0x0B, 8), b"DXT3": (0x0C, 16), b"DXT5": (0x0D, 16)}


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


def dds_to_iwi(src, dst):
    """A DXT DDS (largest mip first) as an IW image: GfxImageFileHeader, then the mips smallest first, which is the
    order Image_LoadDxtc uploads them in. Returns False for a format the engine cannot take."""
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


def model_tris(path):
    """An OAT model OBJ (XModel/Obj/ObjWriter.cpp: game (x, y, z) written as (x, z, -y), v as 1 - v) as
    material -> triangles of (position, uv) in model space, wound like the world's (checked against the OBJ's normals:
    a model whose faces mostly turn away from its normals is reversed)."""
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


def read_lods(dump, name):
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
    for dist, rel in listed:
        path = os.path.join(dump, rel)
        if os.path.exists(path):
            lods.append((dist, model_tris(path)))
    return lods


def alpha_tested(dump, material):
    """Whether OAT's material JSON says the BO2 material tests or blends alpha (fences, foliage, glass)."""
    name = material
    if name.startswith("*"):
        inner = re.search(r"\(([^:)]+)", name)
        name = inner.group(1) if inner else name
    path = os.path.join(dump, "materials", name + ".json")
    try:
        with open(path, encoding="latin-1") as f:
            data = json.load(f)
    except (OSError, ValueError):
        return False
    for bits in data.get("stateBits", []):
        if bits.get("alphaTest", "disabled") != "disabled":
            return True
        if bits.get("colorWriteRgb") and bits.get("srcBlendRgb") == "srcalpha":
            return True
    return False


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
    args = parser.parse_args(argv)
    base = os.path.join(args.dump, "maps", "mp", f"{args.map}.d3dbsp")
    images = os.path.join(args.dump, "images")
    colour = read_mtl(base + ".mtl", images)
    positions, uvs, world = read_obj(base + ".obj")
    groups = {}
    for material, faces in world:
        groups.setdefault(material, []).extend(
            [[(positions[c[0]], uvs[c[1]] if c[1] >= 0 else (0.0, 0.0)) for c in face] for face in faces])

    # props: one model per BO2 xmodel, with its LODs (distances from OAT's xmodel JSON), and one instance per placement
    exports = os.path.join(args.dump, "model_export")
    models, model_index, instances, missing = [], {}, [], set()
    with open(base + ".smodels.csv", encoding="latin-1") as f:
        for row in csv.DictReader(f):
            name = row["model"]
            if name not in model_index:
                # a LOD whose materials are all left out (decals) has nothing to draw: the engine wants surfaces
                lods = [(dist, {m: t for m, t in lod.items() if not skipped(m)})
                        for dist, lod in read_lods(args.dump, name)]
                lods = [(dist, lod) for dist, lod in lods if lod]
                if not lods:
                    missing.add(name)
                    model_index[name] = -1
                    continue
                for material, dds in read_mtl(os.path.join(exports, name + ".mtl"), images).items():
                    colour.setdefault(material, dds)
                for _, tris in lods:
                    for material in tris:
                        groups.setdefault(material, [])  # listed with the world's materials
                model_index[name] = len(models)
                models.append((name, lods))
            if model_index[name] < 0:
                continue
            axes = [float(row[f"a{a}{i}"]) for a in "xyz" for i in range(3)]
            instances.append((model_index[name], [float(row[k]) for k in ("x", "y", "z")], axes,
                              float(row["scale"])))

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

    os.makedirs(os.path.join(args.out, "images"), exist_ok=True)
    names = sorted(m for m in groups if not skipped(m))
    left_out = sorted(m for m in groups if skipped(m))
    materials, records, converted, failed = {}, [], {}, []
    for name in names:
        dds = colour.get(name)
        image = ""
        if dds:
            image = image_name(dds)
            if dds not in converted:
                converted[dds] = dds_to_iwi(dds, os.path.join(args.out, "images", image + ".iwi"))
                if not converted[dds]:
                    failed.append(os.path.basename(dds))
            if not converted[dds]:
                image = ""
        materials[name] = len(records)
        records.append((name, image, 2 if name in sky_materials else 1 if alpha_tested(args.dump, name) else 0))

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
        f.write(b"BO2MESH4")
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
                for (p, uv), n in zip(vs, ns):
                    length = math.sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) or 1.0
                    buf += struct.pack("<8f", p[0] - origin[0], p[1] - origin[1], p[2] - origin[2],
                                       n[0] / length, n[1] / length, n[2] / length, uv[0], uv[1])
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
            for dist, lod in lods:
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
                    for (p, uv), n in zip(vs, ns):
                        length = math.sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) or 1.0
                        buf += struct.pack("<8f", *p, n[0] / length, n[1] / length, n[2] / length, uv[0], uv[1])
                    for tri in fs:
                        buf += struct.pack("<3H", *tri)
                    f.write(buf)
                    model_tris_total[0] += len(fs)
        f.write(struct.pack("<I", len(instances)))
        for model, origin, axes, scale in instances:
            f.write(struct.pack("<I3f9ff", model, *origin, *axes, scale))
        f.write(struct.pack("<i", sky))
    print(f"{out_bin}: {len(records)} materials, {len(chunks)} chunks, {surf_count} surfaces, {verts} vertices, {tris} triangles; "
          f"{sum(1 for v in converted.values() if v)} images; {len(instances)} props of {len(models)} models "
          f"({model_tris_total[0]} triangles over all their LODs), "
          f"{sum(r[2] for r in records)} alpha-tested materials")
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
