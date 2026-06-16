#include "editor/panels/AssetsPanel.h"

#include "editor/EditorContext.h"

#include "rabbet/assets/AssetDatabase.h"
#include "rabbet/assets/AssetManager.h"
#include "rabbet/assets/AssetType.h"
#include "rabbet/audio/AudioAsset.h"
#include "rabbet/audio/AudioImport.h"
#include "rabbet/audio/SoundEmitter.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/particle/ParticleEmitter.h"
#include "rabbet/render/MaterialAsset.h"
#include "rabbet/render/MaterialComponent.h"
#include "rabbet/render/MaterialImport.h"
#include "rabbet/render/ModelAsset.h"
#include "rabbet/render/ModelImport.h"
#include "rabbet/render/ModelRenderer.h"
#include "rabbet/render/TextureAsset.h"
#include "rabbet/render/TextureImport.h"
#include "rabbet/scripting/ScriptAsset.h"
#include "rabbet/scripting/ScriptComponent.h"
#include "rabbet/scripting/ScriptImport.h"
#include "rabbet/scripting/ScriptSystem.h"
#include "rabbet/serialize/Prefab.h"
#include "rabbet/serialize/PrefabAsset.h"
#include "rabbet/serialize/PrefabInstance.h"
#include "rabbet/util/Log.h"

#include <imgui.h>

#include <array>
#include <filesystem>
#include <string>

