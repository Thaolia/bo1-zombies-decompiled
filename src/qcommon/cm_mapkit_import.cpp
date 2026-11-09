#include "cm_mapkit.h"
#include "cm_trace.h"
#include "cm_load.h"
#include "common.h"
#include <universal/com_files.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

// mod: mapkit "collision" edit - the collision of another game's map, dumped by tools/bo2/oat (docs/BO2_MAPS.md) as
// <map>.collision.bin: its world brushes and terrain triangles (those of submodels, the doors and debris, are left
// out: they sit in their model's own space), each triangle turned into a thin prism brush under it. They are cbrush_t
// like the retail ones, with their corners, so the engine's own brush tests (CM_TraceThroughBrush, CM_TestBoxInBrush,
// CM_SightTraceThroughBrush) and the physics' GJK queries (colgeom_visitor_t, phys_traverse.cpp: player and actor
// movement, ragdolls) run on them; a 2D grid keeps a query to the brushes near it. Lookups keep no per-call state:
// traces run on several threads at once.

namespace
{
    const char MAGIC[8] = { 'B', 'O', '2', 'C', 'O', 'L', '2', 0 };
    const float CELL = 256.0f;
    const float TERRAIN_THICKNESS = 16.0f;
    const int MAX_GRID_CELLS = 4 * 1024 * 1024;

    cbrush_t *s_brushes;
    cbrushside_t *s_sides;
    cplane_s *s_planes;
    float (*s_verts)[3];
    int s_brushCount;
    int s_terrainBrushCount;

    float s_gridOrigin[2];
    float s_cellSize = CELL;
    int s_gridSize[2];
    int *s_cellStart;   // s_gridSize[0] * s_gridSize[1] + 1 offsets into s_cellBrushes
    int *s_cellBrushes;

    struct Reader
    {
        const unsigned char *data;
        int size;
        int at;
        bool bad;

        bool Take(void *out, int bytes)
        {
            if (bad || bytes < 0 || at + bytes > size)
            {
                bad = true;
                return false;
            }
            memcpy(out, data + at, bytes);
            at += bytes;
            return true;
        }
        unsigned int U32() { unsigned int v = 0; Take(&v, 4); return v; }
        int I32() { int v = 0; Take(&v, 4); return v; }
        unsigned short U16() { unsigned short v = 0; Take(&v, 2); return v; }
        float F32() { float v = 0.0f; Take(&v, 4); return v; }
        void Vec3(float *v) { Take(v, 12); }
        // a count must fit what is left of the file at `minBytes` each: a corrupt count fails here, not in malloc
        unsigned int Count(int minBytes)
        {
            const unsigned int n = U32();
            if (!bad && (unsigned long long)n * (unsigned int)minBytes > (unsigned long long)(size - at))
                bad = true;
            return bad ? 0 : n;
        }
    };

    void Import_Free()
    {
        free(s_brushes);
        free(s_sides);
        free(s_planes);
        free(s_verts);
        free(s_cellStart);
        free(s_cellBrushes);
        s_brushes = 0;
        s_sides = 0;
        s_planes = 0;
        s_verts = 0;
        s_cellStart = 0;
        s_cellBrushes = 0;
        s_brushCount = 0;
        s_terrainBrushCount = 0;
        s_gridSize[0] = s_gridSize[1] = 0;
    }

    void Import_SetPlane(cplane_s *plane, const float *normal, float dist)
    {
        plane->normal[0] = normal[0];
        plane->normal[1] = normal[1];
        plane->normal[2] = normal[2];
        plane->dist = dist;
        // as CMod_LoadPlanes: 0-2 for an axial normal, 3 otherwise
        plane->type = normal[0] == 1.0f ? 0 : normal[1] == 1.0f ? 1 : normal[2] == 1.0f ? 2 : 3;
        SetPlaneSignbits(plane);
    }

