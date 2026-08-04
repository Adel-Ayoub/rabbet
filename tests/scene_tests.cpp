#include "rabbet/core/Runtime.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/render/Lighting.h"
#include "rabbet/render/Primitive.h"
#include "rabbet/render/RenderView.h"
#include "rabbet/render/Shadow.h"
#include "rabbet/render/Viewport.h"
#include "rabbet/scene/Camera.h"
#include "rabbet/scene/CameraSystem.h"
#include "rabbet/scene/CameraView.h"
#include "rabbet/scene/Hierarchy.h"
#include "rabbet/scene/Light.h"
#include "rabbet/scene/LightSystem.h"
#include "rabbet/scene/Name.h"
#include "rabbet/scene/NameIndex.h"
#include "rabbet/scene/Parent.h"
#include "rabbet/scene/Transform.h"
#include "rabbet/scene/TransformSystem.h"
#include "rabbet/scene/WorldMatrix.h"
#include "rabbet/serialize/BuiltinComponents.h"
#include "rabbet/serialize/ComponentRegistry.h"
#include "rabbet/serialize/SceneSerializer.h"

#include "tests/Test.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace {

bool approx(float a, float b, float eps = 1.0e-4f) {
    return std::fabs(a - b) <= eps;
}

bool approxVec(const glm::vec3& a, const glm::vec3& b) {
    return approx(a.x, b.x) && approx(a.y, b.y) && approx(a.z, b.z);
}

glm::vec3 transformPoint(const glm::mat4& m, const glm::vec3& p) {
    return glm::vec3(m * glm::vec4(p, 1.0f));
}

} // namespace

static void identityLeavesPointsUntouched() {
    const rb::Transform t;
    CHECK(approxVec(transformPoint(t.matrix(), glm::vec3(1.0f, 2.0f, 3.0f)),
                    glm::vec3(1.0f, 2.0f, 3.0f)));
}

static void translationMovesOrigin() {
    rb::Transform t;
    t.position = glm::vec3(2.0f, 3.0f, 4.0f);
    CHECK(approxVec(transformPoint(t.matrix(), glm::vec3(0.0f)), glm::vec3(2.0f, 3.0f, 4.0f)));
}

static void scaleScalesPoint() {
    rb::Transform t;
    t.scale = glm::vec3(2.0f, 3.0f, 4.0f);
    CHECK(approxVec(transformPoint(t.matrix(), glm::vec3(1.0f)), glm::vec3(2.0f, 3.0f, 4.0f)));
}

static void rotationRotatesPoint() {
    rb::Transform t;
    t.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    CHECK(approxVec(transformPoint(t.matrix(), glm::vec3(1.0f, 0.0f, 0.0f)),
                    glm::vec3(0.0f, 1.0f, 0.0f)));
}

static void composedTransformAppliesScaleRotateTranslate() {
    rb::Transform t;
    t.position = glm::vec3(10.0f, 0.0f, 0.0f);
    t.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    t.scale = glm::vec3(2.0f);
    CHECK(approxVec(transformPoint(t.matrix(), glm::vec3(1.0f, 0.0f, 0.0f)),
                    glm::vec3(10.0f, 2.0f, 0.0f)));
}

static void systemCachesWorldMatrix() {
    rb::Runtime rt;
    rt.addSystem<rb::TransformSystem>();

    const rb::Entity e = rt.scene().create();
    rb::Transform t;
    t.position = glm::vec3(5.0f, 6.0f, 7.0f);
    rt.scene().add<rb::Transform>(e, t);

    rt.start();
    rt.tick(0.0f);
    rt.stop();

    CHECK(rt.scene().has<rb::WorldMatrix>(e));
    const glm::mat4 expected = t.matrix();
    const glm::mat4 got = rt.scene().get<rb::WorldMatrix>(e).value;
    bool same = true;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            same = same && approx(expected[col][row], got[col][row]);
        }
    }
    CHECK(same);
}

// Removing an entity's Transform retires its WorldMatrix on the next tick; a stale matrix
// would keep the render and pick passes drawing the ghost at its last pose.
static void removingTransformDropsWorldMatrix() {
    rb::Runtime rt;
    rt.addSystem<rb::TransformSystem>();

    const rb::Entity e = rt.scene().create();
    rt.scene().add<rb::Transform>(e, rb::Transform{});

    rt.start();
    rt.tick(0.0f);
    CHECK(rt.scene().has<rb::WorldMatrix>(e));

    rt.scene().remove<rb::Transform>(e);
    rt.tick(0.0f);
    CHECK(!rt.scene().has<rb::WorldMatrix>(e));
    rt.stop();
}

void sceneTransformSuite() {
    identityLeavesPointsUntouched();
    translationMovesOrigin();
    scaleScalesPoint();
    rotationRotatesPoint();
    composedTransformAppliesScaleRotateTranslate();
    systemCachesWorldMatrix();
    removingTransformDropsWorldMatrix();
}

