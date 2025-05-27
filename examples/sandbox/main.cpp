#include "rabbet/core/Module.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/core/System.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/platform/Window.h"

#include <glad/glad.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string_view>

namespace {

struct Position {
    float x = 0.0f;
    float y = 0.0f;
};

struct Velocity {
    float dx = 0.0f;
    float dy = 0.0f;
};

struct FrameStats {
    std::uint64_t frames = 0;
    float elapsed = 0.0f;
};

class MovementSystem final : public rb::System {
public:
    void onUpdate(rb::Runtime& rt, float dt) override {
        rt.scene().each<Position, Velocity>([dt](rb::Entity, Position& p, Velocity& v) {
            p.x += v.dx * dt;
            p.y += v.dy * dt;
        });
    }
};

class StatsSystem final : public rb::System {
public:
    void onUpdate(rb::Runtime& rt, float dt) override {
        FrameStats& stats = rt.resource<FrameStats>();
        ++stats.frames;
        stats.elapsed += dt;
    }
};

class SandboxModule final : public rb::Module {
public:
    [[nodiscard]] std::string_view name() const override { return "sandbox"; }
    void configure(rb::Runtime& rt) override {
        rt.addResource<FrameStats>();
        rt.addSystem<MovementSystem>();
        rt.addSystem<StatsSystem>();

        const rb::Entity mover = rt.scene().create();
        rt.scene().add<Position>(mover, Position{0.0f, 0.0f});
        rt.scene().add<Velocity>(mover, Velocity{1.0f, 0.5f});
    }
};

} // namespace

int main() {
    rb::WindowConfig config;
    config.title = "Rabbet - Sandbox";

    auto window = rb::Window::create(config);
    if (!window) {
        std::fprintf(stderr, "sandbox: could not open a window\n");
        return 1;
    }

    rb::Runtime runtime;
    runtime.loadModule<SandboxModule>();
    runtime.start();

    auto previous = std::chrono::steady_clock::now();
    while (!window->shouldClose()) {
        const auto now = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(now - previous).count();
        previous = now;

        runtime.tick(dt);

        glClearColor(0.07f, 0.09f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        window->swapBuffers();
        window->pollEvents();
    }

    runtime.stop();

    const FrameStats& stats = runtime.resource<FrameStats>();
    std::fprintf(stderr, "sandbox: ran %llu frames over %.2fs\n",
                 static_cast<unsigned long long>(stats.frames), static_cast<double>(stats.elapsed));
    return 0;
}
