#include "editor/panels/AssetsPanel.h"

#include "editor/AssetAssign.h"
#include "editor/EditorContext.h"
#include "editor/Icons.h"
#include "editor/Palette.h"
#include "editor/ThumbnailRenderer.h"
#include "editor/Widgets.h"

#include "rabbet/assets/AssetDatabase.h"
#include "rabbet/assets/AssetManager.h"
#include "rabbet/assets/AssetTree.h"
#include "rabbet/assets/AssetType.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/serialize/SceneSerializer.h"
#include "rabbet/util/Log.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

namespace rb::editor {
namespace {

constexpr float kTile = 72.0f;

[[nodiscard]] bool isAssignable(rb::AssetType type) {
    return type == rb::AssetType::Model || type == rb::AssetType::Material ||
           type == rb::AssetType::Script || type == rb::AssetType::Audio ||
           type == rb::AssetType::Texture;
}

[[nodiscard]] std::string ellipsize(const std::string& text, std::size_t limit) {
    if (text.size() <= limit) {
        return text;
    }
    return text.substr(0, limit > 2 ? limit - 2 : limit) + "..";
}

[[nodiscard]] bool matchesSearch(const std::string& name, const char* needle) {
    const auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };
    return lower(name).find(lower(needle)) != std::string::npos;
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
        const std::string label =
            std::string(icon::kFolder) + "  " + folder.name + "##" + folder.path.string();
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
    const Palette& p = palette();

    const rb::AssetDatabase::Record* selected =
        m_selected.valid() ? database->find(m_selected) : nullptr;

