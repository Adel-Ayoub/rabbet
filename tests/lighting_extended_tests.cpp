#include "rabbet/core/Runtime.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/render/Lighting.h"
#include "rabbet/render/Primitive.h"
#include "rabbet/scene/Light.h"
#include "rabbet/scene/LightSystem.h"
#include "rabbet/scene/Transform.h"
#include "rabbet/serialize/BuiltinComponents.h"
#include "rabbet/serialize/ComponentRegistry.h"
#include "rabbet/serialize/SceneSerializer.h"
#include "tests/Test.h"

#include <glm/glm.hpp>

#include <cmath>

namespace {

bool approx(float a, float b, float eps = 1.0e-4f) {
    return std::fabs(a - b) <= eps;
}

rb::ComponentRegistry makeRegistry() {
    rb::ComponentRegistry registry;
    rb::registerBuiltinComponents(registry);
    return registry;
}

} // namespace

// cos(0) = 1 at the inner edge, cos(60deg) = 0.5 at the outer; inner cosine always >= outer.
static void coneCosinesMapAnglesToCosines() {
    rb::SpotLight light;
    light.innerAngle = 0.0f;
    light.outerAngle = 60.0f;
    const glm::vec2 cone = rb::spotConeCosines(light);
    CHECK(approx(cone.x, 1.0f));
    CHECK(approx(cone.y, 0.5f));
    CHECK(cone.x >= cone.y);
}

// A misconfigured cone (inner wider than outer) is clamped so inner <= outer, keeping the
// shader falloff denominator non-negative (cosInner >= cosOuter).
static void coneCosinesClampInvertedCone() {
    rb::SpotLight light;
    light.innerAngle = 50.0f;
    light.outerAngle = 30.0f;
    const glm::vec2 cone = rb::spotConeCosines(light);
    CHECK(cone.x >= cone.y);
    CHECK(approx(cone.x, std::cos(glm::radians(30.0f))));
}

static void gathersSpotLights() {
    rb::Runtime rt;
    rt.addSystem<rb::LightSystem>();

    const rb::Entity lamp = rt.scene().create();
    rb::Transform transform;
    transform.position = glm::vec3(2.0f, 6.0f, -1.0f);
    rt.scene().add<rb::Transform>(lamp, transform);
    rb::SpotLight spot;
    spot.direction = glm::vec3(0.0f, -2.0f, 0.0f);
    spot.color = glm::vec3(0.2f, 0.4f, 0.6f);
    spot.intensity = 0.5f;
    spot.innerAngle = 0.0f;
    spot.outerAngle = 60.0f;
    rt.scene().add<rb::SpotLight>(lamp, spot);

    rt.start();
    rt.tick(0.0f);
    rt.stop();

    const rb::Lighting& lighting = rt.resource<rb::Lighting>();
    CHECK(lighting.spotPositions.size() == 1u);
    CHECK(lighting.spotDirections.size() == 1u);
    CHECK(lighting.spotColors.size() == 1u);
    CHECK(lighting.spotAttenuations.size() == 1u);
    CHECK(lighting.spotCones.size() == 1u);

    CHECK(approx(lighting.spotPositions[0].y, 6.0f));
    CHECK(approx(lighting.spotDirections[0].y, -1.0f)); // normalized
    CHECK(approx(lighting.spotColors[0].r, 0.1f));      // color * intensity
    CHECK(approx(lighting.spotColors[0].b, 0.3f));
    CHECK(approx(lighting.spotAttenuations[0].x, 1.0f));
    CHECK(approx(lighting.spotCones[0].x, 1.0f));
    CHECK(approx(lighting.spotCones[0].y, 0.5f));
}