namespace {

// No Camera + Transform pair -> no view, so a caller keeps whatever camera it already
// had (the editor's own, for scenes that author none).
static void noCameraYieldsNothing() {
    rb::Scene scene;
    CHECK(!rb::activeCameraView(scene, 1.5f).has_value());
    const rb::Entity lonely = scene.create();
    scene.add<rb::Camera>(lonely, rb::Camera{});
    CHECK(!rb::activeCameraView(scene, 1.5f).has_value()); // camera with no Transform
}

// The view maps world space into the camera's space: the camera's own position lands on
// the origin, and a point one unit ahead lands one unit down -Z (GL forward).
static void viewInvertsCameraPose() {
    rb::Scene scene;
    const rb::Entity cam = scene.create();
    rb::Transform where;
    where.position = {3.0f, 2.0f, 1.0f};
    scene.add<rb::Transform>(cam, where);
    scene.add<rb::Camera>(cam, rb::Camera{});

    const std::optional<rb::RenderView> view = rb::activeCameraView(scene, 16.0f / 9.0f);
    CHECK(view.has_value());
    if (view.has_value()) {
        CHECK(approx(view->position.x, 3.0f));
        const glm::vec4 self = view->view * glm::vec4(3.0f, 2.0f, 1.0f, 1.0f);
        CHECK(approx(self.x, 0.0f));
        CHECK(approx(self.y, 0.0f));
        CHECK(approx(self.z, 0.0f));
        const glm::vec4 ahead = view->view * glm::vec4(3.0f, 2.0f, 0.0f, 1.0f);
        CHECK(approx(ahead.z, -1.0f));
    }
}

// Rotating the camera aims the view: yawed a quarter turn, "ahead" is along -X.
static void rotationAimsTheView() {
    rb::Scene scene;
    const rb::Entity cam = scene.create();
    rb::Transform where;
    where.rotation = glm::quat(glm::vec3(0.0f, glm::radians(90.0f), 0.0f));
    scene.add<rb::Transform>(cam, where);
    scene.add<rb::Camera>(cam, rb::Camera{});

    const std::optional<rb::RenderView> view = rb::activeCameraView(scene, 1.0f);
    CHECK(view.has_value());
    if (view.has_value()) {
        const glm::vec4 ahead = view->view * glm::vec4(-1.0f, 0.0f, 0.0f, 1.0f);
        CHECK(approx(ahead.x, 0.0f));
        CHECK(approx(ahead.z, -1.0f));
    }
}

// The projection reads the component: a square 90-degree frustum has unit focal terms,
// and the camera's near/far planes bound the depth mapping.
static void projectionUsesCameraParams() {
    rb::Scene scene;
    const rb::Entity cam = scene.create();
    scene.add<rb::Transform>(cam, rb::Transform{});
    rb::Camera camera;
    camera.fovY = glm::radians(90.0f);
    camera.nearPlane = 0.5f;
    camera.farPlane = 50.0f;
    scene.add<rb::Camera>(cam, camera);

    const std::optional<rb::RenderView> view = rb::activeCameraView(scene, 1.0f);
    CHECK(view.has_value());
    if (view.has_value()) {
        CHECK(approx(view->projection[0][0], 1.0f));
        CHECK(approx(view->projection[1][1], 1.0f));
        // The depth terms have to reflect the authored clip planes, or near clipping and
        // z precision silently stop following the component.
        const glm::mat4 expected = glm::perspective(camera.fovY, 1.0f, 0.5f, 50.0f);
        CHECK(approx(view->projection[2][2], expected[2][2]));
        CHECK(approx(view->projection[3][2], expected[3][2]));
    }
}

// The first camera wins when a scene has several, and the camera entity's scale is
// ignored (a scaled camera must not zoom or warp the world).
static void firstCameraWinsAndScaleIsIgnored() {
    rb::Scene scene;
    const rb::Entity first = scene.create();
    rb::Transform a;
    a.position = {1.0f, 0.0f, 0.0f};
    a.scale = {5.0f, 5.0f, 5.0f};
    scene.add<rb::Transform>(first, a);
    scene.add<rb::Camera>(first, rb::Camera{});

    const rb::Entity second = scene.create();
    rb::Transform b;
    b.position = {9.0f, 9.0f, 9.0f};
    scene.add<rb::Transform>(second, b);
    scene.add<rb::Camera>(second, rb::Camera{});

    const std::optional<rb::RenderView> view = rb::activeCameraView(scene, 1.0f);
    CHECK(view.has_value());
    if (view.has_value()) {
        CHECK(approx(view->position.x, 1.0f)); // the first camera, not the second
        const glm::vec4 ahead = view->view * glm::vec4(1.0f, 0.0f, -1.0f, 1.0f);
        CHECK(approx(ahead.z, -1.0f)); // one unit ahead stays one unit, not one fifth
    }
}

// A camera mounted on a rig entity composes its pose through the parent: yawed a quarter
// turn at (10,0,0), a boom offset of five units lands the camera at (15,0,0) looking -X.
static void parentedCameraTracksItsRig() {
    rb::Scene scene;
    const rb::Entity rig = scene.create();
    rb::Transform rigPose;
    rigPose.position = {10.0f, 0.0f, 0.0f};
    rigPose.rotation = glm::quat(glm::vec3(0.0f, glm::radians(90.0f), 0.0f));
    scene.add<rb::Transform>(rig, rigPose);

    const rb::Entity cam = scene.create();
    rb::Transform boom;
    boom.position = {0.0f, 0.0f, 5.0f};
    scene.add<rb::Transform>(cam, boom);
    scene.add<rb::Camera>(cam, rb::Camera{});
    CHECK(rb::setParent(scene, cam, rig));

    const std::optional<rb::RenderView> view = rb::activeCameraView(scene, 1.0f);
    CHECK(view.has_value());
    if (view.has_value()) {
        CHECK(approx(view->position.x, 15.0f));
        CHECK(approx(view->position.z, 0.0f));
        const glm::vec4 ahead = view->view * glm::vec4(14.0f, 0.0f, 0.0f, 1.0f);
        CHECK(approx(ahead.x, 0.0f));
        CHECK(approx(ahead.z, -1.0f)); // the rig's yaw aims the camera down -X
    }
}

// A scaled rig stretches the mounting offset but must not zoom the world: one unit ahead
// of the camera still maps to one unit of view depth.
static void scaledRigStretchesTheOffsetNotTheView() {
    rb::Scene scene;
    const rb::Entity rig = scene.create();
    rb::Transform rigPose;
    rigPose.scale = glm::vec3(3.0f);
    scene.add<rb::Transform>(rig, rigPose);

    const rb::Entity cam = scene.create();
    rb::Transform boom;
    boom.position = {0.0f, 0.0f, 5.0f};
    scene.add<rb::Transform>(cam, boom);
    scene.add<rb::Camera>(cam, rb::Camera{});
    CHECK(rb::setParent(scene, cam, rig));

    const std::optional<rb::RenderView> view = rb::activeCameraView(scene, 1.0f);
    CHECK(view.has_value());
    if (view.has_value()) {
        CHECK(approx(view->position.z, 15.0f)); // 5-unit boom under a 3x rig
        const glm::vec4 ahead = view->view * glm::vec4(0.0f, 0.0f, 14.0f, 1.0f);
        CHECK(approx(ahead.z, -1.0f)); // one unit stays one unit, not one third
    }
}

} // namespace

