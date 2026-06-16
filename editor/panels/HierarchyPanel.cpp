#include "editor/panels/HierarchyPanel.h"

#include "editor/EditorContext.h"

#include "rabbet/assets/AssetDatabase.h"
#include "rabbet/assets/AssetManager.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/particle/ParticleEmitter.h"
#include "rabbet/render/Primitive.h"
#include "rabbet/scene/Light.h"
#include "rabbet/scene/Name.h"
#include "rabbet/scene/Transform.h"
#include "rabbet/serialize/Prefab.h"
#include "rabbet/serialize/SceneSerializer.h"
#include "rabbet/util/Log.h"

#include <glm/glm.hpp>
#include <imgui.h>

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace rb::editor {
namespace {

rb::Entity createEmpty(rb::Scene& scene, const char* name) {
    const rb::Entity e = scene.create();
    scene.add<rb::Name>(e, rb::Name{name});
    scene.add<rb::Transform>(e, rb::Transform{});
    return e;
}

rb::Entity createPrimitive(rb::Scene& scene, const char* name, rb::PrimitiveShape shape,
                           float roughness) {
    const rb::Entity e = createEmpty(scene, name);
    scene.add<rb::Primitive>(e, rb::Primitive{shape, glm::vec3(0.8f), 0.0f, roughness});
    return e;
}

std::string displayName(rb::Scene& scene, rb::Entity e) {
    if (rb::Name* n = scene.tryGet<rb::Name>(e); n != nullptr && !n->value.empty()) {
        return n->value;
    }
    return "Entity " + std::to_string(e.index());
}

// Saves the entity as a reusable prefab under <assets>/prefabs/ and re-catalogues the database so
// it appears in the Assets panel, ready to instantiate.
void createPrefab(EditorContext& context, rb::Entity e) {
    rb::Scene& scene = context.runtime.scene();
    rb::AssetDatabase* database = context.runtime.tryResource<rb::AssetDatabase>();
    rb::AssetManager* assets = context.runtime.tryResource<rb::AssetManager>();
    if (database == nullptr || assets == nullptr || !scene.alive(e)) {
        return;
    }
    const std::filesystem::path dir = database->root() / "prefabs";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    // Sanitized + de-collided so a prefab never silently overwrites another.
    const std::filesystem::path path = rb::prefabFilePath(dir, displayName(scene, e));
    if (rb::savePrefabToFile(scene, context.registry, e, path)) {
        database->scan(database->root(), assets);
        rb::log::info("prefab: saved '{}'", path.string());
    } else {
        rb::log::error("prefab: failed to save prefab '{}'", path.string());
    }
}

} // namespace

void HierarchyPanel::onImGui() {
    rb::Scene& scene = m_context.runtime.scene();

    rb::Entity toSelect{};
    rb::Entity toDuplicate{};
    rb::Entity toDelete{};

    if (ImGui::Button("Create")) {
        ImGui::OpenPopup("##create");
    }
    if (ImGui::BeginPopup("##create")) {
        if (ImGui::MenuItem("Empty")) {
            toSelect = createEmpty(scene, "Empty");
        }
        if (ImGui::MenuItem("Cube")) {
            toSelect = createPrimitive(scene, "Cube", rb::PrimitiveShape::Cube, 0.5f);
        }
        if (ImGui::MenuItem("Sphere")) {
            toSelect = createPrimitive(scene, "Sphere", rb::PrimitiveShape::Sphere, 0.4f);
        }
        if (ImGui::MenuItem("Plane")) {
            toSelect = createPrimitive(scene, "Plane", rb::PrimitiveShape::Plane, 0.9f);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Directional Light")) {
            const rb::Entity e = createEmpty(scene, "Directional Light");
            scene.add<rb::DirectionalLight>(e, rb::DirectionalLight{});
            toSelect = e;
        }
        if (ImGui::MenuItem("Point Light")) {
            const rb::Entity e = createEmpty(scene, "Point Light");
            scene.add<rb::PointLight>(e, rb::PointLight{});
            toSelect = e;
        }
        if (ImGui::MenuItem("Spot Light")) {
            const rb::Entity e = createEmpty(scene, "Spot Light");
            scene.add<rb::SpotLight>(e, rb::SpotLight{});
            toSelect = e;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Particle Emitter")) {
            const rb::Entity e = createEmpty(scene, "Particle Emitter");
            scene.add<rb::ParticleEmitter>(e, rb::ParticleEmitter{});
            toSelect = e;
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    const bool hasSelection = scene.alive(m_context.selected);
    ImGui::BeginDisabled(!hasSelection);
    if (ImGui::Button("Duplicate")) {
        toDuplicate = m_context.selected;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) {
        toDelete = m_context.selected;
    }
    ImGui::SameLine();
    if (ImGui::Button("Create Prefab")) {
        createPrefab(m_context, m_context.selected);
    }
    ImGui::EndDisabled();
    ImGui::Separator();

    const std::vector<rb::Entity> entities = scene.entities();
    for (const rb::Entity e : entities) {
        const std::string label = displayName(scene, e) + "##" + std::to_string(e.index());
        if (ImGui::Selectable(label.c_str(), e == m_context.selected)) {
            toSelect = e;
        }
        if (ImGui::BeginPopupContextItem(label.c_str())) {
            if (ImGui::MenuItem("Duplicate")) {
                toDuplicate = e;
            }
            if (ImGui::MenuItem("Delete")) {
                toDelete = e;
            }
            ImGui::EndPopup();
        }
    }

    // Apply structural changes after iterating the entity snapshot.
    if (toDuplicate.valid() && scene.alive(toDuplicate)) {
        m_context.selected = rb::SceneSerializer::duplicateEntity(scene, m_context.registry, toDuplicate);
    } else if (toDelete.valid() && scene.alive(toDelete)) {
        if (m_context.selected == toDelete) {
            m_context.selected = rb::Entity{};
        }
        scene.destroy(toDelete);
    } else if (toSelect.valid()) {
        m_context.selected = toSelect;
    }
}

} // namespace rb::editor
