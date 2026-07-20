#include "rabbet/core/Clock.h"
#include "rabbet/core/Module.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/core/System.h"
#include "rabbet/platform/Input.h"
#include "rabbet/platform/Window.h"
#include "rabbet/render/Geometry.h"
#include "rabbet/render/Image.h"
#include "rabbet/render/ImageLoader.h"
#include "rabbet/render/Material.h"
#include "rabbet/render/ModelLoader.h"
#include "rabbet/render/PbrMaterial.h"
#include "rabbet/render/RenderDevice.h"
#include "rabbet/render/RenderSystem.h"
#include "rabbet/render/Sky.h"
#include "rabbet/render/Skybox.h"
#include "rabbet/render/SkyboxSystem.h"
#include "rabbet/render/Viewport.h"
#include "rabbet/render/gl/Cubemap.h"
#include "rabbet/render/gl/Mesh.h"
#include "rabbet/render/gl/Texture.h"
#include "rabbet/scene/Camera.h"
#include "rabbet/scene/CameraSystem.h"
#include "rabbet/scene/Light.h"
#include "rabbet/scene/LightSystem.h"
#include "rabbet/scene/Transform.h"
#include "rabbet/scene/TransformSystem.h"
#include "rabbet/util/Log.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <cstddef>
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

glm::vec3 skyFaceDirection(int face, float u, float v) {
    const float uc = 2.0f * u - 1.0f;
    const float vc = 2.0f * v - 1.0f;
    switch (face) {
        case 0: return {1.0f, -vc, -uc};
        case 1: return {-1.0f, -vc, uc};
        case 2: return {uc, 1.0f, vc};
        case 3: return {uc, -1.0f, -vc};
        case 4: return {uc, -vc, 1.0f};
        default: return {-uc, -vc, -1.0f};
    }
}

rb::gl::Cubemap makeSkyCubemap() {
    constexpr int size = 64;
    std::array<rb::Image, 6> faces;
    for (int face = 0; face < 6; ++face) {
        rb::Image image;
        image.width = size;
        image.height = size;
        image.channels = 3;
        image.pixels.resize(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 3u);
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);
                const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
                const glm::vec3 color = rb::skyColor(skyFaceDirection(face, u, v));
                const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(size) +
                                       static_cast<std::size_t>(x)) *
                                      3u;
                image.pixels[i + 0] =
                    static_cast<std::byte>(static_cast<unsigned char>(color.r * 255.0f));
                image.pixels[i + 1] =
                    static_cast<std::byte>(static_cast<unsigned char>(color.g * 255.0f));
                image.pixels[i + 2] =
                    static_cast<std::byte>(static_cast<unsigned char>(color.b * 255.0f));
            }
        }
        faces[static_cast<std::size_t>(face)] = std::move(image);
    }
    return rb::gl::Cubemap::fromFaces(faces);
}

class ShowcaseModule final : public rb::Module {
public:
    explicit ShowcaseModule(std::filesystem::path assets) : m_assets(std::move(assets)) {}

    [[nodiscard]] std::string_view name() const override { return "showcase"; }

    void configure(rb::Runtime& rt) override {
        rt.addSystem<SpinSystem>();
        rt.addSystem<rb::TransformSystem>();
        rt.addSystem<rb::CameraSystem>();
        rt.addSystem<rb::LightSystem>();
        // Before RenderSystem: a skybox swap replaces the cubemap and the irradiance probe
        // that RenderSystem binds for the frame.
        rt.addSystem<rb::SkyboxSystem>();
        rt.addSystem<rb::RenderSystem>();

        rt.addResource<rb::Skybox>(
            rb::Skybox{makeSkyCubemap(), rb::gl::Mesh::create(rb::geometry::cube())});

        const rb::Entity camera = rt.scene().create();
        rb::Transform cameraTransform;
        cameraTransform.position = glm::vec3(0.0f, 2.0f, 6.0f);
        cameraTransform.rotation = glm::angleAxis(glm::radians(-16.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        rt.scene().add<rb::Transform>(camera, cameraTransform);
        rt.scene().add<rb::Camera>(camera, rb::Camera{});

        const rb::Entity floor = rt.scene().create();
        rb::Transform floorTransform;
        floorTransform.position = glm::vec3(0.0f, -1.0f, 0.0f);
        floorTransform.scale = glm::vec3(14.0f, 0.3f, 14.0f);
        rt.scene().add<rb::Transform>(floor, floorTransform);
        rt.scene().add<rb::gl::Mesh>(floor, rb::gl::Mesh::create(rb::geometry::cube()));
        rt.scene().add<rb::Material>(
            floor, rb::Material{rb::gl::Texture::solid(170, 170, 175), glm::vec3(0.85f), 0.1f, 8.0f});

        const rb::Entity cube = rt.scene().create();
        rb::Transform cubeTransform;
        cubeTransform.position = glm::vec3(-1.7f, 0.0f, 0.0f);
        rt.scene().add<rb::Transform>(cube, cubeTransform);
        rt.scene().add<Spin>(cube, Spin{0.8f});
        rt.scene().add<rb::gl::Mesh>(cube, loadCubeMesh(m_assets));
        rt.scene().add<rb::Material>(cube,
                                     rb::Material{loadCheckerTexture(m_assets), glm::vec3(1.0f)});

        addPbrSphere(rt, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.78f, 0.34f), 1.0f, 0.25f);
        addPbrSphere(rt, glm::vec3(1.7f, 0.0f, 0.0f), glm::vec3(0.9f, 0.25f, 0.25f), 0.0f, 0.55f);

        const rb::Entity sun = rt.scene().create();
        rb::DirectionalLight sunLight;
        sunLight.direction = glm::normalize(glm::vec3(-0.5f, -0.9f, -0.6f));
        sunLight.color = glm::vec3(1.0f, 0.95f, 0.85f);
        sunLight.intensity = 2.5f;
        rt.scene().add<rb::DirectionalLight>(sun, sunLight);

        const rb::Entity lamp = rt.scene().create();
        rb::Transform lampTransform;
        lampTransform.position = glm::vec3(2.0f, 2.0f, 2.5f);
        rt.scene().add<rb::Transform>(lamp, lampTransform);
        rb::PointLight lampLight;
        lampLight.color = glm::vec3(0.5f, 0.7f, 1.0f);
        lampLight.intensity = 6.0f;
        rt.scene().add<rb::PointLight>(lamp, lampLight);
    }

private:
    void addPbrSphere(rb::Runtime& rt, const glm::vec3& position, const glm::vec3& color,
                      float metallic, float roughness) {
        const rb::Entity entity = rt.scene().create();
        rb::Transform transform;
        transform.position = position;
        rt.scene().add<rb::Transform>(entity, transform);
        rt.scene().add<rb::gl::Mesh>(entity, rb::gl::Mesh::create(rb::geometry::sphere()));
        rt.scene().add<rb::PbrMaterial>(
            entity,
            rb::PbrMaterial{rb::gl::Texture::solid(255, 255, 255), color, metallic, roughness, 1.0f});
    }

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
    runtime.loadModule<ShowcaseModule>(std::filesystem::path{RB_SANDBOX_ASSETS});
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
