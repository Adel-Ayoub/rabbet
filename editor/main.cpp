#include "editor/Editor.h"

#include "rabbet/platform/Window.h"
#include "rabbet/render/RenderDevice.h"
#include "rabbet/util/Log.h"

#include <cstdlib>
#include <optional>
#include <string_view>

namespace {

std::optional<rb::RendererBackend> rendererFromArguments(int argc, char* argv[]) {
    rb::RendererBackend renderer = rb::RendererBackend::Auto;
    bool rendererSet = false;
    constexpr std::string_view prefix = "--renderer=";
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        if (argument == "--renderer") {
            rb::log::error(
                "forge expected --renderer=auto, --renderer=opengl or --renderer=vulkan");
            return std::nullopt;
        }
        if (!argument.starts_with(prefix)) {
            continue;
        }
        if (rendererSet) {
            rb::log::error("forge accepts only one --renderer option");
            return std::nullopt;
        }
        const std::optional<rb::RendererBackend> parsed =
            rb::rendererBackendFromName(argument.substr(prefix.size()));
        if (!parsed.has_value()) {
            rb::log::error("forge does not know renderer '{}'", argument.substr(prefix.size()));
            return std::nullopt;
        }
        renderer = *parsed;
        rendererSet = true;
    }
    return renderer;
}

} // namespace

int main(int argc, char* argv[]) {
    rb::log::info("starting Rabbet Forge (editor)");

    const std::optional<rb::RendererBackend> renderer = rendererFromArguments(argc, argv);
    if (!renderer.has_value()) {
        return 2;
    }

    rb::WindowConfig config;
    config.title = "Rabbet";
    config.width = 1440;
    config.height = 810;
    config.fullscreen = std::getenv("RB_FORGE_FULLSCREEN") != nullptr;
    config.clientApi = rb::windowClientApiForRenderer(*renderer);
    auto window = rb::Window::create(config);
    if (!window) {
        rb::log::error("forge: could not open a window");
        return 1;
    }

    auto device = rb::createRenderDevice(*renderer, *window);
    if (!device) {
        return 1;
    }
    rb::log::info("render backend: {}", device->backendName());
    device->setDepthTest(true);

    rb::editor::Editor editor(*window, *device);
    editor.run();
    return 0;
}
