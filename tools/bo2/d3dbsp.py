#!/usr/bin/env python3
"""Write a BO1 world BSP (IBSP version 43, the Mod Tools' own compiled maps) without Radiant or cod2map.

The engine reads such a file for a map's world when bo1_bspworld is set (docs/BO2_MAPS.md, "native maps"). This
writer covers what a map without baked lighting needs:
- axial brushes for collision, each drawing the faces asked for;
- triangle meshes drawn as world surfaces (no collision, use brushes for that);
- one cell holding every surface, a one-node tree, no portals: everything is drawn from everywhere;
- one lightmap without pixels, which the engine fills with white (lit world materials refuse a surface without a
  lightmap; the sky takes none, index 31), an empty light grid, no reflection probe (the engine adds its dummy), a sun
  primary light from the worldspawn keys;
- the entity string.

Library use: build a Bsp, add_material / add_brush / add_mesh / add_entity, then write(path).
Command line: `d3dbsp.py room OUT.d3dbsp` writes the test room (a floor, four walls and a sky box).
"""
import math
import struct
import sys

BSP_VERSION = 43
LIGHTMAP_NONE = 31

LUMP_MATERIALS = 0x00
LUMP_LIGHTGRIDENTRIES = 0x02
LUMP_LIGHTGRIDCOLORS = 0x03
LUMP_PLANES = 0x04
LUMP_BRUSHSIDES = 0x05
LUMP_BRUSHES = 0x08
LUMP_TRIANGLES = 0x09
LUMP_DRAWVERTS = 0x0A
LUMP_DRAWINDICES = 0x0B
LUMP_BRUSHVERTSCOUNTS = 0x16
LUMP_BRUSHVERTS = 0x17
LUMP_AABBTREES = 0x18
LUMP_CELLS = 0x19
LUMP_NODES = 0x1B
LUMP_LEAFS = 0x1C
LUMP_LEAFBRUSHES = 0x1D
LUMP_COLLISIONVERTS = 0x1F
LUMP_COLLISIONTRIS = 0x20
LUMP_COLLISIONEDGEWALKABLE = 0x21
LUMP_COLLISIONBORDERS = 0x22
LUMP_COLLISIONPARTITIONS = 0x23
LUMP_COLLISIONAABBS = 0x24
LUMP_UINDS = 0x15
LUMP_MODELS = 0x25
LUMP_ENTITIES = 0x27
LUMP_PRIMARY_LIGHTS = 0x2B
LUMP_LIGHTGRIDHEADER = 0x2C
LUMP_LIGHTGRIDROWS = 0x2D
LUMP_WATERHEADER = 0x34
LUMP_BURNABLEHEADER = 0x37
LUMP_LIGHTREGIONS = 0x3E

# Material flags as the Mod Tools' compiler writes them (asset_viewer.d3dbsp).
SURF_DEFAULT = 0x600000
CONTENTS_SOLID = 0x1
SURF_SKY = 0x34
CONTENTS_SKY = 0x800
CONTENTS_NONE = 0x0

MAX_SOUP_VERTS = 0xFFFF
MAX_SOUP_INDICES = 0xFFFF


def angle_vectors(pitch, yaw):
    p, y = math.radians(pitch), math.radians(yaw)
    return (math.cos(p) * math.cos(y), math.cos(p) * math.sin(y), -math.sin(p))


