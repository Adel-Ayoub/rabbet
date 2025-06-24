#include "rabbet/core/Runtime.h"
#include "rabbet/scene/Transform.h"
#include "rabbet/scene/TransformSystem.h"
#include "rabbet/scene/WorldMatrix.h"
#include "tests/Test.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

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

int main() {
    identityLeavesPointsUntouched();
    translationMovesOrigin();
    scaleScalesPoint();
    rotationRotatesPoint();
    composedTransformAppliesScaleRotateTranslate();
    systemCachesWorldMatrix();
    return rbtest::summary("scene_transform");
}
