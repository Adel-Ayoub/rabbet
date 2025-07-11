#include "rabbet/core/Clock.h"
#include "rabbet/platform/Input.h"
#include "rabbet/platform/Window.h"
#include "rabbet/render/Geometry.h"
#include "rabbet/render/RenderDevice.h"
#include "rabbet/render/gl/Mesh.h"
#include "rabbet/render/gl/Shader.h"
#include "rabbet/render/gl/Texture.h"
#include "rabbet/util/Log.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <vector>

namespace {

constexpr const char* kVertexSource = R"(#version 410 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
out vec2 vUv;
void main() {
    vUv = aUv;
    gl_Position = vec4(aPosition, 1.0);
}
)";

constexpr const char* kFragmentSource = R"(#version 410 core
in vec2 vUv;
out vec4 FragColor;
uniform sampler2D uTexture;
void main() {
    FragColor = texture(uTexture, vUv);
}
)";

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

    auto shader = rb::gl::Shader::fromSource(kVertexSource, kFragmentSource);
    if (!shader) {
        rb::log::error("sandbox: shader failed to build");
        return 1;
    }

    const rb::gl::Mesh mesh = rb::gl::Mesh::create(rb::geometry::quad());
    const rb::gl::Texture texture = makeCheckerTexture();

    rb::Input input(window->handle());
    rb::FrameClock clock;
    while (!window->shouldClose()) {
        window->pollEvents();
        input.update();
        if (input.keyPressed(rb::Key::Escape)) {
            window->requestClose();
        }

        device->setViewport(window->width(), window->height());
        device->setClearColor(glm::vec4(0.07f, 0.09f, 0.12f, 1.0f));
        device->clear();

        shader->bind();
        shader->setInt("uTexture", 0);
        texture.bind(0);
        mesh.draw();

        window->swapBuffers();
        clock.tick();
    }

    rb::log::info("sandbox ran {} frames", clock.frame());
    return 0;
}
