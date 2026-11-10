#include "ClipMapCollisionDumperT6.h"

#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <ostream>
#include <vector>

using namespace T6;

// <map>.d3dbsp.collision.bin, little-endian, written field by field (no struct padding):
//   char[8]  "BO2COL2\0"
//   u32 materialCount, then per material: char[64] name, i32 surfaceFlags, i32 contentFlags
//   u32 brushCount, then per brush: f32[3] mins, f32[3] maxs, i32 contents, i32 owner (0 = world, n = submodel n),
//       i32[2][3] axial_cflags, i32[2][3] axial_sflags, u32 sideCount, then per side: f32[3] normal, f32 dist,
//       i32 cflags, i32 sflags (the non-axial sides; the six axial ones are the box mins/maxs)
//   u32 vertCount, f32[3] per vertex
//   u32 triCount, then per triangle: u16[3] vertex indices, u16 material index (0xFFFF: no partition reached it),
//       u16 owner (0 = world, n = submodel n: in that model's own space)
//   u32 submodelCount, then per submodel: f32[3] mins, f32[3] maxs
// <map>.d3dbsp.smodelcoll.csv, the static models that collide (the clipMap's own list): model, contents, origin,
// invScaledAxis (each model axis divided by the model's scale), one row per model;
// and <map>.d3dbsp.collision.txt, a summary to read.

namespace
{
    class Writer
    {
    public:
        explicit Writer(std::ostream& stream)
            : m_stream(stream)
        {
        }

        void U16(const uint16_t v)
        {
            m_stream.write(reinterpret_cast<const char*>(&v), sizeof(v));
        }

        void U32(const uint32_t v)
        {
            m_stream.write(reinterpret_cast<const char*>(&v), sizeof(v));
        }

        void I32(const int32_t v)
        {
            m_stream.write(reinterpret_cast<const char*>(&v), sizeof(v));
        }

        void F32(const float v)
        {
            m_stream.write(reinterpret_cast<const char*>(&v), sizeof(v));
        }

        void Vec3(const vec3_t& v)
        {
            F32(v.x);
            F32(v.y);
            F32(v.z);
        }

        void Chars(const char* text, const size_t size)
        {
            std::vector<char> buffer(size, 0);
            if (text)
                std::strncpy(buffer.data(), text, size - 1);
            m_stream.write(buffer.data(), static_cast<std::streamsize>(size));
        }

    private:
        std::ostream& m_stream;
    };

    // The brushes under a leaf-brush node (cm_test.cpp's walk: count > 0 a leaf, < 0 node+1 holds a subtree as well,
    // childOffset relative to the node).
    void CollectBrushes(const ClipInfo& info, const unsigned int start, std::vector<int>& owner, const int model)
    {
        std::vector<unsigned int> stack{start};
        size_t guard = 0;
        while (!stack.empty() && guard++ < 4u * info.leafbrushNodesCount + 16u)
        {
            const auto index = stack.back();
            stack.pop_back();
            if (index >= info.leafbrushNodesCount)
                continue;
            const auto& node = info.leafbrushNodes[index];
            if (node.leafBrushCount > 0)
            {
                for (auto k = 0; k < node.leafBrushCount; k++)
                {
                    const auto brush = node.data.leaf.brushes[k];
                    if (brush < owner.size())
                        owner[brush] = model;
                }
                continue;
            }
            if (node.leafBrushCount < 0)
                stack.push_back(index + 1);
            stack.push_back(index + node.data.children.childOffset[0]);
            stack.push_back(index + node.data.children.childOffset[1]);
        }
    }