    // The corners of a convex brush: every point where three of its planes (sides and axial bounds) meet that lies
    // inside all of them. The physics' GJK queries (player movement, ragdolls) read a brush's shape from these, the
    // trace code from the planes. Appends to *out (grown with realloc); returns the number added, -1 when out of memory.
    int Import_BrushVerts(const cbrush_t *brush, float (**out)[3], int *used, int *capacity)
    {
        float normals[64][3], dists[64];
        int count = 0;
        for (int k = 0; k < 3; ++k)
        {
            float n[3] = { 0.0f, 0.0f, 0.0f };
            n[k] = 1.0f;
            memcpy(normals[count], n, sizeof(n));
            dists[count++] = brush->maxs[k];
            n[k] = -1.0f;
            memcpy(normals[count], n, sizeof(n));
            dists[count++] = -brush->mins[k];
        }
        for (unsigned int s = 0; s < brush->numsides && count < 64; ++s)
        {
            memcpy(normals[count], brush->sides[s].plane->normal, sizeof(normals[count]));
            dists[count++] = brush->sides[s].plane->dist;
        }
        const int first = *used;
        for (int a = 0; a < count; ++a)
            for (int b = a + 1; b < count; ++b)
                for (int c = b + 1; c < count; ++c)
                {
                    const float *na = normals[a], *nb = normals[b], *nc = normals[c];
                    const float bc[3] = { nb[1] * nc[2] - nb[2] * nc[1], nb[2] * nc[0] - nb[0] * nc[2],
                        nb[0] * nc[1] - nb[1] * nc[0] };
                    const float det = na[0] * bc[0] + na[1] * bc[1] + na[2] * bc[2];
                    if (fabsf(det) < 1e-6f)
                        continue;
                    const float ca[3] = { nc[1] * na[2] - nc[2] * na[1], nc[2] * na[0] - nc[0] * na[2],
                        nc[0] * na[1] - nc[1] * na[0] };
                    const float ab[3] = { na[1] * nb[2] - na[2] * nb[1], na[2] * nb[0] - na[0] * nb[2],
                        na[0] * nb[1] - na[1] * nb[0] };
                    float p[3];
                    for (int k = 0; k < 3; ++k)
                        p[k] = (dists[a] * bc[k] + dists[b] * ca[k] + dists[c] * ab[k]) / det;
                    bool inside = true;
                    for (int q = 0; q < count && inside; ++q)
                        inside = normals[q][0] * p[0] + normals[q][1] * p[1] + normals[q][2] * p[2] <= dists[q] + 0.05f;
                    for (int v = first; v < *used && inside; ++v)
                        inside = fabsf((*out)[v][0] - p[0]) + fabsf((*out)[v][1] - p[1]) + fabsf((*out)[v][2] - p[2])
                            > 0.05f;
                    if (!inside)
                        continue;
                    if (*used == *capacity)
                    {
                        const int grown = *capacity ? *capacity * 2 : 65536;
                        float (*bigger)[3] = (float (*)[3])realloc(*out, sizeof(float[3]) * grown);
                        if (!bigger)
                            return -1;
                        *out = bigger;
                        *capacity = grown;
                    }
                    memcpy((*out)[(*used)++], p, sizeof(p));
                }
        return *used - first;
    }

