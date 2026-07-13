#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

namespace rb {

// The lit shaders take a fixed number of lights per kind. When a scene carries more, the
// renderer uploads the nearest to the view rather than the first N in pool order, which
// swap-removes reshuffle on every entity destroy (visible lighting pops at spawn/destroy
// churn). Returns the chosen indices, nearest first, at most maxCount; ties break on the
// lower index so the choice is deterministic.
[[nodiscard]] inline std::vector<std::size_t> selectNearestLights(
    const std::vector<glm::vec3>& positions, const glm::vec3& viewPosition,
    std::size_t maxCount) {
    std::vector<std::size_t> indices(positions.size());
    for (std::size_t i = 0; i < indices.size(); ++i) {
        indices[i] = i;
    }
    if (indices.size() <= maxCount) {
        return indices;
    }
    std::partial_sort(indices.begin(),
                      indices.begin() + static_cast<std::ptrdiff_t>(maxCount), indices.end(),
                      [&](std::size_t a, std::size_t b) {
                          const glm::vec3 da = positions[a] - viewPosition;
                          const glm::vec3 db = positions[b] - viewPosition;
                          const float distA = glm::dot(da, da);
                          const float distB = glm::dot(db, db);
                          return distA != distB ? distA < distB : a < b;
                      });
    indices.resize(maxCount);
    return indices;
}

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
