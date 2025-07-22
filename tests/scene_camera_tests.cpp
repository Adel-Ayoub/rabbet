#include "rabbet/core/Runtime.h"
#include "rabbet/render/RenderView.h"
#include "rabbet/render/Viewport.h"
#include "rabbet/scene/Camera.h"
#include "rabbet/scene/CameraSystem.h"
#include "rabbet/scene/Transform.h"
#include "tests/Test.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace {

bool approx(float a, float b, float eps = 1.0e-4f) {
    return std::fabs(a - b) <= eps;
}

} // namespace

static void cameraSystemBuildsViewAndProjection() {
    rb::Runtime rt;
    rt.addResource<rb::Viewport>(rb::Viewport{800, 600});
    rt.addSystem<rb::CameraSystem>();

    const rb::Entity cameraEntity = rt.scene().create();
    rb::Transform transform;
    transform.position = glm::vec3(0.0f, 0.0f, 5.0f);
    rt.scene().add<rb::Transform>(cameraEntity, transform);
    const rb::Camera camera;
    rt.scene().add<rb::Camera>(cameraEntity, camera);

    rt.start();
    rt.tick(0.0f);
    rt.stop();

    CHECK(rt.hasResource<rb::RenderView>());
    const rb::RenderView& view = rt.resource<rb::RenderView>();

    CHECK(approx(view.position.z, 5.0f));

    const glm::vec3 inViewSpace = glm::vec3(view.view * glm::vec4(transform.position, 1.0f));
    CHECK(approx(inViewSpace.x, 0.0f) && approx(inViewSpace.y, 0.0f) && approx(inViewSpace.z, 0.0f));

    const glm::mat4 expected =
        glm::perspective(camera.fovY, 800.0f / 600.0f, camera.nearPlane, camera.farPlane);
    bool same = true;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            same = same && approx(view.projection[col][row], expected[col][row]);
        }
    }
    CHECK(same);
}

int main() {
    cameraSystemBuildsViewAndProjection();
    return rbtest::summary("scene_camera");
}
