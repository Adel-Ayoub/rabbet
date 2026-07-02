#include "editor/panels/AssetsPanel.h"

#include "editor/AssetAssign.h"
#include "editor/EditorContext.h"
#include "editor/ThumbnailRenderer.h"

#include "rabbet/assets/AssetDatabase.h"
#include "rabbet/assets/AssetManager.h"
#include "rabbet/assets/AssetTree.h"
#include "rabbet/assets/AssetType.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/serialize/SceneSerializer.h"
#include "rabbet/util/Log.h"

#include <imgui.h>

#include <filesystem>
#include <string>
#include <vector>

namespace rb::editor {
namespace {

[[nodiscard]] bool isAssignable(rb::AssetType type) {
    return type == rb::AssetType::Model || type == rb::AssetType::Material ||
           type == rb::AssetType::Script || type == rb::AssetType::Audio ||
           type == rb::AssetType::Texture;
}

// Short tag drawn on placeholder tiles for asset kinds with no rendered preview.
[[nodiscard]] const char* typeTag(rb::AssetType type) {
    switch (type) {
    case rb::AssetType::Script:
        return "LUA";
    case rb::AssetType::Audio:
        return "SND";
    case rb::AssetType::Scene:
        return "SCN";
    case rb::AssetType::Prefab:
        return "PFB";
    case rb::AssetType::Shader:
        return "GLSL";
    case rb::AssetType::Mesh:
        return "MSH";
    case rb::AssetType::Material:
        return "MAT";
    case rb::AssetType::Model:
        return "MDL";
    case rb::AssetType::Texture:
        return "TEX";
    case rb::AssetType::Unknown:
        break;
    }
    return "?";
}

[[nodiscard]] std::string ellipsize(const std::string& text, std::size_t limit) {
    if (text.size() <= limit) {
        return text;
    }
    return text.substr(0, limit > 2 ? limit - 2 : limit) + "..";
}

// Descends to the folder node addressed by `rel` (relative to the root); nullptr if it is gone.
[[nodiscard]] const rb::AssetTree* folderAt(const rb::AssetTree& root,
                                            const std::filesystem::path& rel) {
    const rb::AssetTree* node = &root;
    for (const std::filesystem::path& part : rel) {
        const std::string component = part.string();
        if (component.empty() || component == ".") {
            continue;
        }
        const rb::AssetTree* next = nullptr;
        for (const rb::AssetTree& folder : node->folders) {
            if (folder.name == component) {
                next = &folder;
                break;
            }
        }
        if (next == nullptr) {
            return nullptr;
        }
        node = next;
    }
    return node;
}

void drawFolderTree(const rb::AssetTree& node, std::filesystem::path& current) {
    for (const rb::AssetTree& folder : node.folders) {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (folder.folders.empty()) {
            flags |= ImGuiTreeNodeFlags_Leaf;
        }
        if (folder.path == current) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }
        const std::string label = folder.name + "##" + folder.path.string();
        const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            current = folder.path;
        }
        if (open) {
            drawFolderTree(folder, current);
            ImGui::TreePop();
        }
    }
}

} // namespace

