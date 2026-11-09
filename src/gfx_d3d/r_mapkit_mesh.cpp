#include "r_mapkit_mesh.h"
#include "r_dpvs.h"
#include "r_dvars.h"
#include "r_image.h"
#include "r_image_load_obj.h"
#include "r_init.h"
#include "r_material.h"
#include "r_model.h"
#include "r_utils.h"
#include "r_xsurface_optimize.h"
#include "rb_resource.h"
#include <database/db_registry.h>
#include <qcommon/cm_mapkit.h>
#include <qcommon/common.h>
#include <qcommon/threads.h>
#include <universal/com_files.h>
#include <universal/com_pack.h>
#include <xanim/dobj.h>
#include <xanim/xmodel.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

// mod: mapkit "mesh" edit: {"kind":"mesh","file":"imports/<map>/<map>.mesh.bin","images":"imports/<map>/images",
// "base":"<mc/ material>"}. The file (BO2MESH1, written by tools/bo2/bo2_mesh.py, which documents it) holds chunks of
// another game's world; each becomes a rigid XModel, one surface per material, added every frame by
// R_FilterXModelIntoScene like the skybox model (none of it is in the asset database). Each colour map is an IW image
// loaded outside the database; the materials are copies of one BO1 model material ("mc/": the 32-byte packed vertex)
// with that colour map, a flat normal map and no specular, linked into the database so that the material sort (which
// only walks database materials in fastfile mode) gives them a sorted index. Those copies and their images live for
// the whole process: a material linked into the database may outlive the map. The models are rebuilt per map.

namespace
{
    const char MESH_MAGIC[8] = { 'B', 'O', '2', 'M', 'E', 'S', 'H', '1' };
    const int NAME_BYTES = 64;
    const int MAX_SURFS_PER_MODEL = 128; // R_SkinXModel's stack array

    // one colour map: its image and the material made with it (both for the process' lifetime)
    struct MkMeshLook
    {
        char image[NAME_BYTES];
        char base[NAME_BYTES];
        GfxImage gfx;
        MaterialTextureDef *table;
        char materialName[NAME_BYTES + 16];
        Material *material; // the database's copy, from the last link
        unsigned char *pending; // the IW file read on the main thread, uploaded on the render thread
        int pendingSize;
        bool uploaded;
    };
    // each look is allocated on its own: material tables point at its GfxImage, which must never move
    MkMeshLook **s_looks;
    int s_lookCount;
    int s_lookCapacity;

    struct MkMesh
    {
        int chunkCount;
        XModel *models;
        GfxScaledPlacement *placements;
        unsigned short *lighting;
        int surfCount;
        XSurface *surfs;
        XRigidVertList *vertLists;
        Material **materials;
        GfxPackedVertex *verts;
        unsigned short *indices;
        char (*names)[24];
        IDirect3DDevice9 *device;
        bool ready;
    } s_mesh;

    DObjAnimMat s_identityBone = { { 0.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.0f }, 1.0f };
    unsigned short s_boneName;
    unsigned char s_parentList;
    XBoneInfo s_boneInfo;

    struct Reader
    {
        const unsigned char *data;
        int size;
        int at;
        bool bad;

        const unsigned char *Take(int bytes)
        {
            if (bad || bytes < 0 || at + bytes > size)
            {
                bad = true;
                return 0;
            }
            const unsigned char *p = data + at;
            at += bytes;
            return p;
        }
        unsigned int U32()
        {
            unsigned int v = 0;
            if (const unsigned char *p = Take(4))
                memcpy(&v, p, 4);
            return v;
        }
        // a count must fit what is left of the file at `minBytes` each
        unsigned int Count(int minBytes)
        {
            const unsigned int n = U32();
            if (!bad && (unsigned long long)n * (unsigned int)minBytes > (unsigned long long)(size - at))
                bad = true;
            return bad ? 0 : n;
        }
    };

    int Mesh_FindLook(const char *image, const char *base)
    {
        for (int i = 0; i < s_lookCount; ++i)
        {
            if (!strcmp(s_looks[i]->image, image) && !strcmp(s_looks[i]->base, base))
                return i;
        }
        return -1;
    }

    // render thread: the images read since the last call
    void Mesh_UploadImages(void *)
    {
        for (int i = 0; i < s_lookCount; ++i)
        {
            MkMeshLook *look = s_looks[i];
            if (!look->pending)
                continue;
            GfxImageFileHeader header;
            memcpy(&header, look->pending, sizeof(header));
            Image_LoadFromData(&look->gfx, &header, look->pending + sizeof(header));
            look->uploaded = look->gfx.texture.basemap != 0;
            free(look->pending);
            look->pending = 0;
        }
    }