    // A terrain triangle as a prism: its own plane on top, one 16 units below, three edge planes; the box is the
    // prism's bounds. False for a degenerate triangle.
    bool Import_TerrainBrush(cbrush_t *brush, cbrushside_t *sides, cplane_s *planes, const float (*v)[3], int contents,
        int sflags)
    {
        float e0[3], e1[3], n[3];
        for (int k = 0; k < 3; ++k)
        {
            e0[k] = v[1][k] - v[0][k];
            e1[k] = v[2][k] - v[0][k];
        }
        n[0] = e0[1] * e1[2] - e0[2] * e1[1];
        n[1] = e0[2] * e1[0] - e0[0] * e1[2];
        n[2] = e0[0] * e1[1] - e0[1] * e1[0];
        const float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (len < 1e-4f)
            return false;
        for (int k = 0; k < 3; ++k)
            n[k] /= len;
        // BO2 winds its collision triangles clockwise seen from their front (zm_nuked: 10 983 of 13 089 near-horizontal
        // ones give a downward cross product), so the front faces -n. A steep triangle (a wall) takes that front and gets
        // its prism behind it, inside the wall: the other way, house 2's walls stood 16 units into its rooms and hid
        // Pack-a-Punch's trigger from the use search. A near-horizontal one keeps facing up whatever its winding, as
        // before: a floor stored the other way round still gets its prism under it.
        const bool steep = n[2] > -0.7f && n[2] < 0.7f;
        if (steep || n[2] < 0.0f)
            for (int k = 0; k < 3; ++k)
                n[k] = -n[k];
        const float d = n[0] * v[0][0] + n[1] * v[0][1] + n[2] * v[0][2];
        float back[3] = { -n[0], -n[1], -n[2] };
        Import_SetPlane(&planes[0], n, d);
        Import_SetPlane(&planes[1], back, -(d - TERRAIN_THICKNESS));
        float centroid[3];
        for (int k = 0; k < 3; ++k)
            centroid[k] = (v[0][k] + v[1][k] + v[2][k]) / 3.0f;
        for (int edge = 0; edge < 3; ++edge)
        {
            const float *a = v[edge];
            const float *b = v[(edge + 1) % 3];
            float dir[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
            float en[3] = { dir[1] * n[2] - dir[2] * n[1], dir[2] * n[0] - dir[0] * n[2], dir[0] * n[1] - dir[1] * n[0] };
            const float el = sqrtf(en[0] * en[0] + en[1] * en[1] + en[2] * en[2]);
            if (el < 1e-4f)
                return false;
            for (int k = 0; k < 3; ++k)
                en[k] /= el;
            float ed = en[0] * a[0] + en[1] * a[1] + en[2] * a[2];
            if (en[0] * centroid[0] + en[1] * centroid[1] + en[2] * centroid[2] > ed) // the plane must face away
            {
                for (int k = 0; k < 3; ++k)
                    en[k] = -en[k];
                ed = -ed;
            }
            Import_SetPlane(&planes[2 + edge], en, ed);
        }
        memset(brush, 0, sizeof(*brush));
        for (int k = 0; k < 3; ++k)
        {
            // the bounds of both faces: bounds taken from the top face alone would cut a steep prism down to nothing
            const float lo = fminf(fminf(v[0][k], v[1][k]), v[2][k]), hi = fmaxf(fmaxf(v[0][k], v[1][k]), v[2][k]);
            brush->mins[k] = fminf(lo, lo - TERRAIN_THICKNESS * n[k]);
            brush->maxs[k] = fmaxf(hi, hi - TERRAIN_THICKNESS * n[k]);
            brush->axial_cflags[0][k] = brush->axial_cflags[1][k] = contents;
            brush->axial_sflags[0][k] = brush->axial_sflags[1][k] = sflags;
        }
        brush->contents = contents;
        brush->numsides = 5;
        brush->sides = sides;
        for (int s = 0; s < 5; ++s)
        {
            sides[s].plane = &planes[s];
            sides[s].cflags = contents;
            sides[s].sflags = sflags;
        }
        return true;
    }

    void Import_BuildGrid()
    {
        float lo[2] = { 1e30f, 1e30f }, hi[2] = { -1e30f, -1e30f };
        for (int b = 0; b < s_brushCount; ++b)
            for (int k = 0; k < 2; ++k)
            {
                lo[k] = fminf(lo[k], s_brushes[b].mins[k]);
                hi[k] = fmaxf(hi[k], s_brushes[b].maxs[k]);
            }
        if (s_brushCount == 0)
            return;
        float cell = CELL;
        // a map with a far backdrop: grow the cells rather than the grid
        while ((double)((hi[0] - lo[0]) / cell + 1.0f) * ((hi[1] - lo[1]) / cell + 1.0f) > MAX_GRID_CELLS)
            cell *= 2.0f;
        s_gridOrigin[0] = lo[0];
        s_gridOrigin[1] = lo[1];
        s_gridSize[0] = (int)((hi[0] - lo[0]) / cell) + 1;
        s_gridSize[1] = (int)((hi[1] - lo[1]) / cell) + 1;
        const int cells = s_gridSize[0] * s_gridSize[1];
        s_cellStart = (int *)calloc(cells + 1, sizeof(int));
        // two passes: count per cell, then fill
        for (int pass = 0; pass < 2; ++pass)
        {
            int *fill = pass ? (int *)calloc(cells, sizeof(int)) : 0;
            for (int b = 0; b < s_brushCount; ++b)
            {
                const int x0 = (int)((s_brushes[b].mins[0] - lo[0]) / cell), x1 = (int)((s_brushes[b].maxs[0] - lo[0]) / cell);
                const int y0 = (int)((s_brushes[b].mins[1] - lo[1]) / cell), y1 = (int)((s_brushes[b].maxs[1] - lo[1]) / cell);
                for (int y = y0; y <= y1; ++y)
                    for (int x = x0; x <= x1; ++x)
                    {
                        const int c = y * s_gridSize[0] + x;
                        if (pass)
                            s_cellBrushes[s_cellStart[c] + fill[c]++] = b;
                        else
                            ++s_cellStart[c + 1];
                    }
            }
            if (pass)
                free(fill);
            else
            {
                for (int c = 0; c < cells; ++c)
                    s_cellStart[c + 1] += s_cellStart[c];
                s_cellBrushes = (int *)malloc(sizeof(int) * (s_cellStart[cells] ? s_cellStart[cells] : 1));
            }
        }
        s_cellSize = cell;
    }

    // calls test(brush) for every brush of the cells the box touches; stops when it returns true
    template <typename Test>
    void Import_ForBox(const float *mins, const float *maxs, Test test)
    {
        if (!s_brushCount || !s_cellStart)
            return;
        const float cell = s_cellSize;
        int x0 = (int)floorf((mins[0] - s_gridOrigin[0]) / cell), x1 = (int)floorf((maxs[0] - s_gridOrigin[0]) / cell);
        int y0 = (int)floorf((mins[1] - s_gridOrigin[1]) / cell), y1 = (int)floorf((maxs[1] - s_gridOrigin[1]) / cell);
        if (x1 < 0 || y1 < 0 || x0 >= s_gridSize[0] || y0 >= s_gridSize[1])
            return;
        x0 = x0 < 0 ? 0 : x0;
        y0 = y0 < 0 ? 0 : y0;
        x1 = x1 >= s_gridSize[0] ? s_gridSize[0] - 1 : x1;
        y1 = y1 >= s_gridSize[1] ? s_gridSize[1] - 1 : y1;
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
            {
                const int c = y * s_gridSize[0] + x;
                for (int i = s_cellStart[c]; i < s_cellStart[c + 1]; ++i)
                {
                    const cbrush_t *b = &s_brushes[s_cellBrushes[i]];
                    // a brush listed in several cells is tested once, in the first cell it shares with the box
                    // (the same arithmetic as Import_BuildGrid)
                    const int bx = (int)((b->mins[0] - s_gridOrigin[0]) / cell);
                    const int by = (int)((b->mins[1] - s_gridOrigin[1]) / cell);
                    if (x != (bx > x0 ? bx : x0) || y != (by > y0 ? by : y0))
                        continue;
                    bool overlap = true;
                    for (int k = 0; k < 3 && overlap; ++k)
                        overlap = mins[k] <= b->maxs[k] && maxs[k] >= b->mins[k];
                    if (overlap && test(b))
                        return;
                }
            }
    }

    void Import_TraceBox(const traceWork_t *tw, float *mins, float *maxs)
    {
        for (int k = 0; k < 3; ++k)
        {
            mins[k] = tw->bounds[0].vec.v[k] - tw->size.vec.v[k] - 1.0f;
            maxs[k] = tw->bounds[1].vec.v[k] + tw->size.vec.v[k] + 1.0f;
        }
    }
}

bool CM_MapkitImport_Load(const char *path, int edit, const CM_MapkitImportCut *cuts, int cutCount)
{
    Import_Free();
    void *buf = 0;
    const int len = FS_ReadFile(path, &buf);
    if (len <= 0 || !buf)
    {
        Com_PrintWarning(1, "mapkit: collision edit %d: cannot read %s\n", edit, path);
        return false;
    }
    Reader r = { (const unsigned char *)buf, len, 0, false };
    char magic[8];
    if (!r.Take(magic, 8) || memcmp(magic, MAGIC, 8))
    {
        Com_PrintWarning(1, "mapkit: collision edit %d: %s is not a BO2COL2 file (redump it with tools/bo2/oat)\n", edit, path);
        FS_FreeFile(buf);
        return false;
    }

    // materials: only their content and surface flags are used
    const unsigned int materialCount = r.Count(72);
    int *matContents = (int *)calloc(materialCount + 1, sizeof(int));
    int *matSurface = (int *)calloc(materialCount + 1, sizeof(int));
    for (unsigned int m = 0; m < materialCount && !r.bad; ++m)
    {
        char name[64];
        r.Take(name, 64);
        matSurface[m] = r.I32();
        matContents[m] = r.I32();
    }

    // brushes: read them all once to size the arrays (world-owned ones and their sides), then keep those
    const int brushStart = r.at;
    const unsigned int fileBrushes = r.Count(84);
    unsigned int worldBrushes = 0, worldSides = 0;
    for (unsigned int b = 0; b < fileBrushes && !r.bad; ++b)
    {
        r.at += 24 + 4; // mins, maxs, contents
        const int owner = r.I32();
        r.at += 48;      // axial flags
        const unsigned int sides = r.Count(24);
        r.at += sides * 24;
        if (r.at > r.size)
            r.bad = true;
        if (owner == 0)
        {
            ++worldBrushes;
            worldSides += sides;
        }
    }
    const unsigned int vertCount = r.Count(12);
    const int vertStart = r.at;
    r.at += vertCount * 12;
    const unsigned int triCount = r.Count(10);
    const int triStart = r.at;
    if (r.bad || r.at + (int)triCount * 10 > r.size)
    {
        Com_PrintWarning(1, "mapkit: collision edit %d: %s is truncated or corrupt\n", edit, path);
        free(matContents);
        free(matSurface);
        FS_FreeFile(buf);
        return false;
    }

    const int maxBrushes = (int)(worldBrushes + triCount);
    const int maxSides = (int)(worldSides + triCount * 5);
    s_brushes = (cbrush_t *)calloc(maxBrushes ? maxBrushes : 1, sizeof(cbrush_t));
    s_sides = (cbrushside_t *)calloc(maxSides ? maxSides : 1, sizeof(cbrushside_t));
    s_planes = (cplane_s *)calloc(maxSides ? maxSides : 1, sizeof(cplane_s));
    if (!s_brushes || !s_sides || !s_planes)
    {
        Com_PrintWarning(1, "mapkit: collision edit %d: out of memory for %d brushes\n", edit, maxBrushes);
        Import_Free();
        free(matContents);
        free(matSurface);
        FS_FreeFile(buf);
        return false;
    }

    int sideUsed = 0, cutBrushes = 0;
    r.at = brushStart;
    r.U32();
    for (unsigned int b = 0; b < fileBrushes; ++b)
    {
        cbrush_t brush;
        memset(&brush, 0, sizeof(brush));
        r.Vec3(brush.mins);
        r.Vec3(brush.maxs);
        brush.contents = r.I32();
        const int owner = r.I32();
        r.Take(brush.axial_cflags, sizeof(brush.axial_cflags));
        r.Take(brush.axial_sflags, sizeof(brush.axial_sflags));
        const unsigned int sides = r.U32();
        bool cut = false;
        for (int k = 0; k < cutCount && !cut && owner == 0; ++k)
            cut = brush.mins[0] >= cuts[k].mins[0] && brush.mins[1] >= cuts[k].mins[1] && brush.mins[2] >= cuts[k].mins[2]
                && brush.maxs[0] <= cuts[k].maxs[0] && brush.maxs[1] <= cuts[k].maxs[1] && brush.maxs[2] <= cuts[k].maxs[2]
                && (!cuts[k].contents || brush.contents == cuts[k].contents);
        cutBrushes += cut;
        if (owner != 0 || cut)
        {
            r.at += sides * 24;
            continue;
        }
        brush.numsides = sides;
        brush.sides = sides ? &s_sides[sideUsed] : 0;
        for (unsigned int s = 0; s < sides; ++s)
        {
            float normal[3];
            r.Vec3(normal);
            const float dist = r.F32();
            Import_SetPlane(&s_planes[sideUsed], normal, dist);
            s_sides[sideUsed].plane = &s_planes[sideUsed];
            s_sides[sideUsed].cflags = r.I32();
            s_sides[sideUsed].sflags = r.I32();
            ++sideUsed;
        }
        s_brushes[s_brushCount++] = brush;
    }

    const float (*verts)[3] = (const float (*)[3])((const unsigned char *)buf + vertStart);
    r.at = triStart;
    int skipped = 0, submodelTris = 0;
    for (unsigned int t = 0; t < triCount; ++t)
    {
        unsigned short idx[3];
        for (int k = 0; k < 3; ++k)
            idx[k] = r.U16();
        const unsigned short material = r.U16();
        if (r.U16() != 0) // a submodel's triangle
        {
            ++submodelTris;
            continue;
        }
        if (idx[0] >= vertCount || idx[1] >= vertCount || idx[2] >= vertCount)
        {
            ++skipped;
            continue;
        }
        float v[3][3];
        for (int k = 0; k < 3; ++k)
            memcpy(v[k], verts[idx[k]], sizeof(v[k]));
        // a triangle no partition reached has no material: solid, as the terrain around it
        const int contents = material < materialCount ? matContents[material] : 1;
        const int sflags = material < materialCount ? matSurface[material] : 0;
        if (!contents || !Import_TerrainBrush(&s_brushes[s_brushCount], &s_sides[sideUsed], &s_planes[sideUsed], v,
                contents, sflags))
        {
            ++skipped;
            continue;
        }
        sideUsed += 5;
        ++s_brushCount;
        ++s_terrainBrushCount;
    }
    free(matContents);
    free(matSurface);
    FS_FreeFile(buf);

    // corners: offsets first, pointers once the array stops moving
    int vertUsed = 0, vertCapacity = 0;
    int *cornerStart = (int *)malloc(sizeof(int) * (s_brushCount ? s_brushCount : 1));
    for (int b = 0; cornerStart && b < s_brushCount; ++b)
    {
        cornerStart[b] = vertUsed;
        if (Import_BrushVerts(&s_brushes[b], &s_verts, &vertUsed, &vertCapacity) < 0)
        {
            free(cornerStart);
            cornerStart = 0;
        }
    }
    if (!cornerStart)
    {
        Com_PrintWarning(1, "mapkit: collision edit %d: out of memory for the brush corners\n", edit);
        Import_Free();
        return false;
    }
    int flat = 0;
    for (int b = 0; b < s_brushCount; ++b)
    {
        s_brushes[b].numverts = (b + 1 < s_brushCount ? cornerStart[b + 1] : vertUsed) - cornerStart[b];
        s_brushes[b].verts = s_verts + cornerStart[b];
        flat += s_brushes[b].numverts < 4;
    }
    free(cornerStart);

    Import_BuildGrid();
    Com_Printf(1, "mapkit: collision edit %d: %s: %d world brushes (of %u; %u submodel brushes and %d in cut boxes left out), %d "
        "terrain prisms (%d submodel triangles left out, %d skipped: degenerate, no contents or bad index), %d corners (%d "
        "brushes with fewer than 4), grid %dx%d\n", edit, path, s_brushCount - s_terrainBrushCount, fileBrushes,
        fileBrushes - worldBrushes, cutBrushes, s_terrainBrushCount, submodelTris, skipped, vertUsed, flat, s_gridSize[0],
        s_gridSize[1]);
    return true;
}

void CM_MapkitImport_Clear()
{
    Import_Free();
}

int CM_MapkitImport_BrushCount()
{
    return s_brushCount;
}

bool CM_MapkitImport_IsBrush(const void *brush)
{
    return brush >= (const void *)s_brushes && brush < (const void *)(s_brushes + s_brushCount);
}

void CM_MapkitImport_VisitBox(const float *mins, const float *maxs, int mask, void (*visit)(const cbrush_t *, void *),
    void *context)
{
    Import_ForBox(mins, maxs, [&](const cbrush_t *b) {
        if (mask & b->contents)
            visit(b, context);
        return false;
    });
}

void CM_MapkitImport_TraceThrough(const traceWork_t *tw, trace_t *trace)
{
    float mins[3], maxs[3];
    Import_TraceBox(tw, mins, maxs);
    Import_ForBox(mins, maxs, [&](const cbrush_t *b) {
        if (tw->contents & b->contents)
            CM_TraceThroughBrush(tw, b, trace);
        return trace->fraction == 0.0f;
    });
}

void CM_MapkitImport_TestIn(const traceWork_t *tw, trace_t *trace)
{
    float mins[3], maxs[3];
    Import_TraceBox(tw, mins, maxs);
    Import_ForBox(mins, maxs, [&](const cbrush_t *b) {
        if (tw->contents & b->contents)
            CM_TestBoxInBrush(tw, b, trace);
        return trace->allsolid;
    });
}

int CM_MapkitImport_PointContents(const float *p)
{
    int contents = 0;
    Import_ForBox(p, p, [&](const cbrush_t *b) {
        for (unsigned int s = 0; s < b->numsides; ++s)
        {
            const cplane_s *plane = b->sides[s].plane;
            if (plane->normal[0] * p[0] + plane->normal[1] * p[1] + plane->normal[2] * p[2] > plane->dist)
                return false;
        }
        contents |= b->contents;
        return false;
    });
    return contents;
}

// the same numbering as CM_Mapkit_SightTrace: past cm.numBrushes, so the sight-trace caches never keep the hit
int CM_MapkitImport_SightTrace(const traceWork_t *tw, trace_t *trace)
{
    float mins[3], maxs[3];
    Import_TraceBox(tw, mins, maxs);
    int hit = 0;
    Import_ForBox(mins, maxs, [&](const cbrush_t *b) {
        if ((tw->contents & b->contents) && CM_SightTraceThroughBrush(tw, b, cm.numBrushes, trace))
            hit = cm.numBrushes + 1;
        return hit != 0;
    });
    return hit;
}