void AssetsPanel::onImGui() {
    rb::AssetDatabase* database = m_context.runtime.tryResource<rb::AssetDatabase>();
    if (database == nullptr) {
        ImGui::TextDisabled("No asset database");
        return;
    }
    rb::AssetManager* assets = m_context.runtime.tryResource<rb::AssetManager>();
    ThumbnailRenderer* thumbs = m_context.thumbnails;

    const rb::AssetDatabase::Record* selected =
        m_selected.valid() ? database->find(m_selected) : nullptr;

    // Action bar: assign the selected asset to the selected entity, or instantiate a prefab.
    if (selected != nullptr && selected->type == rb::AssetType::Prefab) {
        if (ImGui::Button("Instantiate")) {
            instantiatePrefabAsset(m_context, *selected);
        }
    } else if (selected != nullptr && selected->type == rb::AssetType::Scene) {
        // Replaces the current scene wholesale; gated out of Play so a load can never
        // fight the Stop-time snapshot restore.
        ImGui::BeginDisabled(m_context.runtime.inPlaySession());
        if (ImGui::Button("Load Scene")) {
            m_context.runtime.scene().clear();
            if (rb::SceneSerializer::loadFromFile(m_context.runtime.scene(), m_context.registry,
                                                  selected->path)) {
                m_context.selected = {};
                rb::log::info("assets: loaded scene '{}'", selected->name);
            } else {
                rb::log::error("assets: failed to load scene '{}'", selected->path.string());
            }
        }
        ImGui::EndDisabled();
    } else {
        const bool canAssign = selected != nullptr && isAssignable(selected->type) &&
                               m_context.runtime.scene().alive(m_context.selected);
        ImGui::BeginDisabled(!canAssign);
        if (ImGui::Button("Assign to selected entity") && selected != nullptr) {
            assignAssetToEntity(m_context, *selected, m_context.selected);
        }
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (selected != nullptr) {
        ImGui::TextDisabled("%s (%s)", selected->name.c_str(),
                            std::string(rb::assetTypeName(selected->type)).c_str());
    } else {
        ImGui::TextDisabled("%zu assets", database->size());
    }
    ImGui::SameLine();
    // Right-align the grid/list toggle.
    ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x +
                         ImGui::GetCursorPosX() - 110.0f);
    if (ImGui::SmallButton(m_grid ? "List view" : "Grid view")) {
        m_grid = !m_grid;
    }

    // Breadcrumbs: clickable path from the root to the current folder.
    if (ImGui::SmallButton("assets")) {
        m_folder.clear();
    }
    std::filesystem::path crumb;
    for (const std::filesystem::path& part : m_folder) {
        crumb /= part;
        ImGui::SameLine();
        ImGui::TextUnformatted(">");
        ImGui::SameLine();
        if (ImGui::SmallButton((part.string() + "##bc_" + crumb.string()).c_str())) {
            m_folder = crumb;
        }
    }
    ImGui::Separator();

    const rb::AssetTree tree = rb::buildAssetTree(database->root(), database->records());

    // Left: folder tree. Right: the current folder's contents.
    ImGui::BeginChild("##assetTree", ImVec2(150.0f, 0.0f), ImGuiChildFlags_Borders);
    {
        ImGuiTreeNodeFlags rootFlags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Leaf;
        if (m_folder.empty()) {
            rootFlags |= ImGuiTreeNodeFlags_Selected;
        }
        ImGui::TreeNodeEx("assets##root", rootFlags);
        if (ImGui::IsItemClicked()) {
            m_folder.clear();
        }
        ImGui::TreePop();
        drawFolderTree(tree, m_folder);
    }
    ImGui::EndChild();

    ImGui::SameLine();

    const rb::AssetTree* node = folderAt(tree, m_folder);
    if (node == nullptr) {
        m_folder.clear();
        node = &tree;
    }

    ImGui::BeginChild("##assetContents", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    if (m_grid) {
        const float tile = 72.0f;
        const float cell = tile + ImGui::GetStyle().ItemSpacing.x;
        const int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cell));
        int column = 0;
        const auto wrap = [&]() {
            if (column % columns != columns - 1) {
                ImGui::SameLine();
            }
            ++column;
        };

        for (const rb::AssetTree& folder : node->folders) {
            ImGui::PushID(("d_" + folder.path.string()).c_str());
            ImGui::BeginGroup();
            if (ImGui::Button((ellipsize(folder.name, 9) + "##f").c_str(), ImVec2(tile, tile))) {
                m_folder = folder.path;
            }
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + tile);
            ImGui::TextUnformatted(ellipsize(folder.name, 11).c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndGroup();
            ImGui::PopID();
            wrap();
        }

        for (const rb::AssetTree::Leaf& leaf : node->assets) {
            ImGui::PushID(leaf.id.toString().c_str());
            ImGui::BeginGroup();
            unsigned int texture = 0;
            const rb::AssetDatabase::Record* record = database->find(leaf.id);
            if (thumbs != nullptr && assets != nullptr && record != nullptr) {
                texture = thumbs->thumbnail(*assets, *record);
            }
            const bool isSelected = leaf.id == m_selected;
            bool clicked = false;
            if (texture != 0) {
                const ImVec4 bg = isSelected ? ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive)
                                             : ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
                clicked = ImGui::ImageButton("##t", static_cast<ImTextureID>(texture),
                                             ImVec2(tile, tile), ImVec2(0.0f, 1.0f),
                                             ImVec2(1.0f, 0.0f), bg);
            } else {
                if (isSelected) {
                    ImGui::PushStyleColor(ImGuiCol_Button,
                                          ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                }
                clicked = ImGui::Button((std::string(typeTag(leaf.type)) + "##t").c_str(),
                                        ImVec2(tile, tile));
                if (isSelected) {
                    ImGui::PopStyleColor();
                }
            }
            if (clicked) {
                m_selected = leaf.id;
            }
            beginAssetDragSource(leaf.id, leaf.type, leaf.name);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + tile);
            ImGui::TextUnformatted(ellipsize(leaf.name, 11).c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndGroup();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s (%s)", leaf.name.c_str(),
                                  std::string(rb::assetTypeName(leaf.type)).c_str());
            }
            ImGui::PopID();
            wrap();
        }
    } else {
        for (const rb::AssetTree& folder : node->folders) {
            const std::string label = "[ " + folder.name + " ]##d_" + folder.path.string();
            if (ImGui::Selectable(label.c_str())) {
                m_folder = folder.path;
            }
        }
        for (const rb::AssetTree::Leaf& leaf : node->assets) {
            const std::string label =
                leaf.name + "    (" + std::string(rb::assetTypeName(leaf.type)) + ")##" +
                leaf.id.toString();
            if (ImGui::Selectable(label.c_str(), leaf.id == m_selected)) {
                m_selected = leaf.id;
            }
            beginAssetDragSource(leaf.id, leaf.type, leaf.name);
        }
    }
    ImGui::EndChild();
}

} // namespace rb::editor
