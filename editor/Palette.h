#pragma once

#include <imgui.h>

namespace rb::editor {

// Our own flat dark theme, with its own values:
// layered cool-grey surfaces, a single blue accent, dim secondary text, subtle
// borders, translucent selection.
struct Palette {
    ImVec4 surface0{0.085f, 0.090f, 0.098f, 1.0f};
    ImVec4 surface1{0.115f, 0.122f, 0.132f, 1.0f};
    ImVec4 surface2{0.155f, 0.165f, 0.178f, 1.0f};
    ImVec4 hover{0.225f, 0.238f, 0.258f, 1.0f};
    ImVec4 active{0.300f, 0.318f, 0.342f, 1.0f};
    ImVec4 accent{0.220f, 0.450f, 0.780f, 1.0f};
    ImVec4 accentSoft{0.180f, 0.340f, 0.560f, 1.0f};
    ImVec4 accentStrong{0.300f, 0.560f, 0.900f, 1.0f};
    ImVec4 text{0.880f, 0.890f, 0.900f, 1.0f};
    ImVec4 textDim{0.520f, 0.540f, 0.570f, 1.0f};
    ImVec4 border{0.050f, 0.053f, 0.058f, 1.0f};
    ImVec4 selection{0.220f, 0.450f, 0.780f, 0.35f};
    ImVec4 error{0.820f, 0.300f, 0.300f, 1.0f};
    ImVec4 warning{0.950f, 0.700f, 0.300f, 1.0f};
};

void applyStyle(const Palette& palette = Palette{});

// Loads Inter (UI, default) and JetBrains Mono (console/code). Falls back to the
// built-in font if the .ttf files are missing. Returns the mono font (or nullptr).
ImFont* loadFonts(float uiSize = 16.0f, float monoSize = 15.0f);
[[nodiscard]] ImFont* monoFont();

} // namespace rb::editor
