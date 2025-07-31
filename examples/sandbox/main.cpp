#include "rabbet/core/Clock.h"
#include "rabbet/core/Module.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/core/System.h"
#include "rabbet/platform/Input.h"
#include "rabbet/platform/Window.h"
#include "rabbet/render/Geometry.h"
#include "rabbet/render/ImageLoader.h"
#include "rabbet/render/Material.h"
#include "rabbet/render/ModelLoader.h"
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

#include <filesystem>
#include <optional>
#include <string_view>
#include <utility>

#ifndef RB_SANDBOX_ASSETS
#define RB_SANDBOX_ASSETS "assets"
#endif

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

rb::gl::Mesh loadCubeMesh(const std::filesystem::path& assets) {
    if (const std::optional<rb::Model> model = rb::loadModel(assets / "models" / "cube.obj");
        model && !model->meshes.empty()) {
        return rb::gl::Mesh::create(model->meshes.front().data);
    }
    rb::log::warn("sandbox: using procedural cube (model load failed)");
    return rb::gl::Mesh::create(rb::geometry::cube());
}

rb::gl::Texture loadCheckerTexture(const std::filesystem::path& assets) {
    if (const std::optional<rb::Image> image = rb::loadImage(assets / "textures" / "checker.ppm")) {
        return rb::gl::Texture::fromPixels(image->pixels, image->width, image->height,
                                           image->channels);
    }
    rb::log::warn("sandbox: using a solid texture (image load failed)");
    return rb::gl::Texture::solid(220, 180, 120);
}

class CubeDemoModule final : public rb::Module {
public:
    explicit CubeDemoModule(std::filesystem::path assets) : m_assets(std::move(assets)) {}

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
        rt.scene().add<Spin>(cube, Spin{0.8f});
        rt.scene().add<rb::gl::Mesh>(cube, loadCubeMesh(m_assets));
        rt.scene().add<rb::Material>(cube,
                                     rb::Material{loadCheckerTexture(m_assets), glm::vec3(1.0f)});
    }

private:
    std::filesystem::path m_assets;
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
    runtime.loadModule<CubeDemoModule>(std::filesystem::path{RB_SANDBOX_ASSETS});
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
