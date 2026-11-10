#!/usr/bin/env python3
"""BO2's baked world lighting as vertex colours, for bo2_mesh.py.

BO2 lights a world surface with its lightmap (three slices stacked in `*lightmapN_secondary`, one image per
lightmap index) and, when the surface's primary light is the sun, the sun. Its world pixel shaders (`lmap` techniques,
ps_4_0, read with fxc /dumpbin) compute, in linear light:

    A   = slice0.rgb / slice0.a                 (each slice sampled at (u, v / 3 + slice / 3))
    B   = slice1.rgb / slice1.a
    dir = slice2.rgb * 2 - 1
    L   = A + B * saturate(dot(dir, N)) + vis * sunDiffuse * saturate(dot(sunDir, N))

with vis = slice2.a, the sun's baked visibility (the sun shadow map replaces it near the viewer), sunDiffuse the
world's sun light colour and the pixel's colour = sqrt(albedo^2 * L * exposure). A surface whose primary light is
another light of comworld (<map>.d3dbsp.lights.csv: the street's spots, the houses' lamps) gets that light instead of
the sun, as BO2's light variant draws it:

    vis * colour * saturate(dot(L, N)) * min(dAttenuation / d^2, 1) * cone * window

with L the direction to the light, d its distance and window (1 - (d / radius)^2)^2. A spot (type 2) has a cone, a
smoothstep from cosHalfFovOuter to cosHalfFovInner on dot(L, dir): its dir points from the lit area to the light, as
the sun's does (each spot's surfaces lie at cos -0.97 to -0.82 of dir). The lamps (type 5, "white_light_cube": a cube
shadow map) light all around them: their surfaces lie on every side, so no cone. BO2 shapes the cone and the end of
the range with constants (lightFallOffA/B, the attenuation texture) this bake approximates with those curves.
MapKit draws the world with BO1's
model shaders, which multiply the colour map by the vertex colour: the colour here is sqrt(L / reference), the
reference being the light of the sunlit ground (so sunlit ground keeps the colour map, as before, and shade darkens).

A vertex colour only follows the lightmap at the vertices: `light_triangles` splits a triangle where the light at its
edge midpoints or centre differs from what the corners give, down to about one lightmap texel, so shadow edges stay.

Inputs, from tools/bo2/oat's GfxWorldObjDumperT6.cpp: <map>.d3dbsp.worldlight.bin (one record per OBJ vertex),
<map>.d3dbsp.worldlight.txt (the lightmap images and the sun) and the lightmap images (images/_lightmapN_secondary.dds,
uncompressed RGBA).
"""
import csv
import math
import os
import struct
import sys

# A triangle is split while a midpoint's colour (0..1, per channel) differs by more than this from the corners' average
SPLIT_TOLERANCE = 0.06
# ... and while its longest edge is longer than this: BO2's lightmaps hold about one texel per 13 units on Nuketown
SPLIT_MIN_EDGE = 16.0
SPLIT_MAX_DEPTH = 8
# The darkest colour: a floor under BO2's darkest corners, which its exposure and tone curve lift
LIGHT_FLOOR = 0.12
# The reference is this percentile of the light over the corners the sun reaches (vis above SUNLIT_VIS)
REFERENCE_PERCENTILE = 0.75
SUNLIT_VIS = 0.8
NO_LIGHTMAP = 31  # GfxSurface::lightmapIndex of a surface without one (sky, unlit materials)
SPOT, OMNI = 2, 5  # ComPrimaryLight::type of the street's spots and of the houses' lamps


