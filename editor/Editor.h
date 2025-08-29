#pragma once

#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "rabbet/core/Runtime.h"
#include "rabbet/ecs/Entity.h"

namespace rb {
class Window;
class RenderDevice;
} // namespace rb

namespace forge {

class Editor {
public:
    Editor(rb::Window& window, rb::RenderDevice& device);
    ~Editor();
    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;
    Editor(Editor&&) = delete;
    Editor& operator=(Editor&&) = delete;

    void run();

private:
    void buildScene();
    void addSphere(const std::string& name, const glm::vec3& position, const glm::vec3& color,
                   float metallic, float roughness);
    void drawUi();
    void drawHierarchy();
    void drawInspector();

    rb::Window& m_window;
    rb::RenderDevice& m_device;
    rb::Runtime m_runtime;
    std::vector<std::pair<std::string, rb::Entity>> m_entities;
    rb::Entity m_selected;
};

} // namespace forge
