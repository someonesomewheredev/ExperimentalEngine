#include "EditorWindows.hpp"
#include <ImGui/imgui.h>
#include <Core/NameComponent.hpp>
#include <Libs/IconsFontAwesome5.h>
#include <ImGui/imgui_stdlib.h>
#include <algorithm>
#include <Core/Log.hpp>
#include <Core/WorldComponents.hpp>
#include <Core/HierarchyUtil.hpp>

namespace worlds {
    void showFolderButtons(entt::entity e, EntityFolder& folder, int counter) {
        if (ImGui::Button((folder.name + "##" + std::to_string(counter)).c_str())) {
            folder.entities.push_back(e);
            ImGui::CloseCurrentPopup();
        }

        for (EntityFolder& c : folder.children) {
            showFolderButtons(e, c, counter++);
        }
    }

    void EntityList::draw(entt::registry& reg) {
        static std::string searchText;
        static std::vector<entt::entity> filteredEntities; 
        static size_t numNamedEntities;
        static bool showUnnamed = false;
        static bool folderView = false;
        static entt::entity currentlyRenaming = entt::null;
        static entt::entity popupOpenFor = entt::null;

        if (ImGui::Begin(ICON_FA_LIST u8" Entity List", &active)) {
            size_t currNamedEntCount = reg.view<NameComponent>().size();
            bool searchNeedsUpdate = !searchText.empty() &&
                numNamedEntities != currNamedEntCount;

            if (ImGui::InputText("Search", &searchText) || searchNeedsUpdate) {
                std::string lSearchTxt = searchText;
                std::transform(
                    lSearchTxt.begin(), lSearchTxt.end(),
                    lSearchTxt.begin(),
                    [](unsigned char c) { return std::tolower(c); }
                );

                filteredEntities.clear();
                reg.view<NameComponent>().each([&](auto ent, NameComponent& nc) {
                    std::string name = nc.name;

                    std::transform(
                        name.begin(), name.end(),
                        name.begin(),
                        [](unsigned char c) { return std::tolower(c); }
                    );

                    size_t pos = name.find(lSearchTxt);

                    if (pos != std::string::npos) {
                        filteredEntities.push_back(ent);
                    }
                });
            }

            numNamedEntities = currNamedEntCount;
            ImGui::Checkbox("Show Unnamed Entities", &showUnnamed);
            ImGui::Checkbox("Folder View", &folderView);

            if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseClicked[1]) {
                ImGui::OpenPopup("AddEntity");
            }

            if (ImGui::BeginPopupContextWindow("AddEntity")) {
                if (ImGui::Button("Empty")) {
                    auto emptyEnt = reg.create();
                    Transform& emptyT = reg.emplace<Transform>(emptyEnt);
                    reg.emplace<NameComponent>(emptyEnt).name = "Empty";
                    editor->select(emptyEnt);
                    Camera& cam = editor->getFirstSceneView()->getCamera();
                    emptyT.position = cam.position + cam.rotation * glm::vec3(0.0f, 0.0f, 1.0f);
                    ImGui::CloseCurrentPopup();
                }

                if (ImGui::Button("Light")) {
                    auto emptyEnt = reg.create();
                    Transform& emptyT = reg.emplace<Transform>(emptyEnt);
                    reg.emplace<NameComponent>(emptyEnt).name = "Empty";
                    editor->select(emptyEnt);
                    Camera& cam = editor->getFirstSceneView()->getCamera();
                    emptyT.position = cam.position + cam.rotation * glm::vec3(0.0f, 0.0f, 1.0f);
                    reg.emplace<WorldLight>(emptyEnt);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            bool openEntityContextMenu = false;

            std::function<void(entt::entity)> forEachEnt = [&](entt::entity ent) {
                ImGui::PushID((int)ent);
                auto nc = reg.try_get<NameComponent>(ent);
                float lineHeight = ImGui::CalcTextSize("w").y;

                ImVec2 cursorPos = ImGui::GetCursorPos();
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                float windowWidth = ImGui::GetWindowWidth();
                ImVec2 windowPos = ImGui::GetWindowPos();

                if (editor->isEntitySelected(ent)) {
                    drawList->AddRectFilled(
                        ImVec2(0.0f + windowPos.x, cursorPos.y + windowPos.y - ImGui::GetScrollY()),
                        ImVec2(windowWidth + windowPos.x, cursorPos.y + lineHeight + windowPos.y - ImGui::GetScrollY()),
                        ImColor(0, 75, 150)
                    );
                }

                if (currentlyRenaming != ent) {
                    if (nc == nullptr) {
                        ImGui::Text("Entity %u", static_cast<uint32_t>(ent));
                    } else {
                        ImGui::TextUnformatted(nc->name.c_str());
                    }
                } else {
                    if (nc == nullptr) {
                        currentlyRenaming = entt::null;
                    } else if (ImGui::InputText("###name", &nc->name, ImGuiInputTextFlags_EnterReturnsTrue)) {
                        currentlyRenaming = entt::null;
                    }
                }

                // Parent drag/drop
                ImGuiDragDropFlags entityDropFlags = 0
                    | ImGuiDragDropFlags_SourceNoDisableHover
                    | ImGuiDragDropFlags_SourceNoHoldToOpenOthers
                    | ImGuiDragDropFlags_SourceAllowNullID;

                if (ImGui::BeginDragDropSource(entityDropFlags)) {
                    if (nc == nullptr) {
                        ImGui::Text("Entity %u", static_cast<uint32_t>(ent));
                    } else {
                        ImGui::TextUnformatted(nc->name.c_str());
                    }

                    ImGui::SetDragDropPayload("HIERARCHY_ENTITY", &ent, sizeof(entt::entity));
                    ImGui::EndDragDropSource();
                }

                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY")) {
                        assert(payload->DataSize == sizeof(entt::entity));
                        entt::entity droppedEntity = *reinterpret_cast<entt::entity*>(payload->Data);

                        if (!HierarchyUtil::isEntityChildOf(reg, droppedEntity, ent))
                            HierarchyUtil::setEntityParent(reg, droppedEntity, ent);
                    }
                    ImGui::EndDragDropTarget();
                }

                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                    currentlyRenaming = ent;

                    if (nc == nullptr) {
                        nc = &reg.emplace<NameComponent>(ent);
                        nc->name = "Entity";
                    }
                }

                if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                    popupOpenFor = ent;
                    openEntityContextMenu = true;
                    ImGui::OpenPopup("Entity Context Menu");
                }

