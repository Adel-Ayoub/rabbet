#pragma once

#include "rabbet/ecs/Entity.h"

namespace rb {
class Runtime;
class ComponentRegistry;
} // namespace rb

namespace rb::editor {

// Shared editor state handed to every panel. Holds the engine runtime and the
// component registry, the current selection, and the viewport handshake the
// Editor uses to render the scene into the Viewport panel each frame.
struct EditorContext {
    rb::Runtime& runtime;
    rb::ComponentRegistry& registry;

    rb::Entity selected{};

    unsigned int viewportTexture = 0; // colour texture to display (set by Editor)
    int viewportWidth = 1;            // desired render size (set by ViewportPanel)
    int viewportHeight = 1;
    bool viewportHovered = false; // set by ViewportPanel each frame
    bool viewportFocused = false;
};

} // namespace rb::editor
