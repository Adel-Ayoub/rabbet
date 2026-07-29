#include "rabbet/render/WaterComponent.h"
#include "tests/Test.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <limits>

namespace {

bool approx(float a, float b, float eps = 1.0e-4f) { return std::fabs(a - b) <= eps; }

// The surface takes its world POSITION (including a parent's contribution) but never a parent's
// rotation or scale: water stays horizontal and only `extent` sizes it.
void modelTakesPositionOnly() {
    glm::mat4 world = glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, -2.0f, 7.0f));
    world = glm::rotate(world, glm::radians(40.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    world = glm::scale(world, glm::vec3(5.0f));

    const glm::mat4 model = rb::waterSurfaceModel(world, glm::vec2(10.0f, 4.0f));
    CHECK(approx(model[3][0], 3.0f));
    CHECK(approx(model[3][1], -2.0f));
    CHECK(approx(model[3][2], 7.0f));

    // Extent maps x -> world X and y -> world Z; a swapped axis would break a non-square lake.
    CHECK(approx(model[0][0], 10.0f));
    CHECK(approx(model[2][2], 4.0f));
    CHECK(approx(model[1][1], 1.0f)); // height is never scaled

    // The parent's 40-degree tilt and 5x scale are gone: the basis stays axis-aligned.
    CHECK(approx(model[0][1], 0.0f));
    CHECK(approx(model[1][2], 0.0f));
    CHECK(approx(model[2][1], 0.0f));
}

// Serialized junk must never reach the rasterizer: a NaN survives std::clamp (every comparison
// is false), so the guard has to test finiteness first.
void modelRefusesNonFiniteInput() {
    const glm::mat4 world = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 3.0f));
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    const glm::mat4 a = rb::waterSurfaceModel(world, glm::vec2(nan, 4.0f));
    CHECK(std::isfinite(a[0][0]));
    CHECK(approx(a[2][2], 4.0f)); // the healthy axis is untouched

    const glm::mat4 b = rb::waterSurfaceModel(world, glm::vec2(inf, inf));
    CHECK(std::isfinite(b[0][0]));
    CHECK(std::isfinite(b[2][2]));

    glm::mat4 nanWorld = world;
    nanWorld[3][1] = nan;
    const glm::mat4 c = rb::waterSurfaceModel(nanWorld, glm::vec2(2.0f, 2.0f));
    CHECK(std::isfinite(c[3][1]));

    // Zero and negative extents collapse to a minimum rather than a degenerate quad.
    const glm::mat4 d = rb::waterSurfaceModel(world, glm::vec2(0.0f, -8.0f));
    CHECK(d[0][0] > 0.0f);
    CHECK(d[2][2] > 0.0f);
}

// The same finiteness discipline for the shader-bound floats, which would otherwise paint NaN
// into the scene target and spread it through the bloom chain.
void sanitizeReplacesNonFiniteFields() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    rb::WaterComponent w;
    w.waveTileScale = nan;
    w.waveStrength = std::numeric_limits<float>::infinity();
    w.waveSpeed = -nan;
    w.smoothness = 40.0f; // finite but out of range
    w.deepColor = glm::vec4(nan, 0.5f, 0.5f, 1.0f);
    rb::sanitizeWater(w);

    CHECK(std::isfinite(w.waveTileScale));
    CHECK(std::isfinite(w.waveStrength));
    CHECK(std::isfinite(w.waveSpeed));
    CHECK(approx(w.smoothness, 1.0f)); // clamped into 0..1
    CHECK(std::isfinite(w.deepColor.r));
    CHECK(approx(w.deepColor.g, 0.5f)); // healthy channels survive

    rb::WaterComponent healthy;
    healthy.waveSpeed = 2.5f;
    healthy.smoothness = 0.25f;
    rb::sanitizeWater(healthy);
    CHECK(approx(healthy.waveSpeed, 2.5f)); // a sane component is left alone
    CHECK(approx(healthy.smoothness, 0.25f));
    CHECK(approx(healthy.extent.x, 30.0f));
}

// The wave octaves complete whole cycles over the period the pass wraps on, so a surface's phase
// never jumps - at any authored speed, not just the default.
void wavePeriodWrapsOnWholeCycles() {
    constexpr double kTwoPi = 6.283185307179586;
    constexpr double kPeriod = 8.0 * 3.141592653589793;
    const double multipliers[] = {1.00, 1.25, 1.75};
    for (const double m : multipliers) {
        const double cycles = kPeriod * m / kTwoPi;
        CHECK(std::fabs(cycles - std::round(cycles)) < 1.0e-9);
    }

    // The pass wraps (time * speed), so an arbitrary speed still lands on a whole cycle.
    const double speeds[] = {0.35, 1.0, 1.37, 4.2};
    for (const double s : speeds) {
        const double before = std::fmod((kPeriod / s - 1.0e-6) * s, kPeriod);
        const double after = std::fmod((kPeriod / s + 1.0e-6) * s, kPeriod);
        const double jump = std::fabs((before + 2.0e-6 * s) - (after + kPeriod));
        CHECK(jump < 1.0e-4); // the discontinuity is exactly one period, i.e. invisible
    }
}

} // namespace

int main() {
    modelTakesPositionOnly();
    modelRefusesNonFiniteInput();
    sanitizeReplacesNonFiniteFields();
    wavePeriodWrapsOnWholeCycles();
    return rbtest::summary("render_water");
}
