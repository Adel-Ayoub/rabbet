#include "editor/panels/InspectorPanel.h"

#include "editor/ComponentDrawers.h"
#include "editor/EditorContext.h"

#include "rabbet/core/Runtime.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/serialize/ComponentRegistry.h"

#include <imgui.h>

namespace rb::editor {

void InspectorPanel::onImGui() {
    rb::Scene& scene = m_context.runtime.scene();
    if (!scene.alive(m_context.selected)) {
        ImGui::TextDisabled("Nothing selected");
        return;
    }
    const rb::Entity e = m_context.selected;
    const rb::ComponentRegistry& registry = m_context.registry;

    ImGui::TextDisabled("Entity %u", e.index());
    ImGui::SameLine();
    if (ImGui::Button("Add Component")) {
        ImGui::OpenPopup("##addComponent");
    }
    if (ImGui::BeginPopup("##addComponent")) {
        bool any = false;
        for (const rb::ComponentRegistry::Entry& entry : registry.entries()) {
            if (entry.has(scene, e)) {
                continue;
            }
            any = true;
            if (ImGui::MenuItem(entry.name.c_str())) {
                entry.addDefault(scene, e);
            }
        }
        if (!any) {
            ImGui::TextDisabled("All components added");
        }
        ImGui::EndPopup();
    }
    ImGui::Separator();

    // Draw every component the entity has, in registration order. Removal is deferred
    // until after the loop so we never mutate the component pools mid-iteration.
    const rb::ComponentRegistry::Entry* toRemove = nullptr;
    for (const rb::ComponentRegistry::Entry& entry : registry.entries()) {
        if (!entry.has(scene, e)) {
            continue;
        }
        ImGui::PushID(entry.name.c_str());
        const bool open =
            ImGui::CollapsingHeader(entry.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
        if (ImGui::BeginPopupContextItem("##componentCtx")) {
            if (ImGui::MenuItem("Remove Component")) {
                toRemove = &entry;
            }
            ImGui::EndPopup();
        }
        if (open) {
            if (entry.name == "MaterialComponent") {
                // Needs the AssetManager (shader + reflected uniforms), which the type-erased
                // registry hook cannot reach, so it is drawn directly with the editor context.
                drawMaterialInspector(m_context, e);
            } else if (entry.drawInspector != nullptr) {
                entry.drawInspector(scene, e);
            } else {
                ImGui::TextDisabled("(no inspector for this component)");
            }
        }
        ImGui::PopID();
    }

    if (toRemove != nullptr) {
        toRemove->remove(scene, e);
    }
}

} // namespace rb::editor