    // The triangles under a leaf's AABB trees (a tree with children lists them from firstChildIndex on; a tree
    // without holds a partition of triangles).
    void CollectTriangles(const clipMap_t* clip, const cLeaf_s& leaf, std::vector<uint16_t>& owner, const uint16_t model)
    {
        std::vector<int> stack;
        for (auto k = 0; k < leaf.collAabbCount; k++)
            stack.push_back(leaf.firstCollAabbIndex + k);
        size_t guard = 0;
        while (!stack.empty() && guard++ < 4u * static_cast<size_t>(clip->aabbTreeCount) + 16u)
        {
            const auto index = stack.back();
            stack.pop_back();
            if (index < 0 || index >= clip->aabbTreeCount)
                continue;
            const auto& tree = clip->aabbTrees[index];
            if (tree.childCount)
            {
                for (auto c = 0; c < tree.childCount; c++)
                    stack.push_back(tree.u.firstChildIndex + c);
                continue;
            }
            const auto p = tree.u.partitionIndex;
            if (p < 0 || p >= clip->partitionCount)
                continue;
            const auto& partition = clip->partitions[p];
            for (auto k = 0; k < static_cast<unsigned char>(partition.triCount); k++)
            {
                const auto tri = partition.firstTri + k;
                if (tri >= 0 && tri < clip->triCount)
                    owner[tri] = model;
            }
        }
    }

