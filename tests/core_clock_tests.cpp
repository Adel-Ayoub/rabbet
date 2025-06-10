#include "rabbet/core/Clock.h"
#include "tests/Test.h"

static void frameCountAndDelta() {
    rb::FrameClock clock;
    CHECK(clock.frame() == 0u);
    CHECK(clock.delta() == 0.0f);

    clock.tick();
    CHECK(clock.frame() == 1u);
    CHECK(clock.delta() >= 0.0f);

    clock.tick();
    CHECK(clock.frame() == 2u);
    CHECK(clock.elapsed() >= 0.0);
}

static void resetReturnsToZero() {
    rb::FrameClock clock;
    clock.tick();
    clock.tick();
    clock.reset();
    CHECK(clock.frame() == 0u);
    CHECK(clock.delta() == 0.0f);
}

int main() {
    frameCountAndDelta();
    resetReturnsToZero();
    return rbtest::summary("core_clock");
}
