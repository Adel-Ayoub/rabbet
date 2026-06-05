#pragma once

namespace rb {
class ComponentRegistry;
}

namespace rb::editor {

// Attaches the built-in components' ImGui inspector drawers to the registry. The
// registry stores only a function pointer, so all ImGui lives here in the editor —
// the engine core (and its unit tests) never link a UI library.
void registerComponentDrawers(rb::ComponentRegistry& registry);

} // namespace rb::editor
