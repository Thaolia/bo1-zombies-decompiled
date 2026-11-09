#!/usr/bin/env python3
"""The drawable world of a BO2 map for MapKit's "mesh" edit: its OBJ (tools/bo2/oat) cut into chunks, and its colour
maps as IW images.

    tools/bo2/bo2_mesh.py <dump folder of the zone> <map name> --out <run>/mods/mapkit/imports/<map>

Writes <out>/<map>.mesh.bin (format below) and <out>/images/<name>.iwi (one per colour map). The engine
(src/gfx_d3d/r_mapkit_mesh.cpp) turns each chunk into an XModel: one surface per material, so a chunk holds at most
MAX_SURFS surfaces of at most MAX_VERTS vertices (16-bit indices). Chunks are CELL units across in the playable area
and FAR_CELL beyond it (the distant backdrop), so the view culls them and the light sample at each chunk is local.

<map>.mesh.bin, little-endian:
  char[8] "BO2MESH1"
  u32 materialCount, then per material: char[64] BO2 material name, char[64] colour map image name (images/<it>.iwi)
  u32 chunkCount, then per chunk: f32[3] origin, f32 radius, u32 surfaceCount, then per surface:
      u16 material, u16 pad, u32 vertCount, u32 triCount,
      vertCount x (f32[3] position relative to origin, f32[3] normal, f32[2] uv), triCount x u16[3]
The output is the player's own game data: keep it out of the repository. docs/BO2_MAPS.md has the procedure.
"""
import argparse
import hashlib
import math
import os
import re
import struct
import sys

CELL = 512.0
FAR_CELL = 8192.0
PLAY_HALF = 4096.0  # the playable area is about 5 000 units across, around the origin
MAX_VERTS = 65535   # XSurface::vertCount and triCount are 16-bit
MAX_TRIS = 65535
MAX_SURFS = 120     # the renderer's rigid path keeps at most 128 surfaces per model on its stack (R_SkinXModel)

# Materials not drawn: shadow casters and caulk (never visible), and standalone decals, which lie on another surface
# and need blending this first version does not do (a layered "*N(wpc/a:wpc/b)" material keeps its base layer)
SKIP = re.compile(r"caulk|shadow|nodraw|(^|/)decal_|_decal|_dec(_|\d|$)")

DDS_FORMATS = {b"DXT1": (0x0B, 8), b"DXT3": (0x0C, 16), b"DXT5": (0x0D, 16)}


def read_mtl(path):
    """BO2 material name -> its colour map's DDS path (OAT's .mtl: newmtl, map_Kd)."""
    maps, name = {}, None
    base = os.path.dirname(path)
    with open(path, encoding="latin-1") as f:
        for line in f:
            line = line.rstrip("\n")
            if line.startswith("newmtl "):
                name = line[7:]
            elif line.startswith("map_Kd ") and name is not None:
                maps[name] = os.path.normpath(os.path.join(base, line[7:]))
    return maps


def image_name(dds_path):
    """A file name the engine accepts (OAT's names hold ~ $ & characters): readable part + hash."""
    stem = os.path.splitext(os.path.basename(dds_path))[0]
    clean = re.sub(r"[^a-z0-9_]", "", stem.lower())[:40]
    return f"bo2_{clean}_{hashlib.sha1(stem.encode('latin-1')).hexdigest()[:8]}"


def dds_to_iwi(src, dst):
    """A DXT DDS (largest mip first) as an IW image: GfxImageFileHeader, then the mips smallest first, which is the
    order Image_LoadDxtc uploads them in. Returns False for a format the engine cannot take."""
    with open(src, "rb") as f:
        data = f.read()
    if data[:4] != b"DDS " or len(data) < 128:
        return False
    height, width = struct.unpack_from("<II", data, 12)
    mips = max(1, struct.unpack_from("<I", data, 28)[0])
    fourcc = data[84:88]
    if fourcc not in DDS_FORMATS:
        return False
    fmt, block = DDS_FORMATS[fourcc]
    full = int(math.log2(max(width, height))) + 1
    levels, at = [], 128
    for level in range(mips):
        w, h = max(1, width >> level), max(1, height >> level)
        size = block * ((w + 3) // 4) * ((h + 3) // 4)
        levels.append(data[at:at + size])
        at += size
    if at > len(data):
        return False
    flags = 0 if mips == full else 2  # 2: no mipmaps (Image_CountMipmaps), only the top level is uploaded
    if flags:
        levels = levels[:1]
    body = b"".join(reversed(levels))
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


def skipped(material):
    return not material.startswith("*") and SKIP.search(material) is not None


def cell_of(p):
    if abs(p[0]) < PLAY_HALF and abs(p[1]) < PLAY_HALF:
        return (0, math.floor(p[0] / CELL), math.floor(p[1] / CELL))
    return (1, math.floor(p[0] / FAR_CELL), math.floor(p[1] / FAR_CELL))


def build(positions, uvs, groups, materials):
    """chunk key -> material index -> [vertex keys...] and triangles; vertices shared within one surface."""
    cells = {}
    for material, faces in groups:
        m = materials.get(material)
        if m is None:
            continue
        for face in faces:
            pts = [positions[c[0]] for c in face]
            centre = tuple(sum(p[k] for p in pts) / 3 for k in range(3))
            surf = cells.setdefault(cell_of(centre), {}).setdefault(m, [])
            surf.append([(positions[c[0]], uvs[c[1]] if c[1] >= 0 else (0.0, 0.0)) for c in face])
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
    colour = read_mtl(base + ".mtl")
    positions, uvs, groups = read_obj(base + ".obj")

    os.makedirs(os.path.join(args.out, "images"), exist_ok=True)
    names = sorted({g[0] for g in groups if not skipped(g[0])})
    left_out = sorted({g[0] for g in groups if skipped(g[0])})
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
        records.append((name, image))

    cells = build(positions, uvs, groups, materials)
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
    with open(out_bin, "wb") as f:
        f.write(b"BO2MESH1")
        f.write(struct.pack("<I", len(records)))
        for name, image in records:
            f.write(name.encode("latin-1")[:63].ljust(64, b"\0"))
            f.write(image.encode("latin-1")[:63].ljust(64, b"\0"))
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
    print(f"{out_bin}: {len(records)} materials, {len(chunks)} chunks, {surf_count} surfaces, {verts} vertices, {tris} triangles; "
          f"{sum(1 for v in converted.values() if v)} images")
    if left_out:
        count = sum(len(g[1]) for g in groups if skipped(g[0]))
        print(f"{len(left_out)} materials not drawn ({count} triangles): " + ", ".join(left_out))
    if failed:
        print("colour maps not converted (not DXT1/3/5): " + ", ".join(sorted(set(failed))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
