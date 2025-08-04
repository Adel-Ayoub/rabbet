#pragma once

#include <vector>

#include <glm/glm.hpp>

namespace rb {

struct Lighting {
    glm::vec3 ambient{0.05f};
    std::vector<glm::vec3> directionalDirections;
    std::vector<glm::vec3> directionalColors;
    std::vector<glm::vec3> pointPositions;
    std::vector<glm::vec3> pointColors;
    std::vector<glm::vec3> pointAttenuations;

    void clear() {
        directionalDirections.clear();
        directionalColors.clear();
        pointPositions.clear();
        pointColors.clear();
        pointAttenuations.clear();
    }
};

} // namespace rb
