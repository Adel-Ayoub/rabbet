#include "editor/panels/InspectorPanel.h"

#include "editor/ComponentDrawers.h"
#include "editor/EditorContext.h"
#include "editor/Icons.h"
#include "editor/Palette.h"

#include "rabbet/core/Runtime.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/serialize/ComponentRegistry.h"

#include <imgui.h>

#include <algorithm>
#include <string>

namespace rb::editor {
namespace {

const char* componentGlyph(const std::string& name) {
    if (name == "Name") {
        return icon::kTag;
    }
    if (name == "Transform") {
        return icon::kAxis3d;
    }
    if (name == "Camera") {
        return icon::kVideo;
    }
    if (name == "DirectionalLight") {
        return icon::kSun;
    }
    if (name == "PointLight") {
        return icon::kLightbulb;
    }
    if (name == "SpotLight") {
        return icon::kFlashlight;
    }
    if (name == "Primitive") {
        return icon::kBox;
    }
    if (name == "ModelRenderer") {
        return icon::kBoxes;
    }
    if (name == "MaterialComponent") {
        return icon::kLayers;
    }
    if (name == "ScriptComponent") {
        return icon::kFileCode;
    }
    if (name == "RigidBody") {
        return icon::kAtom;
    }
    if (name == "BoxCollider") {
        return icon::kSquare;
    }
    if (name == "SphereCollider") {
        return icon::kCircleDot;
    }
    if (name == "SoundEmitter") {
        return icon::kVolume;
    }
    if (name == "ParticleEmitter") {
        return icon::kSparkles;
    }
    if (name == "PostProcess") {
        return icon::kWandSparkles;
    }
    if (name == "TerrainComponent") {
        return icon::kMountain;
    }
    if (name == "SkyboxComponent") {
        return icon::kSun;
    }
    if (name == "PrefabInstance") {
        return icon::kLink;
    }
    return icon::kSlidersHorizontal;
}

} // namespace

void InspectorPanel::onImGui() {
    rb::Scene& scene = m_context.runtime.scene();
    if (!scene.alive(m_context.selected)) {
        ImGui::Dummy(ImVec2(0.0f, 10.0f));
        const char* hint = "Select an entity to inspect";
        ImGui::SetCursorPosX(
            std::max(0.0f, (ImGui::GetWindowSize().x - ImGui::CalcTextSize(hint).x) * 0.5f));
        ImGui::TextDisabled("%s", hint);
        return;
    }
    const rb::Entity e = m_context.selected;
    const rb::ComponentRegistry& registry = m_context.registry;

    ImGui::TextDisabled("Entity %u", e.index());
    ImGui::Separator();

    // Draw every component the entity has, in registration order. Removal is deferred
    // until after the loop so we never mutate the component pools mid-iteration.
    const rb::ComponentRegistry::Entry* toRemove = nullptr;
    for (const rb::ComponentRegistry::Entry& entry : registry.entries()) {
        if (!entry.has(scene, e)) {
            continue;
        }
        ImGui::PushID(entry.name.c_str());
        const std::string header =
            std::string(componentGlyph(entry.name)) + "  " + entry.name + "##header";
        ImGui::PushFont(fonts().heading);
        const bool open = ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::PopFont();
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
            } else if (entry.name == "PrefabInstance") {
                drawPrefabInspector(m_context, e);
            } else if (entry.name == "TerrainComponent") {
                // Needs the AssetDatabase to assign heightmap / layer / splat textures, which the
                // type-erased registry hook cannot reach.
                drawTerrainInspector(m_context, e);
            } else if (entry.name == "SkyboxComponent") {
                // Same reason as terrain: assigning the six faces needs the AssetDatabase.
                drawSkyboxInspector(m_context, e);
            } else if (entry.drawInspector != nullptr) {
                entry.drawInspector(scene, e);
            } else {
                ImGui::TextDisabled("(no inspector for this component)");
            }
            ImGui::Spacing();
        }
        ImGui::PopID();
    }

    if (toRemove != nullptr) {
        toRemove->remove(scene, e);
    }

    // The add menu lives at the bottom of the component stack, like most engines.
    ImGui::Spacing();
    const float buttonWidth = std::min(220.0f, ImGui::GetContentRegionAvail().x * 0.7f);
    ImGui::SetCursorPosX(std::max(0.0f, (ImGui::GetWindowSize().x - buttonWidth) * 0.5f));
    if (ImGui::Button((std::string(icon::kPlus) + "  Add Component").c_str(),
                      ImVec2(buttonWidth, 0.0f))) {
        ImGui::OpenPopup("##addComponent");
    }
    if (ImGui::BeginPopup("##addComponent")) {
        bool any = false;
        for (const rb::ComponentRegistry::Entry& entry : registry.entries()) {
            if (entry.has(scene, e)) {
                continue;
            }
            // The prefab link is instantiation metadata, not an authorable component; a
            // hand-added empty link only offers a Revert that eats the entity's contents.
            if (entry.name == "PrefabInstance") {
                continue;
            }
            any = true;
            const std::string item = std::string(componentGlyph(entry.name)) + "  " + entry.name;
            if (ImGui::MenuItem(item.c_str())) {
                entry.addDefault(scene, e);
            }
        }
        if (!any) {
            ImGui::TextDisabled("All components added");
        }
        ImGui::EndPopup();
    }
}

} // namespace rb::editor
