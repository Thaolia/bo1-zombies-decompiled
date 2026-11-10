#include "GfxWorldObjDumperT6.h"

#include "Game/T6/CommonT6.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <algorithm>
#include <iostream>
#include <set>
#include <string>
#include <vector>

using namespace T6;

namespace
{
    // The T6 world vertex in draw.vd0, 36 bytes (worked out from zm_nuked's buffer, 2026-11-09): xyz (3 floats),
    // binormalSign (float), color (4 bytes), texCoord (2 halves), lmapCoord (2 halves), normal and tangent (packed).
    // A surface's vertices start at its tris.vertexDataOffset0, and its indices count from there.
    constexpr size_t STRIDE = 36u;
    constexpr size_t TEXCOORD_OFFSET = 20u;

    float HalfToFloat(const uint16_t h)
    {
        const int sign = (h >> 15) & 1;
        const int exponent = (h >> 10) & 0x1F;
        const int mantissa = h & 0x3FF;
        float value;
        if (exponent == 0)
            value = std::ldexp(static_cast<float>(mantissa), -24);
        else if (exponent == 31)
            value = mantissa ? NAN : INFINITY;
        else
            value = std::ldexp(static_cast<float>(mantissa | 0x400), exponent - 25);
        return sign ? -value : value;
    }

    // A material can hold several colour-map samplers: its base colour and effect layers (embers, thermal vision,
    // reveal, specular), in no fixed order. The base is the first of these names; failing that, the first colour map
    // that is not one of the effect samplers.
    constexpr const char* BASE_COLOR_NAMES[]{"colorMap", "Diffuse_Map", "Color_Map", "radiantDiffuseMap"};
    constexpr const char* EFFECT_NAMES[]{"Ember_Map", "Embers_Super_Map", "Thermal_Gradient", "ThermalMapMask",
                                         "Thermal_Map", "Reveal_Map", "Heat_Map", "SpecularAndGloss",
                                         "Specular_Gloss_Map", "Macro_AO_Mix_Map"};

    const char* ColorMapName(const Material* material)
    {
        if (!material || !material->textureTable)
            return nullptr;
        for (const auto* name : BASE_COLOR_NAMES)
        {
            const auto hash = Common::R_HashString(name);
            for (auto i = 0u; i < material->textureCount; i++)
            {
                const auto& texture = material->textureTable[i];
                if (texture.nameHash == hash && texture.image && texture.image->name)
                    return texture.image->name;
            }
        }
        for (auto i = 0u; i < material->textureCount; i++)
        {
            const auto& texture = material->textureTable[i];
            bool effect = false;
            for (const auto* name : EFFECT_NAMES)
                effect = effect || texture.nameHash == Common::R_HashString(name);
            if (texture.semantic == TS_COLOR_MAP && !effect && texture.image && texture.image->name)
                return texture.image->name;
        }
        return nullptr;
    }

    const char* MaterialName(const Material* material)
    {
        return material && material->info.name ? material->info.name : "no_material";
    }
} // namespace

namespace gfx_world
{
    // <map>.d3dbsp.smodellight.bin, little-endian: BO2's baked lighting of its static models, one colour per vertex of
    // each placed model's LODs (GfxStaticModelDrawInst::lmapVertexInfo), and the vertex positions they follow:
    //   char[8] "BO2SMLT1"
    //   u32 instanceCount (the rows of <map>.d3dbsp.smodels.csv, in order), then per instance: u32 lodCount, then per
    //       LOD: u32 count, count x u32 colour (as the game stores it; 0 when the LOD has none)
    //   u32 modelCount, then per model: char[64] name, u32 lodCount, then per LOD: u32 vertCount, vertCount x f32[3]
    //       position in model space, the order of the colours (the LOD's surfaces' verts0, one after the other)
    static void DumpStaticModelLighting(AssetDumpingContext& context, const std::string& name, const GfxWorldDpvsStatic& dpvs)
    {
        const auto bin = context.OpenAssetFile(name + ".smodellight.bin");
        if (!bin)
            return;
        const auto u32 = [&](uint32_t v) { bin->write(reinterpret_cast<const char*>(&v), 4); };
        bin->write("BO2SMLT1", 8);
        u32(dpvs.smodelCount);
        std::vector<const XModel*> models;
        size_t lit = 0;
        for (auto i = 0u; i < dpvs.smodelCount; i++)
        {
            const auto& inst = dpvs.smodelDrawInsts[i];
            const auto lods = inst.model ? std::min<unsigned>(inst.model->numLods, 4u) : 0u;
            u32(lods);
            for (auto l = 0u; l < lods; l++)
            {
                const auto& info = inst.lmapVertexInfo[l];
                const auto count = info.lmapVertexColors ? info.numLmapVertexColors : 0u;
                u32(count);
                if (count)
                    bin->write(reinterpret_cast<const char*>(info.lmapVertexColors), 4 * count);
                lit += count != 0;
            }
            if (inst.model && std::find(models.begin(), models.end(), inst.model) == models.end())
                models.push_back(inst.model);
        }
        u32(static_cast<uint32_t>(models.size()));
        for (const auto* model : models)
        {
            char padded[64]{};
            std::strncpy(padded, model->name ? model->name : "?", 63);
            bin->write(padded, 64);
            const auto lods = std::min<unsigned>(model->numLods, 4u);
            u32(lods);
            for (auto l = 0u; l < lods; l++)
            {
                const auto& lod = model->lodInfo[l];
                uint32_t verts = 0;
                for (auto k = 0u; k < lod.numsurfs; k++)
                    verts += model->surfs[lod.surfIndex + k].verts0 ? model->surfs[lod.surfIndex + k].vertCount : 0;
                u32(verts);
                for (auto k = 0u; k < lod.numsurfs; k++)
                {
                    const auto& surf = model->surfs[lod.surfIndex + k];
                    for (auto v = 0u; surf.verts0 && v < surf.vertCount; v++)
                        bin->write(reinterpret_cast<const char*>(&surf.verts0[v].xyz), 12);
                }
            }
        }
        std::cout << "gfxworld " << name << ": " << lit << " lit static model LODs, " << models.size() << " models\n";
    }

