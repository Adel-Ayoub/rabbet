#pragma once

#include "rabbet/ecs/Entity.h"

namespace rb {
class ComponentRegistry;
}

namespace rb::editor {

struct EditorContext;

// Attaches the built-in components' ImGui inspector drawers to the registry. The
// registry stores only a function pointer, so all ImGui lives here in the editor —
// the engine core (and its unit tests) never link a UI library.
void registerComponentDrawers(rb::ComponentRegistry& registry);

// Draws the MaterialComponent inspector. Unlike the registry drawers it needs the
// AssetManager (to read the bound material's shader and reflected uniforms), so the
// InspectorPanel calls it directly with the editor context rather than through the
// type-erased registry hook.
void drawMaterialInspector(EditorContext& context, rb::Entity e);

// Draws the PrefabInstance inspector (the prefab link + a "Revert to Prefab" action). Like the
// material drawer it needs the AssetManager + registry, so it is called directly by the panel.
void drawPrefabInspector(EditorContext& context, rb::Entity e);

} // namespace rb::editor
