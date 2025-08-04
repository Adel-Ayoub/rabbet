#include "rabbet/scene/LightSystem.h"

#include "rabbet/core/Runtime.h"
#include "rabbet/render/Lighting.h"
#include "rabbet/scene/Light.h"
#include "rabbet/scene/Transform.h"

#include <glm/glm.hpp>

namespace rb {

void LightSystem::onUpdate(Runtime& runtime, float) {
    if (!runtime.hasResource<Lighting>()) {
        runtime.addResource<Lighting>();
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
}

} // namespace rb