    void ObjDumperT6::DumpAsset(AssetDumpingContext& context, const XAssetInfo<AssetGfxWorld::Type>& asset)
    {
        const auto* world = asset.Asset();
        const auto& draw = world->draw;
        const auto& dpvs = world->dpvs;

        if (!draw.vd0.data || draw.vertexCount == 0 || !draw.indices || !dpvs.surfaces)
        {
            std::cerr << "gfxworld " << asset.m_name << ": no vertex, index or surface data\n";
            return;
        }
        // zm_nuked holds a few more 36-byte vertices than vertexCount says (195164 for 195091): check the stride only.
        if (draw.vertexDataSize0 % STRIDE != 0 || draw.vertexDataSize0 / STRIDE < draw.vertexCount)
        {
            std::cerr << "gfxworld " << asset.m_name << ": " << draw.vertexDataSize0 << " vertex bytes for "
                      << draw.vertexCount << " vertices: not the 36-byte layout this dumper knows\n";
            return;
        }
        const auto* bytes = reinterpret_cast<const char*>(draw.vd0.data);

        {
            const auto mtl = context.OpenAssetFile(asset.m_name + ".mtl");
            if (!mtl)
                return;
            std::set<std::string> written;
            for (auto i = 0u; i < dpvs.staticSurfaceCount; i++)
            {
                const auto* material = dpvs.surfaces[i].material;
                if (!written.insert(MaterialName(material)).second)
                    continue;
                *mtl << "newmtl " << MaterialName(material) << "\n";
                if (const auto* color = ColorMapName(material))
                    *mtl << "map_Kd ../../images/" << color << ".dds\n";
                *mtl << "\n";
            }
        }

        const auto obj = context.OpenAssetFile(asset.m_name + ".obj");
        if (!obj)
            return;
        auto& out = *obj;
        out << std::setprecision(9);
        const auto slash = asset.m_name.find_last_of('/');
        out << "# " << asset.m_name << ", dumped by OAT's local GfxWorldObjDumperT6\n";
        out << "mtllib " << asset.m_name.substr(slash == std::string::npos ? 0 : slash + 1) << ".mtl\n";

        // Each triangle corner written as its own vertex: simple, and Blender's "merge by distance" welds them.
        size_t triangles = 0, skipped = 0, written = 0;
        for (auto s = 0u; s < dpvs.staticSurfaceCount; s++)
        {
            const auto& surface = dpvs.surfaces[s];
            const auto& tris = surface.tris;
            out << "g surf" << s << "\nusemtl " << MaterialName(surface.material) << "\n";
            for (auto t = 0u; t < tris.triCount; t++)
            {
                size_t at[3];
                bool ok = true;
                for (auto c = 0u; c < 3u; c++)
                {
                    const auto index = static_cast<size_t>(tris.baseIndex) + t * 3u + c;
                    if (index >= static_cast<size_t>(draw.indexCount))
                    {
                        ok = false;
                        break;
                    }
                    at[c] = static_cast<size_t>(tris.vertexDataOffset0) + draw.indices[index] * STRIDE;
                    if (at[c] + STRIDE > draw.vertexDataSize0)
                        ok = false;
                }
                if (!ok)
                {
                    skipped++;
                    continue;
                }
                for (const auto a : at)
                {
                    float xyz[3];
                    uint16_t uv[2];
                    std::memcpy(xyz, bytes + a, sizeof(xyz));
                    std::memcpy(uv, bytes + a + TEXCOORD_OFFSET, sizeof(uv));
                    out << "v " << xyz[0] << " " << xyz[1] << " " << xyz[2] << "\n";
                    out << "vt " << HalfToFloat(uv[0]) << " " << 1.0f - HalfToFloat(uv[1]) << "\n";
                }
                out << "f " << written + 1 << "/" << written + 1 << " " << written + 2 << "/" << written + 2 << " "
                    << written + 3 << "/" << written + 3 << "\n";
                written += 3;
                triangles++;
            }
        }
        std::cout << "gfxworld " << asset.m_name << ": " << draw.vertexCount << " vertices, "
                  << dpvs.staticSurfaceCount << " static surfaces, " << triangles << " triangles written, " << skipped
                  << " skipped (index or vertex out of range), " << dpvs.smodelCount << " static models\n";

        const auto csv = context.OpenAssetFile(asset.m_name + ".smodels.csv");
        if (!csv)
            return;
        *csv << std::setprecision(9);
        *csv << "model,x,y,z,ax0,ax1,ax2,ay0,ay1,ay2,az0,az1,az2,scale\n";
        for (auto i = 0u; i < dpvs.smodelCount; i++)
        {
            const auto& inst = dpvs.smodelDrawInsts[i];
            const auto& p = inst.placement;
            *csv << (inst.model && inst.model->name ? inst.model->name : "?") << "," << p.origin.x << ","
                 << p.origin.y << "," << p.origin.z;
            for (const auto& axis : p.axis)
                *csv << "," << axis.x << "," << axis.y << "," << axis.z;
            *csv << "," << p.scale << "\n";
        }
        DumpStaticModelLighting(context, asset.m_name, dpvs);
    }
} // namespace gfx_world