def read_world_light(base):
    """(corners, info) from <base>.worldlight.bin and .txt, or None when the dump has no such files. corners[i] is OBJ
    vertex i's (u, v, lightmap index, primary light index); info holds 'lightmaps' (index -> image name), 'sunDir',
    'sunDiffuse', 'sunPrimaryLightIndex' and 'lights' (read_lights, from <base>.lights.csv)."""
    info = {"lightmaps": {}, "lights": read_lights(base + ".lights.csv")}
    try:
        with open(base + ".worldlight.bin", "rb") as f:
            data = f.read()
        with open(base + ".worldlight.txt", encoding="latin-1") as f:
            text = f.read().splitlines()
    except OSError:
        return None
    if data[:8] != b"BO2WLT1\0":
        sys.exit(f"{base}.worldlight.bin: not a BO2WLT1 file (rerun tools/bo2/oat's Unlinker)")
    count = struct.unpack_from("<I", data, 8)[0]
    if len(data) < 12 + 20 * count:
        sys.exit(f"{base}.worldlight.bin: {count} records announced, the file is shorter")
    corners = []
    for i in range(count):
        lmap, _colour, _p0, _p1, index, primary = struct.unpack_from("<4I2B", data, 12 + 20 * i)
        corners.append(((lmap & 0xFFFF) / 65535.0, (lmap >> 16) / 65535.0, index, primary))
    for line in text:
        words = line.split()
        if not words:
            continue
        if words[0] == "lightmap" and len(words) >= 6:
            info["lightmaps"][int(words[1])] = words[5]
        elif words[0] == "sunLight" and words[1] == "type":
            info["sunDiffuse"] = tuple(float(x) for x in words[4:7])
            info["sunDir"] = tuple(float(x) for x in words[8:11])
        elif words[0] == "sunPrimaryLightIndex":
            info["sunPrimaryLightIndex"] = int(words[1])
    for key in ("sunDiffuse", "sunDir", "sunPrimaryLightIndex"):
        if key not in info:
            sys.exit(f"{base}.worldlight.txt: no {key} (rerun tools/bo2/oat's Unlinker)")
    return corners, info


def read_lights(path):
    """index -> spot light (dict of floats) from tools/bo2/oat's ComWorldLightsDumperT6 CSV; {} without the file."""
    lights = {}
    try:
        with open(path, encoding="latin-1") as f:
            for row in csv.DictReader(f):
                if int(row["type"]) in (SPOT, OMNI):  # 1 is the sun, 0 the empty light
                    lights[int(row["index"])] = {k: float(row[k]) for k in ("type",
                        "r", "g", "b", "dx", "dy", "dz", "x", "y", "z", "radius", "cosHalfFovOuter", "cosHalfFovInner",
                        "dAttenuation")}
    except OSError:
        pass
    return lights


class Lightmap:
    """One lightmap image: three slices of `height` rows each, RGBA bytes."""

    def __init__(self, path):
        with open(path, "rb") as f:
            data = f.read()
        if data[:4] != b"DDS " or len(data) < 128:
            sys.exit(f"{path}: not a DDS file")
        height, width = struct.unpack_from("<II", data, 12)
        flags, fourcc, bits = struct.unpack_from("<I4sI", data, 80)
        masks = struct.unpack_from("<4I", data, 92)
        if flags != 0x41 or bits != 32 or masks != (0xFF, 0xFF00, 0xFF0000, 0xFF000000) or height % 3:
            sys.exit(f"{path}: not the uncompressed RGBA lightmap this reader knows "
                     f"(flags {flags:#x}, {bits} bits, masks {masks}, {width}x{height})")
        if len(data) < 128 + width * height * 4:
            sys.exit(f"{path}: {width}x{height} announced, the pixels are short")
        self.pixels = memoryview(data)[128:128 + width * height * 4]
        self.width, self.height = width, height // 3

    def sample(self, u, v, slice_):
        """The slice's RGBA (0..1) at (u, v), bilinear and clamped to the slice, as the GPU's linear sampler reads it."""
        x = min(max(u * self.width - 0.5, 0.0), self.width - 1.0)
        y = min(max(v * self.height - 0.5, 0.0), self.height - 1.0)
        x0, y0 = int(x), int(y)
        x1, y1 = min(x0 + 1, self.width - 1), min(y0 + 1, self.height - 1)
        fx, fy = x - x0, y - y0
        p, row = self.pixels, slice_ * self.height
        out = [0.0, 0.0, 0.0, 0.0]
        for yy, wy in ((y0, 1.0 - fy), (y1, fy)):
            base = (row + yy) * self.width
            for xx, wx in ((x0, 1.0 - fx), (x1, fx)):
                w = wx * wy
                if w:
                    at = (base + xx) * 4
                    for c in range(4):
                        out[c] += p[at + c] * w
        return [c / 255.0 for c in out]


