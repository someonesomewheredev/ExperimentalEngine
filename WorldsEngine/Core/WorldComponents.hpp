#pragma once
#include <stdint.h>
#include <vector>

#include <glm/ext/scalar_constants.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float4.hpp>
#include <slib/Bitset.hpp>
#include <slib/String.hpp>


namespace worlds {
    typedef uint32_t AssetID;
    const int NUM_SUBMESH_MATS = 32;

    enum class StaticFlags : uint8_t {
        None = 0,
        Audio = 1,
        Rendering = 2,
        Navigation = 4
    };

    enum class UVOverride {
        None,
        XY,
        XZ,
        ZY,
        PickBest
    };

    struct WorldObject {
        WorldObject(AssetID material, AssetID mesh)
            : staticFlags(StaticFlags::None)
            , mesh(mesh)
            , texScaleOffset(1.0f, 1.0f, 0.0f, 0.0f)
            , uvOverride(UVOverride::None) {
            for (int i = 0; i < NUM_SUBMESH_MATS; i++) {
                materials[i] = material;
                presentMaterials[i] = false;
            }
            presentMaterials[0] = true;
        }

        StaticFlags staticFlags;
        AssetID materials[NUM_SUBMESH_MATS];
        slib::Bitset<NUM_SUBMESH_MATS> presentMaterials;
        AssetID mesh;
        glm::vec4 texScaleOffset;
        UVOverride uvOverride;
    };

    struct Bone {
        glm::mat4 restPose;
        uint32_t id;
    };

    class Skeleton {
    public:
        std::vector<Bone> bones;
    };

    class Pose {
    public:
        std::vector<glm::mat4> boneTransforms;
    };

    struct SkinnedWorldObject : public WorldObject {
        SkinnedWorldObject(AssetID material, AssetID mesh)
            : WorldObject(material, mesh) {
            currentPose.boneTransforms.resize(64); // TODO

            for (glm::mat4& t : currentPose.boneTransforms) {
                t = glm::mat4{1.0f};
            }
        }
        Pose currentPose;
    };

    struct UseWireframe {};

    enum class LightType {
        Point,
        Spot,
        Directional,
        Sphere,
        Tube
    };

    struct WorldLight {
        WorldLight() {}
        WorldLight(LightType type) : type(type) {}

        // Whether the light should be actually rendered
        bool enabled = true;
        LightType type = LightType::Point;
        glm::vec3 color = glm::vec3{1.0f};
        float intensity = 1.0f;

        // Angle of the spotlight cutoff in radians
        float spotCutoff = glm::pi<float>() * 0.5f;

        // Physical dimensions of a tube light
        float tubeLength = 0.25f;
        float tubeRadius = 0.1f;

        // Shadowing settings
        bool enableShadows = false;
        uint32_t shadowmapIdx = ~0u;
        float shadowNear = 0.05f;
        float shadowFar = 100.0f;

        float maxDistance = 1.0f;
        // Index of the light in the light buffer
        uint32_t lightIdx = 0u;
    };

    struct WorldCubemap {
        AssetID cubemapId;
        glm::vec3 extent{0.0f};
        bool cubeParallax = false;
        int priority = 0;
    };

    struct EditorLabel {
        slib::String label;
    };

    struct DontSerialize {};
    struct HideFromEditor {};
}
