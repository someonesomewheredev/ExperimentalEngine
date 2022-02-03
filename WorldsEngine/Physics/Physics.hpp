#pragma once
#include <glm/gtx/quaternion.hpp>
#include <physx/PxPhysics.h>
#include <physx/PxPhysicsAPI.h>
#include <physx/PxFoundation.h>
#include <physx/extensions/PxD6Joint.h>
#include "../Core/Transform.hpp"
#include "Core/IGameEventHandler.hpp"
#include "Core/Log.hpp"
#include "PhysicsActor.hpp"
#include <entt/entity/fwd.hpp>
#include <functional>
#include <Core/MeshManager.hpp>

namespace worlds {
    extern physx::PxMaterial* defaultMaterial;
    extern physx::PxScene* g_scene;
    extern physx::PxPhysics* g_physics;
    extern physx::PxCooking* g_cooking;

    const uint32_t DEFAULT_PHYSICS_LAYER = 1;
    const uint32_t PLAYER_PHYSICS_LAYER = 2;
    const uint32_t NOCOLLISION_PHYSICS_LAYER = 4;

    inline physx::PxVec3 glm2px(glm::vec3 vec) {
        return physx::PxVec3(vec.x, vec.y, vec.z);
    }

    inline physx::PxQuat glm2px(glm::quat quat) {
        return physx::PxQuat{ quat.x, quat.y, quat.z, quat.w };
    }

    inline glm::vec3 px2glm(physx::PxVec3 vec) {
        return glm::vec3(vec.x, vec.y, vec.z);
    }

    inline glm::quat px2glm(physx::PxQuat quat) {
        return glm::quat{ quat.w, quat.x, quat.y, quat.z };
    }

    inline Transform px2glm(const physx::PxTransform& t) {
        return Transform{px2glm(t.p), px2glm(t.q)};
    }

    inline physx::PxTransform glm2px(const Transform& t) {
        return physx::PxTransform(glm2px(t.position), glm2px(t.rotation));
    }

    inline void updateMass(DynamicPhysicsActor& pa) {
        physx::PxRigidBodyExt::setMassAndUpdateInertia(*(physx::PxRigidBody*)pa.actor, pa.mass);
        pa.actor->setActorFlag(physx::PxActorFlag::eDISABLE_GRAVITY, !pa.enableGravity);
        pa.actor->setRigidBodyFlag(physx::PxRigidBodyFlag::eENABLE_CCD, pa.enableCCD);
    }

    template <typename T>
    void updatePhysicsShapes(T& pa, glm::vec3 scale = glm::vec3{ 1.0f });

    struct RaycastHitInfo {
        entt::entity entity;
        glm::vec3 normal;
        glm::vec3 worldPos;
        float distance;
    };

    bool raycast(physx::PxVec3 position, physx::PxVec3 direction, float maxDist = FLT_MAX, RaycastHitInfo* hitInfo = nullptr, uint32_t excludeLayer = 0u);
    bool raycast(glm::vec3 position, glm::vec3 direction, float maxDist = FLT_MAX, RaycastHitInfo* hitInfo = nullptr, uint32_t excludeLayer = 0u);
    uint32_t overlapSphereMultiple(glm::vec3 origin, float radius, uint32_t maxTouchCount, uint32_t* hitEntityBuffer, uint32_t excludeLayerMask = 0u);
    bool sweepSphere(glm::vec3 origin, float radius, glm::vec3 direction, float distance, RaycastHitInfo* hitInfo = nullptr, uint32_t excludeLayerMask = 0u);
    void initPhysx(const EngineInterfaces& interfaces, entt::registry& reg);
    void stepSimulation(float deltaTime);
    void shutdownPhysx();

    struct PhysicsContactInfo {
        float relativeSpeed;
        entt::entity otherEntity;
        glm::vec3 averageContactPoint;
        glm::vec3 normal;
    };

    struct PhysicsEvents {
        using ContactFunc = std::function<void(entt::entity, const PhysicsContactInfo&)>;
        static const uint32_t MAX_CONTACT_EVENTS = 4;

        PhysicsEvents() : onContact { } {}

        uint32_t addContactCallback(ContactFunc func) {
            for (uint32_t i = 0; i < MAX_CONTACT_EVENTS; i++) {
                if (onContact[i] == nullptr) {
                    onContact[i] = func;
                    return i;
                }
            }

            logErr("Exhausted contact callbacks");
            return ~0u;
        }

        void removeContactCallback(uint32_t index) {
            for (uint32_t i = 0; i < MAX_CONTACT_EVENTS; i++) {
                if (i == index) {
                    onContact[i] = nullptr;
                    break;
                }
            }
        }

        ContactFunc onContact[MAX_CONTACT_EVENTS] = { nullptr, nullptr, nullptr, nullptr };
    };
}
