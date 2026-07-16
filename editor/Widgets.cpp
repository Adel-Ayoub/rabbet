#include "editor/Widgets.h"

#include "editor/Icons.h"
#include "editor/Palette.h"

#include <algorithm>
#include <cfloat>
#include <string>

namespace rb::editor::ui {
namespace {

constexpr float kClearButtonWidth = 22.0f;

std::string hiddenId(const char* label) {
    return std::string("##") + label;
}

} // namespace

float labelColumnWidth() {
    return std::clamp(ImGui::GetWindowWidth() * 0.38f, 90.0f, 190.0f);
}

void rowLabel(const char* label) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(labelColumnWidth());
    ImGui::SetNextItemWidth(-FLT_MIN);
}

bool dragFloat(const char* label, float* value, float speed, float min, float max) {
    rowLabel(label);
    return ImGui::DragFloat(hiddenId(label).c_str(), value, speed, min, max);
}

bool dragFloat2(const char* label, float* values, float speed, float min, float max) {
    rowLabel(label);
    return ImGui::DragFloat2(hiddenId(label).c_str(), values, speed, min, max);
}

bool dragFloat3(const char* label, float* values, float speed, float min, float max) {
    rowLabel(label);
    return ImGui::DragFloat3(hiddenId(label).c_str(), values, speed, min, max);
}

bool dragInt(const char* label, int* value, float speed, int min, int max) {
    rowLabel(label);
    return ImGui::DragInt(hiddenId(label).c_str(), value, speed, min, max);
}

bool checkbox(const char* label, bool* value) {
    rowLabel(label);
    return ImGui::Checkbox(hiddenId(label).c_str(), value);
}

bool colorEdit3(const char* label, float* rgb, ImGuiColorEditFlags flags) {
    rowLabel(label);
    return ImGui::ColorEdit3(hiddenId(label).c_str(), rgb, flags);
}

bool colorEdit4(const char* label, float* rgba, ImGuiColorEditFlags flags) {
    rowLabel(label);
    return ImGui::ColorEdit4(hiddenId(label).c_str(), rgba, flags);
}

bool inputText(const char* label, char* buffer, std::size_t size) {
    rowLabel(label);
    return ImGui::InputText(hiddenId(label).c_str(), buffer, size);
}

bool beginCombo(const char* label, const char* preview) {
    rowLabel(label);
    return ImGui::BeginCombo(hiddenId(label).c_str(), preview);
}

const char* assetGlyph(rb::AssetType type) {
    switch (type) {
    case rb::AssetType::Model:
        return icon::kBoxes;
    case rb::AssetType::Mesh:
        return icon::kBox;
    case rb::AssetType::Material:
        return icon::kLayers;
    case rb::AssetType::Shader:
        return icon::kFileCode;
    case rb::AssetType::Script:
        return icon::kFileCode;
    case rb::AssetType::Audio:
        return icon::kMusic;
    case rb::AssetType::Texture:
        return icon::kImage;
    case rb::AssetType::Prefab:
        return icon::kPackage;
    case rb::AssetType::Scene:
        return icon::kFile;
    case rb::AssetType::Unknown:
        break;
    }
    return icon::kFile;
}

SlotResult assetSlot(const char* label, rb::AssetType type, const rb::Uuid& id, bool resolved,
                     bool droppable) {
    SlotResult result;
    const Palette& p = palette();
    ImGui::PushID(label);

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(labelColumnWidth());

    const bool bound = id.valid();
    const float clearWidth =
        bound ? kClearButtonWidth + ImGui::GetStyle().ItemInnerSpacing.x : 0.0f;
    const float slotWidth = std::max(60.0f, ImGui::GetContentRegionAvail().x - clearWidth);

    std::string text = std::string(assetGlyph(type)) + "  ";
    if (bound) {
        text += id.toString().substr(0, 8);
    } else {
        text += "None (" + std::string(rb::assetTypeName(type)) + ")";
    }

    // The slot reads as a field, not a button.
    ImGui::PushStyleColor(ImGuiCol_Button, p.layer);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, p.lift);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, p.lift);
    int textStyles = 0;
    if (!bound) {
        ImGui::PushStyleColor(ImGuiCol_Text, p.inkMuted);
        textStyles = 1;
    } else if (!resolved) {
        ImGui::PushStyleColor(ImGuiCol_Text, p.warn);
        textStyles = 1;
    }
    ImGui::Button(text.c_str(), ImVec2(slotWidth, 0.0f));
    if (textStyles > 0) {
        ImGui::PopStyleColor(textStyles);
    }
    ImGui::PopStyleColor(3);

    if (ImGui::IsItemHovered()) {
        if (bound) {
            ImGui::SetTooltip("%s\n%s", id.toString().c_str(),
                              resolved ? "resolved" : "unresolved (reimport or reassign)");
        } else if (droppable) {
            ImGui::SetTooltip("Drop a %s asset from the Assets panel",
                              std::string(rb::assetTypeName(type)).c_str());
        }
    }
    if (droppable) {
        result.dropped = acceptAssetDropTarget();
        if (result.dropped.valid() && result.dropped.type != type) {
            result.dropped = AssetDragPayload{};
        }
    }

    if (bound) {
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        if (ImGui::Button(icon::kX, ImVec2(kClearButtonWidth, 0.0f))) {
            result.cleared = true;
        }
        ImGui::SetItemTooltip("Clear");
    }

    ImGui::PopID();
    return result;
}

} // namespace rb::editor::ui
