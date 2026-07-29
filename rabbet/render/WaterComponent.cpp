#include "rabbet/render/WaterComponent.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace rb {
namespace {

constexpr float kMinExtent = 0.01f;
constexpr float kMaxExtent = 100000.0f;

// NaN fails every comparison, so a plain clamp passes it straight through; gate finiteness first.
float finite(float value, float fallback) noexcept {
    return std::isfinite(value) ? value : fallback;
}

float finiteClamped(float value, float fallback, float low, float high) noexcept {
    return std::clamp(finite(value, fallback), low, high);
}

} // namespace

glm::mat4 waterSurfaceModel(const glm::mat4& world, glm::vec2 extent) {
    const glm::vec3 center{world[3]};
    const glm::vec3 safeCenter{finite(center.x, 0.0f), finite(center.y, 0.0f),
                               finite(center.z, 0.0f)};
    const float x = finiteClamped(extent.x, kMinExtent, kMinExtent, kMaxExtent);
    const float z = finiteClamped(extent.y, kMinExtent, kMinExtent, kMaxExtent);
    glm::mat4 model = glm::translate(glm::mat4(1.0f), safeCenter);
    return glm::scale(model, glm::vec3(x, 1.0f, z));
}

void sanitizeWater(WaterComponent& water) {
    water.extent.x = finiteClamped(water.extent.x, kMinExtent, kMinExtent, kMaxExtent);
    water.extent.y = finiteClamped(water.extent.y, kMinExtent, kMinExtent, kMaxExtent);
    water.waveTileScale = finiteClamped(water.waveTileScale, 0.35f, 0.0f, 1000.0f);
    water.waveStrength = finiteClamped(water.waveStrength, 0.5f, 0.0f, 1000.0f);
    water.waveSpeed = finiteClamped(water.waveSpeed, 1.0f, 0.0f, 1000.0f);
    water.smoothness = finiteClamped(water.smoothness, 0.9f, 0.0f, 1.0f);
    for (int i = 0; i < 4; ++i) {
        water.deepColor[i] = finiteClamped(water.deepColor[i], 0.0f, 0.0f, 1000.0f);
        water.shallowColor[i] = finiteClamped(water.shallowColor[i], 0.0f, 0.0f, 1000.0f);
    }
}

} // namespace rb
