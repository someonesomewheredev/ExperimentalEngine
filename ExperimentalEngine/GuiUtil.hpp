#pragma once
#include <string>
#include "imgui.h"
#include <functional>
#include "imgui_stdlib.h"

namespace worlds {
    typedef uint32_t AssetID;

    // Open with ImGui::OpenPopup(title)
    void saveFileModal(const char* title, std::function<void(const char*)> saveCallback);
    void openFileModal(const char* title, std::function<void(const char*)> openCallback, const char* fileExtension = nullptr);
    bool selectAssetPopup(const char* title, AssetID& id, bool open);
}
