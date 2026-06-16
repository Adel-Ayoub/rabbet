#include "rabbet/scene/LightSystem.h"

#include "rabbet/core/Runtime.h"
#include "rabbet/render/Lighting.h"
#include "rabbet/scene/Light.h"
#include "rabbet/scene/Transform.h"

#include <glm/glm.hpp>
#include <glm/trigonometric.hpp>

#include <cmath>

namespace rb {

glm::vec2 spotConeCosines(const SpotLight& light) noexcept {
    const float outer = glm::clamp(light.outerAngle, 0.0f, 89.9f);
    const float inner = glm::clamp(light.innerAngle, 0.0f, outer);
    return {std::cos(glm::radians(inner)), std::cos(glm::radians(outer))};
}

void LightSystem::onUpdate(Runtime& runtime, float) {
    if (!runtime.hasResource<Lighting>()) {
        Lighting& created = runtime.addResource<Lighting>();
        created.directionalDirections.reserve(4);
        created.directionalColors.reserve(4);
        created.pointPositions.reserve(8);
        created.pointColors.reserve(8);
        created.pointAttenuations.reserve(8);
        created.spotPositions.reserve(4);
        created.spotDirections.reserve(4);
        created.spotColors.reserve(4);
        created.spotAttenuations.reserve(4);
        created.spotCones.reserve(4);
    }
    Lighting& lighting = runtime.resource<Lighting>();
    lighting.clear();

    Scene& scene = runtime.scene();
    scene.each<DirectionalLight>([&lighting](Entity, DirectionalLight& light) {
        lighting.directionalDirections.push_back(glm::normalize(light.direction));
        lighting.directionalColors.push_back(light.color * light.intensity);
    });
    scene.each<PointLight, Transform>(
        [&lighting](Entity, PointLight& light, Transform& transform) {
            lighting.pointPositions.push_back(transform.position);
            lighting.pointColors.push_back(light.color * light.intensity);
            lighting.pointAttenuations.emplace_back(light.constant, light.linear, light.quadratic);
        });
    scene.each<SpotLight, Transform>(
        [&lighting](Entity, SpotLight& light, Transform& transform) {
            lighting.spotPositions.push_back(transform.position);
            lighting.spotDirections.push_back(glm::normalize(light.direction));
            lighting.spotColors.push_back(light.color * light.intensity);
            lighting.spotAttenuations.emplace_back(light.constant, light.linear, light.quadratic);
            lighting.spotCones.push_back(spotConeCosines(light));
        });
}

} // namespace rb
