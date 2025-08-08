#include "rabbet/core/Runtime.h"
#include "rabbet/render/Lighting.h"
#include "rabbet/scene/Light.h"
#include "rabbet/scene/LightSystem.h"
#include "rabbet/scene/Transform.h"
#include "tests/Test.h"

#include <glm/glm.hpp>

#include <cmath>

namespace {

bool approx(float a, float b, float eps = 1.0e-4f) {
    return std::fabs(a - b) <= eps;
}

} // namespace

static void gathersDirectionalAndPointLights() {
    rb::Runtime rt;
    rt.addSystem<rb::LightSystem>();

    const rb::Entity sun = rt.scene().create();
    rb::DirectionalLight directional;
    directional.direction = glm::vec3(0.0f, -2.0f, 0.0f);
    directional.color = glm::vec3(1.0f, 0.9f, 0.8f);
    directional.intensity = 2.0f;
    rt.scene().add<rb::DirectionalLight>(sun, directional);

    const rb::Entity lamp = rt.scene().create();
    rb::Transform transform;
    transform.position = glm::vec3(3.0f, 4.0f, 5.0f);
    rt.scene().add<rb::Transform>(lamp, transform);
    rb::PointLight point;
    point.color = glm::vec3(0.2f, 0.4f, 0.6f);
    point.intensity = 0.5f;
    rt.scene().add<rb::PointLight>(lamp, point);

    rt.start();
    rt.tick(0.0f);
    rt.stop();

    CHECK(rt.hasResource<rb::Lighting>());
    const rb::Lighting& lighting = rt.resource<rb::Lighting>();

    CHECK(lighting.directionalDirections.size() == 1u);
    CHECK(lighting.pointPositions.size() == 1u);

    CHECK(approx(lighting.directionalDirections[0].y, -1.0f));
    CHECK(approx(lighting.directionalColors[0].r, 2.0f));
    CHECK(approx(lighting.directionalColors[0].g, 1.8f));

    CHECK(approx(lighting.pointPositions[0].x, 3.0f));
    CHECK(approx(lighting.pointPositions[0].z, 5.0f));
    CHECK(approx(lighting.pointColors[0].b, 0.3f));
    CHECK(approx(lighting.pointAttenuations[0].x, 1.0f));
}

static void clearsBetweenFrames() {
    rb::Runtime rt;
    rt.addSystem<rb::LightSystem>();
    const rb::Entity sun = rt.scene().create();
    rt.scene().add<rb::DirectionalLight>(sun, rb::DirectionalLight{});

    rt.start();
    rt.tick(0.0f);
    rt.tick(0.0f);
    rt.stop();

    CHECK(rt.resource<rb::Lighting>().directionalDirections.size() == 1u);
}

int main() {
    gathersDirectionalAndPointLights();
    clearsBetweenFrames();
    return rbtest::summary("scene_light");
}
