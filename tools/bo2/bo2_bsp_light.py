"""BO2's baked world lighting as BO1 lightmaps, for bo2_bsp.py.

Each BO2 lightmap becomes one BO1 lightmap with the same coordinates (a world vertex keeps BO2's lightmap uv), its
pixels baked again: every texel a triangle covers takes BO2's light at that point (bo2_worldlight.WorldLight: the
lightmap's slices, the sun or the triangle's lamp, with the triangle's normal), divided by the light of the sunlit
ground (WorldLight.calibrate), so that sunlit ground keeps its colour map. Texels no triangle covers take their
covered neighbours' light (a few passes), so bilinear sampling at a triangle's edge does not fetch black.

A BO1 lightmap (R_LoadLightmaps) is two 512 x 512 halves of float RGBA, whose luminances the world shaders combine,
and a 1024 x 1024 L8 sun visibility for the sun the shaders add. The baked light already holds BO2's sun, so it goes
whole into the first half, the second half and the sun visibility stay 0.

Models take their light from a light grid instead (light_grid): every point takes BO2's light of the floor under it,
without the sun, and the sun primary light where BO2's sun reaches that floor; BO1's sun then has BO2's sun colour on
the same scale (bake_sun_colour).
"""
import numpy as np

import d3dbsp
from bo2_worldlight import OMNI, SPOT, outward_normal

SIZE = 512
DILATE_PASSES = 8


