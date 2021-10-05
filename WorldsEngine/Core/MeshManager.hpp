#pragma once
#include "Render/Render.hpp"
#include <vector>
#include <robin_hood.h>

namespace worlds {
    struct LoadedMesh {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        uint8_t numSubmeshes;
        SubmeshInfo submeshes[NUM_SUBMESH_MATS];
    };

    class MeshManager {
    public:
        static const LoadedMesh& get(AssetID id);
        static const LoadedMesh& loadOrGet(AssetID id);
    private:
        static robin_hood::unordered_node_map<AssetID, LoadedMesh> loadedMeshes;
    };
}
