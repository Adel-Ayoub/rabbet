#include "editor/panels/AssetsPanel.h"

#include "editor/EditorContext.h"

#include "rabbet/assets/AssetDatabase.h"
#include "rabbet/assets/AssetManager.h"
#include "rabbet/assets/AssetType.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/render/ModelAsset.h"
#include "rabbet/render/ModelImport.h"
#include "rabbet/render/ModelRenderer.h"
#include "rabbet/util/Log.h"

#include <imgui.h>

#include <array>
#include <filesystem>
#include <string>

namespace rb::editor {
namespace {

constexpr std::array<rb::AssetType, 4> kBrowseOrder = {
    rb::AssetType::Model, rb::AssetType::Texture, rb::AssetType::Scene, rb::AssetType::Prefab};

void assignModelToEntity(EditorContext& context, const rb::AssetDatabase::Record& record) {
    rb::Scene& scene = context.runtime.scene();
    rb::AssetManager* assets = context.runtime.tryResource<rb::AssetManager>();
    if (assets == nullptr || !scene.alive(context.selected)) {
        return;
    }
    // Import on demand so the reference resolves now; AssetResolveSystem keeps the
    // handle current after. The uuid is what survives save/load — set it, then let
    // the resolve system repopulate the runtime handle.
    const rb::AssetHandle<rb::ModelAsset> handle = rb::loadModelAsset(*assets, record.path);
    if (!handle.valid()) {
        rb::log::error("assets: failed to import '{}'", record.path.string());
        return;
    }
    rb::ModelRenderer* renderer = scene.tryGet<rb::ModelRenderer>(context.selected);
    if (renderer == nullptr) {
        renderer = &scene.add<rb::ModelRenderer>(context.selected, rb::ModelRenderer{});
    }
    renderer->model = record.id;
    renderer->handle = {};
    rb::log::info("assets: assigned model '{}' to entity {}", record.name,
                  context.selected.index());
}

} // namespace

void AssetsPanel::onImGui() {
    rb::AssetDatabase* database = m_context.runtime.tryResource<rb::AssetDatabase>();
    if (database == nullptr) {
        ImGui::TextDisabled("No asset database");
        return;
    }

    const rb::AssetDatabase::Record* selected =
        m_selected.valid() ? database->find(m_selected) : nullptr;

    const bool canAssign = selected != nullptr && selected->type == rb::AssetType::Model &&
                           m_context.runtime.scene().alive(m_context.selected);
    ImGui::BeginDisabled(!canAssign);
    if (ImGui::Button("Assign to selected entity") && selected != nullptr) {
        assignModelToEntity(m_context, *selected);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (selected != nullptr) {
        ImGui::TextDisabled("%s (%s)", selected->name.c_str(),
                            std::string(rb::assetTypeName(selected->type)).c_str());
    } else {
        ImGui::TextDisabled("%zu assets — select one", database->size());
    }
    ImGui::Separator();

    const std::filesystem::path& root = database->root();
    for (const rb::AssetType type : kBrowseOrder) {
        int count = 0;
        for (const rb::AssetDatabase::Record& record : database->records()) {
            if (record.type == type) {
                ++count;
            }
        }
        if (count == 0) {
            continue;
        }
        const std::string header =
            std::string(rb::assetTypeName(type)) + " (" + std::to_string(count) + ")";
        if (!ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            continue;
        }
        for (const rb::AssetDatabase::Record& record : database->records()) {
            if (record.type != type) {
                continue;
            }
            const std::string rel = record.path.lexically_relative(root).string();
            const std::string label = record.name + "    " + rel + "##" + record.id.toString();
            if (ImGui::Selectable(label.c_str(), record.id == m_selected)) {
                m_selected = record.id;
            }
        }
    }
}

} // namespace rb::editor
