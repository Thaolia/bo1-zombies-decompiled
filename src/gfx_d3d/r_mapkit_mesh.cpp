#include "r_mapkit_mesh.h"
#include <win32/win_shared.h>
#include <algorithm>
#include <initializer_list>
#include <unordered_map>
#include <utility>
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
#include <universal/com_math.h>
#include <universal/com_math_anglevectors.h>
#include <universal/com_pack.h>
#include <xanim/dobj.h>
#include <xanim/xmodel.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

// mod: mapkit "mesh" edit: {"kind":"mesh","file":"imports/<map>/<map>.mesh.bin","images":"imports/<map>/images",
// "base":"<mc/ material>","alphaBase":"<alpha-tested mc/ material>","blendBase":"<blended mc/ material>",
// "skyBase":"<skybox material>"}. The file
// (BO2MESH4/5, written by tools/bo2/bo2_mesh.py, which documents it) holds chunks of another game's world, its props and
// its sky: each chunk becomes a rigid XModel, one surface per material, each prop model an XModel with its LODs, drawn
// once per placement, and the sky model is drawn around the viewer (the empty base adds no skybox of its own). All are
// added every frame by R_FilterXModelIntoScene like the skybox model (none of them is in the asset database). Each
// colour map is an IW image loaded outside the database; the materials are copies of one BO1 model material ("mc/": the
// 32-byte packed vertex), chosen by the material's flags (opaque, alpha tested, blended, decal: blended with a depth
// offset, sky), with that colour map, a flat normal map and no specular, linked into the database so that the
// material sort (which only walks database materials in fastfile mode) gives them a sorted index. Those copies and their images live for
// the whole process: a material linked into the database may outlive the map. The models are rebuilt per map.

namespace
{
    const char MESH_MAGIC[8] = { 'B', 'O', '2', 'M', 'E', 'S', 'H', '4' };
    // BO2MESH5: every vertex also carries a u32 colour (0xAARRGGBB, BO2's baked light), 36 bytes instead of 32
    const char MESH_MAGIC_LIT[8] = { 'B', 'O', '2', 'M', 'E', 'S', 'H', '5' };
    int s_vertBytes = 32;
    const unsigned int MATERIAL_ALPHA = 1;
    const unsigned int MATERIAL_SKY = 2;
    const unsigned int MATERIAL_DECAL = 4; // blended over the surface it lies on: blendBase with a depth offset
    const unsigned int MATERIAL_BLEND = 8; // blended (glass)
    // GfxStateBits::loadBits[1]: the depth offset (R_ForceSetPolygonOffset: 0x10 x1 the decal bias, 0x30 shadow maps)
    const unsigned int STATE1_OFFSET_MASK = 0x30;
    const unsigned int STATE1_OFFSET_DECAL = 0x10;
    // MEASURED on the Five base: mc/mtl_veh_jet_f4_decals, its blended decal with a depth offset, sorts at 29 (between
    // the opaque materials and glass, 43)
    const unsigned char DECAL_SORT_KEY = 29;
    const int NAME_BYTES = 64;
    const int MAX_SURFS_PER_LOD = 128; // R_SkinXModel's stack array
    const int MAX_LODS = 4;            // XModel::lodInfo
    const int MAX_SURFS_PER_MODEL = 255; // XModel::numsurfs
    const float CULL_MARGIN = 1.5f;    // past its last LOD's distance (times this, for the LOD scale dvars) a prop is not added

    // one colour map: its image and the material made with it (both for the process' lifetime)
    struct MkMeshLook
    {
        char image[NAME_BYTES];
        char base[NAME_BYTES];
        bool decal; // the base with STATE1_OFFSET_DECAL and DECAL_SORT_KEY
        GfxStateBits *states; // a decal's copy of its base's state table
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

    struct MkMeshOrder
    {
        float gap; // distance from the viewer to the prop's bounding sphere
        int index;
    };

    struct MkMesh
    {
        int chunkCount; // models[0, chunkCount) are the world's chunks, drawn at placements[] with lighting[]
        int modelCount; // models[chunkCount, modelCount) are the props' models
        XModel *models;
        GfxScaledPlacement *placements;
        unsigned short *lighting;
        int instanceCount;
        int *instanceModel;
        GfxScaledPlacement *instancePlacements;
        unsigned short *instanceLighting;
        float *instanceCullSq; // 0: always added
        int *instanceEdit;     // the door or debris edit whose purchase removes this prop, -1: none
        MkMeshOrder *instanceOrder; // R_Mapkit_AddMesh's per-frame sort
        int skyModel;          // -1: none
        // "moverModel" drawn at the brush model of the "ent" edit named "moverEntity": its origin is the model's point
        // "moverOffset" (model space), so the model sits at pose origin - axes * offset. The brush model spawns with
        // angles 0 whatever way the vehicle faces: "moverYaw" is that heading (TranZit's bus leaves the depot at 179.5
        // degrees, and was drawn driving backwards)
        int moverModel;        // -1: none
        int moverBModel;
        float moverOffset[3];
        float moverYaw;
        bool moverPosed;
        float moverOrigin[3];
        float moverAngles[3];
        char (*modelNames)[NAME_BYTES];
        int surfCount;
        XSurface *surfs;
        XRigidVertList *vertLists;
        Material **materials;
        GfxPackedVertex *verts;
        unsigned short *indices;
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