class Bsp:
    def __init__(self):
        self.materials = []          # (name, surfaceFlags, contentFlags)
        # (mins, maxs, contents material, axial side materials [-x, +x, -y, +y, -z, +z], [(normal, dist, material)])
        self.brushes = []
        self.soups = []              # [material, verts, indices]: verts are packed DiskGfxVertex
        self.coll_verts = []         # collision triangles' vertices
        self.coll_tris = []          # (a, b, c, material)
        self.entities = []           # list of dicts, worldspawn first
        self.sun_color = (0.875, 0.875, 0.875)

    def add_material(self, name, surface_flags=SURF_DEFAULT, content_flags=CONTENTS_SOLID):
        for i, m in enumerate(self.materials):
            if m[0] == name:
                return i
        if len(name.encode()) >= 64:
            raise ValueError("material name too long: " + name)
        self.materials.append((name, surface_flags, content_flags))
        return len(self.materials) - 1

    def add_entity(self, **keys):
        self.entities.append({k: str(v) for k, v in keys.items()})

    # faces: "outer" draws the six faces facing out, "inner" the six facing in (a sky box), "none" nothing.
    # A brush that only blocks takes a nodraw material and faces "none".
    def add_brush(self, mins, maxs, material, faces="outer", tex_scale=128.0):
        if any(a >= b for a, b in zip(mins, maxs)):
            raise ValueError("empty brush %r %r" % (mins, maxs))
        self.add_collision_brush(mins, maxs, material, [material] * 6, [])
        if faces == "none":
            return
        inward = faces == "inner"
        for axis in range(3):
            for side in (0, 1):
                normal = [0.0, 0.0, 0.0]
                normal[axis] = 1.0 if side else -1.0
                if inward:
                    normal[axis] = -normal[axis]
                self._add_box_face(mins, maxs, axis, side, normal, material, tex_scale)

    # A brush of any shape: its bounding box (the six axial sides, each with its material) and its other sides, each a
    # plane facing out of the brush (normal, dist: points p inside have dot(normal, p) <= dist).
    def add_collision_brush(self, mins, maxs, material, axial_materials, sides):
        if any(a >= b for a, b in zip(mins, maxs)):
            raise ValueError("empty brush %r %r" % (mins, maxs))
        if len(axial_materials) != 6:
            raise ValueError("six axial side materials")
        self.brushes.append((tuple(map(float, mins)), tuple(map(float, maxs)), material, list(axial_materials),
                             [(tuple(map(float, n)), float(d), m) for n, d, m in sides]))

    # Collision triangles (terrain): one-sided, their front (c - a) x (b - a) facing what they stop; the engine traces
    # capsules against their faces, edges and corners. Vertex indices are 16-bit.
    def add_collision_triangles(self, verts, tris):
        base = len(self.coll_verts)
        if base + len(verts) > 0x10000:
            raise ValueError("more than 65536 collision vertices")
        self.coll_verts.extend(tuple(map(float, v)) for v in verts)
        self.coll_tris.extend((a + base, b + base, c + base, m) for a, b, c, m in tris)

    def _add_box_face(self, mins, maxs, axis, side, normal, material, tex_scale):
        u_axis, v_axis = [(1, 2), (0, 2), (0, 1)][axis]
        d = maxs[axis] if side else mins[axis]
        corners = []
        for cu, cv in ((0, 0), (1, 0), (1, 1), (0, 1)):
            p = [0.0, 0.0, 0.0]
            p[axis] = d
            p[u_axis] = maxs[u_axis] if cu else mins[u_axis]
            p[v_axis] = maxs[v_axis] if cv else mins[v_axis]
            corners.append(tuple(p))
        tri = [(0, 1, 2), (2, 3, 0)]
        if _dot(_cross(_sub(corners[2], corners[0]), _sub(corners[1], corners[0])), normal) < 0:
            tri = [(0, 2, 1), (2, 0, 3)]
        self.add_mesh(material, corners, [i for t in tri for i in t], normals=[normal] * 4, tex_scale=tex_scale)

    # Triangles wind clockwise seen from the front, as D3D and the Mod Tools' compiler do.
    def add_mesh(self, material, positions, indices, normals=None, uvs=None, colors=None, tex_scale=128.0):
        if len(indices) % 3:
            raise ValueError("index count not a multiple of 3")
        if normals is None:
            normals = _face_normals(positions, indices)
        verts = []
        for i, p in enumerate(positions):
            n = _normalize(normals[i])
            tangent, binormal = _basis(n)
            if uvs is not None:
                uv = uvs[i]
            else:
                uv = (_dot(p, tangent) / tex_scale, -_dot(p, binormal) / tex_scale)
            c = colors[i] if colors is not None else (255, 255, 255, 255)
            verts.append(struct.pack("<3f3f4B2f2f3f3f", *p, *n, *c, *uv, 0.0, 0.0, *tangent, *binormal))
        # Split into soups that fit the 16-bit counts, triangles kept whole.
        start = 0
        while start < len(indices):
            remap, soup_verts, soup_indices = {}, [], []
            pos = start
            while pos < len(indices):
                tri = indices[pos:pos + 3]
                new = sum(1 for i in set(tri) if i not in remap)
                if len(soup_verts) + new > MAX_SOUP_VERTS or len(soup_indices) + 3 > MAX_SOUP_INDICES:
                    break
                for i in tri:
                    if i not in remap:
                        remap[i] = len(soup_verts)
                        soup_verts.append(verts[i])
                    soup_indices.append(remap[i])
                pos += 3
            self.soups.append([material, soup_verts, soup_indices])
            start = pos

    def bounds(self):
        mins, maxs = [1e30] * 3, [-1e30] * 3
        for bmin, bmax, *_ in self.brushes:
            mins = [min(a, b) for a, b in zip(mins, bmin)]
            maxs = [max(a, b) for a, b in zip(maxs, bmax)]
        for _, verts, _ in self.soups:
            for v in verts:
                p = struct.unpack_from("<3f", v)
                mins = [min(a, b) for a, b in zip(mins, p)]
                maxs = [max(a, b) for a, b in zip(maxs, p)]
        if mins[0] > maxs[0]:
            raise ValueError("empty world")
        return mins, maxs

    def write(self, path):
        if not self.materials:
            raise ValueError("a map needs a material")
        if not self.brushes:
            raise ValueError("a map needs a brush (the collision tree splits on one)")
        if not self.entities or self.entities[0].get("classname") != "worldspawn":
            raise ValueError("the first entity must be worldspawn")
        mins, maxs = self.bounds()
        lumps = {}

        lumps[LUMP_MATERIALS] = b"".join(
            struct.pack("<64sii", n.encode(), s, c) for n, s, c in self.materials)

        # Planes: the collision tree's splits, then the brushes' non-axial sides (shared when equal).
        planes, plane_index = [], {}

        def plane(normal, dist):
            key = (round(normal[0], 6), round(normal[1], 6), round(normal[2], 6), round(dist, 3))
            if key not in plane_index:
                plane_index[key] = len(planes)
                planes.append((normal[0], normal[1], normal[2], dist))
            return plane_index[key]

        partitions, ptris = _partition_triangles(self.coll_verts, self.coll_tris)
        boxes = [(b[0], b[1]) for b in self.brushes] + [(p[0], p[1]) for p in partitions]
        nodes, leaf_items = _build_tree(boxes, mins, maxs, plane)
        nbrush = len(self.brushes)
        # A leaf's terrain: one AABB tree root per collision material (a leaf's first index is 16-bit, so the roots
        # come first), each with the leaf's partitions of that material as children (a hit takes its root's
        # material's flags).
        leafs, leafbrushes, roots, children = [], [], [], []
        for items in leaf_items:
            brush_ids = [i for i in items if i < nbrush]
            by_material = {}
            for i in items:
                if i >= nbrush:
                    by_material.setdefault(partitions[i - nbrush][2], []).append(i - nbrush)
            leafs.append((len(roots), len(by_material), len(leafbrushes), len(brush_ids)))
            leafbrushes.extend(brush_ids)
            for material, part_ids in sorted(by_material.items()):
                roots.append((material, part_ids))
        if len(roots) > 0xFFFF:
            raise ValueError("more than 65535 collision AABB roots")

        def box(lo, hi, material, count, index):
            origin = [(lo[k] + hi[k]) / 2.0 for k in range(3)]
            half = [(hi[k] - lo[k]) / 2.0 + 1.0 for k in range(3)]
            return struct.pack("<3f3fHHI", *origin, *half, material, count, index)

        aabbs = []
        first_child = len(roots)
        for material, part_ids in roots:
            lo = [min(partitions[p][0][k] for p in part_ids) for k in range(3)]
            hi = [max(partitions[p][1][k] for p in part_ids) for k in range(3)]
            aabbs.append(box(lo, hi, material, len(part_ids), first_child + len(children)))
            children.extend(part_ids)
        for pi in children:
            aabbs.append(box(partitions[pi][0], partitions[pi][1], partitions[pi][2], 0, pi))
        tri_bytes, uinds, part_bytes = [], [], []
        for pi, (lo, hi, material, tri_list) in enumerate(partitions):
            first = len(tri_bytes)
            used = sorted({v for t in tri_list for v in ptris[t][:3]})
            for t in tri_list:
                tri_bytes.append(struct.pack("<3H", *ptris[t][:3]))
            part_bytes.append(struct.pack("<BBBBiiii", 0, 0, len(tri_list), 0, first, 0, len(used), len(uinds)))
            uinds.extend(used)
        if self.coll_tris:
            lumps[LUMP_COLLISIONVERTS] = b"".join(struct.pack("<3f", *v) for v in self.coll_verts)
            lumps[LUMP_COLLISIONTRIS] = b"".join(tri_bytes)
            # Every edge walkable: a capsule resting on an edge stands on it.
            lumps[LUMP_COLLISIONEDGEWALKABLE] = bytes([0xFF] * ((3 * len(tri_bytes) + 7) // 8))
            lumps[LUMP_COLLISIONBORDERS] = b""
            lumps[LUMP_COLLISIONPARTITIONS] = b"".join(part_bytes)
            lumps[LUMP_UINDS] = struct.pack("<%dH" % len(uinds), *uinds)
            lumps[LUMP_COLLISIONAABBS] = b"".join(aabbs)
        sides, brushes, bverts, bvcounts = [], [], [], []
        for bmin, bmax, mat, axial, extra in self.brushes:
            for axis in range(3):
                sides.append(struct.pack("<fi", bmin[axis], axial[2 * axis]))
                sides.append(struct.pack("<fi", bmax[axis], axial[2 * axis + 1]))
            for n, d, m in extra:
                sides.append(struct.pack("<ii", plane(n, d), m))
            if 6 + len(extra) > 0x7FFF:
                raise ValueError("brush with too many sides")
            brushes.append(struct.pack("<hh", 6 + len(extra), mat))
            corners = _brush_corners(bmin, bmax, extra)
            if len(corners) > 255:
                corners = corners[:255]
            bvcounts.append(len(corners))
            bverts.extend(struct.pack("<3f", *c) for c in corners)
        if len(self.brushes) > 0xFFFF:
            raise ValueError("more than 65535 brushes")
        lumps[LUMP_PLANES] = b"".join(struct.pack("<4f", *p) for p in planes)
        lumps[LUMP_BRUSHSIDES] = b"".join(sides)
        lumps[LUMP_BRUSHES] = b"".join(brushes)
        lumps[LUMP_BRUSHVERTSCOUNTS] = bytes(bvcounts)
        lumps[LUMP_BRUSHVERTS] = b"".join(bverts)
        lumps[LUMP_NODES] = b"".join(struct.pack("<3i3i3i", *n) for n in nodes)
        lumps[LUMP_LEAFS] = b"".join(struct.pack("<6i", 0, first_aabb, aabb_count, first, count, 0)
                                     for first_aabb, aabb_count, first, count in leafs)
        lumps[LUMP_LEAFBRUSHES] = struct.pack("<%di" % len(leafbrushes), *leafbrushes)
        self.tree_stats = (len(nodes), len(leafs), len(leafbrushes), len(partitions), len(aabbs))

        # Surfaces sorted by material, as the compiler leaves them.
        soups = sorted(self.soups, key=lambda s: s[0])
        tris, verts, indices = [], [], []
        for mat, sv, si in soups:
            sky = self.materials[mat][1] & 0x4
            tris.append(struct.pack("<HBBBBBxhxxiIHHi", mat, LIGHTMAP_NONE if sky else 0, 0, 0 if sky else 1, 0, 0 if sky else 1,
                                    0 if sky else 1, 0, len(verts), len(sv), len(si), len(indices)))
            verts.extend(sv)
            indices.extend(si)
        if len(indices) and max(indices) > 0xFFFF:
            raise ValueError("index out of range")
        lumps[LUMP_TRIANGLES] = b"".join(tris)
        lumps[LUMP_DRAWVERTS] = b"".join(verts)
        lumps[LUMP_DRAWINDICES] = struct.pack("<%dH" % len(indices), *indices)

        lumps[LUMP_AABBTREES] = struct.pack("<3I", 0, len(soups), 0)
        cell = struct.pack("<3f3fHxxiiiiB", *mins, *maxs, 0, 0, 0, 0, 0, 0)
        lumps[LUMP_CELLS] = cell + bytes(0x70 - len(cell))
        lumps[LUMP_MODELS] = struct.pack("<3f3fHHiiii", *mins, *maxs, 0, len(soups), 0, 0, 0, len(self.brushes))

        lumps[LUMP_ENTITIES] = _entity_string(self.entities)

        # Primary light 0 is "none", 1 the sun (type 1), its direction from the worldspawn's sundirection.
        ws = self.entities[0]
        sd = [float(x) for x in ws.get("sundirection", "-60 45 0").split()]
        sun_dir = angle_vectors(sd[0], sd[1])
        none_light = bytes(0x10C)
        sun = struct.pack("<BBhb3x3f3f3f", 1, 0, 0, 0, *sun_dir, 0.0, 0.0, 0.0, *self.sun_color)
        lumps[LUMP_PRIMARY_LIGHTS] = none_light + sun + bytes(0x10C - len(sun))
        lumps[LUMP_LIGHTREGIONS] = bytes(2)

        # An empty light grid: one row, no entries. The engine then lights models with its default grid colour.
        lumps[LUMP_LIGHTGRIDHEADER] = struct.pack("<6HIIH", 0, 0, 0, 0, 0, 0, 0, 1, 0)
        lumps[LUMP_LIGHTGRIDROWS] = b""
        lumps[LUMP_LIGHTGRIDENTRIES] = b""
        lumps[LUMP_LIGHTGRIDCOLORS] = b""

        # No water and no burnable area: the compiler's empty headers.
        lumps[LUMP_WATERHEADER] = struct.pack("<iiiiHH", 0x7FFFFFFF, 0x7FFFFFFF, -0x80000000, -0x80000000, 0xFFFF, 0)
        lumps[LUMP_BURNABLEHEADER] = bytes(16)

        order = [LUMP_MATERIALS, LUMP_LIGHTGRIDHEADER, LUMP_LIGHTGRIDROWS, LUMP_LIGHTGRIDENTRIES,
                 LUMP_LIGHTGRIDCOLORS, LUMP_PLANES, LUMP_BRUSHSIDES, LUMP_BRUSHVERTS, LUMP_BRUSHVERTSCOUNTS,
                 LUMP_BRUSHES, LUMP_TRIANGLES, LUMP_DRAWVERTS, LUMP_DRAWINDICES, LUMP_AABBTREES, LUMP_CELLS,
                 LUMP_NODES, LUMP_LEAFS, LUMP_LEAFBRUSHES, LUMP_COLLISIONVERTS, LUMP_COLLISIONTRIS,
                 LUMP_COLLISIONEDGEWALKABLE, LUMP_COLLISIONBORDERS, LUMP_COLLISIONPARTITIONS, LUMP_UINDS,
                 LUMP_COLLISIONAABBS, LUMP_MODELS, LUMP_ENTITIES, LUMP_PRIMARY_LIGHTS,
                 LUMP_LIGHTREGIONS, LUMP_WATERHEADER, LUMP_BURNABLEHEADER]
        order = [lid for lid in order if lid in lumps]
        out = [struct.pack("<4sII", b"IBSP", BSP_VERSION, len(order))]
        out += [struct.pack("<II", lid, len(lumps[lid])) for lid in order]
        for lid in order:
            data = lumps[lid]
            out.append(data + bytes(-len(data) & 3))
        with open(path, "wb") as f:
            f.write(b"".join(out))


LEAF_BRUSHES = 24
TREE_DEPTH = 28
SPLIT_KEEP = 0.8


def _build_tree(boxes, mins, maxs, plane):
    """A k-d tree over boxes (brushes and collision partitions) for the collision: axial splits at the median of the
    box centres, until a leaf holds LEAF_BRUSHES boxes or a split stops helping (one side would keep more than
    SPLIT_KEEP of them). A leaf lists every box that reaches into it, so a trace that walks the tree meets everything it
    can touch. Returns (nodes, leaf item lists): nodes as (plane, front child, back child, mins, maxs) with a leaf child
    written -(leaf + 1)."""
    nodes, leafs = [], []

    def leaf(ids):
        leafs.append(ids)
        return -len(leafs)

    def build(ids, lo, hi, depth):
        if len(ids) <= LEAF_BRUSHES or depth >= TREE_DEPTH:
            return leaf(ids)
        axis = max(range(3), key=lambda k: hi[k] - lo[k])
        centres = sorted((boxes[i][0][axis] + boxes[i][1][axis]) / 2.0 for i in ids)
        split = math.floor(centres[len(centres) // 2])
        if not lo[axis] < split < hi[axis]:
            split = math.floor((lo[axis] + hi[axis]) / 2.0)
        front = [i for i in ids if boxes[i][1][axis] > split]
        back = [i for i in ids if boxes[i][0][axis] < split]
        # A split that leaves either side most of the boxes (large ones crossing it) only multiplies references.
        if max(len(front), len(back)) > SPLIT_KEEP * len(ids):
            return leaf(ids)
        index = len(nodes)
        nodes.append(None)
        normal = [0.0, 0.0, 0.0]
        normal[axis] = 1.0
        front_lo, back_hi = list(lo), list(hi)
        front_lo[axis] = split
        back_hi[axis] = split
        f = build(front, front_lo, hi, depth + 1)
        b = build(back, lo, back_hi, depth + 1)
        nodes[index] = (plane(tuple(normal), float(split)), f, b,
                        *[int(math.floor(v)) for v in lo], *[int(math.ceil(v)) for v in hi])
        return index

    lo = [math.floor(v) - 16.0 for v in mins]
    hi = [math.ceil(v) + 16.0 for v in maxs]
    root = build(list(range(len(boxes))), lo, hi, 0)
    if root < 0:
        # The engine wants a node: a split above everything, the world in its back leaf.
        top = hi[2] + 1.0
        empty = leaf([])
        nodes.append((plane((0.0, 0.0, 1.0), top), empty, root, *map(int, lo), *map(int, hi)))
    return nodes, leafs


# The physics that moves players and actors (gjk_partition_t, phys_colgeom.cpp) collides with a partition as the
# convex hull of its vertices: a partition of several non-coplanar triangles fills the space between them (a player
# stood in the air over Nuketown's street with 32). One triangle per partition is its own hull.
PARTITION_TRIS = 1


def _partition_triangles(verts, tris):
    """Collision triangles in partitions of at most PARTITION_TRIS, one material each, spatially close (median splits
    of their centres). Returns ([(mins, maxs, material, [triangle indices])], tris)."""
    partitions = []
    by_material = {}
    for i, t in enumerate(tris):
        by_material.setdefault(t[3], []).append(i)

    def centre(i):
        a, b, c = (verts[tris[i][k]] for k in range(3))
        return [(a[k] + b[k] + c[k]) / 3.0 for k in range(3)]

    def split(ids, material):
        pts = [verts[tris[i][k]] for i in ids for k in range(3)]
        lo = [min(p[k] for p in pts) for k in range(3)]
        hi = [max(p[k] for p in pts) for k in range(3)]
        if len(ids) <= PARTITION_TRIS:
            partitions.append((lo, hi, material, ids))
            return
        axis = max(range(3), key=lambda k: hi[k] - lo[k])
        ids = sorted(ids, key=lambda i: centre(i)[axis])
        half = len(ids) // 2
        split(ids[:half], material)
        split(ids[half:], material)

    for material, ids in sorted(by_material.items()):
        split(ids, material)
    return partitions, tris


def _brush_corners(bmin, bmax, sides):
    """The points where three of the brush's planes meet inside all the others (what cod2map stores per brush)."""
    if not sides:
        return [(x, y, z) for x in (bmin[0], bmax[0]) for y in (bmin[1], bmax[1]) for z in (bmin[2], bmax[2])]
    import numpy as np
    normals = [(-1, 0, 0), (1, 0, 0), (0, -1, 0), (0, 1, 0), (0, 0, -1), (0, 0, 1)] + [n for n, _, _ in sides]
    dists = [-bmin[0], bmax[0], -bmin[1], bmax[1], -bmin[2], bmax[2]] + [d for _, d, _ in sides]
    n = np.array(normals, dtype=np.float64)
    d = np.array(dists, dtype=np.float64)
    count = len(dists)
    idx = np.array([(i, j, k) for i in range(count) for j in range(i + 1, count) for k in range(j + 1, count)])
    a = n[idx]
    det = np.linalg.det(a)
    ok = np.abs(det) > 1e-6
    if not ok.any():
        return []
    pts = np.linalg.solve(a[ok], d[idx[ok]][..., None])[..., 0]
    inside = (pts @ n.T <= d + 0.01).all(axis=1)
    out = []
    for p in pts[inside]:
        if all(max(abs(p[0] - q[0]), abs(p[1] - q[1]), abs(p[2] - q[2])) > 0.01 for q in out):
            out.append((float(p[0]), float(p[1]), float(p[2])))
    return out


def _entity_string(entities):
    parts = []
    for e in entities:
        lines = ["{"]
        for k, v in e.items():
            if '"' in k or '"' in v:
                raise ValueError("quote in entity key or value")
            lines.append('"%s" "%s"' % (k, v))
        lines.append("}")
        parts.append("\n".join(lines))
    return ("\n".join(parts) + "\n").encode() + b"\0"


def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _normalize(v):
    length = math.sqrt(_dot(v, v))
    return (0.0, 0.0, 1.0) if length == 0 else (v[0] / length, v[1] / length, v[2] / length)


def _face_normals(positions, indices):
    acc = [(0.0, 0.0, 0.0)] * len(positions)
    for t in range(0, len(indices), 3):
        a, b, c = indices[t:t + 3]
        # Clockwise front faces: the front normal is (c - a) x (b - a).
        n = _cross(_sub(positions[c], positions[a]), _sub(positions[b], positions[a]))
        for i in (a, b, c):
            acc[i] = (acc[i][0] + n[0], acc[i][1] + n[1], acc[i][2] + n[2])
    return acc


def _basis(n):
    # Tangent along the world axis least aligned with the normal, then binormal = n x tangent (asset_viewer's floor:
    # n (0,0,1), tangent (1,0,0), binormal (0,-1,0) = -(n x t)).
    ref = (1.0, 0.0, 0.0) if abs(n[0]) < 0.9 else (0.0, 1.0, 0.0)
    t = _normalize(_sub(ref, tuple(n[i] * _dot(ref, n) for i in range(3))))
    b = _cross(n, t)
    return t, (-b[0], -b[1], -b[2])


def build_room():
    bsp = Bsp()
    # Retail BO1 world materials: the map's zone must hold them (material,wc/<name> in its zone source).
    ground = bsp.add_material("eb_art_floor_concrete_tile_dirty")
    wall_mat = bsp.add_material("cub_art_plaster_white_brick")
    sky = bsp.add_material("sky_day", SURF_SKY, CONTENTS_SKY)
    half, wall, height = 1024, 16, 512
    bsp.add_brush((-half, -half, -16), (half, half, 0), ground)
    bsp.add_brush((-half - wall, -half, 0), (-half, half, height), wall_mat)
    bsp.add_brush((half, -half, 0), (half + wall, half, height), wall_mat)
    bsp.add_brush((-half - wall, -half - wall, 0), (half + wall, -half, height), wall_mat)
    bsp.add_brush((-half - wall, half, 0), (half + wall, half + wall, height), wall_mat)
    # The sky box: six thin brushes around everything, drawing their inner faces only.
    lo, hi, t = (-half - 64, -half - 64, -64), (half + 64, half + 64, height + 512), 16
    for axis in range(3):
        for side in (0, 1):
            bmin, bmax = list(lo), list(hi)
            if side:
                bmin[axis] = hi[axis]
                bmax[axis] = hi[axis] + t
            else:
                bmax[axis] = lo[axis]
                bmin[axis] = lo[axis] - t
            bsp.add_brush(bmin, bmax, sky, faces="none")
            face_normal = [0.0, 0.0, 0.0]
            face_normal[axis] = -1.0 if side else 1.0
            bsp._add_box_face(lo, hi, axis, side, face_normal, sky, 128.0)
    bsp.add_entity(classname="worldspawn", skyboxmodel="skybox_day", suncolor="1 1 1", sunlight="1.2",
                   sundirection="245 45 0", ambient="0", _color="0.5 0.5 0.5")
    bsp.add_entity(classname="info_player_start", origin="0 0 8", angles="0 90 0")
    return bsp


def main(argv):
    if len(argv) == 3 and argv[1] == "room":
        build_room().write(argv[2])
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
