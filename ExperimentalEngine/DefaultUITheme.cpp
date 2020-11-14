#include "imgui.h"

namespace worlds {
    void loadDefaultUITheme() {
        ImVec4* colors = ImGui::GetStyle().Colors;
        colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
        colors[ImGuiCol_WindowBg] = ImVec4(0.079f, 0.076f, 0.090f, 1.000f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.15f, 0.15f, 0.17f, 0.94f);
        colors[ImGuiCol_Border] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
        colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.31f, 0.29f, 0.37f, 0.40f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.41f, 0.39f, 0.50f, 0.40f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.41f, 0.40f, 0.50f, 0.62f);
        colors[ImGuiCol_TitleBg] = ImVec4(0.15f, 0.15f, 0.17f, 1.00f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.28f, 0.26f, 0.35f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
        colors[ImGuiCol_MenuBarBg] = ImVec4(0.20f, 0.19f, 0.24f, 1.00f);
        colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
        colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
        colors[ImGuiCol_CheckMark] = ImVec4(0.60f, 0.56f, 0.77f, 1.00f);
        colors[ImGuiCol_SliderGrab] = ImVec4(0.56f, 0.54f, 0.66f, 0.40f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(0.76f, 0.73f, 0.88f, 0.40f);
        colors[ImGuiCol_Button] = ImVec4(0.31f, 0.29f, 0.37f, 0.40f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.47f, 0.45f, 0.57f, 0.40f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.21f, 0.20f, 0.26f, 0.40f);
        colors[ImGuiCol_Header] = ImVec4(0.31f, 0.29f, 0.37f, 0.40f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.47f, 0.45f, 0.57f, 0.40f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.21f, 0.20f, 0.25f, 0.40f);
        colors[ImGuiCol_Separator] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
        colors[ImGuiCol_SeparatorHovered] = ImVec4(0.10f, 0.40f, 0.75f, 0.78f);
        colors[ImGuiCol_SeparatorActive] = ImVec4(0.10f, 0.40f, 0.75f, 1.00f);
        colors[ImGuiCol_ResizeGrip] = ImVec4(0.47f, 0.45f, 0.57f, 0.74f);
        colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.59f, 0.57f, 0.71f, 0.74f);
        colors[ImGuiCol_ResizeGripActive] = ImVec4(0.35f, 0.33f, 0.41f, 0.74f);
        colors[ImGuiCol_Tab] = ImVec4(0.456f, 0.439f, 0.541f, 0.400f);
        colors[ImGuiCol_TabHovered] = ImVec4(0.51f, 0.49f, 0.62f, 0.40f);
        colors[ImGuiCol_TabActive] = ImVec4(0.56f, 0.54f, 0.71f, 0.40f);
        colors[ImGuiCol_TabUnfocused] = ImVec4(0.27f, 0.26f, 0.32f, 0.40f);
        colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.42f, 0.39f, 0.57f, 0.40f);
        colors[ImGuiCol_DockingPreview] = ImVec4(0.58f, 0.54f, 0.80f, 0.78f);
        colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
        colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
        colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
        colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
        colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
        colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
        colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
        colors[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
        colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
        colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
        colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);

        ImGui::GetStyle().WindowBorderSize = 0.0f;
        ImGui::GetStyle().PopupBorderSize = 0.0f;
        ImGui::GetStyle().FrameRounding = 2.0f;
        ImGui::GetStyle().PopupRounding = 1.0f;
        ImGui::GetStyle().ScrollbarRounding = 3.0f;
        ImGui::GetStyle().GrabRounding = 2.0f;
        ImGui::GetStyle().ChildBorderSize = 0.0f;
    }
}