namespace rb::editor {
namespace {

constexpr std::array<rb::AssetType, 8> kBrowseOrder = {
    rb::AssetType::Model,   rb::AssetType::Material, rb::AssetType::Shader,
    rb::AssetType::Script,  rb::AssetType::Audio,    rb::AssetType::Texture,
    rb::AssetType::Scene,   rb::AssetType::Prefab};

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

void assignScriptToEntity(EditorContext& context, const rb::AssetDatabase::Record& record) {
    rb::Scene& scene = context.runtime.scene();
    rb::AssetManager* assets = context.runtime.tryResource<rb::AssetManager>();
    if (assets == nullptr || !scene.alive(context.selected)) {
        return;
    }
    const rb::AssetHandle<rb::ScriptAsset> handle = rb::loadScriptAsset(*assets, record.path);
    if (!handle.valid()) {
        rb::log::error("assets: failed to load script '{}'", record.path.string());
        return;
    }
    rb::ScriptComponent* script = scene.tryGet<rb::ScriptComponent>(context.selected);
    if (script == nullptr) {
        script = &scene.add<rb::ScriptComponent>(context.selected, rb::ScriptComponent{});
    }
    script->script = record.id;
    script->handle = {}; // ScriptAssetResolveSystem repopulates it from the uuid
    script->fields.clear();
    if (const rb::ScriptAsset* asset = assets->get<rb::ScriptAsset>(handle)) {
        rb::introspectScriptFields(asset->source, script->fields);
    }
    rb::log::info("assets: assigned script '{}' to entity {}", record.name,
                  context.selected.index());
}

void assignAudioToEntity(EditorContext& context, const rb::AssetDatabase::Record& record) {
    rb::Scene& scene = context.runtime.scene();
    rb::AssetManager* assets = context.runtime.tryResource<rb::AssetManager>();
    if (assets == nullptr || !scene.alive(context.selected)) {
        return;
    }
    const rb::AssetHandle<rb::AudioAsset> handle = rb::loadAudioAsset(*assets, record.path);
    if (!handle.valid()) {
        rb::log::error("assets: failed to load audio '{}'", record.path.string());
        return;
    }
    rb::SoundEmitter* emitter = scene.tryGet<rb::SoundEmitter>(context.selected);
    if (emitter == nullptr) {
        emitter = &scene.add<rb::SoundEmitter>(context.selected, rb::SoundEmitter{});
    }
    emitter->sound = record.id;
    emitter->handle = {}; // AudioAssetResolveSystem repopulates it from the uuid
    rb::log::info("assets: assigned audio '{}' to entity {}", record.name,
                  context.selected.index());
}

void assignMaterialToEntity(EditorContext& context, const rb::AssetDatabase::Record& record) {
    rb::Scene& scene = context.runtime.scene();
    rb::AssetManager* assets = context.runtime.tryResource<rb::AssetManager>();
    if (assets == nullptr || !scene.alive(context.selected)) {
        return;
    }
    const rb::AssetHandle<rb::MaterialAsset> handle = rb::loadMaterialAsset(*assets, record.path);
    if (!handle.valid()) {
        rb::log::error("assets: failed to load material '{}'", record.path.string());
        return;
    }
    rb::MaterialComponent* material = scene.tryGet<rb::MaterialComponent>(context.selected);
    if (material == nullptr) {
        material = &scene.add<rb::MaterialComponent>(context.selected, rb::MaterialComponent{});
    }
    material->material = record.id;
    material->handle = {}; // MaterialAssetResolveSystem repopulates it from the uuid
    rb::log::info("assets: assigned material '{}' to entity {}", record.name,
                  context.selected.index());
}

// A texture is assigned as a particle sprite, so it targets an existing ParticleEmitter rather than
// creating a component (unlike a model, a bare texture is not itself a renderable thing).
void assignTextureToEntity(EditorContext& context, const rb::AssetDatabase::Record& record) {
    rb::Scene& scene = context.runtime.scene();
    rb::AssetManager* assets = context.runtime.tryResource<rb::AssetManager>();
    if (assets == nullptr || !scene.alive(context.selected)) {
        return;
    }
    rb::ParticleEmitter* emitter = scene.tryGet<rb::ParticleEmitter>(context.selected);
    if (emitter == nullptr) {
        rb::log::warn("assets: select an entity with a Particle Emitter to assign a sprite");
        return;
    }
    const rb::AssetHandle<rb::TextureAsset> handle = rb::loadTextureAsset(*assets, record.path);
    if (!handle.valid()) {
        rb::log::error("assets: failed to load texture '{}'", record.path.string());
        return;
    }
    emitter->sprite = record.id;
    emitter->handle = {}; // AssetResolveSystem repopulates it from the uuid
    rb::log::info("assets: assigned sprite '{}' to entity {}", record.name,
                  context.selected.index());
}

void instantiatePrefabAsset(EditorContext& context, const rb::AssetDatabase::Record& record) {
    rb::AssetManager* assets = context.runtime.tryResource<rb::AssetManager>();
    if (assets == nullptr) {
        return;
    }
    const rb::AssetHandle<rb::PrefabAsset> handle = rb::loadPrefabAsset(*assets, record.path);
    rb::PrefabAsset* prefab = assets->get<rb::PrefabAsset>(handle);
    if (prefab == nullptr) {
        rb::log::error("assets: failed to load prefab '{}'", record.path.string());
        return;
    }
    const rb::Entity e = rb::instantiatePrefab(context.runtime.scene(), context.registry, *prefab);
    context.runtime.scene().add<rb::PrefabInstance>(e, rb::PrefabInstance{record.id, {}});
    context.selected = e;
    rb::log::info("assets: instantiated prefab '{}' as entity {}", record.name, e.index());
}

void assignToEntity(EditorContext& context, const rb::AssetDatabase::Record& record) {
    if (record.type == rb::AssetType::Model) {
        assignModelToEntity(context, record);
    } else if (record.type == rb::AssetType::Material) {
        assignMaterialToEntity(context, record);
    } else if (record.type == rb::AssetType::Script) {
        assignScriptToEntity(context, record);
    } else if (record.type == rb::AssetType::Audio) {
        assignAudioToEntity(context, record);
    } else if (record.type == rb::AssetType::Texture) {
        assignTextureToEntity(context, record);
    }
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

    const bool isPrefab = selected != nullptr && selected->type == rb::AssetType::Prefab;
    if (isPrefab) {
        // A prefab is instantiated into a new entity rather than assigned to the selected one.
        if (ImGui::Button("Instantiate")) {
            instantiatePrefabAsset(m_context, *selected);
        }
    } else {
        const bool assignable = selected != nullptr && (selected->type == rb::AssetType::Model ||
                                                        selected->type == rb::AssetType::Material ||
                                                        selected->type == rb::AssetType::Script ||
                                                        selected->type == rb::AssetType::Audio ||
                                                        selected->type == rb::AssetType::Texture);
        const bool canAssign = assignable && m_context.runtime.scene().alive(m_context.selected);
        ImGui::BeginDisabled(!canAssign);
        if (ImGui::Button("Assign to selected entity") && selected != nullptr) {
            assignToEntity(m_context, *selected);
        }
        ImGui::EndDisabled();
    }
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
