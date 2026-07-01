#pragma once

#include <optional>

#include "rabbet/render/RenderView.h"

namespace rb {

class Scene;

// Builds the render view of the scene's first Camera + Transform entity (the gameplay
// camera), or nullopt when the scene has none. The editor uses it to hand the Play-mode
// viewport to the scene's own camera, falling back to the editor camera otherwise.
[[nodiscard]] std::optional<RenderView> activeCameraView(Scene& scene, float aspect);

} // namespace rb
