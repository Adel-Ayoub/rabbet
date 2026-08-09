#include "examples/common/FlyCamera.h"
#include "examples/common/FlyCameraSystem.h"
#include "examples/common/SceneLoader.h"

#include "rabbet/core/Clock.h"
#include "rabbet/core/Module.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/platform/Input.h"
#include "rabbet/platform/Window.h"
#include "rabbet/render/Lighting.h"
#include "rabbet/render/RenderDevice.h"
#include "rabbet/render/RenderSystem.h"
#include "rabbet/render/Viewport.h"
#include "rabbet/scene/Camera.h"
#include "rabbet/scene/CameraSystem.h"
#include "rabbet/scene/Light.h"
#include "rabbet/scene/LightSystem.h"
#include "rabbet/scene/Transform.h"
#include "rabbet/scene/TransformSystem.h"
#include "rabbet/util/Log.h"

#include <glm/glm.hpp>

#include <filesystem>
#include <string_view>
#include <utility>

#ifndef RB_GARDEN_ASSETS
#define RB_GARDEN_ASSETS "assets"
#endif

namespace {

void addPointLight(rb::Runtime& rt, const glm::vec3& at, const glm::vec3& color, float intensity) {
    const rb::Entity light = rt.scene().create();
    rb::Transform t;
    t.position = at;
    rt.scene().add<rb::Transform>(light, t);
    rb::PointLight point;
    point.color = color;
    point.intensity = intensity;
    point.linear = 0.02f;
    point.quadratic = 0.0015f;
    rt.scene().add<rb::PointLight>(light, point);
}

class GardenModule final : public rb::Module {
public:
    explicit GardenModule(std::filesystem::path assets) : m_assets(std::move(assets)) {}

    [[nodiscard]] std::string_view name() const override { return "garden.walk"; }

    void configure(rb::Runtime& rt) override {
        rt.addSystem<ex::FlyCameraSystem>();
        rt.addSystem<rb::TransformSystem>();
        rt.addSystem<rb::CameraSystem>();
        rt.addSystem<rb::LightSystem>();
        rt.addSystem<rb::RenderSystem>();

        rb::Lighting& lighting = rt.addResource<rb::Lighting>();
        lighting.ambient = glm::vec3(0.34f, 0.40f, 0.46f);

        const rb::Entity camera = rt.scene().create();
        rb::Transform cameraTransform;
        cameraTransform.position = glm::vec3(0.0f, 4.0f, 22.0f);
        rt.scene().add<rb::Transform>(camera, cameraTransform);
        rt.scene().add<rb::Camera>(camera, rb::Camera{glm::radians(66.0f), 0.05f, 300.0f});
        ex::FlyCamera fly;
        fly.pitch = -0.12f;
        rt.scene().add<ex::FlyCamera>(camera, fly);

        rb::Transform garden;
        garden.position = glm::vec3(0.0f, -1.0f, 0.0f);
        garden.scale = glm::vec3(0.1f);
        ex::spawnModel(rt, m_assets / "models/courtyard.obj", garden, 0.0f, 0.85f);

        rb::Transform figure;
        figure.position = glm::vec3(5.0f, 0.0f, 2.0f);
        figure.scale = glm::vec3(0.1f);
        ex::spawnModel(rt, m_assets / "models/figure.obj", figure, 0.0f, 0.7f);

        const rb::Entity sun = rt.scene().create();
        rb::DirectionalLight sunLight;
        sunLight.direction = glm::normalize(glm::vec3(-0.4f, -0.85f, -0.5f));
        sunLight.color = glm::vec3(0.72f, 0.78f, 0.96f);
        sunLight.intensity = 2.4f;
        rt.scene().add<rb::DirectionalLight>(sun, sunLight);

        addPointLight(rt, glm::vec3(0.0f, 9.0f, 0.0f), glm::vec3(0.6f, 0.6f, 0.95f), 9.0f);
        addPointLight(rt, glm::vec3(-9.0f, 6.0f, 8.0f), glm::vec3(0.55f, 0.7f, 1.0f), 7.0f);
    }

private:
    std::filesystem::path m_assets;
};

} // namespace

int main() {
    rb::log::info("starting Garden");

    rb::WindowConfig config;
    config.title = "Garden";
    auto window = rb::Window::create(config);
    if (!window) {
        rb::log::error("garden: could not open a window");
        return 1;
    }
    window->setCursorCaptured(true);

    auto device = rb::createRenderDevice(rb::RendererBackend::Auto, *window);
    if (!device) {
        rb::log::error("garden could not create the render device");
        return 1;
    }
    device->setDepthTest(true);

    rb::Runtime runtime;
    runtime.addResource<rb::Viewport>();
    rb::Input& input = runtime.addResource<rb::Input>(window->handle());
    runtime.loadModule<GardenModule>(std::filesystem::path{RB_GARDEN_ASSETS});
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
        device->setClearColor(glm::vec4(0.549f, 0.671f, 0.631f, 1.0f));
        device->clear();

        runtime.tick(clock.tick());

        device->present();
    }

    runtime.stop();
    rb::log::info("garden ran {} frames", clock.frame());
    return 0;
}
