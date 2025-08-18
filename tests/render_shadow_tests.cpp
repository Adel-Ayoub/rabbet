#include "rabbet/render/Shadow.h"
#include "tests/Test.h"

#include <glm/glm.hpp>

#include <cmath>

namespace {

bool approx(float a, float b, float eps = 1.0e-3f) {
    return std::fabs(a - b) <= eps;
}

} // namespace

static void originProjectsToFrustumCenter() {
    const glm::mat4 lightSpace =
        rb::directionalLightSpace(glm::vec3(0.0f, -1.0f, 0.0f), 5.0f, 1.0f, 20.0f, 10.0f);
    const glm::vec4 clip = lightSpace * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    CHECK(approx(ndc.x, 0.0f));
    CHECK(approx(ndc.y, 0.0f));
    CHECK(ndc.z >= -1.0f && ndc.z <= 1.0f);
}

static void pointBeyondExtentFallsOutsideNdc() {
    const glm::mat4 lightSpace =
        rb::directionalLightSpace(glm::vec3(0.0f, -1.0f, 0.0f), 5.0f, 1.0f, 20.0f, 10.0f);
    const glm::vec4 clip = lightSpace * glm::vec4(12.0f, 0.0f, 0.0f, 1.0f);
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    CHECK(std::fabs(ndc.x) > 1.0f);
}

int main() {
    originProjectsToFrustumCenter();
    pointBeyondExtentFallsOutsideNdc();
    return rbtest::summary("render_shadow");
}
