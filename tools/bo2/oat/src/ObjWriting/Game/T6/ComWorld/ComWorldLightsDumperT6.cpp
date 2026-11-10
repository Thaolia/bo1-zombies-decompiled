#include "ComWorldLightsDumperT6.h"

#include <iomanip>
#include <iostream>

using namespace T6;

namespace com_world
{
    // <name>.lights.csv: one row per primary light, its index first (GfxSurface::primaryLightIndex counts from 0, the
    // empty light; the sun is GfxWorld::sunPrimaryLightIndex). Every field of ComPrimaryLight but the cookies.
    void LightsDumperT6::DumpAsset(AssetDumpingContext& context, const XAssetInfo<AssetComWorld::Type>& asset)
    {
        const auto* world = asset.Asset();
        if (!world->primaryLights)
        {
            std::cerr << "comworld " << asset.m_name << ": no primary lights\n";
            return;
        }
        const auto csv = context.OpenAssetFile(asset.m_name + ".lights.csv");
        if (!csv)
            return;
        auto& out = *csv;
        out << std::setprecision(9);
        out << "index,type,canUseShadowMap,exponent,priority,cullDist,useCookie,r,g,b,dx,dy,dz,x,y,z,radius,"
               "cosHalfFovOuter,cosHalfFovInner,cosHalfFovExpanded,dAttenuation,roundness,dr,dg,db,da,falloff0,falloff1,"
               "falloff2,falloff3,angle0,angle1,angle2,angle3,aAbB0,aAbB1,aAbB2,aAbB3,defName\n";
        for (auto i = 0u; i < world->primaryLightCount; i++)
        {
            const auto& l = world->primaryLights[i];
            out << i << "," << static_cast<int>(l.type) << "," << static_cast<int>(l.canUseShadowMap) << ","
                << static_cast<int>(l.exponent) << "," << static_cast<int>(l.priority) << "," << l.cullDist << ","
                << static_cast<int>(l.useCookie) << "," << l.color.x << "," << l.color.y << "," << l.color.z << ","
                << l.dir.x << "," << l.dir.y << "," << l.dir.z << "," << l.origin.x << "," << l.origin.y << ","
                << l.origin.z << "," << l.radius << "," << l.cosHalfFovOuter << "," << l.cosHalfFovInner << ","
                << l.cosHalfFovExpanded << "," << l.dAttenuation << "," << l.roundness << "," << l.diffuseColor.x
                << "," << l.diffuseColor.y << "," << l.diffuseColor.z << "," << l.diffuseColor.w << ","
                << l.falloff.x << "," << l.falloff.y << "," << l.falloff.z << "," << l.falloff.w << ","
                << l.angle.x << "," << l.angle.y << "," << l.angle.z << "," << l.angle.w << "," << l.aAbB.x << ","
                << l.aAbB.y << "," << l.aAbB.z << "," << l.aAbB.w << "," << (l.defName ? l.defName : "") << "\n";
        }
        std::cout << "comworld " << asset.m_name << ": " << world->primaryLightCount << " primary lights\n";
    }
} // namespace com_world
