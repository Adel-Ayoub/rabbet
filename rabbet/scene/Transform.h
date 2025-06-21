#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace rb {

struct Transform {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};

    [[nodiscard]] glm::mat4 matrix() const {
        glm::mat4 model = glm::translate(glm::mat4(1.0f), position);
        model *= glm::mat4_cast(rotation);
        model = glm::scale(model, scale);
        return model;
    }

    [[nodiscard]] glm::vec3 forward() const { return rotation * glm::vec3(0.0f, 0.0f, -1.0f); }
    [[nodiscard]] glm::vec3 right() const { return rotation * glm::vec3(1.0f, 0.0f, 0.0f); }
    [[nodiscard]] glm::vec3 up() const { return rotation * glm::vec3(0.0f, 1.0f, 0.0f); }

    [[nodiscard]] static Transform fromEuler(const glm::vec3& eulerRadians) {
        Transform transform;
        transform.rotation = glm::quat(eulerRadians);
        return transform;
    }
};

} // namespace rb
