#include "rabbet/platform/Window.h"

#include <glad/glad.h>

#include <cstdio>

int main() {
    rb::WindowConfig config;
    config.title = "Rabbet - Sandbox";

    auto window = rb::Window::create(config);
    if (!window) {
        std::fprintf(stderr, "sandbox: could not open a window\n");
        return 1;
    }

    while (!window->shouldClose()) {
        glClearColor(0.07f, 0.09f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        window->swapBuffers();
        window->pollEvents();
    }

    return 0;
}