    // render thread: vertex and index buffers of every chunk
    void Mesh_CreateBuffers(void *)
    {
        for (int c = 0; c < s_mesh.chunkCount; ++c)
            _XModelOptimize(&s_mesh.models[c]);
    }

    void Mesh_ReleaseBuffers(void *)
    {
        for (int s = 0; s < s_mesh.surfCount; ++s)
            XSurfaceReleaseResources(&s_mesh.surfs[s]);
    }

    void Mesh_OnRenderThread(void (*callback)(void *))
    {
        if (Sys_IsRenderThread())
            callback(0);
        else
        {
            RB_Resource_CallbackParam(callback, 0);
            RB_Resource_Flush();
        }
    }

    // the IW image <dir>/<name>.iwi (tools/bo2/bo2_mesh.py): header checked, kept until the render thread uploads it
    bool Mesh_ReadImage(MkMeshLook *look, const char *dir)
    {
        char path[256];
        Com_sprintf(path, sizeof(path), "%s/%s.iwi", dir, look->image);
        void *buf = 0;
        const int len = FS_ReadFile(path, &buf);
        if (len <= 0 || !buf)
        {
            Com_PrintWarning(8, "mapkit: mesh image %s not found\n", path);
            return false;
        }
        GfxImageFileHeader header;
        bool ok = len >= (int)sizeof(header);
        if (ok)
        {
            memcpy(&header, buf, sizeof(header));
            ok = !memcmp(header.tag, "IWi", 3) && (header.format == 0x0B || header.format == 0x0C || header.format == 0x0D)
                && header.dimensions[0] > 0 && header.dimensions[1] > 0 && header.dimensions[2] == 1
                && header.fileSizeForPicmip[0] == len && !(header.flags & ~2);
        }
        if (!ok)
        {
            Com_PrintWarning(8, "mapkit: mesh image %s is not a DXT IW image this loader takes\n", path);
            FS_FreeFile(buf);
            return false;
        }
        look->pending = (unsigned char *)malloc(len);
        if (look->pending)
        {
            memcpy(look->pending, buf, len);
            look->pendingSize = len;
        }
        FS_FreeFile(buf);
        return look->pending != 0;
    }

    // the look's material: a copy of base with the colour map swapped and the normal and specular maps neutral,
    // linked into the database (again on each load: the zone that held the previous link may be gone)
    Material *Mesh_LinkMaterial(MkMeshLook *look, Material *base)
    {
        if (!look->table)
        {
            look->table = (MaterialTextureDef *)calloc(base->textureCount ? base->textureCount : 1, sizeof(MaterialTextureDef));
            if (!look->table)
                return 0;
            memcpy(look->table, base->textureTable, sizeof(MaterialTextureDef) * base->textureCount);
            const unsigned int colorMap = R_HashString("colorMap");
            const unsigned int normalMap = R_HashString("normalMap");
            const unsigned int specularMap = R_HashString("specularMap");
            for (int t = 0; t < base->textureCount; ++t)
            {
                MaterialTextureDef *def = &look->table[t];
                if (def->nameHash == colorMap)
                    def->u.image = &look->gfx;
                else if (def->nameHash == normalMap)
                    def->u.image = rgp.identityNormalMapImage;
                else if (def->nameHash == specularMap)
                    def->u.image = rgp.blackImage;
            }
        }
        Material copy;
        memcpy(&copy, base, sizeof(copy));
        copy.info.name = look->materialName;
        copy.textureTable = look->table;
        XAssetHeader header;
        header.material = &copy;
        look->material = DB_AddXAsset(ASSET_TYPE_MATERIAL, header).material;
        return look->material;
    }

