#pragma once

#include "rabbet/ecs/Entity.h"
#include "rabbet/render/RenderView.h"

namespace rb {
class Runtime;
class ComponentRegistry;
} // namespace rb

namespace rb::editor {

class ThumbnailRenderer;

// Shared editor state handed to every panel. Holds the engine runtime and the
// component registry, the current selection, and the viewport handshake the
// Editor uses to render the scene into the Viewport panel each frame.
struct EditorContext {
    rb::Runtime& runtime;
    rb::ComponentRegistry& registry;

    rb::Entity selected{};

    ThumbnailRenderer* thumbnails = nullptr; // asset preview cache (set by Editor)

    unsigned int viewportTexture = 0; // colour texture to display (set by Editor)
    int viewportWidth = 1;            // desired render size (set by ViewportPanel)
    int viewportHeight = 1;
    bool viewportHovered = false; // set by ViewportPanel each frame
    bool viewportFocused = false;

    rb::RenderView renderView{}; // view/projection the image was rendered with (set by Editor)

    // Click-to-pick handshake: ViewportPanel requests a pick at image-local pixels
    // (origin top-left); the Editor services it after rendering and clears the flag.
    bool pickRequested = false;
    int pickX = 0;
    int pickY = 0;
};

} // namespace rb::editor
