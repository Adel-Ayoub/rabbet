#include "examples/common/FlyCamera.h"
#include "examples/common/FlyCameraSystem.h"
#include "examples/common/SceneLoader.h"

#include "rabbet/core/Clock.h"
#include "rabbet/core/Module.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/platform/Input.h"
#include "rabbet/platform/Window.h"
#include "rabbet/render/Geometry.h"
#include "rabbet/render/Lighting.h"
#include "rabbet/render/RenderDevice.h"
#include "rabbet/render/RenderSystem.h"
#include "rabbet/render/Skybox.h"
#include "rabbet/render/SkyboxSystem.h"
#include "rabbet/render/Viewport.h"
#include "rabbet/render/gl/Mesh.h"
#include "rabbet/scene/Camera.h"
#include "rabbet/scene/CameraSystem.h"
#include "rabbet/scene/Light.h"
#include "rabbet/scene/LightSystem.h"
#include "rabbet/scene/Transform.h"
#include "rabbet/scene/TransformSystem.h"
#include "rabbet/util/Log.h"

#include <glm/glm.hpp>

#include <array>
#include <filesystem>
#include <string_view>
#include <utility>

#ifndef RB_ATRIUM_ASSETS
#define RB_ATRIUM_ASSETS "assets"
#endif

namespace {

class AtriumModule final : public rb::Module {
public:
    explicit AtriumModule(std::filesystem::path assets) : m_assets(std::move(assets)) {}

    [[nodiscard]] std::string_view name() const override { return "atrium.walkthrough"; }

    void configure(rb::Runtime& rt) override {
        rt.addSystem<ex::FlyCameraSystem>();
        rt.addSystem<rb::TransformSystem>();
        rt.addSystem<rb::CameraSystem>();
        rt.addSystem<rb::LightSystem>();
        // Before RenderSystem: a skybox swap replaces the cubemap and the irradiance probe
        // that RenderSystem binds for the frame.
        rt.addSystem<rb::SkyboxSystem>();
        rt.addSystem<rb::RenderSystem>();

        const std::array<std::string, 6> faces = {"right.bmp", "left.bmp",  "top.bmp",
                                                  "bottom.bmp", "front.bmp", "back.bmp"};
        rt.addResource<rb::Skybox>(rb::Skybox{ex::loadCubemap(m_assets / "skybox/sky", faces),
                                              rb::gl::Mesh::create(rb::geometry::cube())});

        rb::Lighting& lighting = rt.addResource<rb::Lighting>();
        lighting.ambient = glm::vec3(0.30f, 0.30f, 0.24f);

        const rb::Entity camera = rt.scene().create();
        rb::Transform cameraTransform;
        cameraTransform.position = glm::vec3(0.0f, 1.6f, 2.0f);
        rt.scene().add<rb::Transform>(camera, cameraTransform);
        rt.scene().add<rb::Camera>(camera, rb::Camera{glm::radians(66.0f), 0.05f, 300.0f});
        rt.scene().add<ex::FlyCamera>(camera, ex::FlyCamera{});

        rb::Transform hall;
        hall.scale = glm::vec3(0.008f);
        ex::spawnModel(rt, m_assets / "models/hall/scene.gltf", hall, 0.0f, 0.85f);

        const rb::Entity sun = rt.scene().create();
        rb::DirectionalLight sunLight;
        sunLight.direction = glm::normalize(glm::vec3(-0.86f, -1.0f, -0.97f));
        sunLight.color = glm::vec3(1.0f, 0.95f, 0.82f);
        sunLight.intensity = 3.0f;
        rt.scene().add<rb::DirectionalLight>(sun, sunLight);
    }

private:
    std::filesystem::path m_assets;
};

} // namespace

int main() {
    rb::log::info("starting Atrium");

    rb::WindowConfig config;
    config.title = "Atrium";
    auto window = rb::Window::create(config);
    if (!window) {
        rb::log::error("atrium: could not open a window");
        return 1;
    }
    window->setCursorCaptured(true);

    auto device = rb::createRenderDevice(rb::RendererBackend::Auto, *window);
    if (!device) {
        rb::log::error("atrium could not create the render device");
        return 1;
    }
    device->setDepthTest(true);

    rb::Runtime runtime;
    runtime.addResource<rb::Viewport>();
    rb::Input& input = runtime.addResource<rb::Input>(window->handle());
    runtime.loadModule<AtriumModule>(std::filesystem::path{RB_ATRIUM_ASSETS});
    runtime.start();

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
        device->setClearColor(glm::vec4(0.1f, 0.1f, 0.1f, 1.0f));
        device->clear();

        runtime.tick(clock.tick());

        device->present();
    }

    runtime.stop();
    rb::log::info("atrium ran {} frames", clock.frame());
    return 0;
}