    // Action bar: assign the selected asset to the selected entity, or instantiate a prefab.
    if (selected != nullptr && selected->type == rb::AssetType::Prefab) {
        if (ImGui::Button((std::string(icon::kPackage) + "  Instantiate").c_str())) {
            instantiatePrefabAsset(m_context, *selected);
        }
    } else if (selected != nullptr && selected->type == rb::AssetType::Scene) {
        // Replaces the current scene wholesale; gated out of Play so a load can never
        // fight the Stop-time snapshot restore. No clear first: loadFromFile clears only
        // once the document proves to be a scene, so a refused file keeps the live scene.
        ImGui::BeginDisabled(m_context.runtime.inPlaySession());
        if (ImGui::Button((std::string(icon::kFile) + "  Load Scene").c_str())) {
            if (rb::SceneSerializer::loadFromFile(m_context.runtime.scene(), m_context.registry,
                                                  selected->path)) {
                m_context.selected = {};
                // A panel load is an Open: plain Save must target this file from now on,
                // not whichever path was tracked before the swap.
                m_context.scenePath = selected->path.string();
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
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s %s (%s)", ui::assetGlyph(selected->type), selected->name.c_str(),
                            std::string(rb::assetTypeName(selected->type)).c_str());
    } else {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%zu assets", database->size());
    }

    // Right block: search box + view toggles.
    const float rightBlock = 170.0f + 2.0f * 26.0f + 3.0f * ImGui::GetStyle().ItemInnerSpacing.x;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowSize().x - rightBlock));
    ImGui::SetNextItemWidth(170.0f);
    ImGui::InputTextWithHint("##assetSearch", "Search assets", m_search, sizeof(m_search));
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    const auto viewToggle = [&](const char* glyph, bool active, const char* tip) {
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Text, p.accent);
        }
        const bool clicked = ImGui::Button(glyph, ImVec2(26.0f, 0.0f));
        if (active) {
            ImGui::PopStyleColor();
        }
        ImGui::SetItemTooltip("%s", tip);
        return clicked;
    };
    if (viewToggle(icon::kLayoutGrid, m_grid, "Grid view")) {
        m_grid = true;
    }
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    if (viewToggle(icon::kList, !m_grid, "List view")) {
        m_grid = false;
    }

    const bool searching = m_search[0] != '\0';

    // Breadcrumbs while browsing; a match count while searching.
    if (searching) {
        int matches = 0;
        for (const rb::AssetDatabase::Record& record : database->records()) {
            if (matchesSearch(record.name, m_search)) {
                ++matches;
            }
        }
        ImGui::TextDisabled("%d match%s", matches, matches == 1 ? "" : "es");
    } else {
        if (ImGui::SmallButton((std::string(icon::kFolderOpen) + " assets").c_str())) {
            m_folder.clear();
        }
        // Navigation is applied after the loop: assigning m_folder mid-iteration would
        // invalidate the path iterators driving the range-for.
        std::filesystem::path crumb;
        std::filesystem::path target;
        bool navigate = false;
        for (const std::filesystem::path& part : m_folder) {
            crumb /= part;
            ImGui::SameLine(0.0f, 2.0f);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", icon::kChevronRight);
            ImGui::SameLine(0.0f, 2.0f);
            if (ImGui::SmallButton((part.string() + "##bc_" + crumb.string()).c_str())) {
                target = crumb;
                navigate = true;
            }
        }
        if (navigate) {
            m_folder = target;
        }
    }
    ImGui::Separator();

    const rb::AssetTree tree = rb::buildAssetTree(database->root(), database->records());

    // Left: folder tree. Right: the current folder's contents (or search matches).
    ImGui::BeginChild("##assetTree", ImVec2(150.0f, 0.0f), ImGuiChildFlags_Borders);
    {
        ImGuiTreeNodeFlags rootFlags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Leaf;
        if (m_folder.empty()) {
            rootFlags |= ImGuiTreeNodeFlags_Selected;
        }
        ImGui::TreeNodeEx((std::string(icon::kFolderOpen) + "  assets##root").c_str(), rootFlags);
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

    // One tile: preview image (or a large kind glyph), selection ring, kind badge,
    // centered caption; the drag source and the click-to-select both live on the tile.
    const auto drawTile = [&](const rb::Uuid& id, const std::string& name, rb::AssetType type) {
        ImGui::PushID(id.toString().c_str());
        ImGui::BeginGroup();
        unsigned int texture = 0;
        const rb::AssetDatabase::Record* record = database->find(id);
        if (thumbs != nullptr && assets != nullptr && record != nullptr) {
            texture = thumbs->thumbnail(*assets, *record);
        }
        const bool isSelected = id == m_selected;
        bool clicked = false;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
        if (texture != 0) {
            clicked = ImGui::ImageButton("##t", static_cast<ImTextureID>(texture),
                                         ImVec2(kTile, kTile), ImVec2(0.0f, 1.0f),
                                         ImVec2(1.0f, 0.0f));
        } else {
            clicked = ImGui::Button("##t", ImVec2(kTile, kTile));
        }
        ImGui::PopStyleVar();
        if (clicked) {
            m_selected = id;
        }
        beginAssetDragSource(id, type, name);

        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 tileMin = ImGui::GetItemRectMin();
        const ImVec2 tileMax = ImGui::GetItemRectMax();
        if (texture == 0) {
            // Placeholder: the kind glyph, centered.
            const ImVec2 glyphSize = ImGui::CalcTextSize(ui::assetGlyph(type));
            draw->AddText(ImVec2(tileMin.x + (kTile - glyphSize.x) * 0.5f,
                                 tileMin.y + (kTile - glyphSize.y) * 0.5f),
                          ImGui::GetColorU32(p.inkMuted), ui::assetGlyph(type));
        } else {
            // Kind badge in the corner of the preview.
            const ImVec2 badgeMin(tileMin.x + 3.0f, tileMax.y - 19.0f);
            const ImVec2 badgeMax(badgeMin.x + 20.0f, badgeMin.y + 16.0f);
            draw->AddRectFilled(badgeMin, badgeMax, ImGui::GetColorU32(withAlpha(p.base, 0.85f)),
                                p.roundWidget);
            draw->AddText(ImVec2(badgeMin.x + 3.0f, badgeMin.y + 1.0f),
                          ImGui::GetColorU32(p.ink), ui::assetGlyph(type));
        }
        if (isSelected) {
            draw->AddRect(tileMin, tileMax, ImGui::GetColorU32(p.accent), p.roundWidget, 0, 2.0f);
        } else if (ImGui::IsItemHovered()) {
            draw->AddRect(tileMin, tileMax, ImGui::GetColorU32(p.press), p.roundWidget, 0, 1.0f);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s (%s)", name.c_str(),
                              std::string(rb::assetTypeName(type)).c_str());
        }

        const std::string caption = ellipsize(name, 11);
        const float captionWidth = ImGui::CalcTextSize(caption.c_str()).x;
        ImGui::SetCursorScreenPos(
            ImVec2(tileMin.x + std::max(0.0f, (kTile - captionWidth) * 0.5f),
                   ImGui::GetCursorScreenPos().y));
        ImGui::TextUnformatted(caption.c_str());
        ImGui::EndGroup();
        ImGui::PopID();
    };

    ImGui::BeginChild("##assetContents", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    const float cell = kTile + ImGui::GetStyle().ItemSpacing.x;
    const int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cell));
    int column = 0;
    const auto wrap = [&]() {
        if (column % columns != columns - 1) {
            ImGui::SameLine();
        }
        ++column;
    };

    if (searching) {
        int shown = 0;
        for (const rb::AssetDatabase::Record& record : database->records()) {
            if (!matchesSearch(record.name, m_search)) {
                continue;
            }
            ++shown;
            if (m_grid) {
                drawTile(record.id, record.name, record.type);
                wrap();
            } else {
                const std::string label = std::string(ui::assetGlyph(record.type)) + "  " +
                                          record.name + "    (" +
                                          std::string(rb::assetTypeName(record.type)) + ")##" +
                                          record.id.toString();
                if (ImGui::Selectable(label.c_str(), record.id == m_selected)) {
                    m_selected = record.id;
                }
                beginAssetDragSource(record.id, record.type, record.name);
            }
        }
        if (shown == 0) {
            ImGui::Dummy(ImVec2(0.0f, 10.0f));
            ImGui::TextDisabled("No assets match '%s'", m_search);
        }
    } else if (m_grid) {
        for (const rb::AssetTree& folder : node->folders) {
            ImGui::PushID(("d_" + folder.path.string()).c_str());
            ImGui::BeginGroup();
            if (ImGui::Button("##f", ImVec2(kTile, kTile))) {
                m_folder = folder.path;
            }
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const ImVec2 tileMin = ImGui::GetItemRectMin();
            const ImVec2 glyphSize = ImGui::CalcTextSize(icon::kFolder);
            draw->AddText(ImVec2(tileMin.x + (kTile - glyphSize.x) * 0.5f,
                                 tileMin.y + (kTile - glyphSize.y) * 0.5f),
                          ImGui::GetColorU32(p.warn), icon::kFolder);
            const std::string caption = ellipsize(folder.name, 11);
            const float captionWidth = ImGui::CalcTextSize(caption.c_str()).x;
            ImGui::SetCursorScreenPos(
                ImVec2(tileMin.x + std::max(0.0f, (kTile - captionWidth) * 0.5f),
                       ImGui::GetCursorScreenPos().y));
            ImGui::TextUnformatted(caption.c_str());
            ImGui::EndGroup();
            ImGui::PopID();
            wrap();
        }
        for (const rb::AssetTree::Leaf& leaf : node->assets) {
            drawTile(leaf.id, leaf.name, leaf.type);
            wrap();
        }
        if (node->folders.empty() && node->assets.empty()) {
            ImGui::Dummy(ImVec2(0.0f, 10.0f));
            ImGui::TextDisabled("Empty folder");
        }
    } else {
        for (const rb::AssetTree& folder : node->folders) {
            const std::string label = std::string(icon::kFolder) + "  " + folder.name + "##d_" +
                                      folder.path.string();
            if (ImGui::Selectable(label.c_str())) {
                m_folder = folder.path;
            }
        }
        for (const rb::AssetTree::Leaf& leaf : node->assets) {
            const std::string label = std::string(ui::assetGlyph(leaf.type)) + "  " + leaf.name +
                                      "    (" + std::string(rb::assetTypeName(leaf.type)) + ")##" +
                                      leaf.id.toString();
            if (ImGui::Selectable(label.c_str(), leaf.id == m_selected)) {
                m_selected = leaf.id;
            }
            beginAssetDragSource(leaf.id, leaf.type, leaf.name);
        }
        if (node->folders.empty() && node->assets.empty()) {
            ImGui::Dummy(ImVec2(0.0f, 10.0f));
            ImGui::TextDisabled("Empty folder");
        }
    }
    ImGui::EndChild();
}

} // namespace rb::editor
