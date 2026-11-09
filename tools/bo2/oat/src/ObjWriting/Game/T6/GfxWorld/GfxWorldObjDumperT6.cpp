#include "GfxWorldObjDumperT6.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <set>
#include <string>

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

    const char* ColorMapName(const Material* material)
    {
        if (!material || !material->textureTable)
            return nullptr;
        for (auto i = 0u; i < material->textureCount; i++)
        {
            const auto& texture = material->textureTable[i];
            if (texture.semantic == TS_COLOR_MAP && texture.image && texture.image->name)
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
    }
} // namespace gfx_world
