#pragma once
#include <Core/Engine.hpp>
#include <entt/entity/fwd.hpp>

namespace worlds
{
    class IGameEventHandler
    {
      public:
        virtual void init(entt::registry& registry, EngineInterfaces interfaces) = 0;
        virtual void preSimUpdate(entt::registry& registry, float deltaTime) = 0;
        virtual void update(entt::registry& registry, float deltaTime, float interpAlpha) = 0;
        virtual void simulate(entt::registry& registry, float stepTime) = 0;
        virtual void onSceneStart(entt::registry& registry) = 0;
        virtual void shutdown(entt::registry& registry) = 0;
        virtual ~IGameEventHandler()
        {
        }
    };
}