                if (ImGui::IsItemClicked()) {
                    if (!interfaces.inputManager->keyHeld(SDL_SCANCODE_LSHIFT)) {
                        editor->select(ent);
                    } else {
                        editor->multiSelect(ent);
                    }
                }

                if (reg.has<ParentComponent>(ent)) {
                    auto& pc = reg.get<ParentComponent>(ent);
                    
                    entt::entity currentChild = pc.firstChild;

                    ImGui::Indent();

                    while (reg.valid(currentChild)) {
                        auto& childComponent = reg.get<ChildComponent>(currentChild);

                        forEachEnt(currentChild);

                        currentChild = childComponent.nextChild;
                    }

                    ImGui::Unindent();
                }
                ImGui::PopID();
            };


            static uint32_t renamingFolder = 0u;

            std::function<void(EntityFolder&, EntityFolder*)> doFolderEntry = [&](EntityFolder& folder, EntityFolder* parent) {
                bool thisFolderRenaming = folder.randomId == renamingFolder;

                std::string label;
                ImGui::PushID(folder.randomId);

                if (thisFolderRenaming)
                    label = "##" + std::to_string(folder.randomId);
                else
                    label = folder.name + "##" + std::to_string(folder.randomId);

                ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_None;

                if (thisFolderRenaming) {
                    treeNodeFlags |= ImGuiTreeNodeFlags_AllowItemOverlap;
                }

                glm::vec2 tl = ImGui::GetCursorPos();
                glm::vec2 br = tl + glm::vec2(ImGui::GetWindowWidth(), ImGui::GetTextLineHeightWithSpacing());
                tl = tl + (glm::vec2)ImGui::GetWindowPos();
                br = br + (glm::vec2)ImGui::GetWindowPos();

                if (ImGui::IsMouseHoveringRect(tl, br) && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                    renamingFolder = folder.randomId;
                }

                bool showContents = ImGui::TreeNodeEx(label.c_str(), treeNodeFlags);

                if (thisFolderRenaming) {
                    ImGui::SameLine();
                    if (ImGui::InputText("##foldername", &folder.name, ImGuiInputTextFlags_EnterReturnsTrue)) {
                        renamingFolder = 0u;
                    }
                }

                if (showContents) {
                    if (parent) {
                        ImGui::SameLine();
                        if (ImGui::Button("Remove")) {
                            uint32_t id = folder.randomId;
                            parent->children.erase(
                                std::remove_if(parent->children.begin(), parent->children.end(), [id](EntityFolder& f) { return f.randomId == id; })
                            );
                        }
                    }

                    for (entt::entity ent : folder.entities) {
                        forEachEnt(ent);
                    }

                    for (EntityFolder& child : folder.children) {
                        doFolderEntry(child, &folder);
                    }

                    if (ImGui::Button("Add Folder")) {
                        folder.children.emplace_back(EntityFolder{"Untitled Entity Folder"});
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            };

            if (ImGui::BeginChild("Entities")) {
                if (searchText.empty()) {
                    if (folderView) {
                        EntityFolders& folders = reg.ctx<EntityFolders>();
                        doFolderEntry(folders.rootFolder, 0);
                    } else {
                        if (showUnnamed) {
                            reg.each(forEachEnt);
                        } else {
                            reg.view<NameComponent>(entt::exclude_t<ChildComponent>{}).each([&](auto ent, NameComponent) {
                                forEachEnt(ent);
                            });
                        }
                    }
                } else {
                    for (auto& ent : filteredEntities)
                        forEachEnt(ent);
                }

            }
            ImGui::EndChild();

            bool openFolderPopup = false;

            // Using a lambda here for early exit
            auto entityPopup = [&](entt::entity e) {
                if (!reg.valid(e)) {
                    ImGui::CloseCurrentPopup();
                    return;
                }

                if (ImGui::Button("Delete")) {
                    reg.destroy(e);
                    ImGui::CloseCurrentPopup();
                    return;
                }

                if (ImGui::Button("Rename")) {
                    currentlyRenaming = e;
                    ImGui::CloseCurrentPopup();
                }

                if (ImGui::Button("Add to folder")) {
                    ImGui::CloseCurrentPopup();
                    openFolderPopup = true;
                }
            };

            if (openEntityContextMenu) {
                ImGui::OpenPopup("Entity Context Menu");
            }

            if (ImGui::BeginPopup("Entity Context Menu")) {
                entityPopup(popupOpenFor);
                ImGui::EndPopup();
            }

            auto folderPopup = [&](entt::entity e) {
                EntityFolders& folders = reg.ctx<EntityFolders>();
                showFolderButtons(e, folders.rootFolder, 0);
            };

            if (openFolderPopup)
                ImGui::OpenPopup("Add to folder");

            if (ImGui::BeginPopup("Add to folder")) {
                folderPopup(popupOpenFor);
                ImGui::EndPopup();
            }
        }
        ImGui::End();
    }
}
