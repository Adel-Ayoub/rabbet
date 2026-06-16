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
    std::vector<glm::vec3> spotPositions;
    std::vector<glm::vec3> spotDirections;
    std::vector<glm::vec3> spotColors;
    std::vector<glm::vec3> spotAttenuations;
    std::vector<glm::vec2> spotCones; // x = cos(inner), y = cos(outer)

    void clear() {
        directionalDirections.clear();
        directionalColors.clear();
        pointPositions.clear();
        pointColors.clear();
        pointAttenuations.clear();
        spotPositions.clear();
        spotDirections.clear();
        spotColors.clear();
        spotAttenuations.clear();
        spotCones.clear();
    }
};

} // namespace rb