// The system seam over the same math: CameraSystem reads the Viewport's aspect and publishes
// the composed RenderView resource each tick, which is what the renderer actually consumes.
static void cameraSystemPublishesTheRenderView() {
    rb::Runtime rt;
    rt.addResource<rb::Viewport>(rb::Viewport{800, 600});
    rt.addSystem<rb::CameraSystem>();

    const rb::Entity eye = rt.scene().create();
    rb::Transform pose;
    pose.position = glm::vec3(0.0f, 0.0f, 5.0f);
    rt.scene().add<rb::Transform>(eye, pose);
    rt.scene().add<rb::Camera>(eye, rb::Camera{});

    rt.start();
    rt.tick(0.0f);
    rt.stop();

    CHECK(rt.hasResource<rb::RenderView>());
    const rb::RenderView& view = rt.resource<rb::RenderView>();
    CHECK(approx(view.position.z, 5.0f));
    // The projection's focal terms differ by exactly the viewport aspect, so the system really
    // read the Viewport rather than assuming a square target.
    CHECK(approx(view.projection[1][1] / view.projection[0][0], 800.0f / 600.0f));
}

void cameraViewSuite() {
    noCameraYieldsNothing();
    viewInvertsCameraPose();
    rotationAimsTheView();
    projectionUsesCameraParams();
    firstCameraWinsAndScaleIsIgnored();
    parentedCameraTracksItsRig();
    scaledRigStretchesTheOffsetNotTheView();
    cameraSystemPublishesTheRenderView();
}

