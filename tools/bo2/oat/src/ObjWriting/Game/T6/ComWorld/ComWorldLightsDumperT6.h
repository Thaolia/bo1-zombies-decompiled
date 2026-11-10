#pragma once

#include "Dumping/AbstractAssetDumper.h"
#include "Game/T6/T6.h"

// Local addition (not upstream OAT): exports a T6 ComWorld's primary lights as CSV, the lights a GfxSurface's
// primaryLightIndex names (BO-Zombies_switch, 2026-11-10).
namespace com_world
{
    class LightsDumperT6 final : public AbstractAssetDumper<T6::AssetComWorld>
    {
    protected:
        void DumpAsset(AssetDumpingContext& context, const XAssetInfo<T6::AssetComWorld::Type>& asset) override;
    };
} // namespace com_world
