#pragma once

#include <glm/glm.hpp>

namespace rb {

// One slot in an emitter's fixed pool. `age`/`lifetime` drive recycling; `size` and `color` are
// the current interpolated values, so the renderer consumes them directly without touching the
// emitter. A dead slot (alive == false) is free to be respawned.
struct Particle {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    glm::vec4 color{1.0f};
    float size = 0.0f;
    float age = 0.0f;
    float lifetime = 1.0f;
    bool alive = false;
};

} // namespace rb
