#pragma once

#include "editor/AssetAssign.h"

#include "rabbet/assets/AssetType.h"
#include "rabbet/core/Uuid.h"

#include <imgui.h>

#include <cstddef>

namespace rb::editor::ui {

// Inspector row layout: labels in a fixed left column, the widget filling the
// rest of the line. Each helper draws one full row and returns the change flag.
[[nodiscard]] float labelColumnWidth();

// Draws the label cell and prepares the widget cell (full remaining width).
void rowLabel(const char* label);

bool dragFloat(const char* label, float* value, float speed, float min = 0.0f, float max = 0.0f);
bool dragFloat2(const char* label, float* values, float speed, float min = 0.0f, float max = 0.0f);
bool dragFloat3(const char* label, float* values, float speed, float min = 0.0f, float max = 0.0f);
bool dragInt(const char* label, int* value, float speed, int min = 0, int max = 0);
bool checkbox(const char* label, bool* value);
bool colorEdit3(const char* label, float* rgb, ImGuiColorEditFlags flags = 0);
bool colorEdit4(const char* label, float* rgba, ImGuiColorEditFlags flags = 0);
bool inputText(const char* label, char* buffer, std::size_t size);

// Row-aligned combo; pair with ImGui::EndCombo() exactly like the raw API.
bool beginCombo(const char* label, const char* preview);

// The glyph an asset kind is shown with (slots, tiles, badges).
[[nodiscard]] const char* assetGlyph(rb::AssetType type);

// What an assetSlot interaction produced this frame.
struct SlotResult {
    AssetDragPayload dropped; // valid when a matching asset was dropped on the slot
    bool cleared = false;
};

// A reference slot for an asset link: a framed field showing the bound uuid (or
// "None") with its kind glyph, a warn tint while unresolved, the full uuid in the
// tooltip, and an inline clear button. Drops of the matching asset type land in
// the result; the caller writes them into its component.
SlotResult assetSlot(const char* label, rb::AssetType type, const rb::Uuid& id, bool resolved,
                     bool droppable = true);

} // namespace rb::editor::ui
