#include "editor/panels/HierarchyPanel.h"

#include "editor/AssetAssign.h"
#include "editor/EditorContext.h"

#include "rabbet/assets/AssetDatabase.h"
#include "rabbet/assets/AssetManager.h"
#include "rabbet/assets/AssetType.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/particle/ParticleEmitter.h"
#include "rabbet/render/PostProcess.h"
#include "rabbet/render/Primitive.h"
#include "rabbet/scene/Camera.h"
#include "rabbet/scene/Hierarchy.h"
#include "rabbet/scene/Light.h"
#include "rabbet/scene/Name.h"
#include "rabbet/scene/Parent.h"
#include "rabbet/scene/Transform.h"
#include "rabbet/serialize/Prefab.h"
#include "rabbet/terrain/TerrainComponent.h"
#include "rabbet/serialize/SceneSerializer.h"
#include "rabbet/util/Log.h"

#include <glm/glm.hpp>
#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <unordered_map>
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

constexpr const char* kEntityDragType = "RB_ENTITY";

// What one frame's tree interaction asked for; applied after the draw pass so the scene
// is never mutated while the tree is iterating it.
struct TreeActions {
    rb::Entity select{};
    rb::Entity duplicate{};
    rb::Entity destroy{};
    rb::Entity reparentChild{};
    rb::Entity reparentParent{}; // null = drop to root (un-parent)
    bool reparent = false;
};

using ChildIndex = std::unordered_map<rb::Entity, std::vector<rb::Entity>>;

void drawEntityNode(EditorContext& context, rb::Scene& scene, rb::AssetDatabase* database,
                    const ChildIndex& childIndex, rb::Entity e, TreeActions& actions) {
    static const std::vector<rb::Entity> kNoChildren;
    const auto childIt = childIndex.find(e);
    const std::vector<rb::Entity>& children =
        childIt != childIndex.end() ? childIt->second : kNoChildren;
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
    if (e == context.selected) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (children.empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }
    const std::string label = displayName(scene, e) + "##" + std::to_string(e.index());
    const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) {
        actions.select = e;
    }
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Duplicate")) {
            actions.duplicate = e;
        }
        if (ImGui::MenuItem("Delete")) {
            actions.destroy = e;
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload(kEntityDragType, &e, sizeof(rb::Entity));
        ImGui::TextUnformatted(displayName(scene, e).c_str());
        ImGui::EndDragDropSource();
    }
    // Drop another row here to re-parent it under this entity.
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kEntityDragType)) {
            if (payload->Data != nullptr &&
                payload->DataSize == static_cast<int>(sizeof(rb::Entity))) {
                actions.reparent = true;
                actions.reparentChild = *static_cast<const rb::Entity*>(payload->Data);
                actions.reparentParent = e;
            }
        }
        ImGui::EndDragDropTarget();
    }
    // Drop an asset from the Assets browser onto a row: a prefab instantiates, anything
    // else is assigned to this entity by its type.
    if (const AssetDragPayload dropped = acceptAssetDropTarget();
        dropped.valid() && database != nullptr) {
        if (const rb::AssetDatabase::Record* record = database->find(dropped.id)) {
            if (record->type == rb::AssetType::Prefab) {
                instantiatePrefabAsset(context, *record);
            } else {
                assignAssetToEntity(context, *record, e);
                actions.select = e;
            }
        }
    }
    if (open) {
        for (const rb::Entity child : children) {
            drawEntityNode(context, scene, database, childIndex, child, actions);
        }
        ImGui::TreePop();
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
        if (ImGui::MenuItem("Camera")) {
            const rb::Entity e = createEmpty(scene, "Camera");
            scene.add<rb::Camera>(e, rb::Camera{});
            toSelect = e;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Particle Emitter")) {
            const rb::Entity e = createEmpty(scene, "Particle Emitter");
            scene.add<rb::ParticleEmitter>(e, rb::ParticleEmitter{});
            toSelect = e;
        }
        if (ImGui::MenuItem("Post Process")) {
            const rb::Entity e = createEmpty(scene, "Post Process");
            rb::PostProcess pp;
            pp.enabled = true; // created on -> the HDR pipeline engages immediately
            scene.add<rb::PostProcess>(e, pp);
            toSelect = e;
        }
        if (ImGui::MenuItem("Terrain")) {
            const rb::Entity e = createEmpty(scene, "Terrain");
            rb::TerrainComponent terrain;
            terrain.size = 48.0f;
            terrain.resolution = 96;
            terrain.heightScale = 6.0f;
            // Two slope-banded layers so the height/slope auto-blend is visible the moment albedos
            // are assigned (flats vs. slopes); both fall back to grey until then.
            terrain.layerCount = 2;
            terrain.layers[0].slopeRange = {0.0f, 0.35f};
            terrain.layers[0].tiling = 18.0f;
            terrain.layers[1].slopeRange = {0.25f, 1.0f};
            terrain.layers[1].tiling = 12.0f;
            scene.add<rb::TerrainComponent>(e, terrain);
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

    TreeActions actions;
    actions.select = toSelect;
    actions.duplicate = toDuplicate;
    actions.destroy = toDelete;

    rb::AssetDatabase* database = m_context.runtime.tryResource<rb::AssetDatabase>();
    // One Parent-pool pass builds the whole child index; a per-node childrenOf scan made
    // the tree O(entities x links) per frame, which is real money at spawn-heavy counts.
    ChildIndex childIndex;
    scene.each<rb::Parent>([&scene, &childIndex](rb::Entity child, rb::Parent& link) {
        if (scene.alive(link.entity)) {
            childIndex[link.entity].push_back(child);
        }
    });
    const std::vector<rb::Entity> entities = scene.entities();
    for (const rb::Entity e : entities) {
        if (rb::parentOf(scene, e) == rb::kNullEntity) {
            drawEntityNode(m_context, scene, database, childIndex, e, actions);
        }
    }

    // The space under the tree un-parents: drop a row there to make it a root again.
    ImVec2 rootZone = ImGui::GetContentRegionAvail();
    rootZone.x = std::max(rootZone.x, 1.0f);
    rootZone.y = std::max(rootZone.y, 24.0f);
    ImGui::Dummy(rootZone);
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kEntityDragType)) {
            if (payload->Data != nullptr &&
                payload->DataSize == static_cast<int>(sizeof(rb::Entity))) {
                actions.reparent = true;
                actions.reparentChild = *static_cast<const rb::Entity*>(payload->Data);
                actions.reparentParent = rb::Entity{};
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Apply structural changes after iterating the entity snapshot.
    if (actions.reparent && scene.alive(actions.reparentChild)) {
        rb::setParentKeepingWorldPose(scene, actions.reparentChild, actions.reparentParent);
    } else if (actions.duplicate.valid() && scene.alive(actions.duplicate)) {
        m_context.selected =
            rb::SceneSerializer::duplicateSubtree(scene, m_context.registry, actions.duplicate);
    } else if (actions.destroy.valid() && scene.alive(actions.destroy)) {
        if (m_context.selected == actions.destroy ||
            rb::isAncestor(scene, actions.destroy, m_context.selected)) {
            m_context.selected = rb::Entity{}; // the selection dies with the subtree
        }
        rb::destroySubtree(scene, actions.destroy);
    } else if (actions.select.valid()) {
        m_context.selected = actions.select;
    }
}

} // namespace rb::editor
