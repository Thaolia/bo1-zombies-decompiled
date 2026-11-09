#pragma once

#include "Dumping/AbstractAssetDumper.h"
#include "Game/T6/T6.h"

// Local addition (not upstream OAT): exports a T6 gameworldmp's path data (the nodes as the map compiler placed them,
// with the links it made) as a CSV for the BO1 port's MapKit import (BO-Zombies_switch, 2026-11-09). The format is
// described in GameWorldPathDumperT6.cpp.
namespace game_world
{
    class PathDumperT6 final : public AbstractAssetDumper<T6::AssetGameWorldMp>
    {
    protected:
        void DumpAsset(AssetDumpingContext& context, const XAssetInfo<T6::AssetGameWorldMp::Type>& asset) override;
    };
} // namespace game_world
