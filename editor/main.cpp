#include "editor/Editor.h"

#include "rabbet/platform/Window.h"
#include "rabbet/render/RenderDevice.h"
#include "rabbet/util/Log.h"

#include <cstdlib>

int main() {
    rb::log::info("starting Rabbet Forge (editor)");

    rb::WindowConfig config;
    config.title = "Rabbet";
    config.width = 1440;
    config.height = 810;
    config.fullscreen = std::getenv("RB_FORGE_FULLSCREEN") != nullptr;
    auto window = rb::Window::create(config);
    if (!window) {
        rb::log::error("forge: could not open a window");
        return 1;
    }

    auto device = rb::createRenderDevice();
    rb::log::info("render backend: {}", device->backendName());
    device->setDepthTest(true);

    rb::editor::Editor editor(*window, *device);
    editor.run();
    return 0;
}
