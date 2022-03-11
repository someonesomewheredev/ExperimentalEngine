#pragma once
#include "robin_hood.h"
#include <entt/entity/fwd.hpp>
#include <physfs.h>
#include <nlohmann/json_fwd.hpp>

namespace worlds {
    struct ComponentEditorLink;
    class Editor;

    typedef robin_hood::unordered_flat_map<entt::entity, entt::entity> EntityIDMap;
    class ComponentEditor {
    public:
        static ComponentEditorLink* first;
        ComponentEditor();
        virtual int getSortID() { return 0; }
        virtual const char* getName() = 0;
        virtual uint32_t getSerializedID() = 0;
        virtual bool allowInspectorAdd() = 0;
        virtual ENTT_ID_TYPE getComponentID() = 0;
        virtual void create(entt::entity ent, entt::registry& reg) = 0;
        virtual void destroy(entt::entity ent, entt::registry& reg) = 0;
        virtual void clone(entt::entity from, entt::entity to, entt::registry& reg) = 0;
        virtual void edit(entt::entity ent, entt::registry& reg, Editor* ed) = 0;
        virtual void toJson(entt::entity ent, entt::registry& reg, nlohmann::json& j) = 0;
        virtual void fromJson(entt::entity ent, entt::registry& reg, EntityIDMap& entityRemap, const nlohmann::json& j) = 0;
        virtual ~ComponentEditor() {}
    };

    struct ComponentEditorLink {
        ComponentEditorLink() : editor(nullptr), next(nullptr) {}
        ComponentEditor* editor;
        ComponentEditorLink* next;
    };
}
