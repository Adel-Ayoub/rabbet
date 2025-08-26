#include "rabbet/render/Sky.h"
#include "tests/Test.h"

#include <glm/glm.hpp>

namespace {

bool inUnitRange(const glm::vec3& c) {
    return c.x >= 0.0f && c.x <= 1.0f && c.y >= 0.0f && c.y <= 1.0f && c.z >= 0.0f && c.z <= 1.0f;
}

} // namespace

static void colorsStayInRange() {
    CHECK(inUnitRange(rb::skyColor(glm::vec3(0.0f, 1.0f, 0.0f))));
    CHECK(inUnitRange(rb::skyColor(glm::vec3(0.0f, -1.0f, 0.0f))));
    CHECK(inUnitRange(rb::skyColor(glm::vec3(1.0f, 0.0f, 0.0f))));
}

static void zenithIsBlueAndBrighterThanGround() {
    const glm::vec3 up = rb::skyColor(glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 down = rb::skyColor(glm::vec3(0.0f, -1.0f, 0.0f));
    CHECK(up.z > up.x);
    CHECK(up.z > down.z);
}

static void sunDirectionIsBright() {
    const glm::vec3 sun = rb::skyColor(glm::vec3(0.5f, 0.8f, 0.4f));
    const glm::vec3 away = rb::skyColor(glm::vec3(-0.5f, 0.8f, -0.4f));
    CHECK(sun.x + sun.y + sun.z > away.x + away.y + away.z);
}

int main() {
    colorsStayInRange();
    zenithIsBlueAndBrighterThanGround();
    sunDirectionIsBright();
    return rbtest::summary("render_sky");
}