class WorldLight:
    """BO2's light at a point of the world, and its vertex colour."""

    def __init__(self, dump, info):
        self.lightmaps = {}
        for index, name in info["lightmaps"].items():
            if name != "-":
                # OAT writes an image named "*lightmap0_secondary" as "_lightmap0_secondary.dds"
                self.lightmaps[index] = Lightmap(os.path.join(dump, "images", name.replace("*", "_") + ".dds"))
        self.sun_diffuse = info["sunDiffuse"]
        self.sun_dir = info["sunDir"]
        self.sun_index = info["sunPrimaryLightIndex"]
        self.lights = info["lights"]
        self.reference = (1.0, 1.0, 1.0)

    def light(self, index, primary, u, v, normal, position=None):
        """Linear light (r, g, b) of the lmap shader, None without a lightmap. A lamp needs the position."""
        lightmap = self.lightmaps.get(index)
        if lightmap is None:
            return None
        s0 = lightmap.sample(u, v, 0)
        s1 = lightmap.sample(u, v, 1)
        s2 = lightmap.sample(u, v, 2)
        a0, a1 = 1.0 / (s0[3] + 1e-6), 1.0 / (s1[3] + 1e-6)
        d = sum((s2[k] * 2.0 - 1.0) * normal[k] for k in range(3))
        d = min(max(d, 0.0), 1.0)
        out = [s0[k] * a0 + s1[k] * a1 * d for k in range(3)]
        if primary == self.sun_index:
            ndl = min(max(sum(self.sun_dir[k] * normal[k] for k in range(3)), 0.0), 1.0)
            vis = s2[3]
            out = [out[k] + vis * self.sun_diffuse[k] * ndl for k in range(3)]
        elif primary in self.lights and position is not None:
            lamp = self.lights[primary]
            to = (lamp["x"] - position[0], lamp["y"] - position[1], lamp["z"] - position[2])
            d2 = to[0] * to[0] + to[1] * to[1] + to[2] * to[2]
            d = math.sqrt(d2)
            if 1e-3 < d < lamp["radius"]:
                l = (to[0] / d, to[1] / d, to[2] / d)
                ndl = min(max(sum(l[k] * normal[k] for k in range(3)), 0.0), 1.0)
                atten = min(lamp["dAttenuation"] / d2, 1.0)
                window = (1.0 - d2 / (lamp["radius"] * lamp["radius"])) ** 2
                cone = 1.0
                if lamp["type"] == SPOT:
                    axis = l[0] * lamp["dx"] + l[1] * lamp["dy"] + l[2] * lamp["dz"]
                    outer, inner = lamp["cosHalfFovOuter"], lamp["cosHalfFovInner"]
                    t = min(max((axis - outer) / max(inner - outer, 1e-6), 0.0), 1.0)
                    cone = t * t * (3.0 - 2.0 * t)
                scale = s2[3] * ndl * atten * window * cone
                out = [out[k] + scale * lamp[c] for k, c in enumerate("rgb")]
        return out

    def calibrate(self, samples):
        """Takes the reference from sunlit samples [(light, vis)]: REFERENCE_PERCENTILE of each channel."""
        lit = [s for s, vis in samples if vis > SUNLIT_VIS]
        if not lit:
            return
        ref = []
        for k in range(3):
            values = sorted(s[k] for s in lit)
            ref.append(max(values[int(REFERENCE_PERCENTILE * (len(values) - 1))], 1e-3))
        self.reference = tuple(ref)

    def colour(self, light):
        """The vertex colour (0..1 per channel) of a linear light."""
        if light is None:
            return (1.0, 1.0, 1.0)
        return tuple(min(max(math.sqrt(max(light[k], 0.0) / self.reference[k]), LIGHT_FLOOR), 1.0) for k in range(3))

    def calibrate_corners(self, corners_by_tri):
        """calibrate() over the corners of [[(position, uv, (lu, lv), index, primary)] * 3] lit by the sun."""
        samples = []
        for tri in corners_by_tri:
            index, primary = tri[0][3], tri[0][4]
            lightmap = self.lightmaps.get(index)
            if lightmap is None or primary != self.sun_index:
                continue
            normal = outward_normal(tri[0][0], tri[1][0], tri[2][0])
            if normal is None:
                continue
            for c in tri:
                lu, lv = c[2]
                samples.append((self.light(index, primary, lu, lv, normal), lightmap.sample(lu, lv, 2)[3]))
        self.calibrate(samples)


