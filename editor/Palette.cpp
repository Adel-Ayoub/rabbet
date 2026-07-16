#include "editor/Palette.h"

#include "editor/Icons.h"

#include "rabbet/util/Log.h"

#ifndef RB_EDITOR_RESOURCES
#define RB_EDITOR_RESOURCES "editor/resources"
#endif

namespace rb::editor {
namespace {

Palette g_palette;
FontSet g_fonts;

constexpr float kUiSize = 15.0f;
constexpr float kHeadingSize = 16.0f;
constexpr float kMonoSize = 13.0f;

ImFont* addFont(const char* path, float size, float density) {
    ImFontConfig config;
    config.RasterizerDensity = density;
    return ImGui::GetIO().Fonts->AddFontFromFileTTF(path, size, &config);
}

// Merges the Lucide glyph window into the last-added font so icon literals from
// Icons.h render inline with that font's text.
bool mergeIcons(float size, float density) {
    static const ImWchar ranges[] = {icon::kRangeBegin, icon::kRangeEnd, 0};
    ImFontConfig config;
    config.MergeMode = true;
    config.RasterizerDensity = density;
    config.GlyphMinAdvanceX = size; // icons advance uniformly, so glyph columns align
    config.GlyphOffset = ImVec2(0.0f, 1.0f);
    return ImGui::GetIO().Fonts->AddFontFromFileTTF(RB_EDITOR_RESOURCES "/fonts/Lucide.ttf", size,
                                                    &config, ranges) != nullptr;
}

} // namespace

const Palette& palette() {
    return g_palette;
}

void applyTheme(const Palette& t) {
    g_palette = t;
    ImGuiStyle& style = ImGui::GetStyle();

    // metrics: dense but breathable, square docked chrome, pill scrollbars
    style.WindowPadding = ImVec2(10.0f, 8.0f);
    style.FramePadding = ImVec2(7.0f, 4.0f);
    style.CellPadding = ImVec2(6.0f, 3.0f);
    style.ItemSpacing = ImVec2(7.0f, 5.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.IndentSpacing = 15.0f;
    style.ScrollbarSize = 11.0f;
    style.GrabMinSize = 10.0f;
    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.PopupRounding = t.roundWidget;
    style.FrameRounding = t.roundWidget;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = t.roundWidget;
    style.TabRounding = 0.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.TabBorderSize = 0.0f;
    style.TabBarBorderSize = 1.0f;
    style.TabBarOverlineSize = 2.0f;
    style.DockingSeparatorSize = 2.0f;
    style.SeparatorTextBorderSize = 1.0f;
    style.SeparatorTextAlign = ImVec2(0.0f, 0.5f);
    style.SeparatorTextPadding = ImVec2(16.0f, 4.0f);
    style.WindowMenuButtonPosition = ImGuiDir_None;

    ImVec4* c = style.Colors;

    // chrome
    c[ImGuiCol_WindowBg] = t.base;
    c[ImGuiCol_ChildBg] = t.well;
    c[ImGuiCol_PopupBg] = t.base;
    c[ImGuiCol_MenuBarBg] = t.base;
    c[ImGuiCol_TitleBg] = t.well;
    c[ImGuiCol_TitleBgActive] = t.base;
    c[ImGuiCol_TitleBgCollapsed] = t.well;
    c[ImGuiCol_Border] = t.outline;
    c[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    // text
    c[ImGuiCol_Text] = t.ink;
    c[ImGuiCol_TextDisabled] = t.inkMuted;
    c[ImGuiCol_TextSelectedBg] = t.select;
    c[ImGuiCol_TextLink] = t.accentHot;

    // fields and buttons
    c[ImGuiCol_FrameBg] = t.layer;
    c[ImGuiCol_FrameBgHovered] = t.lift;
    c[ImGuiCol_FrameBgActive] = t.press;
    c[ImGuiCol_Button] = t.layer;
    c[ImGuiCol_ButtonHovered] = t.lift;
    c[ImGuiCol_ButtonActive] = t.press;
    c[ImGuiCol_CheckMark] = t.accent;
    c[ImGuiCol_SliderGrab] = t.accentDim;
    c[ImGuiCol_SliderGrabActive] = t.accent;

    c[ImGuiCol_Header] = t.layer;
    c[ImGuiCol_HeaderHovered] = t.lift;
    c[ImGuiCol_HeaderActive] = t.press;

    c[ImGuiCol_Separator] = t.outline;
    c[ImGuiCol_SeparatorHovered] = t.accentDim;
    c[ImGuiCol_SeparatorActive] = t.accent;
    c[ImGuiCol_ResizeGrip] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_ResizeGripHovered] = t.accentDim;
    c[ImGuiCol_ResizeGripActive] = t.accent;
    c[ImGuiCol_ScrollbarBg] = t.well;
    c[ImGuiCol_ScrollbarGrab] = t.layer;
    c[ImGuiCol_ScrollbarGrabHovered] = t.lift;
    c[ImGuiCol_ScrollbarGrabActive] = t.press;

    // tabs mark the selected page with an accent overline
    c[ImGuiCol_Tab] = t.well;
    c[ImGuiCol_TabHovered] = t.lift;
    c[ImGuiCol_TabSelected] = t.layer;
    c[ImGuiCol_TabSelectedOverline] = t.accent;
    c[ImGuiCol_TabDimmed] = t.well;
    c[ImGuiCol_TabDimmedSelected] = t.base;
    c[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    // docking, drag-drop, navigation
    c[ImGuiCol_DockingPreview] = withAlpha(t.accent, 0.60f);
    c[ImGuiCol_DockingEmptyBg] = t.well;
    c[ImGuiCol_DragDropTarget] = t.accentHot;
    c[ImGuiCol_NavCursor] = t.accentDim;
    c[ImGuiCol_NavWindowingHighlight] = withAlpha(t.accent, 0.50f);
    c[ImGuiCol_NavWindowingDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.40f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.50f);

    // tables and plots
    c[ImGuiCol_TableHeaderBg] = t.layer;
    c[ImGuiCol_TableBorderStrong] = t.outline;
    c[ImGuiCol_TableBorderLight] = t.outline;
    c[ImGuiCol_TableRowBg] = t.base;
    c[ImGuiCol_TableRowBgAlt] = t.well;
    c[ImGuiCol_PlotLines] = t.accent;
    c[ImGuiCol_PlotLinesHovered] = t.accentHot;
    c[ImGuiCol_PlotHistogram] = t.accentDim;
    c[ImGuiCol_PlotHistogramHovered] = t.accent;
}

void loadFonts(float contentScale) {
    ImGuiIO& io = ImGui::GetIO();
    const float density = contentScale > 0.0f ? contentScale : 1.0f;

    g_fonts.ui = addFont(RB_EDITOR_RESOURCES "/fonts/Inter.ttf", kUiSize, density);
    if (g_fonts.ui == nullptr) {
        log::warn("editor: Inter.ttf not found under {}; using the built-in font",
                  RB_EDITOR_RESOURCES);
        g_fonts.ui = io.Fonts->AddFontDefault();
    }
    io.FontDefault = g_fonts.ui;
    if (!mergeIcons(kUiSize, density)) {
        log::warn("editor: Lucide.ttf not found; icon glyphs render as placeholders");
    }

    g_fonts.heading = addFont(RB_EDITOR_RESOURCES "/fonts/Inter-SemiBold.ttf", kHeadingSize, density);
    if (g_fonts.heading == nullptr) {
        log::warn("editor: Inter-SemiBold.ttf not found; headings use the UI font");
        g_fonts.heading = g_fonts.ui;
    } else {
        mergeIcons(kHeadingSize, density);
    }

    g_fonts.mono = addFont(RB_EDITOR_RESOURCES "/fonts/JetBrainsMono-Regular.ttf", kMonoSize, density);
    if (g_fonts.mono == nullptr) {
        log::warn("editor: JetBrainsMono-Regular.ttf not found; the console uses the UI font");
        g_fonts.mono = g_fonts.ui;
    }
}

const FontSet& fonts() {
    return g_fonts;
}

} // namespace rb::editor