// A SpotLight needs a Transform to be placed; without one it is skipped, and the spot arrays
// are cleared every frame (no accumulation across ticks).
static void spotWithoutTransformIsSkippedAndClears() {
    rb::Runtime rt;
    rt.addSystem<rb::LightSystem>();
    const rb::Entity orphan = rt.scene().create();
    rt.scene().add<rb::SpotLight>(orphan, rb::SpotLight{});

    const rb::Entity lamp = rt.scene().create();
    rt.scene().add<rb::Transform>(lamp, rb::Transform{});
    rt.scene().add<rb::SpotLight>(lamp, rb::SpotLight{});

    rt.start();
    rt.tick(0.0f);
    rt.tick(0.0f);
    rt.stop();

    CHECK(rt.resource<rb::Lighting>().spotPositions.size() == 1u);
}

static void spotLightRoundTrips() {
    const rb::ComponentRegistry registry = makeRegistry();

    rb::Scene source;
    const rb::Entity e = source.create();
    rb::SpotLight spot;
    spot.direction = glm::vec3(1.0f, -1.0f, 0.5f);
    spot.color = glm::vec3(0.9f, 0.8f, 0.7f);
    spot.intensity = 3.5f;
    spot.constant = 1.0f;
    spot.linear = 0.14f;
    spot.quadratic = 0.07f;
    spot.innerAngle = 12.0f;
    spot.outerAngle = 24.0f;
    source.add<rb::SpotLight>(e, spot);

    const nlohmann::json doc = rb::SceneSerializer::toJson(source, registry);
    rb::Scene loaded;
    rb::SceneSerializer::fromJson(doc, loaded, registry);

    CHECK(loaded.count<rb::SpotLight>() == 1u);
    loaded.each<rb::SpotLight>([](rb::Entity, rb::SpotLight& l) {
        CHECK(approx(l.direction.z, 0.5f));
        CHECK(approx(l.color.g, 0.8f));
        CHECK(approx(l.intensity, 3.5f));
        CHECK(approx(l.linear, 0.14f));
        CHECK(approx(l.quadratic, 0.07f));
        CHECK(approx(l.innerAngle, 12.0f));
        CHECK(approx(l.outerAngle, 24.0f));
    });
}

static void primitiveEmissiveRoundTrips() {
    const rb::ComponentRegistry registry = makeRegistry();

    rb::Scene source;
    const rb::Entity e = source.create();
    rb::Primitive primitive;
    primitive.shape = rb::PrimitiveShape::Sphere;
    primitive.emissive = glm::vec3(0.5f, 1.0f, 2.0f);
    source.add<rb::Primitive>(e, primitive);

    const nlohmann::json doc = rb::SceneSerializer::toJson(source, registry);
    rb::Scene loaded;
    rb::SceneSerializer::fromJson(doc, loaded, registry);

    loaded.each<rb::Primitive>([](rb::Entity, rb::Primitive& p) {
        CHECK(approx(p.emissive.r, 0.5f));
        CHECK(approx(p.emissive.g, 1.0f));
        CHECK(approx(p.emissive.b, 2.0f));
    });
}

// A Primitive saved before emissive existed (no "emissive" key) loads with emissive zeroed,
// not a parse failure.
static void primitiveWithoutEmissiveDefaultsToZero() {
    const nlohmann::json legacy = {
        {"shape", "Cube"}, {"color", {0.8f, 0.8f, 0.8f}}, {"metallic", 0.0f}, {"roughness", 0.6f}};
    const auto primitive = legacy.get<rb::Primitive>();
    CHECK(approx(primitive.emissive.r, 0.0f));
    CHECK(approx(primitive.emissive.g, 0.0f));
    CHECK(approx(primitive.emissive.b, 0.0f));
}

int main() {
    coneCosinesMapAnglesToCosines();
    coneCosinesClampInvertedCone();
    gathersSpotLights();
    spotWithoutTransformIsSkippedAndClears();
    spotLightRoundTrips();
    primitiveEmissiveRoundTrips();
    primitiveWithoutEmissiveDefaultsToZero();
    return rbtest::summary("lighting_extended");
}
