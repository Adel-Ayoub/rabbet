#pragma once

#include <imgui.h>

namespace rb::editor {

// 0xRRGGBB -> ImVec4, so the theme table below reads as plain hex.
constexpr ImVec4 rgb(unsigned int hex, float alpha = 1.0f) {
    return ImVec4(static_cast<float>((hex >> 16) & 0xFFu) / 255.0f,
                  static_cast<float>((hex >> 8) & 0xFFu) / 255.0f,
                  static_cast<float>(hex & 0xFFu) / 255.0f, alpha);
}

constexpr ImVec4 withAlpha(const ImVec4& color, float alpha) {
    return ImVec4(color.x, color.y, color.z, alpha);
}

// The forge theme: layered cool-slate surfaces with a single blue accent, plus
// a small semantic set. Panels read these tokens through palette(), never raw hex.
struct Palette {
    // surface ramp, darkest to lightest
    ImVec4 well = rgb(0x161719);  // sunken chrome: log wells, tracks, title bars
    ImVec4 base = rgb(0x1D1F22);  // panels and popups
    ImVec4 layer = rgb(0x282A2D); // interactive fills: frames, buttons, tiles
    ImVec4 lift = rgb(0x393D42);  // hovered fill
    ImVec4 press = rgb(0x4C5157); // active fill
    ImVec4 outline = rgb(0x0D0E0F);

    ImVec4 ink = rgb(0xE0E3E6);
    ImVec4 inkMuted = rgb(0x858A91);

    ImVec4 accent = rgb(0x3B78CC);
    ImVec4 accentDim = rgb(0x2E578F);
    ImVec4 accentHot = rgb(0x4D8FE6);
    ImVec4 select = rgb(0x3B78CC, 0.35f);

    ImVec4 danger = rgb(0xD14D4D);
    ImVec4 warn = rgb(0xF2B34D);
    ImVec4 good = rgb(0x5CB878);
    ImVec4 info = rgb(0x6E9EC8);

    // shared metrics for panel-drawn shapes (tiles, slots, chips)
    float roundWidget = 3.0f;
    float roundTile = 4.0f;
};

// The applied theme; valid after applyTheme().
[[nodiscard]] const Palette& palette();

// Writes the token set into ImGui's style: colors and metrics together.
void applyTheme(const Palette& theme = Palette{});

// Font roles: UI (Inter), headings (Inter SemiBold), mono (JetBrains Mono).
// Missing files degrade down the chain, so every pointer is always usable.
struct FontSet {
    ImFont* ui = nullptr;
    ImFont* heading = nullptr;
    ImFont* mono = nullptr;
};

[[nodiscard]] const FontSet& fonts();

// Rasterizes every role at the window content scale so glyphs stay crisp on
// hidpi displays; pass 1.0 when the scale is unknown.
void loadFonts(float contentScale);

} // namespace rb::editor
