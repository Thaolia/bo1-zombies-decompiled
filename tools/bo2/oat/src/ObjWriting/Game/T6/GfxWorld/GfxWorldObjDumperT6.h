#pragma once

#include "Dumping/AbstractAssetDumper.h"
#include "Game/T6/T6.h"

// Local addition (not upstream OAT): exports a T6 GfxWorld's static surfaces as OBJ + MTL and the placed static models
// as CSV, to study rebuilding a BO2 zombies map for BO1 (BO-Zombies_switch, 2026-11-09).
namespace gfx_world
{
    class ObjDumperT6 final : public AbstractAssetDumper<T6::AssetGfxWorld>
    {
    protected:
        void DumpAsset(AssetDumpingContext& context, const XAssetInfo<T6::AssetGfxWorld::Type>& asset) override;
    };
} // namespace gfx_world
