#include "MeshManager.hpp"
#include "../Render/RenderInternal.hpp"
#include "../Render/Loaders/WMDLLoader.hpp"

namespace worlds {
    robin_hood::unordered_node_map<AssetID, LoadedMesh> MeshManager::loadedMeshes;
    const LoadedMesh& MeshManager::get(AssetID id) {
        return loadedMeshes.at(id);
    }

    const LoadedMesh& MeshManager::loadOrGet(AssetID id) {
        if (loadedMeshes.contains(id)) return loadedMeshes.at(id);

        std::vector<VertSkinningInfo> vertSkinning;
        LoadedMeshData lmd;
        LoadedMesh lm;
        loadWorldsModel(id, lm.vertices, lm.indices, vertSkinning, lmd);

        lm.numSubmeshes = lmd.numSubmeshes;
        lm.skinned = lmd.isSkinned;
        lm.boneNames.resize(lmd.meshBones.size());
        lm.boneRestPositions.resize(lmd.meshBones.size());

        for (size_t i = 0; i < lm.boneNames.size(); i++) {
            lm.boneNames[i] = lmd.meshBones[i].name;
            lm.boneRestPositions[i] = lmd.meshBones[i].restPosition;
        }

        for (int i = 0; i < lmd.numSubmeshes; i++) {
            lm.submeshes[i] = lmd.submeshes[i];
        }

        loadedMeshes.insert({ id, std::move(lm) });

        return loadedMeshes.at(id);
    }
}