    int Mesh_FindLook(const char *image, const char *base, bool decal)
    {
        for (int i = 0; i < s_lookCount; ++i)
        {
            if (!strcmp(s_looks[i]->image, image) && !strcmp(s_looks[i]->base, base) && s_looks[i]->decal == decal)
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
        for (int m = 0; m < s_mesh.modelCount; ++m)
            _XModelOptimize(&s_mesh.models[m]);
    }

    void Mesh_ReleaseBuffers(void *)
    {
        for (int s = 0; s < s_mesh.surfCount; ++s)
            XSurfaceReleaseResources(&s_mesh.surfs[s]);
    }

    int Mesh_SurfacesWithoutBuffers()
    {
        int count = 0;
        for (int s = 0; s < s_mesh.surfCount; ++s)
            count += !s_mesh.surfs[s].vb0 || !s_mesh.surfs[s].indexBuffer;
        return count;
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
            // flags: 2 no mipmaps, 4 a cube map (the sky)
            ok = !memcmp(header.tag, "IWi", 3) && (header.format == 0x0B || header.format == 0x0C || header.format == 0x0D)
                && header.dimensions[0] > 0 && header.dimensions[1] > 0 && header.dimensions[2] == 1
                && header.fileSizeForPicmip[0] == len && !(header.flags & ~6);
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
        if (look->decal)
        {
            if (!look->states)
            {
                look->states = (GfxStateBits *)calloc(base->stateBitsCount ? base->stateBitsCount : 1, sizeof(GfxStateBits));
                if (!look->states)
                    return 0;
                memcpy(look->states, base->stateBitsTable, sizeof(GfxStateBits) * base->stateBitsCount);
                // BO2 draws a decal over its surface at the decal depth offset; the passes that have their own offset
                // (shadow maps) keep it
                for (int k = 0; k < base->stateBitsCount; ++k)
                {
                    if (!(look->states[k].loadBits[1] & STATE1_OFFSET_MASK))
                        look->states[k].loadBits[1] |= STATE1_OFFSET_DECAL;
                }
            }
            copy.stateBitsTable = look->states;
            copy.info.sortKey = DECAL_SORT_KEY;
        }
        XAssetHeader header;
        header.material = &copy;
        look->material = DB_AddXAsset(ASSET_TYPE_MATERIAL, header).material;
        return look->material;
    }

    // Vec3PackUnitVec tries 256 scales per vector: TranZit's 1.5 million vertices took 31 s to pack. Many share a
    // normal (flat world surfaces, LODs): each distinct normal is packed once, with its tangent, which follows from it.
    struct MkNormalKey
    {
        unsigned int bits[3];
        bool operator==(const MkNormalKey &o) const
        {
            return bits[0] == o.bits[0] && bits[1] == o.bits[1] && bits[2] == o.bits[2];
        }
    };
    struct MkNormalHash
    {
        size_t operator()(const MkNormalKey &k) const
        {
            return (size_t)(k.bits[0] * 0x9E3779B1u ^ k.bits[1] * 0x85EBCA77u ^ k.bits[2] * 0xC2B2AE3Du);
        }
    };
    std::unordered_map<MkNormalKey, std::pair<PackedUnitVec, PackedUnitVec>, MkNormalHash> s_packedNormals;

    void Mesh_PackVert(GfxPackedVertex *out, const float *p, unsigned int colour)
    {
        // p: position (3), normal (3), uv (2), as bo2_mesh.py writes them; the model shaders multiply the colour map
        // by the vertex colour, which carries the baked light of a BO2MESH5 file (white otherwise)
        out->xyz[0] = p[0];
        out->xyz[1] = p[1];
        out->xyz[2] = p[2];
        out->binormalSign = 1.0f;
        out->color.packed = colour;
        float n[3] = { p[3], p[4], p[5] };
        MkNormalKey key;
        memcpy(key.bits, n, sizeof(key.bits));
        const auto known = s_packedNormals.find(key);
        if (known != s_packedNormals.end())
        {
            out->normal = known->second.first;
            out->tangent = known->second.second;
            out->texCoord = Vec2PackTexCoords(p + 6);
            return;
        }
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
        s_packedNormals.emplace(key, std::make_pair(out->normal, out->tangent));
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
        free(s_mesh.instanceModel);
        free(s_mesh.instancePlacements);
        free(s_mesh.instanceLighting);
        free(s_mesh.instanceCullSq);
        free(s_mesh.instanceEdit);
        free(s_mesh.instanceOrder);
        free(s_mesh.modelNames);
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


namespace
{
    struct MeshTotals
    {
        unsigned int surfs, verts, tris;
    };

    // surfaces as the tool writes them (u16 material, u16 pad, u32 verts, u32 tris, data): sizes only
    bool Mesh_ScanSurfaces(Reader &r, unsigned int count, MeshTotals *totals)
    {
        for (unsigned int s = 0; s < count && !r.bad; ++s)
        {
            r.Take(4);
            const unsigned int verts = r.U32(), tris = r.U32();
            if (verts > 65535 || tris > 65535)
                r.bad = true;
            r.Take(verts * s_vertBytes + tris * 6);
            totals->verts += verts;
            totals->tris += tris;
        }
        totals->surfs += count;
        return !r.bad;
    }

    struct MeshFill
    {
        unsigned int surfUsed, vertUsed, triUsed;
        const int *lookOf;
        const unsigned int *materialFlags;
        unsigned int materialCount;
        Material *base, *alphaBase, *blendBase, *skyBase;
        int untextured;
    };

    Material *Mesh_BaseFor(const MeshFill *fill, unsigned int flags)
    {
        return (flags & MATERIAL_SKY) ? fill->skyBase : (flags & (MATERIAL_DECAL | MATERIAL_BLEND)) ? fill->blendBase
            : (flags & MATERIAL_ALPHA) ? fill->alphaBase : fill->base;
    }

    // the database keys a material by name: one prefix per base
    const char *Mesh_LookPrefix(unsigned int flags)
    {
        return (flags & MATERIAL_SKY) ? "mapkit_sky" : (flags & MATERIAL_DECAL) ? "mapkit_decal"
            : (flags & MATERIAL_BLEND) ? "mapkit_blend" : (flags & MATERIAL_ALPHA) ? "mapkit_alpha" : "mapkit";
    }

    void Mesh_ReadSurfaces(Reader &r, unsigned int count, MeshFill *fill)
    {
        for (unsigned int s = 0; s < count; ++s)
        {
            unsigned short materialIndex = 0;
            memcpy(&materialIndex, r.Take(4), 2);
            const unsigned int verts = r.U32(), tris = r.U32();
            XSurface *surf = &s_mesh.surfs[fill->surfUsed];
            XRigidVertList *list = &s_mesh.vertLists[fill->surfUsed];
            const bool known = materialIndex < fill->materialCount;
            const int look = known ? fill->lookOf[materialIndex] : -1;
            Material *material = look >= 0 ? s_looks[look]->material : 0;
            if (!material)
            {
                material = Mesh_BaseFor(fill, known ? fill->materialFlags[materialIndex] : 0);
                ++fill->untextured;
            }
            s_mesh.materials[fill->surfUsed] = material;
            const unsigned char *src = r.Take(verts * s_vertBytes);
            for (unsigned int v = 0; v < verts; ++v)
            {
                float p[8];
                unsigned int colour = 0xFFFFFFFFu;
                memcpy(p, src + v * s_vertBytes, sizeof(p)); // the file is not aligned for floats
                if (s_vertBytes > 32)
                    memcpy(&colour, src + v * s_vertBytes + 32, 4);
                Mesh_PackVert(&s_mesh.verts[fill->vertUsed + v], p, colour);
            }
            memcpy(&s_mesh.indices[fill->triUsed * 3], r.Take(tris * 6), tris * 6);
            surf->vertCount = (unsigned short)verts;
            surf->triCount = (unsigned short)tris;
            surf->triIndices = &s_mesh.indices[fill->triUsed * 3];
            surf->verts0 = &s_mesh.verts[fill->vertUsed];
            surf->vertListCount = 1;
            surf->vertList = list;
            list->vertCount = (unsigned short)verts;
            list->triCount = (unsigned short)tris;
            ++fill->surfUsed;
            fill->vertUsed += verts;
            fill->triUsed += tris;
        }
    }

    // a rigid one-bone model whose surfaces start at the next free surface
    void Mesh_InitModel(XModel *model, const char *name, unsigned int firstSurf, float radius)
    {
        model->name = name;
        model->numBones = 1;
        model->numRootBones = 1;
        model->boneNames = &s_boneName;
        model->parentList = &s_parentList;
        model->baseMat = &s_identityBone;
        model->boneInfo = &s_boneInfo;
        model->surfs = &s_mesh.surfs[firstSurf];
        model->materialHandles = &s_mesh.materials[firstSurf];
        model->radius = radius;
        for (int k = 0; k < 3; ++k)
        {
            model->mins[k] = -radius;
            model->maxs[k] = radius;
        }
    }

    Material *Mesh_BaseMaterial(int edit, const char *key, const char *fallback)
    {
        const char *name = 0;
        float unused[3];
        if (CM_Mapkit_GetField(edit, key, &name, unused) != MKF_STRING)
            name = fallback;
        Material *material = Material_RegisterHandle(name, 0);
        if (!material || material == rgp.defaultMaterial || !material->textureTable)
        {
            Com_PrintWarning(8, "mapkit: mesh edit %d: %s material '%s' is not loaded\n", edit, key, name);
            return 0;
        }
        return material;
    }
}

void __cdecl R_Mapkit_LoadMesh()
{
    const unsigned int start = Sys_Milliseconds();
    struct Report
    {
        unsigned int start;
        ~Report()
        {
            if (s_mesh.ready)
                Com_Printf(8, "mapkit: mesh loaded in %u ms\n", Sys_Milliseconds() - start);
        }
    } report{start};
    R_Mapkit_FreeMesh();
    if (!r_loadForRenderer->current.enabled)
        return;
    const char *file = 0, *imageDir = 0;
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
    Material *base = Mesh_BaseMaterial(edit, "base", "mc/rus_metal_panel03");
    if (!base)
        return;
    // MEASURED on the Five base: a static (not wind-animated) leaf, alpha tested
    Material *alphaBase = Mesh_BaseMaterial(edit, "alphaBase", "mc/t5_foliage_tree_sorbus_leaf01_static");
    if (!alphaBase)
        alphaBase = base;
    // MEASURED on the Five base: glass, blended (premultiplied: one / invsrcalpha), alpha tested above 0, no depth
    // write, camera region 1, sort key 43, technique set mc_l_sm_b0c0n0s0 (colour, normal and specular maps). The
    // decals are copies of it with a depth offset: Five's own model decals either test alpha or carry samplers of
    // their own (mc/mtl_veh_jet_f4_decals: technique set mc_l_sm_b0c0s0sc0x0)
    Material *blendBase = Mesh_BaseMaterial(edit, "blendBase", "mc/global_glass");
    // MEASURED on the Five base: the frontend skybox's material (technique skycubemaphdr: a cube map by view direction)
    Material *skyBase = Mesh_BaseMaterial(edit, "skyBase", "mc/mtl_skybox_sp_frontend");
    if (skyBase)
    {
        // what the sky's copy inherits: its camera region (3 is never drawn) and its samplers (the cube map's name)
        const unsigned int colorMap = R_HashString("colorMap");
        int colorMaps = 0;
        for (int t = 0; t < skyBase->textureCount; ++t)
            colorMaps += skyBase->textureTable[t].nameHash == colorMap;
        Com_Printf(8, "mapkit: mesh sky base %s: camera region %d, sort key %d, %d textures (%d named colorMap, first "
            "hash 0x%x)\n", skyBase->info.name, skyBase->cameraRegion, skyBase->info.sortKey, skyBase->textureCount,
            colorMaps, skyBase->textureCount ? skyBase->textureTable[0].nameHash : 0);
    }
    MeshFill fill = {};
    fill.base = base;
    fill.alphaBase = alphaBase;
    fill.blendBase = blendBase ? blendBase : alphaBase;
    fill.skyBase = skyBase ? skyBase : base;

    void *buf = 0;
    const int len = FS_ReadFile(file, &buf);
    const unsigned int readAt = Sys_Milliseconds();
    if (len <= 0 || !buf)
    {
        Com_PrintWarning(8, "mapkit: mesh edit %d: cannot read %s\n", edit, file);
        return;
    }
    Reader r = { (const unsigned char *)buf, len, 0, false };
    const unsigned char *magic = r.Take(8);
    s_vertBytes = magic && !memcmp(magic, MESH_MAGIC_LIT, 8) ? 36 : 32;
    if (!magic || (memcmp(magic, MESH_MAGIC, 8) && memcmp(magic, MESH_MAGIC_LIT, 8)))
    {
        Com_PrintWarning(8, "mapkit: mesh edit %d: %s is not a BO2MESH4/5 file (rerun tools/bo2/bo2_mesh.py)\n", edit, file);
        FS_FreeFile(buf);
        return;
    }

    // materials -> looks (one per colour map and base); a material without an image draws with its base's own map
    const unsigned int materialCount = r.Count(2 * NAME_BYTES + 4);
    int *lookOf = (int *)calloc(materialCount ? materialCount : 1, sizeof(int));
    unsigned int *materialFlags = (unsigned int *)calloc(materialCount ? materialCount : 1, sizeof(unsigned int));
    int newImages = 0, missingImages = 0;
    for (unsigned int m = 0; m < materialCount && lookOf && materialFlags; ++m)
    {
        r.Take(NAME_BYTES); // the source material's name: for the tool's report only
        char image[NAME_BYTES];
        memcpy(image, r.Take(NAME_BYTES), NAME_BYTES);
        image[NAME_BYTES - 1] = 0;
        materialFlags[m] = r.U32();
        lookOf[m] = -1;
        if (!image[0])
            continue;
        const char *baseName = Mesh_BaseFor(&fill, materialFlags[m])->info.name;
        // a decal needs its depth offset from a blended base (without one it draws alpha tested, as before)
        const bool decal = (materialFlags[m] & MATERIAL_DECAL) && blendBase;
        int look = Mesh_FindLook(image, baseName, decal);
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
            l->decal = decal;
            Com_sprintf(l->materialName, sizeof(l->materialName), "%s/%s", Mesh_LookPrefix(materialFlags[m]), image);
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
    const unsigned int imagesAt = Sys_Milliseconds();
    int linked = 0;
    for (int i = 0; i < s_lookCount; ++i)
    {
        MkMeshLook *look = s_looks[i];
        Material *lookBase = 0;
        for (Material *candidate : { base, alphaBase, fill.blendBase, fill.skyBase })
        {
            if (!lookBase && !strcmp(look->base, candidate->info.name))
                lookBase = candidate;
        }
        if (look->uploaded && lookBase && Mesh_LinkMaterial(look, lookBase))
            ++linked;
    }
    Material_DirtySort();

    // sizes first (one pass over the file), then the arrays, then the data
    const int chunkStart = r.at;
    MeshTotals totals = {};
    const unsigned int chunkCount = r.Count(20);
    bool tooManySurfs = false;
    for (unsigned int c = 0; c < chunkCount && !r.bad; ++c)
    {
        r.Take(16);
        const unsigned int surfs = r.Count(12);
        tooManySurfs |= surfs > MAX_SURFS_PER_LOD;
        Mesh_ScanSurfaces(r, surfs, &totals);
    }
    const unsigned int modelCount = r.Count(NAME_BYTES + 8);
    for (unsigned int m = 0; m < modelCount && !r.bad; ++m)
    {
        r.Take(NAME_BYTES + 4);
        const unsigned int lods = r.U32();
        if (lods < 1 || lods > MAX_LODS)
            r.bad = true;
        unsigned int modelSurfs = 0;
        for (unsigned int l = 0; l < lods && !r.bad; ++l)
        {
            r.Take(4);
            const unsigned int surfs = r.Count(12);
            tooManySurfs |= surfs > MAX_SURFS_PER_LOD;
            modelSurfs += surfs;
            Mesh_ScanSurfaces(r, surfs, &totals);
        }
        tooManySurfs |= modelSurfs > MAX_SURFS_PER_MODEL;
    }
    const unsigned int instanceCount = r.Count(56);
    r.Take(instanceCount * 56);
    const int skyIndex = (int)r.U32();
    if (skyIndex < -1 || skyIndex >= (int)modelCount)
        r.bad = true;
    if (r.bad || tooManySurfs || !lookOf || !materialFlags)
    {
        Com_PrintWarning(8, "mapkit: mesh edit %d: %s is truncated, corrupt or has a model of too many surfaces\n", edit,
            file);
        free(lookOf);
        free(materialFlags);
        FS_FreeFile(buf);
        return;
    }
    const unsigned int models = chunkCount + modelCount;
    s_mesh.chunkCount = (int)chunkCount;
    s_mesh.modelCount = (int)models;
    s_mesh.instanceCount = (int)instanceCount;
    s_mesh.surfCount = (int)totals.surfs;
    s_mesh.models = (XModel *)calloc(models ? models : 1, sizeof(XModel));
    s_mesh.modelNames = (char (*)[NAME_BYTES])calloc(models ? models : 1, NAME_BYTES);
    s_mesh.placements = (GfxScaledPlacement *)calloc(chunkCount ? chunkCount : 1, sizeof(GfxScaledPlacement));
    s_mesh.lighting = (unsigned short *)calloc(chunkCount ? chunkCount : 1, sizeof(unsigned short));
    s_mesh.instanceModel = (int *)calloc(instanceCount ? instanceCount : 1, sizeof(int));
    s_mesh.instancePlacements = (GfxScaledPlacement *)calloc(instanceCount ? instanceCount : 1, sizeof(GfxScaledPlacement));
    s_mesh.instanceLighting = (unsigned short *)calloc(instanceCount ? instanceCount : 1, sizeof(unsigned short));
    s_mesh.instanceCullSq = (float *)calloc(instanceCount ? instanceCount : 1, sizeof(float));
    s_mesh.instanceEdit = (int *)malloc(sizeof(int) * (instanceCount ? instanceCount : 1));
    s_mesh.instanceOrder = (MkMeshOrder *)malloc(sizeof(MkMeshOrder) * (instanceCount ? instanceCount : 1));
    s_mesh.surfs = (XSurface *)calloc(totals.surfs ? totals.surfs : 1, sizeof(XSurface));
    s_mesh.vertLists = (XRigidVertList *)calloc(totals.surfs ? totals.surfs : 1, sizeof(XRigidVertList));
    s_mesh.materials = (Material **)calloc(totals.surfs ? totals.surfs : 1, sizeof(Material *));
    s_mesh.verts = (GfxPackedVertex *)malloc(sizeof(GfxPackedVertex) * (totals.verts ? totals.verts : 1));
    s_mesh.indices = (unsigned short *)malloc(sizeof(unsigned short) * 3 * (totals.tris ? totals.tris : 1));
    if (!s_mesh.models || !s_mesh.modelNames || !s_mesh.placements || !s_mesh.lighting || !s_mesh.instanceModel
        || !s_mesh.instancePlacements || !s_mesh.instanceLighting || !s_mesh.instanceCullSq || !s_mesh.instanceEdit || !s_mesh.instanceOrder || !s_mesh.surfs
        || !s_mesh.vertLists || !s_mesh.materials || !s_mesh.verts || !s_mesh.indices)
    {
        Com_PrintWarning(8, "mapkit: mesh edit %d: out of memory for %u vertices\n", edit, totals.verts);
        Mesh_FreeModels();
        free(lookOf);
        free(materialFlags);
        FS_FreeFile(buf);
        return;
    }

    fill.lookOf = lookOf;
    fill.materialFlags = materialFlags;
    fill.materialCount = materialCount;
    r.at = chunkStart;
    r.U32();
    for (unsigned int c = 0; c < chunkCount; ++c)
    {
        float head[4];
        memcpy(head, r.Take(16), 16);
        const unsigned int surfs = r.U32();
        XModel *model = &s_mesh.models[c];
        Com_sprintf(s_mesh.modelNames[c], NAME_BYTES, "mapkit_mesh_%u", c);
        Mesh_InitModel(model, s_mesh.modelNames[c], fill.surfUsed, head[3]);
        model->numsurfs = (unsigned char)surfs;
        model->lodInfo[0].dist = 0.0f; // 0: this LOD at every distance (XModelGetLodForDist_Internal)
        model->lodInfo[0].numsurfs = (unsigned short)surfs;
        model->numLods = 1;
        GfxScaledPlacement *placement = &s_mesh.placements[c];
        placement->base.quat[3] = 1.0f;
        memcpy(placement->base.origin, head, 12);
        placement->scale = 1.0f;
        Mesh_ReadSurfaces(r, surfs, &fill);
    }
    r.U32();
    float *lastLodDist = (float *)calloc(modelCount ? modelCount : 1, sizeof(float));
    bool *emptyModel = (bool *)calloc(modelCount ? modelCount : 1, sizeof(bool));
    for (unsigned int m = 0; m < modelCount; ++m)
    {
        XModel *model = &s_mesh.models[chunkCount + m];
        memcpy(s_mesh.modelNames[chunkCount + m], r.Take(NAME_BYTES), NAME_BYTES);
        s_mesh.modelNames[chunkCount + m][NAME_BYTES - 1] = 0;
        float radius = 0.0f;
        memcpy(&radius, r.Take(4), 4);
        const unsigned int lods = r.U32();
        const unsigned int firstSurf = fill.surfUsed;
        Mesh_InitModel(model, s_mesh.modelNames[chunkCount + m], firstSurf, radius);
        model->numLods = (short)lods;
        for (unsigned int l = 0; l < lods; ++l)
        {
            float dist = 0.0f;
            memcpy(&dist, r.Take(4), 4);
            const unsigned int surfs = r.U32();
            model->lodInfo[l].dist = dist;
            model->lodInfo[l].numsurfs = (unsigned short)surfs;
            model->lodInfo[l].surfIndex = (unsigned short)(fill.surfUsed - firstSurf);
            model->lodInfo[l].lod = (unsigned char)l;
            Mesh_ReadSurfaces(r, surfs, &fill);
            if (lastLodDist)
                lastLodDist[m] = dist;
            // a LOD without surfaces asserts in XModelGetSurfaces: such a model is never added
            if (!surfs && emptyModel)
                emptyModel[m] = true;
        }
        model->numsurfs = (unsigned char)(fill.surfUsed - firstSurf);
    }
    r.U32();
    int badInstances = 0;
    for (unsigned int i = 0; i < instanceCount; ++i)
    {
        unsigned int model = 0;
        float data[13];
        memcpy(&model, r.Take(4), 4);
        memcpy(data, r.Take(52), 52);
        if (model >= modelCount || data[12] <= 0.0f || !emptyModel || emptyModel[model])
        {
            s_mesh.instanceModel[i] = -1;
            ++badInstances;
            continue;
        }
        s_mesh.instanceModel[i] = (int)(chunkCount + model);
        GfxScaledPlacement *placement = &s_mesh.instancePlacements[i];
        memcpy(placement->base.origin, data, 12);
        const float axes[3][3] = { { data[3], data[4], data[5] }, { data[6], data[7], data[8] },
            { data[9], data[10], data[11] } };
        AxisToQuat(axes, placement->base.quat);
        placement->scale = data[12];
        const float cull = lastLodDist ? lastLodDist[model] * data[12] * CULL_MARGIN : 0.0f;
        s_mesh.instanceCullSq[i] = cull * cull;
    }
    s_mesh.skyModel = skyIndex >= 0 && emptyModel && !emptyModel[skyIndex] ? (int)chunkCount + skyIndex : -1;
    // a model drawn with a moving layout brush model (BO2's TranZit bus: bo2_mesh.py --mover, bo2_layout.py)
    s_mesh.moverModel = -1;
    s_mesh.moverPosed = false;
    const char *moverName = 0, *moverEntity = 0;
    if (CM_Mapkit_GetField(edit, "moverModel", &moverName, unused) == MKF_STRING
        && CM_Mapkit_GetField(edit, "moverEntity", &moverEntity, unused) == MKF_STRING)
    {
        for (unsigned int m = 0; m < modelCount && s_mesh.moverModel < 0; ++m)
            if (!strcmp(s_mesh.modelNames[chunkCount + m], moverName) && emptyModel && !emptyModel[m])
                s_mesh.moverModel = (int)(chunkCount + m);
        s_mesh.moverBModel = CM_Mapkit_EntBModel(moverEntity);
        const char *ignored = 0;
        if (CM_Mapkit_GetField(edit, "moverOffset", &ignored, s_mesh.moverOffset) != MKF_VECTOR)
            memset(s_mesh.moverOffset, 0, sizeof(s_mesh.moverOffset));
        float yaw[3] = {};
        s_mesh.moverYaw = CM_Mapkit_GetField(edit, "moverYaw", &ignored, yaw) == MKF_NUMBER ? yaw[0] : 0.0f;
        Com_Printf(8, "mapkit: mesh edit %d: mover %s on %s (brush model %d)%s\n", edit, moverName, moverEntity,
            s_mesh.moverBModel, s_mesh.moverModel < 0 || s_mesh.moverBModel < 0 ? ": not found, not drawn" : "");
        if (s_mesh.moverBModel < 0)
            s_mesh.moverModel = -1;
    }
    // Optional after the sky index: named groups of props that a door or debris edit removes when bought (another
    // game's doors and furniture: BO2's blockers are script models, which bo2_mesh.py writes as props). A group
    // binds to the edit whose "meshGroup" names it.
    for (unsigned int i = 0; i < instanceCount; ++i)
        s_mesh.instanceEdit[i] = -1;
    r.U32();
    int grouped = 0;
    if (!r.bad && r.at < r.size)
    {
        const unsigned int groupCount = r.Count(NAME_BYTES + 4);
        for (unsigned int g = 0; g < groupCount && !r.bad; ++g)
        {
            const unsigned char *raw = r.Take(NAME_BYTES);
            if (!raw)
                break;
            char name[NAME_BYTES];
            memcpy(name, raw, NAME_BYTES);
            name[NAME_BYTES - 1] = 0;
            const unsigned int count = r.Count(4);
            int owner = -1;
            for (int e = 0; e < CM_Mapkit_EditCount() && owner < 0; ++e)
            {
                const char *group = 0;
                if (CM_Mapkit_GetField(e, "meshGroup", &group, unused) == MKF_STRING && !strcmp(group, name))
                    owner = e;
            }
            for (unsigned int k = 0; k < count && !r.bad; ++k)
            {
                const unsigned int index = r.U32();
                if (index < instanceCount && owner >= 0)
                {
                    s_mesh.instanceEdit[index] = owner;
                    ++grouped;
                }
            }
            if (owner < 0)
                Com_PrintWarning(8, "mapkit: mesh edit %d: group %s has no door or debris edit\n", edit, name);
        }
    }
    free(lastLodDist);
    free(emptyModel);
    free(lookOf);
    free(materialFlags);
    FS_FreeFile(buf);

    const unsigned int builtAt = Sys_Milliseconds();
    const size_t distinctNormals = s_packedNormals.size();
    s_packedNormals = {};
    s_mesh.device = dx.device;
    Mesh_OnRenderThread(Mesh_CreateBuffers);
    Com_Printf(8, "mapkit: mesh load times: file read %u ms, images %u ms, models built %u ms (%u distinct normals), GPU "
        "buffers %u ms\n", readAt - start, imagesAt - readAt, builtAt - imagesAt, (unsigned int)distinctNormals,
        Sys_Milliseconds() - builtAt);
    const int noBuffers = Mesh_SurfacesWithoutBuffers();
    s_mesh.ready = noBuffers == 0;
    Com_Printf(8, "mapkit: mesh edit %d: %s: %u chunks, %u prop models, %u props (%d bad), %d surfaces, %u vertices, %u "
        "triangles; %d colour maps (%d new, %d missing), %d materials linked on %s, %s and %s, %d surfaces "
        "untextured, %d without GPU buffers; sky %s; %d props removed by a door or debris\n", edit, file, chunkCount,
        modelCount, instanceCount, badInstances, s_mesh.surfCount, fill.vertUsed, fill.triUsed, s_lookCount, newImages,
        missingImages, linked, base->info.name, alphaBase->info.name, fill.blendBase->info.name, fill.untextured,
        noBuffers,
        s_mesh.skyModel >= 0 ? s_mesh.models[s_mesh.skyModel].name : "none", grouped);
}

// The buffers are D3DPOOL_DEFAULT ones (R_AllocStaticVertexBuffer): a device Reset (vid_restart with a new mode, a
// full-screen device lost to alt-tab) fails while one is alive. R_ReleaseLostDeviceAssets2 releases the database's
// models and R_RecoverLostDevice rebuilds them; these models are outside the database, so both call here.
void __cdecl R_Mapkit_ReleaseMeshBuffers()
{
    if (!s_mesh.models || s_mesh.device != dx.device)
        return;
    s_mesh.ready = false;
    Mesh_OnRenderThread(Mesh_ReleaseBuffers);
}

void __cdecl R_Mapkit_RestoreMeshBuffers()
{
    if (!s_mesh.models || s_mesh.device != dx.device || s_mesh.ready)
        return;
    Mesh_OnRenderThread(Mesh_CreateBuffers);
    const int noBuffers = Mesh_SurfacesWithoutBuffers();
    s_mesh.ready = noBuffers == 0;
    Com_Printf(8, "mapkit: mesh buffers rebuilt on the reset device (%d surfaces without)\n", noBuffers);
}

void __cdecl R_Mapkit_SetMoverPose(int brushModel, const float *origin, const float *angles)
{
    if (!s_mesh.ready || s_mesh.moverModel < 0 || brushModel != s_mesh.moverBModel)
        return;
    memcpy(s_mesh.moverOrigin, origin, sizeof(s_mesh.moverOrigin));
    memcpy(s_mesh.moverAngles, angles, sizeof(s_mesh.moverAngles));
    s_mesh.moverPosed = true;
}

void __cdecl R_Mapkit_AddMesh(const float *viewOrigin)
{
    if (!s_mesh.ready)
        return;
    if (s_mesh.device != dx.device)
    {
        // a new device (the renderer restarted without a map reload): the buffers went with the old one
        Com_PrintWarning(8, "mapkit: mesh dropped with the old graphics device (reload the map to draw it)\n");
        s_mesh.ready = false;
        return;
    }
    if (s_mesh.skyModel >= 0)
    {
        // centred on the viewer, as R_AddSkyboxModel places a map's own skybox
        static unsigned short skyLighting;
        GfxScaledPlacement placement = {};
        placement.base.quat[3] = 1.0f;
        memcpy(placement.base.origin, viewOrigin, 12);
        placement.scale = 1.0f;
        R_FilterXModelIntoScene(&s_mesh.models[s_mesh.skyModel], &placement, 0, &skyLighting, 0.0f);
    }
    for (int c = 0; c < s_mesh.chunkCount; ++c)
        R_FilterXModelIntoScene(&s_mesh.models[c], &s_mesh.placements[c], 0, &s_mesh.lighting[c], 0.0f);
    if (s_mesh.moverModel >= 0 && s_mesh.moverPosed)
    {
        static unsigned short moverLighting;
        float axis[3][3];
        const float angles[3] = { s_mesh.moverAngles[0], s_mesh.moverAngles[1] + s_mesh.moverYaw, s_mesh.moverAngles[2] };
        AnglesToAxis(angles, axis);
        GfxScaledPlacement placement = {};
        for (int k = 0; k < 3; ++k)
            placement.base.origin[k] = s_mesh.moverOrigin[k] - axis[0][k] * s_mesh.moverOffset[0]
                - axis[1][k] * s_mesh.moverOffset[1] - axis[2][k] * s_mesh.moverOffset[2];
        AxisToQuat(axis, placement.base.quat);
        placement.scale = 1.0f;
        R_FilterXModelIntoScene(&s_mesh.models[s_mesh.moverModel], &placement, 0, &moverLighting, 0.0f);
    }
    // The scene holds 1024 models a frame (GfxScene::sceneModel, r_modelLimit; up to 4096 with r_mod_sceneModels) and
    // Nuketown has 2096 props: past the limit R_AllocSceneModel drops the rest. The props go nearest first, by the
    // distance to their bounding sphere, so the dropped ones are the farthest. The sphere, not the origin: a prop's
    // origin can lie far from its geometry (Nuketown's clock tower: origin 1 000 units off, radius 1 378). A first
    // version only put the props within 1 024 units first, in file order: in front of a house the budget ran out before
    // the doors, written last.
    int candidates = 0;
    for (int i = 0; i < s_mesh.instanceCount; ++i)
    {
        const int model = s_mesh.instanceModel[i];
        if (model < 0 || !CM_Mapkit_EditShown(s_mesh.instanceEdit[i]))
            continue;
        const float *o = s_mesh.instancePlacements[i].base.origin;
        const float d[3] = { o[0] - viewOrigin[0], o[1] - viewOrigin[1], o[2] - viewOrigin[2] };
        const float distSq = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
        if (s_mesh.instanceCullSq[i] > 0.0f && distSq > s_mesh.instanceCullSq[i])
            continue;
        const float gap = sqrtf(distSq) - s_mesh.models[model].radius * s_mesh.instancePlacements[i].scale;
        s_mesh.instanceOrder[candidates].gap = gap > 0.0f ? gap : 0.0f;
        s_mesh.instanceOrder[candidates].index = i;
        ++candidates;
    }
    std::sort(s_mesh.instanceOrder, s_mesh.instanceOrder + candidates,
        [](const MkMeshOrder &a, const MkMeshOrder &b) { return a.gap < b.gap; });
    for (int c = 0; c < candidates; ++c)
    {
        const int i = s_mesh.instanceOrder[c].index;
        R_FilterXModelIntoScene(&s_mesh.models[s_mesh.instanceModel[i]], &s_mesh.instancePlacements[i], 0,
            &s_mesh.instanceLighting[i], 0.0f);
    }
}
