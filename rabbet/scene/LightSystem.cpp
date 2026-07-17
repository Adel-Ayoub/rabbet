#include "rabbet/scene/LightSystem.h"

#include "rabbet/core/Runtime.h"
#include "rabbet/render/Lighting.h"
#include "rabbet/scene/Hierarchy.h"
#include "rabbet/scene/Light.h"
#include "rabbet/scene/Transform.h"

#include <glm/glm.hpp>
#include <glm/trigonometric.hpp>

#include <cmath>

namespace rb {
namespace {

// An authored zero direction (typed into the inspector, then saved) would normalize to NaN
// and poison the light uniforms and the shadow matrix; fall back to straight down instead.
glm::vec3 normalizedDirection(const glm::vec3& direction) noexcept {
    const float lengthSq = glm::dot(direction, direction);
    if (lengthSq < 1.0e-12f) {
        return {0.0f, -1.0f, 0.0f};
    }
    return direction * glm::inversesqrt(lengthSq);
}

} // namespace

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
        lighting.directionalDirections.push_back(normalizedDirection(light.direction));
        lighting.directionalColors.push_back(light.color * light.intensity);
    });
    // Positions compose through the parent chain (a root's pose is exactly its own
    // Transform). Directions stay authored world-space, the same rule as a flat scene,
    // where rotating the light entity never steers the cone either.
    scene.each<PointLight, Transform>(
        [&lighting, &scene](Entity entity, PointLight& light, Transform&) {
            lighting.pointPositions.push_back(worldPoseOf(scene, entity).position);
            lighting.pointColors.push_back(light.color * light.intensity);
            lighting.pointAttenuations.emplace_back(light.constant, light.linear, light.quadratic);
        });
    scene.each<SpotLight, Transform>(
        [&lighting, &scene](Entity entity, SpotLight& light, Transform&) {
            lighting.spotPositions.push_back(worldPoseOf(scene, entity).position);
            lighting.spotDirections.push_back(normalizedDirection(light.direction));
            lighting.spotColors.push_back(light.color * light.intensity);
            lighting.spotAttenuations.emplace_back(light.constant, light.linear, light.quadratic);
            lighting.spotCones.push_back(spotConeCosines(light));
        });
}

} // namespace rb