def pack(colour):
    r, g, b = (int(c * 255.0 + 0.5) for c in colour)
    return 0xFF000000 | r << 16 | g << 8 | b


def outward_normal(a, b, c):
    """The world OBJ keeps BO2's winding, whose right-hand normal points into the surface (checked against the
    lightmap directions: 99 % of floors): the outward normal is the opposite one."""
    e1 = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
    e2 = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
    n = (e1[2] * e2[1] - e1[1] * e2[2], e1[0] * e2[2] - e1[2] * e2[0], e1[1] * e2[0] - e1[0] * e2[1])
    length = math.sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2])
    return (n[0] / length, n[1] / length, n[2] / length) if length > 1e-12 else None


def light_triangles(world_light, tris, stats):
    """tris: [[(position, uv, (lu, lv), index, primary)] * 3] of one material. Returns [[(position, uv, colour)] * 3],
    split where the light changes inside a triangle."""
    out = []

    def colour_at(corner, normal):
        p, uv, (lu, lv), index, primary = corner
        return world_light.colour(world_light.light(index, primary, lu, lv, normal, p))

    def mid(c0, c1):
        lerp = lambda a, b: tuple((a[k] + b[k]) * 0.5 for k in range(len(a)))  # noqa: E731
        return (lerp(c0[0], c1[0]), lerp(c0[1], c1[1]), lerp(c0[2], c1[2]), c0[3], c0[4])

    def emit(tri, cols):
        out.append([(tri[k][0], tri[k][1], pack(cols[k])) for k in range(3)])

    def split(tri, cols, normal, depth):
        edges = [(math.dist(tri[k][0], tri[(k + 1) % 3][0]), k) for k in range(3)]
        longest, k = max(edges)
        if depth < SPLIT_MAX_DEPTH and longest > SPLIT_MIN_EDGE:
            # test the three edge midpoints and the centre against the colour the corners interpolate there
            worst = 0.0
            for e in range(3):
                m = mid(tri[e], tri[(e + 1) % 3])
                want = tuple((cols[e][c] + cols[(e + 1) % 3][c]) * 0.5 for c in range(3))
                got = colour_at(m, normal)
                worst = max(worst, max(abs(got[c] - want[c]) for c in range(3)))
            centre = (tuple((tri[0][0][c] + tri[1][0][c] + tri[2][0][c]) / 3 for c in range(3)),
                      tuple((tri[0][1][c] + tri[1][1][c] + tri[2][1][c]) / 3 for c in range(2)),
                      tuple((tri[0][2][c] + tri[1][2][c] + tri[2][2][c]) / 3 for c in range(2)), tri[0][3], tri[0][4])
            want = tuple(sum(cols[j][c] for j in range(3)) / 3 for c in range(3))
            got = colour_at(centre, normal)
            worst = max(worst, max(abs(got[c] - want[c]) for c in range(3)))
            if worst > SPLIT_TOLERANCE:
                # split the longest edge k -> k+1 at its midpoint: two triangles with the same winding
                a, b, c = tri[k], tri[(k + 1) % 3], tri[(k + 2) % 3]
                ca, cb, cc = cols[k], cols[(k + 1) % 3], cols[(k + 2) % 3]
                m = mid(a, b)
                cm = colour_at(m, normal)
                stats["splits"] += 1
                split([a, m, c], [ca, cm, cc], normal, depth + 1)
                split([m, b, c], [cm, cb, cc], normal, depth + 1)
                return
        emit(tri, cols)

    for tri in tris:
        if tri[0][3] == NO_LIGHTMAP or tri[0][3] not in world_light.lightmaps:
            out.append([(c[0], c[1], 0xFFFFFFFF) for c in tri])
            stats["unlit"] += 1
            continue
        normal = outward_normal(tri[0][0], tri[1][0], tri[2][0])
        if normal is None:
            out.append([(c[0], c[1], 0xFFFFFFFF) for c in tri])
            stats["unlit"] += 1
            continue
        cols = [colour_at(c, normal) for c in tri]
        split(tri, cols, normal, 0)
    return out
