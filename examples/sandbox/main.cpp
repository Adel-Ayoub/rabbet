#include "rabbet/core/Clock.h"
#include "rabbet/core/Module.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/core/System.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/platform/Input.h"
#include "rabbet/platform/Window.h"
#include "rabbet/util/Log.h"

#include <glad/glad.h>

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

class MovementSystem final : public rb::System {
public:
    void onUpdate(rb::Runtime& rt, float dt) override {
        rt.scene().each<Position, Velocity>([dt](rb::Entity, Position& p, Velocity& v) {
            p.x += v.dx * dt;
            p.y += v.dy * dt;
        });
    }
};

class SandboxModule final : public rb::Module {
public:
    [[nodiscard]] std::string_view name() const override { return "sandbox"; }
    void configure(rb::Runtime& rt) override {
        rt.addSystem<MovementSystem>();
        const rb::Entity mover = rt.scene().create();
        rt.scene().add<Position>(mover, Position{0.0f, 0.0f});
        rt.scene().add<Velocity>(mover, Velocity{1.0f, 0.5f});
    }
};

} // namespace

int main() {
    rb::log::info("starting Rabbet sandbox");

    rb::WindowConfig config;
    config.title = "Rabbet - Sandbox";
    auto window = rb::Window::create(config);
    if (!window) {
        rb::log::error("sandbox: could not open a window");
        return 1;
    }

    rb::Runtime runtime;
    runtime.addResource<rb::Input>(window->handle());
    runtime.loadModule<SandboxModule>();
    runtime.start();

    rb::Input& input = runtime.resource<rb::Input>();
    rb::FrameClock clock;
    while (!window->shouldClose()) {
        window->pollEvents();
        input.update();

        if (input.keyPressed(rb::Key::Escape)) {
            window->requestClose();
        }
        if (input.keyPressed(rb::Key::Space)) {
            rb::log::info("space pressed on frame {}", clock.frame());
        }

        const float dt = clock.tick();
        runtime.tick(dt);

        glClearColor(0.07f, 0.09f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        window->swapBuffers();
    }

    runtime.stop();
    rb::log::info("sandbox ran {} frames over {:.2f}s", clock.frame(), clock.elapsed());
    return 0;
}
