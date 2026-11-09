#pragma once

#include "Dumping/AbstractAssetDumper.h"
#include "Game/T6/T6.h"

// Local addition (not upstream OAT): exports a T6 clipMap (the asset OAT lists as "clipmap", ASSET_TYPE_CLIPMAP_PVS)'s collision (brushes with their planes, terrain triangles,
// materials, submodel bounds) as one binary file for the BO1 port's MapKit import (BO-Zombies_switch, 2026-11-09).
// The format is described in ClipMapCollisionDumperT6.cpp.
namespace clip_map
{
    class CollisionDumperT6 final : public AbstractAssetDumper<T6::AssetClipMapPvs>
    {
    protected:
        void DumpAsset(AssetDumpingContext& context, const XAssetInfo<T6::AssetClipMapPvs::Type>& asset) override;
    };
} // namespace clip_map