    void DumpCollision(AssetDumpingContext& context, const std::string& name, const clipMap_t* clip)
    {
        const auto& info = clip->info;
        const auto brushCount = static_cast<unsigned int>(info.numBrushes);

        // owner 0 = world, n = submodel n (doors, debris, other script brush models)
        std::vector<int> owner(brushCount, 0);
        std::vector<uint16_t> triOwner(static_cast<size_t>(clip->triCount), 0);
        size_t ownInfo = 0;
        for (auto m = 1u; m < clip->numSubModels; m++)
        {
            CollectBrushes(info, static_cast<unsigned int>(clip->cmodels[m].leaf.leafBrushNode), owner, static_cast<int>(m));
            CollectTriangles(clip, clip->cmodels[m].leaf, triOwner, static_cast<uint16_t>(m));
            ownInfo += clip->cmodels[m].info && clip->cmodels[m].info != &clip->info;
        }

        // a triangle's material: the leaf of the AABB tree whose partition holds it
        std::vector<uint16_t> triMaterial(static_cast<size_t>(clip->triCount), 0xFFFF);
        for (auto t = 0; t < clip->aabbTreeCount; t++)
        {
            const auto& tree = clip->aabbTrees[t];
            if (tree.childCount != 0)
                continue;
            const auto p = tree.u.partitionIndex;
            if (p < 0 || p >= clip->partitionCount)
                continue;
            const auto& partition = clip->partitions[p];
            for (auto k = 0; k < static_cast<unsigned char>(partition.triCount); k++)
            {
                const auto tri = partition.firstTri + k;
                if (tri >= 0 && tri < clip->triCount)
                    triMaterial[tri] = tree.materialIndex;
            }
        }

        const auto bin = context.OpenAssetFile(name + ".collision.bin");
        if (!bin)
            return;
        Writer w(*bin);
        w.Chars("BO2COL2", 8);
        w.U32(info.numMaterials);
        for (auto i = 0u; i < info.numMaterials; i++)
        {
            w.Chars(info.materials[i].name, 64);
            w.I32(info.materials[i].surfaceFlags);
            w.I32(info.materials[i].contentFlags);
        }
        w.U32(brushCount);
        size_t sides = 0, worldBrushes = 0;
        for (auto b = 0u; b < brushCount; b++)
        {
            const auto& brush = info.brushes[b];
            w.Vec3(brush.mins);
            w.Vec3(brush.maxs);
            w.I32(brush.contents);
            w.I32(owner[b]);
            for (const auto& row : brush.axial_cflags)
                for (const auto v : row)
                    w.I32(v);
            for (const auto& row : brush.axial_sflags)
                for (const auto v : row)
                    w.I32(v);
            w.U32(brush.numsides);
            for (auto s = 0u; s < brush.numsides; s++)
            {
                const auto& side = brush.sides[s];
                if (side.plane)
                {
                    w.Vec3(side.plane->normal);
                    w.F32(side.plane->dist);
                }
                else
                {
                    w.Vec3(vec3_t{});
                    w.F32(0.0f);
                }
                w.I32(side.cflags);
                w.I32(side.sflags);
            }
            sides += brush.numsides;
            worldBrushes += owner[b] == 0;
        }
        w.U32(clip->vertCount);
        for (auto v = 0u; v < clip->vertCount; v++)
            w.Vec3(clip->verts[v]);
        w.U32(static_cast<uint32_t>(clip->triCount));
        size_t untyped = 0, submodelTris = 0;
        for (auto t = 0; t < clip->triCount; t++)
        {
            for (const auto i : clip->triIndices[t])
                w.U16(i);
            w.U16(triMaterial[t]);
            w.U16(triOwner[t]);
            untyped += triMaterial[t] == 0xFFFF;
            submodelTris += triOwner[t] != 0;
        }
        w.U32(clip->numSubModels);
        for (auto m = 0u; m < clip->numSubModels; m++)
        {
            w.Vec3(clip->cmodels[m].mins);
            w.Vec3(clip->cmodels[m].maxs);
        }

        if (const auto csv = context.OpenAssetFile(name + ".smodelcoll.csv"))
        {
            auto& out = *csv;
            out << "model,contents,x,y,z,i00,i01,i02,i10,i11,i12,i20,i21,i22\n";
            out << std::setprecision(9);
            for (auto i = 0u; i < clip->numStaticModels; i++)
            {
                const auto& smodel = clip->staticModelList[i];
                if (!smodel.xmodel || !smodel.xmodel->name)
                    continue;
                // a model of another zone is a reference named ",<name>" (zm_transit's gump zones): without the comma
                const char* model = smodel.xmodel->name[0] == ',' ? smodel.xmodel->name + 1 : smodel.xmodel->name;
                out << model << "," << smodel.contents << "," << smodel.origin.x << "," << smodel.origin.y
                    << "," << smodel.origin.z;
                for (const auto& axis : smodel.invScaledAxis)
                    out << "," << axis.x << "," << axis.y << "," << axis.z;
                out << "\n";
            }
        }

        const auto txt = context.OpenAssetFile(name + ".collision.txt");
        if (!txt)
            return;
        auto& out = *txt;
        out << name << "\n";
        out << "materials " << info.numMaterials << "\n";
        out << "brushes " << brushCount << " (world " << worldBrushes << ", submodels " << brushCount - worldBrushes
            << "), non-axial sides " << sides << "\n";
        out << "terrain vertices " << clip->vertCount << ", triangles " << clip->triCount << " (" << untyped
            << " without a material, " << submodelTris << " of submodels)\n";
        out << "submodels with their own ClipInfo " << ownInfo << "\n";
        out << "partitions " << clip->partitionCount << ", aabb trees " << clip->aabbTreeCount << ", submodels "
            << clip->numSubModels << ", static models " << clip->numStaticModels << "\n";
        for (auto i = 0u; i < info.numMaterials; i++)
            out << "material " << i << " " << (info.materials[i].name ? info.materials[i].name : "?") << " surface 0x"
                << std::hex << info.materials[i].surfaceFlags << " contents 0x" << info.materials[i].contentFlags
                << std::dec << "\n";
        std::cout << "clipmap " << name << ": " << brushCount << " brushes (" << worldBrushes << " world), "
                  << clip->triCount << " triangles (" << submodelTris << " of submodels), " << clip->numSubModels
                  << " submodels (" << ownInfo << " with their own ClipInfo)\n";
    }
} // namespace

namespace clip_map
{
    void CollisionDumperT6::DumpAsset(AssetDumpingContext& context, const XAssetInfo<AssetClipMapPvs::Type>& asset)
    {
        DumpCollision(context, asset.m_name, asset.Asset());
    }
} // namespace clip_map
