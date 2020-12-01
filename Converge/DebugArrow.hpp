#pragma once
#include "entt/entity/fwd.hpp"
#include <glm/glm.hpp>
#include <entt/entt.hpp>

namespace converge {
    class DebugArrows {
    public:
        DebugArrows(entt::registry& reg);
        void drawArrow(glm::vec3 start, glm::vec3 dir);
        void newFrame();
        void createEntities();
    private:
        entt::registry& reg;
        std::vector<entt::entity> arrowEntities;
        size_t arrowsInUse;
    };

    extern DebugArrows* g_dbgArrows;
}
