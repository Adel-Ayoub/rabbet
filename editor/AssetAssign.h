#pragma once

#include "rabbet/assets/AssetDatabase.h"
#include "rabbet/assets/AssetType.h"
#include "rabbet/core/Uuid.h"
#include "rabbet/ecs/Entity.h"

#include <string>

namespace rb::editor {

struct EditorContext;

// The ImGui drag-drop payload type carrying an asset between the Assets browser and the
// Hierarchy / Inspector drop targets.
inline constexpr const char* kAssetDragType = "RB_ASSET";

// Copied by value into ImGui's payload buffer, so it stays trivially copyable. The type rides
// along so a drop target can validate the asset kind without reaching the AssetDatabase
// (registry-hook drawers like the particle emitter's have no way to get at it).
struct AssetDragPayload {
    rb::Uuid id;
    rb::AssetType type = rb::AssetType::Unknown;

    [[nodiscard]] bool valid() const noexcept { return id.valid(); }
};

// Assigns an asset to `target` by its type: Model -> ModelRenderer, Material -> MaterialComponent,
// Script -> ScriptComponent, Audio -> SoundEmitter, Texture -> the target's ParticleEmitter sprite.
// Imports on demand; the resolve systems repopulate the runtime handle from the uuid. A prefab is
// not assignable (instantiate it instead).
void assignAssetToEntity(EditorContext& context, const rb::AssetDatabase::Record& record,
                         rb::Entity target);

// Instantiates a prefab into a new entity and selects it.
void instantiatePrefabAsset(EditorContext& context, const rb::AssetDatabase::Record& record);

// Emits an asset drag payload for the last submitted item, with `name` as the drag preview.
// Call between an item and the next, inside a window.
void beginAssetDragSource(const rb::Uuid& id, rb::AssetType type, const std::string& name);

// Accepts an asset drop on the last submitted item, returning the dropped payload (invalid when
// no drop occurred this frame). Call right after the target item.
[[nodiscard]] AssetDragPayload acceptAssetDropTarget();

} // namespace rb::editor
