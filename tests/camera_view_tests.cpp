#include "rabbet/ecs/Scene.h"
#include "rabbet/scene/Camera.h"
#include "rabbet/scene/CameraView.h"
#include "rabbet/scene/Transform.h"
#include "tests/Test.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <optional>

namespace {

bool approx(float a, float b) { return std::fabs(a - b) < 1e-4f; }

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

} // namespace

int main() {
    noCameraYieldsNothing();
    viewInvertsCameraPose();
    rotationAimsTheView();
    projectionUsesCameraParams();
    firstCameraWinsAndScaleIsIgnored();
    return rbtest::summary("camera_view");
}
