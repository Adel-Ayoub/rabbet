#include "editor/AssetAssign.h"

#include "editor/EditorContext.h"

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

#include <cstddef>

namespace rb::editor {
namespace {

void assignModel(EditorContext& context, const rb::AssetDatabase::Record& record,
                 rb::Entity target) {
    rb::Scene& scene = context.runtime.scene();
    rb::AssetManager* assets = context.runtime.tryResource<rb::AssetManager>();
    if (assets == nullptr || !scene.alive(target)) {
        return;
    }
    // Import on demand so the reference resolves now; AssetResolveSystem keeps the handle current
    // after. The uuid is what survives save/load. Set it, then let the resolve system repopulate.
    const rb::AssetHandle<rb::ModelAsset> handle = rb::loadModelAsset(*assets, record.path);
    if (!handle.valid()) {
        rb::log::error("assets: failed to import '{}'", record.path.string());
        return;
    }
    rb::ModelRenderer* renderer = scene.tryGet<rb::ModelRenderer>(target);
    if (renderer == nullptr) {
        renderer = &scene.add<rb::ModelRenderer>(target, rb::ModelRenderer{});
    }
    renderer->model = record.id;
    renderer->handle = {};
    rb::log::info("assets: assigned model '{}' to entity {}", record.name, target.index());
}

void assignScript(EditorContext& context, const rb::AssetDatabase::Record& record,
                  rb::Entity target) {
    rb::Scene& scene = context.runtime.scene();
    rb::AssetManager* assets = context.runtime.tryResource<rb::AssetManager>();
    if (assets == nullptr || !scene.alive(target)) {
        return;
    }
    const rb::AssetHandle<rb::ScriptAsset> handle = rb::loadScriptAsset(*assets, record.path);
    if (!handle.valid()) {
        rb::log::error("assets: failed to load script '{}'", record.path.string());
        return;
    }
    rb::ScriptComponent* script = scene.tryGet<rb::ScriptComponent>(target);
    if (script == nullptr) {
        script = &scene.add<rb::ScriptComponent>(target, rb::ScriptComponent{});
    }
    script->script = record.id;
    script->handle = {}; // ScriptAssetResolveSystem repopulates it from the uuid
    script->fields.clear();
    if (const rb::ScriptAsset* asset = assets->get<rb::ScriptAsset>(handle)) {
        rb::introspectScriptFields(asset->source, script->fields);
    }
    rb::log::info("assets: assigned script '{}' to entity {}", record.name, target.index());
}

void assignAudio(EditorContext& context, const rb::AssetDatabase::Record& record,
                 rb::Entity target) {
    rb::Scene& scene = context.runtime.scene();
    rb::AssetManager* assets = context.runtime.tryResource<rb::AssetManager>();
    if (assets == nullptr || !scene.alive(target)) {
        return;
    }
    const rb::AssetHandle<rb::AudioAsset> handle = rb::loadAudioAsset(*assets, record.path);
    if (!handle.valid()) {
        rb::log::error("assets: failed to load audio '{}'", record.path.string());
        return;
    }
    rb::SoundEmitter* emitter = scene.tryGet<rb::SoundEmitter>(target);
    if (emitter == nullptr) {
        emitter = &scene.add<rb::SoundEmitter>(target, rb::SoundEmitter{});
    }
    emitter->sound = record.id;
    emitter->handle = {}; // AudioAssetResolveSystem repopulates it from the uuid
    rb::log::info("assets: assigned audio '{}' to entity {}", record.name, target.index());
}

void assignMaterial(EditorContext& context, const rb::AssetDatabase::Record& record,
                    rb::Entity target) {
    rb::Scene& scene = context.runtime.scene();
    rb::AssetManager* assets = context.runtime.tryResource<rb::AssetManager>();
    if (assets == nullptr || !scene.alive(target)) {
        return;
    }
    const rb::AssetHandle<rb::MaterialAsset> handle = rb::loadMaterialAsset(*assets, record.path);
    if (!handle.valid()) {
        rb::log::error("assets: failed to load material '{}'", record.path.string());
        return;
    }
    rb::MaterialComponent* material = scene.tryGet<rb::MaterialComponent>(target);
    if (material == nullptr) {
        material = &scene.add<rb::MaterialComponent>(target, rb::MaterialComponent{});
    }
    material->material = record.id;
    material->handle = {}; // MaterialAssetResolveSystem repopulates it from the uuid
    rb::log::info("assets: assigned material '{}' to entity {}", record.name, target.index());
}

// A texture is assigned as a particle sprite, so it targets an existing ParticleEmitter rather than
// creating a component (unlike a model, a bare texture is not itself a renderable thing).
void assignTexture(EditorContext& context, const rb::AssetDatabase::Record& record,
                   rb::Entity target) {
    rb::Scene& scene = context.runtime.scene();
    rb::AssetManager* assets = context.runtime.tryResource<rb::AssetManager>();
    if (assets == nullptr || !scene.alive(target)) {
        return;
    }
    rb::ParticleEmitter* emitter = scene.tryGet<rb::ParticleEmitter>(target);
    if (emitter == nullptr) {
        rb::log::warn("assets: drop a texture on an entity with a Particle Emitter to set its sprite");
        return;
    }
    const rb::AssetHandle<rb::TextureAsset> handle = rb::loadTextureAsset(*assets, record.path);
    if (!handle.valid()) {
        rb::log::error("assets: failed to load texture '{}'", record.path.string());
        return;
    }
    emitter->sprite = record.id;
    emitter->handle = {}; // AssetResolveSystem repopulates it from the uuid
    rb::log::info("assets: assigned sprite '{}' to entity {}", record.name, target.index());
}

} // namespace

void assignAssetToEntity(EditorContext& context, const rb::AssetDatabase::Record& record,
                         rb::Entity target) {
    switch (record.type) {
    case rb::AssetType::Model:
        assignModel(context, record, target);
        break;
    case rb::AssetType::Material:
        assignMaterial(context, record, target);
        break;
    case rb::AssetType::Script:
        assignScript(context, record, target);
        break;
    case rb::AssetType::Audio:
        assignAudio(context, record, target);
        break;
    case rb::AssetType::Texture:
        assignTexture(context, record, target);
        break;
    default:
        break;
    }
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

void beginAssetDragSource(const rb::Uuid& id, rb::AssetType type, const std::string& name) {
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        const AssetDragPayload payload{id, type};
        ImGui::SetDragDropPayload(kAssetDragType, &payload, sizeof(payload));
        ImGui::TextUnformatted(name.c_str());
        ImGui::EndDragDropSource();
    }
}

AssetDragPayload acceptAssetDropTarget() {
    AssetDragPayload result;
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetDragType)) {
            if (payload->Data != nullptr &&
                payload->DataSize == static_cast<int>(sizeof(AssetDragPayload))) {
                result = *static_cast<const AssetDragPayload*>(payload->Data);
            }
        }
        ImGui::EndDragDropTarget();
    }
    return result;
}

} // namespace rb::editor