def bake(world_light, triangles):
    """triangles: [(lightmap index, primary light, [(position, (lu, lv))] * 3)]. Returns {BO2 lightmap index:
    (SIZE, SIZE, 3) float32 light}."""
    light = {i: np.zeros((SIZE, SIZE, 3), np.float32) for i in world_light.lightmaps}
    weight = {i: np.zeros((SIZE, SIZE), np.float32) for i in world_light.lightmaps}
    samples = []
    for index, primary, corners in triangles:
        if index not in light:
            continue
        normal = outward_normal(*(c[0] for c in corners))
        if normal is None:
            continue
        (p0, t0), (p1, t1), (p2, t2) = corners
        x = [t[0] * SIZE - 0.5 for t in (t0, t1, t2)]
        y = [t[1] * SIZE - 0.5 for t in (t0, t1, t2)]
        area = (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0])
        pixels = []
        if abs(area) > 1e-9:
            for py in range(max(int(np.floor(min(y))), 0), min(int(np.ceil(max(y))), SIZE - 1) + 1):
                for px in range(max(int(np.floor(min(x))), 0), min(int(np.ceil(max(x))), SIZE - 1) + 1):
                    w1 = ((px - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (py - y[0])) / area
                    w2 = ((x[1] - x[0]) * (py - y[0]) - (px - x[0]) * (y[1] - y[0])) / area
                    w0 = 1.0 - w1 - w2
                    if min(w0, w1, w2) >= -0.01:
                        pixels.append((px, py, w0, w1, w2))
        if not pixels:
            # smaller than a texel: its centre's texel
            pixels.append((min(max(int(round(sum(x) / 3)), 0), SIZE - 1),
                           min(max(int(round(sum(y) / 3)), 0), SIZE - 1), 1 / 3, 1 / 3, 1 / 3))
        for px, py, w0, w1, w2 in pixels:
            pos = [w0 * p0[k] + w1 * p1[k] + w2 * p2[k] for k in range(3)]
            l = world_light.light(index, primary, (px + 0.5) / SIZE, (py + 0.5) / SIZE, normal, pos)
            if l is None:
                continue
            light[index][py, px] += l
            weight[index][py, px] += 1.0
            if primary == world_light.sun_index:
                samples.append((l, world_light.lightmaps[index].sample((px + 0.5) / SIZE, (py + 0.5) / SIZE, 2)[3]))
    world_light.calibrate(samples)
    ref = np.array(world_light.reference, np.float32)
    out = {}
    for index in light:
        w = weight[index]
        image = np.where(w[..., None] > 0, light[index] / np.maximum(w, 1)[..., None], 0) / ref
        covered = w > 0
        for _ in range(DILATE_PASSES):
            if covered.all():
                break
            total = np.zeros_like(image)
            count = np.zeros(covered.shape, np.float32)
            padded = np.pad(image * covered[..., None], ((1, 1), (1, 1), (0, 0)))
            padded_cov = np.pad(covered.astype(np.float32), 1)
            for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                total += padded[1 + dy:1 + dy + SIZE, 1 + dx:1 + dx + SIZE]
                count += padded_cov[1 + dy:1 + dy + SIZE, 1 + dx:1 + dx + SIZE]
            fill = ~covered & (count > 0)
            image[fill] = total[fill] / count[fill][:, None]
            covered = covered | fill
        out[index] = image.astype(np.float32)
    return out


def lightmap_bytes(image):
    """The 9 MB a BSP stores per lightmap: the first half (float RGBA, the light, alpha 0), the second half (zero),
    then the sun visibility (zero)."""
    rgba = np.zeros((SIZE, SIZE, 4), np.float32)
    rgba[..., :3] = np.clip(image, 0.0, 31.875)
    return rgba.tobytes() + bytes(SIZE * SIZE * 16) + bytes(1024 * 1024)


def white_lightmap_bytes():
    """A lightmap of light 1 for lit surfaces BO2 drew without one."""
    rgba = np.zeros((SIZE, SIZE, 4), np.float32)
    rgba[..., :3] = 1.0
    return rgba.tobytes() + bytes(SIZE * SIZE * 16) + bytes(1024 * 1024)


GRID_ORIGIN = -131072.0
GRID_XY, GRID_Z = 32.0, 64.0       # the engine's light grid spacing (rb_light.cpp)
GRID_PAD = 1024.0                  # beyond the play area (the rocks and houses at its edge)
GRID_ABOVE = 192.0                 # grid points above the highest floor
FLOOR_REACH = 24.0                 # a floor counts for a point this far above it (a point inside a step)
FLOOR_NORMAL_Z = 0.5               # a triangle this flat is a floor
COLOUR_STEP = 1.0 / 64.0           # light grid colours are quantised to this
SMOOTH_RADIUS = 2                  # columns on each side a point's light is averaged over (64 units)
AMBIENT_MIN = 0.12                 # the darkest ambient light, as the lightmaps' LIGHT_FLOOR: BO2 lifts its shade
# The share of its colour a grid point keeps around its luminance. The floors' light without the sun is mostly light
# bounced off Nuketown's orange ground, and dividing it by the reference (the sunlit ground: BO2's blue sun) turned
# it orange, 1.8 times more red than blue on average: doors and hedges out of the sun drew orange and brown where the
# MapKit mesh, lit by BO2's own light of each prop, draws them neutral (measured on screenshots of the same door).
AMBIENT_SATURATION = 0.0
LUMA = (0.2126, 0.7152, 0.0722)


def desaturate(colours):
    """colours (..., 3) with AMBIENT_SATURATION of their colour around their luminance."""
    colours = np.asarray(colours, np.float32)
    luma = (colours * np.asarray(LUMA, np.float32)).sum(axis=-1, keepdims=True)
    return luma + AMBIENT_SATURATION * (colours - luma)


def light_grid(world_light, triangles, play_mins, play_maxs, light_index, none_index):
    """BO1's light grid over the play area (play_mins / play_maxs, world units) from BO2's light. Each point takes
    the light of the floor under it, BO2's lightmap there without its primary light: its first slice plus half its
    second (the second is directional light, which a model takes from every side), divided by world_light.reference
    as the lightmaps are; and the floor's primary light (the sun or a lamp, light_index maps BO2's index to the BSP's)
    where it reaches that floor (none_index elsewhere): BO1 adds that light to the model as it draws it. Each point
    then takes at least the mean of the floors around it, and AMBIENT_MIN, and the primary light most of them have; a
    point with no floor below takes that mean (colour 0, the median, without one).
    triangles: as bake's. Returns (mins, maxs, entries, colours) for D3dBsp.set_light_grid."""
    ref = world_light.reference
    cells = {}
    for index, primary, corners in triangles:
        lightmap = world_light.lightmaps.get(index)
        normal = outward_normal(*(c[0] for c in corners))
        if lightmap is None or normal is None or normal[2] < FLOOR_NORMAL_Z:
            continue
        xs = [c[0][0] for c in corners]
        ys = [c[0][1] for c in corners]
        if max(xs) < play_mins[0] - GRID_PAD or min(xs) > play_maxs[0] + GRID_PAD \
                or max(ys) < play_mins[1] - GRID_PAD or min(ys) > play_maxs[1] + GRID_PAD:
            continue
        for gx in range(int((min(xs) - GRID_ORIGIN) // GRID_XY), int((max(xs) - GRID_ORIGIN) // GRID_XY) + 1):
            for gy in range(int((min(ys) - GRID_ORIGIN) // GRID_XY), int((max(ys) - GRID_ORIGIN) // GRID_XY) + 1):
                cells.setdefault((gx, gy), []).append((lightmap, primary, corners))
    if not cells:
        return None

    def floors_at(x, y, candidates):
        out = []
        for lightmap, primary, ((p0, t0), (p1, t1), (p2, t2)) in candidates:
            area = (p1[0] - p0[0]) * (p2[1] - p0[1]) - (p2[0] - p0[0]) * (p1[1] - p0[1])
            if abs(area) < 1e-6:
                continue
            w1 = ((x - p0[0]) * (p2[1] - p0[1]) - (p2[0] - p0[0]) * (y - p0[1])) / area
            w2 = ((p1[0] - p0[0]) * (y - p0[1]) - (x - p0[0]) * (p1[1] - p0[1])) / area
            w0 = 1.0 - w1 - w2
            if min(w0, w1, w2) < -1e-4:
                continue
            z = w0 * p0[2] + w1 * p1[2] + w2 * p2[2]
            u = w0 * t0[0] + w1 * t1[0] + w2 * t2[0]
            v = w0 * t0[1] + w1 * t1[1] + w2 * t2[1]
            out.append((z, lightmap, primary, u, v))
        out.sort(key=lambda f: f[0])
        return out

    def floor_light(lightmap, primary, u, v):
        s0, s1, s2 = (lightmap.sample(u, v, k) for k in range(3))
        a0, a1 = 1.0 / (s0[3] + 1e-6), 1.0 / (s1[3] + 1e-6)
        colour = [(s0[k] * a0 + 0.5 * s1[k] * a1) / ref[k] for k in range(3)]
        return colour, light_index[primary] if primary in light_index and s2[3] >= 0.5 else none_index

    mins = [int((play_mins[k] - GRID_PAD - GRID_ORIGIN) // GRID_XY) for k in range(2)]
    maxs = [int((play_maxs[k] + GRID_PAD - GRID_ORIGIN) // GRID_XY) + 1 for k in range(2)]
    columns = {}
    low, high = None, None
    for gx in range(mins[0], maxs[0] + 1):
        for gy in range(mins[1], maxs[1] + 1):
            x, y = GRID_ORIGIN + gx * GRID_XY, GRID_ORIGIN + gy * GRID_XY
            # a point on a cell's corner: the triangles of the four cells around it
            floors = floors_at(x, y, [t for c in ((gx, gy), (gx - 1, gy), (gx, gy - 1), (gx - 1, gy - 1))
                                      for t in cells.get(c, ())])
            if floors:
                columns[(gx, gy)] = floors
                low = floors[0][0] if low is None else min(low, floors[0][0])
                high = floors[-1][0] if high is None else max(high, floors[-1][0])
    if low is None:
        return None
    zmin = int((low - GRID_ORIGIN) // GRID_Z)
    zmax = min(int((high + GRID_ABOVE - GRID_ORIGIN) // GRID_Z) + 1, zmin + 254)
    shape = (maxs[0] - mins[0] + 1, maxs[1] - mins[1] + 1, zmax - zmin + 1)
    light = np.full(shape + (3,), np.nan, np.float32)
    prim = np.full(shape, -1, np.int32)
    cache = {}
    for (gx, gy), floors in columns.items():
        if not (mins[0] <= gx <= maxs[0] and mins[1] <= gy <= maxs[1]):
            continue
        for gz in range(zmin, zmax + 1):
            z = GRID_ORIGIN + gz * GRID_Z
            below = [f for f in floors if f[0] <= z + FLOOR_REACH]
            if not below:
                continue
            f = below[-1]
            key = (gx, gy, f[0])
            if key not in cache:
                cache[key] = floor_light(f[1], f[2], f[3], f[4])
            colour, primary = cache[key]
            light[gx - mins[0], gy - mins[1], gz - zmin] = colour
            prim[gx - mins[0], gy - mins[1], gz - zmin] = primary
    # A model's lighting origin often lies over the floor its own shadow darkens (a bus, a truck, a rock): each point
    # takes at least the mean of the floors around it (SMOOTH_RADIUS columns), and the primary light most of them have.
    # Points without a floor of their own take that mean too, so the edges of the area are not black.
    have = ~np.isnan(light[..., 0])
    filled = np.where(have[..., None], light, 0.0)
    total = np.zeros_like(filled)
    count = np.zeros(shape, np.float32)
    r = SMOOTH_RADIUS
    padded = np.pad(filled, ((r, r), (r, r), (0, 0), (0, 0)))
    padded_have = np.pad(have.astype(np.float32), ((r, r), (r, r), (0, 0)))
    padded_prim = np.pad(prim, ((r, r), (r, r), (0, 0)), constant_values=-1)
    candidates = sorted(set(light_index.values()) | {none_index})
    votes = np.zeros((len(candidates),) + shape, np.int32)
    for dx in range(2 * r + 1):
        for dy in range(2 * r + 1):
            total += padded[dx:dx + shape[0], dy:dy + shape[1]]
            count += padded_have[dx:dx + shape[0], dy:dy + shape[1]]
            window = padded_prim[dx:dx + shape[0], dy:dy + shape[1]]
            for i, c in enumerate(candidates):
                votes[i] += window == c
    mean = total / np.maximum(count, 1.0)[..., None]
    default = np.median(light[have], axis=0) if have.any() else np.full(3, 0.5, np.float32)
    out = np.where(have[..., None], np.maximum(light, mean), np.where(count[..., None] > 0, mean, default))
    out = desaturate(out)
    out = np.clip(np.maximum(out, AMBIENT_MIN), 0.0, 31.0)
    out = np.round(out / COLOUR_STEP) * COLOUR_STEP
    sun = light_index.get(world_light.sun_index, none_index)
    chosen = np.where(count > 0, np.asarray(candidates)[np.argmax(votes, axis=0)], sun)

    default = np.maximum(desaturate(default), AMBIENT_MIN)
    default = tuple(float(c) for c in np.round(default / COLOUR_STEP) * COLOUR_STEP)
    colours, colour_index, entries = [default], {default: 0}, []
    for gx in range(shape[0]):
        for gy in range(shape[1]):
            for gz in range(shape[2]):
                colour = tuple(float(c) for c in out[gx, gy, gz])
                if colour not in colour_index:
                    colour_index[colour] = len(colours)
                    colours.append(colour)
                entries.append((colour_index[colour], int(chosen[gx, gy, gz])))
    return (mins[0], mins[1], zmin), (maxs[0], maxs[1], zmax), entries, colours


def bake_sun_colour(world_light):
    """BO1's sun colour on the lightmaps' scale: BO2's sun diffuse colour over world_light.reference."""
    return tuple(world_light.sun_diffuse[k] / world_light.reference[k] for k in range(3))


def add_lamps(bsp, world_light):
    """BO2's lamps and spots (<map>.d3dbsp.lights.csv) as the BSP's primary lights: a spot stays a spot, the cube-lit
    lamps (type 5) become omni lights, their colour over the reference's luminance. They light the models only (the
    lightmaps already hold their light), and over the reference channel by channel, as the lightmaps are, BO2's blue
    sunlit ground turned a warm lamp (16, 10.8, 6.4) into (1.88, 0.68, 0.32): 5.8 times more red than blue, which drew
    the doors and hedges near a house orange. Over the luminance a lamp keeps BO2's own colour. Returns {BO2 index:
    BSP index}."""
    out = {}
    for index in sorted(world_light.lights):
        lamp = world_light.lights[index]
        if lamp["type"] not in (SPOT, OMNI):
            continue
        kind = d3dbsp.LIGHT_SPOT if lamp["type"] == SPOT else d3dbsp.LIGHT_OMNI
        luma = sum(w * r for w, r in zip(LUMA, world_light.reference))
        colour = [lamp[c] / luma for c in "rgb"]
        out[index] = bsp.add_primary_light(kind, (lamp["x"], lamp["y"], lamp["z"]), (lamp["dx"], lamp["dy"], lamp["dz"]),
                                           colour, lamp["radius"], lamp["cosHalfFovOuter"], lamp["cosHalfFovInner"],
                                           lamp.get("roundness", 0.0), cull_dist=int(lamp.get("cullDist", 0)))
    return out
