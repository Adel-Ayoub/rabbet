#include "rabbet/core/Clock.h"
#include "rabbet/core/Module.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/core/System.h"
#include "rabbet/platform/Input.h"
#include "rabbet/platform/Window.h"
#include "rabbet/render/Geometry.h"
#include "rabbet/render/Material.h"
#include "rabbet/render/RenderDevice.h"
#include "rabbet/render/RenderSystem.h"
#include "rabbet/render/Viewport.h"
#include "rabbet/render/gl/Mesh.h"
#include "rabbet/render/gl/Texture.h"
#include "rabbet/scene/Camera.h"
#include "rabbet/scene/CameraSystem.h"
#include "rabbet/scene/Transform.h"
#include "rabbet/scene/TransformSystem.h"
#include "rabbet/util/Log.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <string_view>
#include <vector>

namespace {

struct Spin {
    float speed = 1.0f;
};

class SpinSystem final : public rb::System {
public:
    void onUpdate(rb::Runtime& rt, float dt) override {
        rt.scene().each<Spin, rb::Transform>([dt](rb::Entity, Spin& spin, rb::Transform& transform) {
            const glm::quat step =
                glm::angleAxis(spin.speed * dt, glm::normalize(glm::vec3(0.3f, 1.0f, 0.0f)));
            transform.rotation = step * transform.rotation;
        });
    }
};

rb::gl::Texture makeCheckerTexture() {
    constexpr int size = 64;
    constexpr int tile = 8;
    std::vector<std::byte> pixels(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) *
                                  4u);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const bool light = ((x / tile) + (y / tile)) % 2 == 0;
            const std::byte shade = light ? std::byte{230} : std::byte{45};
            const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(size) +
                                   static_cast<std::size_t>(x)) *
                                  4u;
            pixels[i + 0] = shade;
            pixels[i + 1] = shade;
            pixels[i + 2] = shade;
            pixels[i + 3] = std::byte{255};
        }
    }
    rb::gl::TextureConfig config;
    config.generateMipmaps = false;
    config.linearFilter = false;
    return rb::gl::Texture::fromPixels(pixels, size, size, 4, config);
}

class CubeDemoModule final : public rb::Module {
public:
    [[nodiscard]] std::string_view name() const override { return "cube-demo"; }
    void configure(rb::Runtime& rt) override {
        rt.addSystem<SpinSystem>();
        rt.addSystem<rb::TransformSystem>();
        rt.addSystem<rb::CameraSystem>();
        rt.addSystem<rb::RenderSystem>();

        const rb::Entity camera = rt.scene().create();
        rb::Transform cameraTransform;
        cameraTransform.position = glm::vec3(0.0f, 0.0f, 3.0f);
        rt.scene().add<rb::Transform>(camera, cameraTransform);
        rt.scene().add<rb::Camera>(camera, rb::Camera{});

        const rb::Entity cube = rt.scene().create();
        rt.scene().add<rb::Transform>(cube, rb::Transform{});
        rt.scene().add<Spin>(cube, Spin{1.0f});
        rt.scene().add<rb::gl::Mesh>(cube, rb::gl::Mesh::create(rb::geometry::cube()));
        rt.scene().add<rb::Material>(cube, rb::Material{makeCheckerTexture(), glm::vec3(1.0f)});
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

    auto device = rb::createRenderDevice();
    rb::log::info("render backend: {}", device->backendName());
    device->setDepthTest(true);

    rb::Runtime runtime;
    runtime.addResource<rb::Viewport>();
    runtime.loadModule<CubeDemoModule>();
    runtime.start();

    rb::Input input(window->handle());
    rb::FrameClock clock;
    while (!window->shouldClose()) {
        window->pollEvents();
        input.update();
        if (input.keyPressed(rb::Key::Escape)) {
            window->requestClose();
        }

        rb::Viewport& viewport = runtime.resource<rb::Viewport>();
        viewport.width = window->width();
        viewport.height = window->height();

        device->setViewport(viewport.width, viewport.height);
        device->setClearColor(glm::vec4(0.07f, 0.09f, 0.12f, 1.0f));
        device->clear();

        runtime.tick(clock.tick());

        window->swapBuffers();
    }

    runtime.stop();
    rb::log::info("sandbox ran {} frames", clock.frame());
    return 0;
}
