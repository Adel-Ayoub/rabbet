#include "editor/Palette.h"

#include "rabbet/util/Log.h"

#ifndef RB_EDITOR_RESOURCES
#define RB_EDITOR_RESOURCES "editor/resources"
#endif

namespace rb::editor {
namespace {
ImFont* g_monoFont = nullptr;
}

void applyStyle(const Palette& p) {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* c = style.Colors;

    c[ImGuiCol_WindowBg] = p.surface1;
    c[ImGuiCol_ChildBg] = p.surface0;
    c[ImGuiCol_PopupBg] = p.surface1;
    c[ImGuiCol_Text] = p.text;
    c[ImGuiCol_TextDisabled] = p.textDim;
    c[ImGuiCol_Border] = p.border;
    c[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_FrameBg] = p.surface2;
    c[ImGuiCol_FrameBgHovered] = p.hover;
    c[ImGuiCol_FrameBgActive] = p.active;
    c[ImGuiCol_TitleBg] = p.surface0;
    c[ImGuiCol_TitleBgActive] = p.surface1;
    c[ImGuiCol_TitleBgCollapsed] = p.surface0;
    c[ImGuiCol_MenuBarBg] = p.surface1;
    c[ImGuiCol_Header] = p.surface2;
    c[ImGuiCol_HeaderHovered] = p.hover;
    c[ImGuiCol_HeaderActive] = p.active;
    c[ImGuiCol_Button] = p.surface2;
    c[ImGuiCol_ButtonHovered] = p.hover;
    c[ImGuiCol_ButtonActive] = p.active;
    c[ImGuiCol_Tab] = p.surface0;
    c[ImGuiCol_TabHovered] = p.hover;
    c[ImGuiCol_TabSelected] = p.surface2;
    c[ImGuiCol_TabSelectedOverline] = p.accent;
    c[ImGuiCol_TabDimmed] = p.surface0;
    c[ImGuiCol_TabDimmedSelected] = p.surface1;
    c[ImGuiCol_Separator] = p.border;
    c[ImGuiCol_SeparatorHovered] = p.accentSoft;
    c[ImGuiCol_SeparatorActive] = p.accent;
    c[ImGuiCol_ScrollbarBg] = p.surface0;
    c[ImGuiCol_ScrollbarGrab] = p.surface2;
    c[ImGuiCol_ScrollbarGrabHovered] = p.hover;
    c[ImGuiCol_ScrollbarGrabActive] = p.active;
    c[ImGuiCol_CheckMark] = p.accent;
    c[ImGuiCol_SliderGrab] = p.accentSoft;
    c[ImGuiCol_SliderGrabActive] = p.accent;
    c[ImGuiCol_TextSelectedBg] = p.selection;
    c[ImGuiCol_DockingPreview] = ImVec4(p.accent.x, p.accent.y, p.accent.z, 0.60f);
    c[ImGuiCol_DockingEmptyBg] = p.surface0;
    c[ImGuiCol_TableHeaderBg] = p.surface2;
    c[ImGuiCol_TableRowBg] = p.surface1;
    c[ImGuiCol_TableRowBgAlt] = p.surface0;
    c[ImGuiCol_TableBorderStrong] = p.border;
    c[ImGuiCol_TableBorderLight] = p.border;
    c[ImGuiCol_NavCursor] = p.accentSoft;
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.5f);

    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.PopupRounding = 0.0f;
    style.FrameRounding = 2.0f;
    style.ScrollbarRounding = 2.0f;
    style.GrabRounding = 2.0f;
    style.TabRounding = 2.0f;
    style.FrameBorderSize = 1.0f;
    style.WindowBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.WindowPadding = ImVec2(10.0f, 8.0f);
    style.FramePadding = ImVec2(7.0f, 4.0f);
    style.ItemSpacing = ImVec2(7.0f, 6.0f);
    style.IndentSpacing = 14.0f;
    style.ScrollbarSize = 12.0f;
    style.WindowMenuButtonPosition = ImGuiDir_None;
}

ImFont* loadFonts(float uiSize, float monoSize) {
    ImGuiIO& io = ImGui::GetIO();
    ImFont* ui = io.Fonts->AddFontFromFileTTF(RB_EDITOR_RESOURCES "/fonts/Inter.ttf", uiSize);
    if (ui == nullptr) {
        log::warn("editor: Inter.ttf not found under {}; using built-in font", RB_EDITOR_RESOURCES);
        io.Fonts->AddFontDefault();
    } else {
        io.FontDefault = ui;
    }
    g_monoFont =
        io.Fonts->AddFontFromFileTTF(RB_EDITOR_RESOURCES "/fonts/JetBrainsMono-Regular.ttf", monoSize);
    if (g_monoFont == nullptr) {
        log::warn("editor: JetBrainsMono-Regular.ttf not found; console uses default font");
    }
    return g_monoFont;
}

ImFont* monoFont() {
    return g_monoFont;
}

} // namespace rb::editor
