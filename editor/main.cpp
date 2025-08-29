#include "editor/Editor.h"

#include "rabbet/platform/Window.h"
#include "rabbet/render/RenderDevice.h"
#include "rabbet/util/Log.h"

int main() {
    rb::log::info("starting Rabbet Forge (editor)");

    rb::WindowConfig config;
    config.title = "Rabbet Forge";
    config.width = 1440;
    config.height = 900;
    auto window = rb::Window::create(config);
    if (!window) {
        rb::log::error("forge: could not open a window");
        return 1;
    }

    auto device = rb::createRenderDevice();
    rb::log::info("render backend: {}", device->backendName());
    device->setDepthTest(true);

    forge::Editor editor(*window, *device);
    editor.run();
    return 0;
}
