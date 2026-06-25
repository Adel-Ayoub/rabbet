#pragma once

#include "rabbet/assets/AssetDatabase.h"
#include "rabbet/core/Uuid.h"
#include "rabbet/ecs/Entity.h"

#include <string>

namespace rb::editor {

struct EditorContext;

// The ImGui drag-drop payload type carrying an asset uuid (as a string) between the Assets browser
// and the Hierarchy / Inspector drop targets.
inline constexpr const char* kAssetDragType = "RB_ASSET_UUID";

// Assigns an asset to `target` by its type: Model -> ModelRenderer, Material -> MaterialComponent,
// Script -> ScriptComponent, Audio -> SoundEmitter, Texture -> the target's ParticleEmitter sprite.
// Imports on demand; the resolve systems repopulate the runtime handle from the uuid. A prefab is
// not assignable (instantiate it instead).
void assignAssetToEntity(EditorContext& context, const rb::AssetDatabase::Record& record,
                         rb::Entity target);

// Instantiates a prefab into a new entity and selects it.
void instantiatePrefabAsset(EditorContext& context, const rb::AssetDatabase::Record& record);

// Emits an asset-uuid drag payload for the last submitted item, with `name` as the drag preview.
// Call between an item and the next, inside a window.
void beginAssetDragSource(const rb::Uuid& id, const std::string& name);

// Accepts an asset-uuid drop on the last submitted item, returning the dropped uuid (invalid when
// no drop occurred this frame). Call right after the target item.
[[nodiscard]] rb::Uuid acceptAssetDropTarget();

} // namespace rb::editor
