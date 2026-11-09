#include "GameWorldPathDumperT6.h"

#include <iomanip>
#include <iostream>

using namespace T6;

// <map>.d3dbsp.paths.csv, one row per node of the compiled path data (originalNodeCount first: the map's own nodes,
// then the ones the compiler added, if any):
//   index,type,x,y,z,links
// type is the nodeType number (0 bad, 1 path, 0x11 negotiation begin, 0x12 negotiation end...), x y z the origin the
// compiler dropped the node to, links the node's outgoing links separated by ';', each "to:dist:negotiation".

namespace game_world
{
    void PathDumperT6::DumpAsset(AssetDumpingContext& context, const XAssetInfo<AssetGameWorldMp::Type>& asset)
    {
        const auto* world = asset.Asset();
        const auto& path = world->path;
        if (!path.nodes)
            return;
        const auto csv = context.OpenAssetFile(asset.m_name + ".paths.csv");
        if (!csv)
            return;
        auto& out = *csv;
        out << std::setprecision(9);
        out << "index,type,x,y,z,links\n";
        unsigned links = 0;
        for (auto i = 0u; i < path.nodeCount; i++)
        {
            const auto& node = path.nodes[i].constant;
            out << i << "," << static_cast<int>(node.type) << "," << node.vOrigin.x << "," << node.vOrigin.y << ","
                << node.vOrigin.z << ",";
            for (auto k = 0u; node.Links && k < node.totalLinkCount; k++)
            {
                const auto& link = node.Links[k];
                out << (k ? ";" : "") << link.nodeNum << ":" << link.fDist << ":" << static_cast<int>(link.negotiationLink);
            }
            links += node.Links ? node.totalLinkCount : 0;
            out << "\n";
        }
        std::cout << "gameworldmp " << asset.m_name << ": " << path.nodeCount << " nodes (" << path.originalNodeCount
                  << " placed), " << links << " links\n";
    }
} // namespace game_world
