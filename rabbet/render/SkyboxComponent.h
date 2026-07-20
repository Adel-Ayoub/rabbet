#pragma once

#include <array>

#include "rabbet/core/Uuid.h"

namespace rb {

class Scene;

// The sky a scene wants, as data. Six catalogued textures in GL cubemap face order
// (+X,-X,+Y,-Y,+Z,-Z), which is the same right/left/top/bottom/front/back order the
// skybox folders on disk use. Referencing six textures rather than one cubemap asset
// keeps this in the engine's existing idiom: TerrainComponent addresses its layers the
// same way, and every face is already a first-class Texture with a uuid.
struct SkyboxComponent {
    static constexpr int kFaceCount = 6;
    std::array<Uuid, kFaceCount> faces{};
};

// The sky a scene falls back to when it authors none. Without this a scene's appearance
// would depend on what was loaded before it, and Stop could not put back the sky a play
// session swapped away. Set once by the host (the editor points it at its startup sky).
struct SkyboxDefault {
    std::array<Uuid, SkyboxComponent::kFaceCount> faces{};
};

// The first skybox in the scene, or nullptr. First-wins, like the active camera.
[[nodiscard]] SkyboxComponent* activeSkybox(Scene& scene);

} // namespace rb