    void Mesh_PackVert(GfxPackedVertex *out, const float *p)
    {
        // p: position (3), normal (3), uv (2), as bo2_mesh.py writes them
        out->xyz[0] = p[0];
        out->xyz[1] = p[1];
        out->xyz[2] = p[2];
        out->binormalSign = 1.0f;
        out->color.packed = -1;
        float n[3] = { p[3], p[4], p[5] };
        // any unit vector across the normal: the normal map is flat, so the tangent only has to be valid
        float t[3] = { -n[1], n[0], 0.0f };
        if (fabsf(n[2]) > 0.9f)
        {
            t[0] = 0.0f;
            t[1] = n[2];
            t[2] = -n[1];
        }
        const float len = sqrtf(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
        if (len > 0.0f)
        {
            t[0] /= len;
            t[1] /= len;
            t[2] /= len;
        }
        out->normal = Vec3PackUnitVec(n);
        out->tangent = Vec3PackUnitVec(t);
        out->texCoord = Vec2PackTexCoords(p + 6);
    }

    void Mesh_FreeModels()
    {
        free(s_mesh.models);
        free(s_mesh.placements);
        free(s_mesh.lighting);
        free(s_mesh.surfs);
        free(s_mesh.vertLists);
        free(s_mesh.materials);
        free(s_mesh.verts);
        free(s_mesh.indices);
        free(s_mesh.names);
        memset(&s_mesh, 0, sizeof(s_mesh));
    }
}

void __cdecl R_Mapkit_FreeMesh()
{
    if (!s_mesh.models)
        return;
    s_mesh.ready = false;
    // a new device (vid_restart) already dropped the buffers with the old one
    if (s_mesh.device == dx.device)
        Mesh_OnRenderThread(Mesh_ReleaseBuffers);
    Mesh_FreeModels();
}

void __cdecl R_Mapkit_LoadMesh()
{
    R_Mapkit_FreeMesh();
    if (!r_loadForRenderer->current.enabled)
        return;
    const char *file = 0, *imageDir = 0, *baseName = 0;
    float unused[3];
    int edit = -1;
    for (int e = 0; e < CM_Mapkit_EditCount() && edit < 0; ++e)
    {
        const char *kind = 0;
        if (CM_Mapkit_GetField(e, "kind", &kind, unused) == MKF_STRING && !strcmp(kind, "mesh"))
            edit = e;
    }
    if (edit < 0)
        return;
    if (CM_Mapkit_GetField(edit, "file", &file, unused) != MKF_STRING
        || CM_Mapkit_GetField(edit, "images", &imageDir, unused) != MKF_STRING)
    {
        Com_PrintWarning(8, "mapkit: mesh edit %d needs \"file\" and \"images\"\n", edit);
        return;
    }
    if (CM_Mapkit_GetField(edit, "base", &baseName, unused) != MKF_STRING)
        baseName = "mc/rus_metal_panel03";
    Material *base = Material_RegisterHandle(baseName, 0);
    if (!base || base == rgp.defaultMaterial || !base->textureTable)
    {
        Com_PrintWarning(8, "mapkit: mesh edit %d: base material '%s' is not loaded\n", edit, baseName);
        return;
    }

    void *buf = 0;
    const int len = FS_ReadFile(file, &buf);
    if (len <= 0 || !buf)
    {
        Com_PrintWarning(8, "mapkit: mesh edit %d: cannot read %s\n", edit, file);
        return;
    }
    Reader r = { (const unsigned char *)buf, len, 0, false };
    const unsigned char *magic = r.Take(8);
    if (!magic || memcmp(magic, MESH_MAGIC, 8))
    {
        Com_PrintWarning(8, "mapkit: mesh edit %d: %s is not a BO2MESH1 file\n", edit, file);
        FS_FreeFile(buf);
        return;
    }

    // materials -> looks (one per colour map); a material without an image draws with the base's own colour map
    const unsigned int materialCount = r.Count(2 * NAME_BYTES);
    int *lookOf = (int *)calloc(materialCount ? materialCount : 1, sizeof(int));
    int newImages = 0, missingImages = 0;
    for (unsigned int m = 0; m < materialCount && lookOf; ++m)
    {
        r.Take(NAME_BYTES); // the source material's name: for the tool's report only
        char image[NAME_BYTES];
        memcpy(image, r.Take(NAME_BYTES), NAME_BYTES);
        image[NAME_BYTES - 1] = 0;
        lookOf[m] = -1;
        if (!image[0])
            continue;
        int look = Mesh_FindLook(image, baseName);
        if (look < 0)
        {
            if (s_lookCount == s_lookCapacity)
            {
                const int grown = s_lookCapacity ? s_lookCapacity * 2 : 128;
                MkMeshLook **bigger = (MkMeshLook **)realloc(s_looks, sizeof(MkMeshLook *) * grown);
                if (!bigger)
                    break;
                s_looks = bigger;
                s_lookCapacity = grown;
            }
            MkMeshLook *l = (MkMeshLook *)calloc(1, sizeof(MkMeshLook));
            if (!l)
                break;
            I_strncpyz(l->image, image, sizeof(l->image));
            I_strncpyz(l->base, baseName, sizeof(l->base));
            Com_sprintf(l->materialName, sizeof(l->materialName), "mapkit/%s", image);
            l->gfx.name = l->image;
            l->gfx.semantic = 2;
            if (!Mesh_ReadImage(l, imageDir))
            {
                ++missingImages;
                free(l);
                continue; // not kept: the next load tries again
            }
            look = s_lookCount;
            s_looks[s_lookCount++] = l;
            ++newImages;
        }
        lookOf[m] = look;
    }
    if (newImages)
        Mesh_OnRenderThread(Mesh_UploadImages);
    int linked = 0;
    for (int i = 0; i < s_lookCount; ++i)
    {
        if (s_looks[i]->uploaded && !strcmp(s_looks[i]->base, baseName) && Mesh_LinkMaterial(s_looks[i], base))
            ++linked;
    }
    Material_DirtySort();

    // chunks: sizes first (one pass over the file), then the arrays, then the data
    const int chunkStart = r.at;
    const unsigned int chunkCount = r.Count(20);
    unsigned int surfTotal = 0, vertTotal = 0, triTotal = 0;
    bool tooManySurfs = false;
    for (unsigned int c = 0; c < chunkCount && !r.bad; ++c)
    {
        r.Take(16);
        const unsigned int surfs = r.Count(12);
        tooManySurfs |= surfs > MAX_SURFS_PER_MODEL;
        for (unsigned int s = 0; s < surfs && !r.bad; ++s)
        {
            r.Take(4);
            const unsigned int verts = r.U32(), tris = r.U32();
            if (verts > 65535 || tris > 65535)
                r.bad = true;
            r.Take(verts * 32 + tris * 6);
            vertTotal += verts;
            triTotal += tris;
        }
        surfTotal += surfs;
    }
    if (r.bad || tooManySurfs || !lookOf)
    {
        Com_PrintWarning(8, "mapkit: mesh edit %d: %s is truncated, corrupt or has a chunk of more than %d surfaces\n",
            edit, file, MAX_SURFS_PER_MODEL);
        free(lookOf);
        FS_FreeFile(buf);
        return;
    }
    s_mesh.chunkCount = (int)chunkCount;
    s_mesh.surfCount = (int)surfTotal;
    s_mesh.models = (XModel *)calloc(chunkCount ? chunkCount : 1, sizeof(XModel));
    s_mesh.placements = (GfxScaledPlacement *)calloc(chunkCount ? chunkCount : 1, sizeof(GfxScaledPlacement));
    s_mesh.lighting = (unsigned short *)calloc(chunkCount ? chunkCount : 1, sizeof(unsigned short));
    s_mesh.names = (char (*)[24])calloc(chunkCount ? chunkCount : 1, 24);
    s_mesh.surfs = (XSurface *)calloc(surfTotal ? surfTotal : 1, sizeof(XSurface));
    s_mesh.vertLists = (XRigidVertList *)calloc(surfTotal ? surfTotal : 1, sizeof(XRigidVertList));
    s_mesh.materials = (Material **)calloc(surfTotal ? surfTotal : 1, sizeof(Material *));
    s_mesh.verts = (GfxPackedVertex *)malloc(sizeof(GfxPackedVertex) * (vertTotal ? vertTotal : 1));
    s_mesh.indices = (unsigned short *)malloc(sizeof(unsigned short) * 3 * (triTotal ? triTotal : 1));
    if (!s_mesh.models || !s_mesh.placements || !s_mesh.lighting || !s_mesh.names || !s_mesh.surfs || !s_mesh.vertLists
        || !s_mesh.materials || !s_mesh.verts || !s_mesh.indices)
    {
        Com_PrintWarning(8, "mapkit: mesh edit %d: out of memory for %u vertices\n", edit, vertTotal);
        Mesh_FreeModels();
        free(lookOf);
        FS_FreeFile(buf);
        return;
    }

    r.at = chunkStart;
    r.U32();
    unsigned int surfUsed = 0, vertUsed = 0, triUsed = 0;
    int untextured = 0;
    for (unsigned int c = 0; c < chunkCount; ++c)
    {
        float head[4];
        memcpy(head, r.Take(16), 16);
        const unsigned int surfs = r.U32();
        XModel *model = &s_mesh.models[c];
        Com_sprintf(s_mesh.names[c], 24, "mapkit_mesh_%u", c);
        model->name = s_mesh.names[c];
        model->numBones = 1;
        model->numRootBones = 1;
        model->boneNames = &s_boneName;
        model->parentList = &s_parentList;
        model->baseMat = &s_identityBone;
        model->boneInfo = &s_boneInfo;
        model->numsurfs = (unsigned char)surfs;
        model->surfs = &s_mesh.surfs[surfUsed];
        model->materialHandles = &s_mesh.materials[surfUsed];
        model->lodInfo[0].dist = 0.0f; // 0: this LOD at every distance (XModelGetLodForDist_Internal)
        model->lodInfo[0].numsurfs = (unsigned short)surfs;
        model->lodInfo[0].surfIndex = 0;
        model->numLods = 1;
        model->radius = head[3];
        for (int k = 0; k < 3; ++k)
        {
            model->mins[k] = -head[3];
            model->maxs[k] = head[3];
        }
        GfxScaledPlacement *placement = &s_mesh.placements[c];
        placement->base.quat[3] = 1.0f;
        placement->base.origin[0] = head[0];
        placement->base.origin[1] = head[1];
        placement->base.origin[2] = head[2];
        placement->scale = 1.0f;
        for (unsigned int s = 0; s < surfs; ++s)
        {
            unsigned short materialIndex = 0;
            memcpy(&materialIndex, r.Take(4), 2);
            const unsigned int verts = r.U32(), tris = r.U32();
            XSurface *surf = &s_mesh.surfs[surfUsed];
            XRigidVertList *list = &s_mesh.vertLists[surfUsed];
            const int look = materialIndex < materialCount ? lookOf[materialIndex] : -1;
            Material *material = look >= 0 ? s_looks[look]->material : 0;
            if (!material)
            {
                material = base;
                ++untextured;
            }
            s_mesh.materials[surfUsed] = material;
            const float *src = (const float *)r.Take(verts * 32);
            for (unsigned int v = 0; v < verts; ++v)
            {
                float p[8];
                memcpy(p, src + v * 8, sizeof(p)); // the file is not aligned for floats
                Mesh_PackVert(&s_mesh.verts[vertUsed + v], p);
            }
            memcpy(&s_mesh.indices[triUsed * 3], r.Take(tris * 6), tris * 6);
            surf->vertCount = (unsigned short)verts;
            surf->triCount = (unsigned short)tris;
            surf->triIndices = &s_mesh.indices[triUsed * 3];
            surf->verts0 = &s_mesh.verts[vertUsed];
            surf->vertListCount = 1;
            surf->vertList = list;
            list->vertCount = (unsigned short)verts;
            list->triCount = (unsigned short)tris;
            ++surfUsed;
            vertUsed += verts;
            triUsed += tris;
        }
    }
    free(lookOf);
    FS_FreeFile(buf);

    s_mesh.device = dx.device;
    Mesh_OnRenderThread(Mesh_CreateBuffers);
    int noBuffers = 0;
    for (int s = 0; s < s_mesh.surfCount; ++s)
        noBuffers += !s_mesh.surfs[s].vb0 || !s_mesh.surfs[s].indexBuffer;
    s_mesh.ready = noBuffers == 0;
    Com_Printf(8, "mapkit: mesh edit %d: %s: %d chunks, %d surfaces, %u vertices, %u triangles; %d colour maps (%d new, "
        "%d missing), %d materials linked on %s, %d surfaces untextured, %d without GPU buffers\n", edit, file,
        s_mesh.chunkCount, s_mesh.surfCount, vertUsed, triUsed, s_lookCount, newImages, missingImages, linked, baseName,
        untextured, noBuffers);
}

void __cdecl R_Mapkit_AddMesh()
{
    if (!s_mesh.ready)
        return;
    if (s_mesh.device != dx.device)
    {
        // vid_restart: the buffers went with the old device; this mesh is not drawn until the next map load
        Com_PrintWarning(8, "mapkit: mesh dropped with the old graphics device (reload the map to draw it)\n");
        s_mesh.ready = false;
        return;
    }
    for (int c = 0; c < s_mesh.chunkCount; ++c)
        R_FilterXModelIntoScene(&s_mesh.models[c], &s_mesh.placements[c], 0, &s_mesh.lighting[c], 0.0f);
}
