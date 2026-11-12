"""BO2's baked world lighting as BO1 lightmaps, for bo2_bsp.py.

Each BO2 lightmap becomes one BO1 lightmap with the same coordinates (a world vertex keeps BO2's lightmap uv), its
pixels baked again: every texel a triangle covers takes BO2's light at that point (bo2_worldlight.WorldLight: the
lightmap's slices, the sun or the triangle's lamp, with the triangle's normal), divided by the light of the sunlit
ground (WorldLight.calibrate), so that sunlit ground keeps its colour map. Texels no triangle covers take their
covered neighbours' light (a few passes), so bilinear sampling at a triangle's edge does not fetch black.

A BO1 lightmap (R_LoadLightmaps) is two 512 x 512 halves of float RGBA, whose luminances the world shaders combine,
and a 1024 x 1024 L8 sun visibility for the sun the shaders add. The baked light already holds BO2's sun, so it goes
whole into the first half, the second half and the sun visibility stay 0.
"""
import numpy as np

from bo2_worldlight import outward_normal

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