namespace {

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

// A parented light illuminates from its composed world position; the rig's pose carries
// the bulb, while a root light keeps reading its own Transform exactly as before.
static void parentedLightGathersAtWorldPosition() {
    rb::Runtime rt;
    rt.addSystem<rb::LightSystem>();
    rb::Scene& scene = rt.scene();

    const rb::Entity rig = scene.create();
    rb::Transform rigPose;
    rigPose.position = glm::vec3(10.0f, 2.0f, 0.0f);
    scene.add<rb::Transform>(rig, rigPose);

    const rb::Entity bulb = scene.create();
    rb::Transform local;
    local.position = glm::vec3(1.0f, 0.0f, 0.0f);
    scene.add<rb::Transform>(bulb, local);
    scene.add<rb::PointLight>(bulb, rb::PointLight{});
    CHECK(rb::setParent(scene, bulb, rig));

    rt.start();
    rt.tick(0.0f);
    rt.stop();

    const rb::Lighting& lighting = rt.resource<rb::Lighting>();
    CHECK(lighting.pointPositions.size() == 1u);
    CHECK(!lighting.pointPositions.empty() && approx(lighting.pointPositions[0].x, 11.0f));
    CHECK(!lighting.pointPositions.empty() && approx(lighting.pointPositions[0].y, 2.0f));
}

// Over the shader caps the renderer keeps the lights nearest the view, deterministically
// (distance, then index): pool churn from destroys can no longer reshuffle which lights
// survive the cut.
static void nearestLightSelectionIsDeterministic() {
    std::vector<glm::vec3> positions;
    for (int i = 0; i < 12; ++i) {
        positions.push_back(glm::vec3(static_cast<float>(12 - i), 0.0f, 0.0f));
    }

    const std::vector<std::size_t> keep =
        rb::selectNearestLights(positions, glm::vec3(0.0f), 8);
    CHECK(keep.size() == 8u);
    CHECK(keep.front() == 11u); // distance 1, the nearest
    for (std::size_t i = 1; i < keep.size(); ++i) {
        CHECK(keep[i] == 11u - i); // strictly increasing distance
    }

    const std::vector<std::size_t> all =
        rb::selectNearestLights(positions, glm::vec3(0.0f), 16);
    CHECK(all.size() == positions.size()); // under the cap: everything, original order
    CHECK(all.front() == 0u);

    std::vector<glm::vec3> tied(3, glm::vec3(2.0f, 0.0f, 0.0f));
    const std::vector<std::size_t> firstTwo = rb::selectNearestLights(tied, glm::vec3(0.0f), 2);
    CHECK(firstTwo.size() == 2u);
    CHECK(firstTwo[0] == 0u); // ties break on the lower index
    CHECK(firstTwo[1] == 1u);
}

// The base gather: directionals normalize and premultiply intensity into colour, points carry
// their world position and attenuation, and every array clears between frames instead of
// accumulating a copy of the scene per tick.
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

static void lightArraysClearBetweenFrames() {
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

// The sun's shadow frustum stays centred on the world origin: it lands mid-NDC, so the
// shadow map's texel budget is spent around the scene rather than off to one side.
static void shadowFrustumCentersItsFocus() {
    const glm::mat4 lightSpace =
        rb::directionalLightSpace(glm::vec3(0.0f, -1.0f, 0.0f), 5.0f, 1.0f, 20.0f, 10.0f);
    const glm::vec4 clip = lightSpace * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    CHECK(approx(ndc.x, 0.0f));
    CHECK(approx(ndc.y, 0.0f));
    CHECK(ndc.z >= -1.0f && ndc.z <= 1.0f);
}

// Geometry past the ortho extent falls outside NDC and out of the shadow map, so the
// extent really bounds what casts.
static void shadowFrustumClipsBeyondExtent() {
    const glm::mat4 lightSpace =
        rb::directionalLightSpace(glm::vec3(0.0f, -1.0f, 0.0f), 5.0f, 1.0f, 20.0f, 10.0f);
    const glm::vec4 clip = lightSpace * glm::vec4(12.0f, 0.0f, 0.0f, 1.0f);
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    CHECK(std::fabs(ndc.x) > 1.0f);
}

void lightingExtendedSuite() {
    gathersDirectionalAndPointLights();
    lightArraysClearBetweenFrames();
    shadowFrustumCentersItsFocus();
    shadowFrustumClipsBeyondExtent();
    coneCosinesMapAnglesToCosines();
    coneCosinesClampInvertedCone();
    gathersSpotLights();
    spotWithoutTransformIsSkippedAndClears();
    spotLightRoundTrips();
    primitiveEmissiveRoundTrips();
    primitiveWithoutEmissiveDefaultsToZero();
    parentedLightGathersAtWorldPosition();
    nearestLightSelectionIsDeterministic();
}

namespace {

// The reference the index must agree with: the scan world.find used to run.
rb::Entity scanFor(rb::Scene& scene, const std::string& name) {
    rb::Entity found = rb::kNullEntity;
    scene.each<rb::Name>([&](rb::Entity e, rb::Name& n) {
        if (!found.valid() && n.value == name) {
            found = e;
        }
    });
    return found;
}

rb::Entity named(rb::Scene& scene, const std::string& name) {
    const rb::Entity e = scene.create();
    scene.add<rb::Name>(e, rb::Name{name});
    return e;
}

void findsByNameAndMissesCleanly() {
    rb::Scene scene;
    const rb::Entity lamp = named(scene, "Lamp");
    (void)named(scene, "Wisp");
    rb::NameIndex index;
    index.rebuild(scene);

    CHECK(index.find("Lamp") == lamp);
    CHECK(!index.find("Ghost").valid());
    CHECK(index.size() == 2u);
}

void duplicateNamesKeepFirstMatch() {
    rb::Scene scene;
    const rb::Entity first = named(scene, "Wisp");
    (void)named(scene, "Wisp");
    (void)named(scene, "Wisp");
    rb::NameIndex index;
    index.rebuild(scene);

    CHECK(index.find("Wisp") == first);
    CHECK(index.find("Wisp") == scanFor(scene, "Wisp"));
    CHECK(index.size() == 1u); // one entry per distinct value
}

void emptyNameIsAValueLikeAnyOther() {
    rb::Scene scene;
    const rb::Entity anonymous = named(scene, "");
    rb::NameIndex index;
    index.rebuild(scene);

    CHECK(index.find("") == anonymous);
    CHECK(index.find("") == scanFor(scene, ""));
}

void rebuildDropsStaleEntries() {
    rb::Scene scene;
    const rb::Entity doomed = named(scene, "Doomed");
    const rb::Entity keeper = named(scene, "Keeper");
    rb::NameIndex index;
    index.rebuild(scene);
    CHECK(index.find("Doomed") == doomed);

    scene.destroy(doomed);
    index.rebuild(scene);
    CHECK(!index.find("Doomed").valid());
    CHECK(index.find("Keeper") == keeper);
    CHECK(index.size() == 1u);
}

void recycledSlotResolvesToTheNewEntity() {
    rb::Scene scene;
    const rb::Entity old = named(scene, "Wisp");
    scene.destroy(old);
    const rb::Entity fresh = named(scene, "Wisp"); // reuses the slot, bumps the version
    CHECK(fresh.index() == old.index());
    CHECK(fresh.version() != old.version());

    rb::NameIndex index;
    index.rebuild(scene);
    CHECK(index.find("Wisp") == fresh);
    CHECK(index.find("Wisp") != old);
}

// Destroys swap-remove inside the pool and reorder it, which is exactly when a stale or
// order-assuming index would diverge from the scan. Deterministic churn, no RNG.
void churnStaysScanIdentical() {
    rb::Scene scene;
    std::vector<rb::Entity> entities;
    const char* names[] = {"Wisp", "Lamp", "Wisp", "Orb", "Wisp", "Lamp", "Orb", "Wisp"};
    for (const char* name : names) {
        entities.push_back(named(scene, name));
    }

    rb::NameIndex index;
    const auto agrees = [&] {
        index.rebuild(scene);
        for (const char* name : {"Wisp", "Lamp", "Orb", "Ghost"}) {
            if (index.find(name) != scanFor(scene, name)) {
                return false;
            }
        }
        return true;
    };
    CHECK(agrees());

    // Kill from the middle and the front, then respawn under a reused name.
    scene.destroy(entities[0]);
    scene.destroy(entities[3]);
    scene.destroy(entities[5]);
    CHECK(agrees());
    (void)named(scene, "Orb");
    (void)named(scene, "Ghost");
    CHECK(agrees());
    scene.destroy(entities[2]);
    scene.destroy(entities[4]);
    scene.destroy(entities[7]);
    CHECK(agrees());
    CHECK(index.find("Wisp") == scanFor(scene, "Wisp"));
}

// The index deliberately answers as of the last rebuild: freshness is the owner's
// per-tick rebuild discipline, and versioned handles make a stale answer read as dead
// rather than aliasing a recycled slot.
void staleIndexAnswersAsOfRebuildTime() {
    rb::Scene scene;
    const rb::Entity wisp = named(scene, "Wisp");
    rb::NameIndex index;
    index.rebuild(scene);

    scene.destroy(wisp);
    CHECK(index.find("Wisp") == wisp);
    CHECK(!scene.alive(index.find("Wisp")));
    index.rebuild(scene);
    CHECK(!index.find("Wisp").valid());
}

void clearEmptiesTheIndex() {
    rb::Scene scene;
    (void)named(scene, "Lamp");
    rb::NameIndex index;
    index.rebuild(scene);
    CHECK(index.size() == 1u);
    index.clear();
    CHECK(index.size() == 0u);
    CHECK(!index.find("Lamp").valid());
}

} // namespace

void nameIndexSuite() {
    findsByNameAndMissesCleanly();
    duplicateNamesKeepFirstMatch();
    emptyNameIsAValueLikeAnyOther();
    rebuildDropsStaleEntries();
    recycledSlotResolvesToTheNewEntity();
    churnStaysScanIdentical();
    staleIndexAnswersAsOfRebuildTime();
    clearEmptiesTheIndex();
}

namespace {

glm::vec3 worldOrigin(rb::Scene& scene, rb::Entity e) {
    return glm::vec3(scene.get<rb::WorldMatrix>(e).value * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
}

rb::Entity spawnAt(rb::Scene& scene, const glm::vec3& position) {
    const rb::Entity e = scene.create();
    rb::Transform t;
    t.position = position;
    scene.add<rb::Transform>(e, t);
    return e;
}

} // namespace

// Attach, reparent, detach: both directions of the link stay consistent through the basic
// gestures the editor performs.
static void parentLinksFollowTheGestures() {
    rb::Scene scene;
    const rb::Entity first = scene.create();
    const rb::Entity second = scene.create();
    const rb::Entity child = scene.create();
    CHECK(rb::parentOf(scene, child) == rb::kNullEntity);
    CHECK(rb::childrenOf(scene, child).empty());
    CHECK(!rb::isAncestor(scene, child, child));

    CHECK(rb::setParent(scene, child, first));
    CHECK(rb::parentOf(scene, child) == first);
    const std::vector<rb::Entity> children = rb::childrenOf(scene, first);
    CHECK(children.size() == 1u);
    CHECK(!children.empty() && children.front() == child);
    CHECK(rb::isAncestor(scene, first, child));
    CHECK(!rb::isAncestor(scene, child, first));

    CHECK(rb::setParent(scene, child, second));
    CHECK(rb::parentOf(scene, child) == second);
    CHECK(rb::childrenOf(scene, first).empty());
    CHECK(rb::childrenOf(scene, second).size() == 1u);

    CHECK(rb::setParent(scene, child, rb::kNullEntity));
    CHECK(rb::parentOf(scene, child) == rb::kNullEntity);
    CHECK(rb::childrenOf(scene, second).empty());
}

// Self, dead entities and would-be cycles are all refused, and a refused link leaves the
// child unparented.
static void setParentRefusesIllegalLinks() {
    rb::Scene scene;
    const rb::Entity a = scene.create();
    const rb::Entity b = scene.create();
    const rb::Entity c = scene.create();
    CHECK(!rb::setParent(scene, a, a));

    const rb::Entity dead = scene.create();
    scene.destroy(dead);
    CHECK(!rb::setParent(scene, a, dead));
    CHECK(!rb::setParent(scene, dead, a));

    CHECK(rb::setParent(scene, b, a));
    CHECK(rb::setParent(scene, c, b));
    CHECK(rb::isAncestor(scene, a, c)); // grandparent across two links
    CHECK(!rb::setParent(scene, a, c)); // a under its own grandchild would cycle
    CHECK(!rb::setParent(scene, a, b));
    CHECK(rb::parentOf(scene, a) == rb::kNullEntity);
}

static void destroyedParentOrphansChild() {
    rb::Scene scene;
    const rb::Entity parent = scene.create();
    const rb::Entity child = scene.create();
    CHECK(rb::setParent(scene, child, parent));

    scene.destroy(parent);
    CHECK(scene.alive(child));
    CHECK(rb::parentOf(scene, child) == rb::kNullEntity);

    // The freed index comes back with a bumped version; the stale link must not adopt
    // the recycled entity as the child's parent.
    const rb::Entity recycled = scene.create();
    CHECK(recycled.index() == parent.index());
    CHECK(rb::parentOf(scene, child) == rb::kNullEntity);
    CHECK(rb::childrenOf(scene, recycled).empty());
}

// Destroying a subtree takes the whole branch and nothing else: a middle node's branch
// (both of its children, not just the first) dies with it while the root, a sibling and
// an unrelated bystander survive.
static void destroySubtreeTakesTheBranchOnly() {
    rb::Scene scene;
    const rb::Entity root = scene.create();
    const rb::Entity doomed = scene.create();
    const rb::Entity kept = scene.create();
    const rb::Entity grandA = scene.create();
    const rb::Entity grandB = scene.create();
    const rb::Entity bystander = scene.create();
    CHECK(rb::setParent(scene, doomed, root));
    CHECK(rb::setParent(scene, kept, root));
    CHECK(rb::setParent(scene, grandA, doomed));
    CHECK(rb::setParent(scene, grandB, doomed));

    rb::destroySubtree(scene, doomed);
    CHECK(scene.alive(root));
    CHECK(scene.alive(kept));
    CHECK(scene.alive(bystander));
    CHECK(!scene.alive(doomed));
    CHECK(!scene.alive(grandA));
    CHECK(!scene.alive(grandB));
    const std::vector<rb::Entity> children = rb::childrenOf(scene, root);
    CHECK(children.size() == 1u);
    CHECK(!children.empty() && children.front() == kept);

    rb::destroySubtree(scene, root); // and from the root, everything goes
    CHECK(!scene.alive(root));
    CHECK(!scene.alive(kept));
    CHECK(scene.alive(bystander));
    CHECK(scene.aliveCount() == 1u);
}

// World matrices compose through the chain: a child's local offset is scaled then rotated
// into its parent's frame, and a three-deep chain accumulates every link.
static void worldMatrixComposesThroughParents() {
    rb::Runtime rt;
    rt.addSystem<rb::TransformSystem>();
    rb::Scene& scene = rt.scene();

    const rb::Entity parent = scene.create();
    rb::Transform pt;
    pt.position = {10.0f, 0.0f, 0.0f};
    pt.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    pt.scale = glm::vec3(2.0f);
    scene.add<rb::Transform>(parent, pt);
    const rb::Entity child = spawnAt(scene, {1.0f, 0.0f, 0.0f});
    CHECK(rb::setParent(scene, child, parent));

    const rb::Entity a = spawnAt(scene, {1.0f, 0.0f, 0.0f});
    const rb::Entity b = spawnAt(scene, {0.0f, 1.0f, 0.0f});
    const rb::Entity c = spawnAt(scene, {0.0f, 0.0f, 1.0f});
    CHECK(rb::setParent(scene, b, a));
    CHECK(rb::setParent(scene, c, b));

    rt.start();
    rt.tick(0.0f);
    CHECK(approxVec(worldOrigin(scene, child), glm::vec3(10.0f, 2.0f, 0.0f)));
    CHECK(approxVec(worldOrigin(scene, parent), glm::vec3(10.0f, 0.0f, 0.0f)));
    CHECK(approxVec(worldOrigin(scene, c), glm::vec3(1.0f, 1.0f, 1.0f)));
    rt.stop();
}

static void rootWorldMatrixIsBitIdenticalToLocal() {
    rb::Runtime rt;
    rt.addSystem<rb::TransformSystem>();
    rb::Scene& scene = rt.scene();

    rb::Transform t;
    t.position = {0.1f, 0.2f, 0.3f};
    t.rotation = glm::angleAxis(0.7f, glm::normalize(glm::vec3(0.3f, 0.5f, 0.8f)));
    t.scale = {1.3f, 0.7f, 2.1f};
    const rb::Entity e = scene.create();
    scene.add<rb::Transform>(e, t);

    rt.start();
    rt.tick(0.0f);

    const glm::mat4 expected = t.matrix();
    const glm::mat4 got = scene.get<rb::WorldMatrix>(e).value;
    bool identical = true;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            identical = identical && expected[col][row] == got[col][row];
        }
    }
    CHECK(identical); // the flat path must not pick up any composition arithmetic
    rt.stop();
}

static void transformlessMiddleNodeComposesThrough() {
    rb::Runtime rt;
    rt.addSystem<rb::TransformSystem>();
    rb::Scene& scene = rt.scene();

    const rb::Entity top = spawnAt(scene, {5.0f, 0.0f, 0.0f});
    const rb::Entity mid = scene.create(); // grouping node, no Transform
    const rb::Entity leaf = spawnAt(scene, {1.0f, 0.0f, 0.0f});
    CHECK(rb::setParent(scene, mid, top));
    CHECK(rb::setParent(scene, leaf, mid));

    rt.start();
    rt.tick(0.0f);
    CHECK(approxVec(worldOrigin(scene, leaf), glm::vec3(6.0f, 0.0f, 0.0f)));
    CHECK(!scene.has<rb::WorldMatrix>(mid)); // only Transform holders get a world matrix
    rt.stop();
}

// Topology edits recompose on the next tick: a reparented child picks up its new frame,
// and one whose parent is destroyed reverts to composing as a root.
static void topologyChangesRecomposeNextTick() {
    rb::Runtime rt;
    rt.addSystem<rb::TransformSystem>();
    rb::Scene& scene = rt.scene();

    const rb::Entity first = spawnAt(scene, {3.0f, 0.0f, 0.0f});
    const rb::Entity second = spawnAt(scene, {7.0f, 0.0f, 0.0f});
    const rb::Entity child = spawnAt(scene, {1.0f, 0.0f, 0.0f});
    CHECK(rb::setParent(scene, child, first));

    rt.start();
    rt.tick(0.0f);
    CHECK(approxVec(worldOrigin(scene, child), glm::vec3(4.0f, 0.0f, 0.0f)));

    CHECK(rb::setParent(scene, child, second));
    rt.tick(0.0f);
    CHECK(approxVec(worldOrigin(scene, child), glm::vec3(8.0f, 0.0f, 0.0f)));

    scene.destroy(second);
    rt.tick(0.0f);
    CHECK(approxVec(worldOrigin(scene, child), glm::vec3(1.0f, 0.0f, 0.0f)));
    rt.stop();
}

static void reparentKeepingWorldPoseHoldsThePose() {
    rb::Scene scene;

    const rb::Entity rig = scene.create();
    rb::Transform rigPose;
    rigPose.position = {10.0f, 0.0f, 0.0f};
    rigPose.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    rigPose.scale = glm::vec3(2.0f);
    scene.add<rb::Transform>(rig, rigPose);

    const rb::Entity other = scene.create();
    rb::Transform otherPose;
    otherPose.position = {-5.0f, 3.0f, 0.0f};
    scene.add<rb::Transform>(other, otherPose);

    const rb::Entity child = spawnAt(scene, {1.0f, 0.0f, 0.0f});
    CHECK(rb::setParent(scene, child, rig));

    const auto origin = [&](rb::Entity e) {
        return glm::vec3(rb::worldMatrixOf(scene, e) * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    };
    const glm::vec3 before = origin(child);
    CHECK(approxVec(before, glm::vec3(10.0f, 2.0f, 0.0f)));

    // Moving between parents rewrites the local TRS so the world position holds.
    CHECK(rb::setParentKeepingWorldPose(scene, child, other));
    CHECK(rb::parentOf(scene, child) == other);
    CHECK(approxVec(origin(child), before));

    // Un-parenting bakes the world pose straight into the local Transform.
    CHECK(rb::setParentKeepingWorldPose(scene, child, rb::kNullEntity));
    CHECK(rb::parentOf(scene, child) == rb::kNullEntity);
    CHECK(approxVec(origin(child), before));
    CHECK(approxVec(scene.get<rb::Transform>(child).position, before));
}

// Hand-edited files can smuggle a parent cycle past setParent's gate. Every walker has to
// terminate on it: the ancestor walk, the compose tick, and the subtree collectors.
static void corruptCycleNeverHangs() {
    {
        rb::Scene scene;
        const rb::Entity a = scene.create();
        const rb::Entity b = scene.create();
        const rb::Entity outsider = scene.create();
        scene.add<rb::Parent>(a, rb::Parent{b});
        scene.add<rb::Parent>(b, rb::Parent{a});
        CHECK(rb::isAncestor(scene, b, a));         // one hop, before the loop matters
        CHECK(!rb::isAncestor(scene, outsider, a)); // the walk detects the cycle, no hang
        CHECK(rb::setParent(scene, outsider, a));   // no new cycle through the outsider
        CHECK(rb::parentOf(scene, outsider) == a);
    }
    {
        rb::Runtime rt;
        rt.addSystem<rb::TransformSystem>();
        rb::Scene& scene = rt.scene();
        const rb::Entity a = spawnAt(scene, {1.0f, 0.0f, 0.0f});
        const rb::Entity b = spawnAt(scene, {2.0f, 0.0f, 0.0f});
        scene.add<rb::Parent>(a, rb::Parent{b});
        scene.add<rb::Parent>(b, rb::Parent{a});
        rt.start();
        rt.tick(0.0f); // must terminate at the depth cap, not hang or overflow
        CHECK(scene.has<rb::WorldMatrix>(a));
        CHECK(scene.has<rb::WorldMatrix>(b));
        rt.stop();
    }
    {
        rb::Scene scene;
        const rb::Entity a = scene.create();
        const rb::Entity b = scene.create();
        scene.add<rb::Parent>(a, rb::Parent{b});
        scene.add<rb::Parent>(b, rb::Parent{a});
        CHECK(rb::collectSubtree(scene, a).size() == 2u); // each member once, no loop
        rb::destroySubtree(scene, a);
        CHECK(!scene.alive(a));
        CHECK(!scene.alive(b));
    }
}

// The gate must see the whole chain: with a capped ancestor walk, linking the root under
// the leaf of a deeper-than-cap chain used to be accepted and closed a live cycle.
static void deepChainCannotCloseACycle() {
    rb::Scene scene;
    const int depth = rb::kMaxHierarchyDepth + 50;
    std::vector<rb::Entity> chain;
    chain.reserve(static_cast<std::size_t>(depth));
    chain.push_back(scene.create());
    for (int i = 1; i < depth; ++i) {
        chain.push_back(scene.create());
        CHECK(rb::setParent(scene, chain.back(), chain[chain.size() - 2]));
    }

    CHECK(!rb::setParent(scene, chain.front(), chain.back())); // would close the cycle
    CHECK(!rb::parentOf(scene, chain.front()).valid());

    rb::destroySubtree(scene, chain.front()); // the collector survives the full depth
    for (const rb::Entity e : chain) {
        CHECK(!scene.alive(e));
    }
}

// A degenerate (zero-scale) ancestor cannot express the child's world pose as a finite
// local TRS; the gesture must refuse and leave both the link and the Transform untouched
// rather than writing NaN (which would serialize as null and lose the component on load).
static void degenerateParentScaleRefusesPoseKeepingReparent() {
    rb::Scene scene;

    const rb::Entity oldParent = scene.create();
    scene.add<rb::Transform>(oldParent, rb::Transform{});

    const rb::Entity flat = scene.create();
    rb::Transform flatPose;
    flatPose.scale = {0.0f, 1.0f, 1.0f};
    scene.add<rb::Transform>(flat, flatPose);

    const rb::Entity child = spawnAt(scene, {1.0f, 2.0f, 3.0f});
    CHECK(rb::setParent(scene, child, oldParent));

    CHECK(!rb::setParentKeepingWorldPose(scene, child, flat));
    CHECK(rb::parentOf(scene, child) == oldParent); // link restored
    const rb::Transform& t = scene.get<rb::Transform>(child);
    CHECK(approxVec(t.position, glm::vec3(1.0f, 2.0f, 3.0f))); // local TRS untouched
    CHECK(approxVec(t.scale, glm::vec3(1.0f)));
}

void hierarchySuite() {
    parentLinksFollowTheGestures();
    setParentRefusesIllegalLinks();
    destroyedParentOrphansChild();
    destroySubtreeTakesTheBranchOnly();
    worldMatrixComposesThroughParents();
    rootWorldMatrixIsBitIdenticalToLocal();
    transformlessMiddleNodeComposesThrough();
    topologyChangesRecomposeNextTick();
    reparentKeepingWorldPoseHoldsThePose();
    corruptCycleNeverHangs();
    deepChainCannotCloseACycle();
    degenerateParentScaleRefusesPoseKeepingReparent();
}

int main() {
    sceneTransformSuite();
    cameraViewSuite();
    lightingExtendedSuite();
    nameIndexSuite();
    hierarchySuite();
    return rbtest::summary("scene");
}
