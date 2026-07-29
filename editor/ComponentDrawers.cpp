#include "editor/ComponentDrawers.h"

#include "editor/AssetAssign.h"
#include "editor/EditorContext.h"
#include "editor/Icons.h"
#include "editor/Widgets.h"

#include "rabbet/assets/AssetDatabase.h"
#include "rabbet/assets/AssetManager.h"
#include "rabbet/assets/AssetType.h"
#include "rabbet/audio/SoundEmitter.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/core/Uuid.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/particle/ParticleEmitter.h"
#include "rabbet/render/PostProcess.h"
#include "rabbet/render/SkyboxComponent.h"
#include "rabbet/render/WaterComponent.h"
#include "rabbet/physics/BoxCollider.h"
#include "rabbet/physics/RigidBody.h"
#include "rabbet/physics/SphereCollider.h"
#include "rabbet/render/MaterialAsset.h"
#include "rabbet/render/MaterialComponent.h"
#include "rabbet/render/MaterialImport.h"
#include "rabbet/render/ModelRenderer.h"
#include "rabbet/render/Primitive.h"
#include "rabbet/render/ShaderAsset.h"
#include "rabbet/render/ShaderUniform.h"
#include "rabbet/scene/Camera.h"
#include "rabbet/scene/Light.h"
#include "rabbet/scene/Name.h"
#include "rabbet/scene/Transform.h"
#include "rabbet/scripting/ScriptComponent.h"
#include "rabbet/scripting/ScriptField.h"
#include "rabbet/serialize/ComponentRegistry.h"
#include "rabbet/terrain/TerrainComponent.h"
#include "rabbet/util/Log.h"
#include "rabbet/serialize/Prefab.h"
#include "rabbet/serialize/PrefabAsset.h"
#include "rabbet/serialize/PrefabInstance.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>
#include <imgui.h>
#include <magic_enum/magic_enum.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <type_traits>
#include <vector>

namespace rb::editor {
namespace {

// One row-aligned dropdown for any enum: the labels come straight from magic_enum,
// so adding an enumerator needs no editor change.
template <typename T>
void enumCombo(const char* label, T& value) {
    static_assert(std::is_enum_v<T>, "enumCombo requires an enum type");
    if (ui::beginCombo(label, magic_enum::enum_name(value).data())) {
        for (const T option : magic_enum::enum_values<T>()) {
            const bool selected = option == value;
            if (ImGui::Selectable(magic_enum::enum_name(option).data(), selected)) {
                value = option;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
}

void drawName(rb::Scene& scene, rb::Entity e) {
    rb::Name& n = scene.get<rb::Name>(e);
    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), "%s", n.value.c_str());
    if (ui::inputText("Name", buffer, sizeof(buffer))) {
        n.value = buffer;
    }
}

void drawTransform(rb::Scene& scene, rb::Entity e) {
    rb::Transform& t = scene.get<rb::Transform>(e);
    ui::dragFloat3("Position", &t.position.x, 0.05f);
    glm::vec3 euler = glm::degrees(glm::eulerAngles(t.rotation));
    if (ui::dragFloat3("Rotation", &euler.x, 0.5f)) {
        t.rotation = glm::quat(glm::radians(euler));
    }
    ui::dragFloat3("Scale", &t.scale.x, 0.05f, 0.01f, 100.0f);
}

void drawCamera(rb::Scene& scene, rb::Entity e) {
    rb::Camera& c = scene.get<rb::Camera>(e);
    float fovDeg = glm::degrees(c.fovY);
    if (ui::dragFloat("FOV (Y)", &fovDeg, 0.5f, 10.0f, 170.0f)) {
        c.fovY = glm::radians(fovDeg);
    }
    ui::dragFloat("Near", &c.nearPlane, 0.01f, 0.001f, c.farPlane);
    ui::dragFloat("Far", &c.farPlane, 0.5f, c.nearPlane, 10000.0f);
}

void drawDirectionalLight(rb::Scene& scene, rb::Entity e) {
    rb::DirectionalLight& l = scene.get<rb::DirectionalLight>(e);
    ui::colorEdit3("Color", &l.color.x);
    ui::dragFloat("Intensity", &l.intensity, 0.05f, 0.0f, 20.0f);
    ui::dragFloat3("Direction", &l.direction.x, 0.02f);
}

void drawPointLight(rb::Scene& scene, rb::Entity e) {
    rb::PointLight& l = scene.get<rb::PointLight>(e);
    ui::colorEdit3("Color", &l.color.x);
    ui::dragFloat("Intensity", &l.intensity, 0.05f, 0.0f, 50.0f);
    ui::dragFloat("Constant", &l.constant, 0.01f, 0.0f, 5.0f);
    ui::dragFloat("Linear", &l.linear, 0.001f, 0.0f, 2.0f);
    ui::dragFloat("Quadratic", &l.quadratic, 0.001f, 0.0f, 2.0f);
}

void drawSpotLight(rb::Scene& scene, rb::Entity e) {
    rb::SpotLight& l = scene.get<rb::SpotLight>(e);
    ui::colorEdit3("Color", &l.color.x);
    ui::dragFloat("Intensity", &l.intensity, 0.05f, 0.0f, 50.0f);
    ui::dragFloat3("Direction", &l.direction.x, 0.02f);
    ui::dragFloat("Inner Angle", &l.innerAngle, 0.5f, 0.0f, l.outerAngle);
    ui::dragFloat("Outer Angle", &l.outerAngle, 0.5f, 0.0f, 89.9f);
    ui::dragFloat("Constant", &l.constant, 0.01f, 0.0f, 5.0f);
    ui::dragFloat("Linear", &l.linear, 0.001f, 0.0f, 2.0f);
    ui::dragFloat("Quadratic", &l.quadratic, 0.001f, 0.0f, 2.0f);
}

void drawPrimitive(rb::Scene& scene, rb::Entity e) {
    rb::Primitive& p = scene.get<rb::Primitive>(e);
    enumCombo("Shape", p.shape);
    ui::colorEdit3("Color", &p.color.x);
    ui::colorEdit3("Emissive", &p.emissive.x,
                   ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
    ui::dragFloat("Metallic", &p.metallic, 0.01f, 0.0f, 1.0f);
    ui::dragFloat("Roughness", &p.roughness, 0.01f, 0.0f, 1.0f);
}

void drawModelRenderer(rb::Scene& scene, rb::Entity e) {
    rb::ModelRenderer& r = scene.get<rb::ModelRenderer>(e);
    const ui::SlotResult slot =
        ui::assetSlot("Model", rb::AssetType::Model, r.model, r.handle.valid());
    if (slot.dropped.valid()) {
        r.model = slot.dropped.id;
        r.handle = {}; // AssetResolveSystem repopulates it from the uuid
    } else if (slot.cleared) {
        r.model = rb::Uuid{};
        r.handle = {};
    }
}

// The .lua reference stays panel-assigned (dropping here could not introspect the
// script's fields, which needs the asset database); the slot still shows the link.
void drawScript(rb::Scene& scene, rb::Entity e) {
    rb::ScriptComponent& s = scene.get<rb::ScriptComponent>(e);
    const ui::SlotResult slot =
        ui::assetSlot("Script", rb::AssetType::Script, s.script, s.handle.valid(), false);
    if (slot.cleared) {
        s.script = rb::Uuid{};
        s.handle = {};
        s.fields.clear();
        return;
    }
    if (!s.script.valid()) {
        return;
    }
    if (s.fields.empty()) {
        ImGui::TextDisabled("(no exposed fields)");
        return;
    }
    ImGui::SeparatorText("Fields");
    for (rb::ScriptField& f : s.fields) {
        switch (f.type) {
        case rb::ScriptField::Type::Number: {
            float value = static_cast<float>(f.number);
            if (ui::dragFloat(f.name.c_str(), &value, 0.05f)) {
                f.number = static_cast<double>(value);
            }
            break;
        }
        case rb::ScriptField::Type::Boolean:
            ui::checkbox(f.name.c_str(), &f.boolean);
            break;
        case rb::ScriptField::Type::String: {
            char buffer[256];
            std::snprintf(buffer, sizeof(buffer), "%s", f.text.c_str());
            if (ui::inputText(f.name.c_str(), buffer, sizeof(buffer))) {
                f.text = buffer;
            }
            break;
        }
        }
    }
}

void drawRigidBody(rb::Scene& scene, rb::Entity e) {
    rb::RigidBody& b = scene.get<rb::RigidBody>(e);
    enumCombo("Type", b.type);
    ui::dragFloat("Mass", &b.mass, 0.05f, 0.0f, 1000.0f);
    ui::dragFloat("Friction", &b.friction, 0.01f, 0.0f, 2.0f);
    ui::dragFloat("Restitution", &b.restitution, 0.01f, 0.0f, 1.0f);
    ui::checkbox("Gravity", &b.gravity);
}

void drawBoxCollider(rb::Scene& scene, rb::Entity e) {
    rb::BoxCollider& c = scene.get<rb::BoxCollider>(e);
    ui::dragFloat3("Half Extents", &c.halfExtents.x, 0.05f, 0.01f, 1000.0f);
    ui::dragFloat3("Offset", &c.offset.x, 0.05f);
}

void drawSphereCollider(rb::Scene& scene, rb::Entity e) {
    rb::SphereCollider& c = scene.get<rb::SphereCollider>(e);
    ui::dragFloat("Radius", &c.radius, 0.05f, 0.01f, 1000.0f);
    ui::dragFloat3("Offset", &c.offset.x, 0.05f);
}

void drawSoundEmitter(rb::Scene& scene, rb::Entity e) {
    rb::SoundEmitter& s = scene.get<rb::SoundEmitter>(e);
    const ui::SlotResult slot =
        ui::assetSlot("Clip", rb::AssetType::Audio, s.sound, s.handle.valid());
    if (slot.dropped.valid()) {
        s.sound = slot.dropped.id;
        s.handle = {}; // AudioAssetResolveSystem lazily imports + resolves
    } else if (slot.cleared) {
        s.sound = rb::Uuid{};
        s.handle = {};
    }
    ui::dragFloat("Volume", &s.volume, 0.01f, 0.0f, 2.0f);
    ui::dragFloat("Pitch", &s.pitch, 0.01f, 0.1f, 4.0f);
    ui::checkbox("Loop", &s.loop);
    ui::checkbox("Spatial", &s.spatial);
    ui::checkbox("Play on start", &s.playOnStart);
    ui::checkbox("Stream", &s.stream);
    ImGui::SetItemTooltip("Stream from disk instead of decoding up front (for long music tracks).");
}

void drawParticleEmitter(rb::Scene& scene, rb::Entity e) {
    rb::ParticleEmitter& p = scene.get<rb::ParticleEmitter>(e);

    ui::dragFloat("Emission Rate", &p.emissionRate, 0.5f, 0.0f, 10000.0f);
    ui::dragInt("Max Particles", &p.maxParticles, 1.0f, 0, 100000);
    enumCombo("Blend Mode", p.blendMode);
    ui::checkbox("Looping", &p.looping);
    if (!p.looping) {
        ui::dragFloat("Duration", &p.duration, 0.1f, 0.0f, 600.0f);
    }

    ImGui::SeparatorText("Life");
    ui::dragFloat("Lifetime", &p.lifetime, 0.05f, 0.01f, 600.0f);
    ui::dragFloat("Lifetime Jitter", &p.lifetimeJitter, 0.05f, 0.0f, 600.0f);

    ImGui::SeparatorText("Shape");
    enumCombo("Emission Shape", p.shape);
    if (p.shape == rb::ParticleEmissionShape::Sphere ||
        p.shape == rb::ParticleEmissionShape::Ring) {
        ui::dragFloat("Radius", &p.shapeRadius, 0.05f, 0.0f, 100.0f);
        ui::dragFloat("Thickness", &p.shapeThickness, 0.01f, 0.0f, 1.0f);
        ImGui::SetItemTooltip("Emissive fraction of the radius: 1 fills the volume, 0 is the "
                              "surface only.");
    } else if (p.shape == rb::ParticleEmissionShape::Box) {
        ui::dragFloat3("Extents", &p.shapeExtents.x, 0.05f, 0.0f, 100.0f);
    }
    if (p.shape != rb::ParticleEmissionShape::Point) {
        ui::checkbox("Emit Outward", &p.emitOutward);
        ImGui::SetItemTooltip("Births fly away from the centre at Velocity's speed instead of "
                              "along its direction.");
    }

    ImGui::SeparatorText("Motion");
    ui::dragFloat3("Velocity", &p.velocity.x, 0.05f);
    ui::dragFloat("Cone Angle", &p.coneAngle, 0.5f, 0.0f, 89.9f);
    ui::dragFloat("Speed Jitter", &p.speedJitter, 0.01f, 0.0f, 1.0f);
    ui::dragFloat3("Gravity", &p.gravity.x, 0.05f);

    ImGui::SeparatorText("Appearance");
    ui::dragFloat("Start Size", &p.startSize, 0.01f, 0.0f, 100.0f);
    ui::dragFloat("End Size", &p.endSize, 0.01f, 0.0f, 100.0f);
    constexpr ImGuiColorEditFlags colorFlags =
        ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_AlphaPreviewHalf;
    ui::colorEdit4("Start Color", &p.startColor.x, colorFlags);
    ui::colorEdit4("End Color", &p.endColor.x, colorFlags);

    int seed = static_cast<int>(p.seed);
    if (ui::dragInt("Seed", &seed, 1.0f, 0, 1000000)) {
        p.seed = static_cast<std::uint32_t>(seed < 0 ? 0 : seed);
    }

    ImGui::SeparatorText("Sprite");
    const ui::SlotResult slot =
        ui::assetSlot("Sprite", rb::AssetType::Texture, p.sprite, p.handle.valid());
    if (slot.dropped.valid()) {
        p.sprite = slot.dropped.id;
        p.handle = {}; // AssetResolveSystem imports + resolves it
    } else if (slot.cleared) {
        p.sprite = rb::Uuid{};
        p.handle = {};
    }
}

void drawWater(rb::Scene& scene, rb::Entity e) {
    rb::WaterComponent& w = scene.get<rb::WaterComponent>(e);
    ui::checkbox("Enabled", &w.enabled);
    ui::dragFloat2("Extent", &w.extent.x, 0.25f, 0.01f, 5000.0f);
    ImGui::SetItemTooltip("Half-size of the surface; the entity's position sets the water level.");

    ImGui::SeparatorText("Waves");
    ui::dragFloat("Tile Scale", &w.waveTileScale, 0.01f, 0.0f, 10.0f);
    ui::dragFloat("Strength", &w.waveStrength, 0.01f, 0.0f, 4.0f);
    ui::dragFloat("Speed", &w.waveSpeed, 0.01f, 0.0f, 10.0f);
    ui::dragFloat("Smoothness", &w.smoothness, 0.01f, 0.0f, 1.0f);

    ImGui::SeparatorText("Colour");
    constexpr ImGuiColorEditFlags waterFlags =
        ImGuiColorEditFlags_Float | ImGuiColorEditFlags_AlphaPreviewHalf;
    ui::colorEdit4("Deep", &w.deepColor.x, waterFlags);
    ui::colorEdit4("Shallow", &w.shallowColor.x, waterFlags);
}

// Post-processing settings for the scene (a single enabled instance drives the editor's view).
void drawPostProcess(rb::Scene& scene, rb::Entity e) {
    rb::PostProcess& p = scene.get<rb::PostProcess>(e);
    ui::checkbox("Enabled", &p.enabled);

    ImGui::SeparatorText("Tonemap");
    enumCombo("Operator", p.tonemap);
    ui::dragFloat("Exposure (EV)", &p.exposure, 0.05f, -8.0f, 8.0f);
    ui::dragFloat("Gamma", &p.gamma, 0.01f, 1.0f, 3.0f);

    ImGui::SeparatorText("Bloom");
    ui::checkbox("Bloom", &p.bloom);
    ui::dragFloat("Threshold", &p.bloomThreshold, 0.02f, 0.0f, 20.0f);
    ui::dragFloat("Knee", &p.bloomKnee, 0.01f, 0.0f, 1.0f);
    ui::dragFloat("Intensity", &p.bloomIntensity, 0.005f, 0.0f, 2.0f);

    ImGui::SeparatorText("Grade");
    ui::dragFloat("Contrast", &p.contrast, 0.01f, 0.0f, 2.0f);
    ui::dragFloat("Saturation", &p.saturation, 0.01f, 0.0f, 2.0f);
    ui::dragFloat("Vignette", &p.vignette, 0.01f, 0.0f, 1.0f);

    ImGui::SeparatorText("Anti-aliasing");
    ui::checkbox("FXAA", &p.fxaa);
}

bool isColorName(const std::string& name) {
    return name.find("olor") != std::string::npos || name.find("int") != std::string::npos ||
           name.find("lbedo") != std::string::npos || name.find("missive") != std::string::npos;
}

// One row per shader-reflected uniform. A material starts with no overrides (so it renders like
// the built-in defaults); pressing "Override" adds one, which then drives the shader on top of
// the per-surface values. Samplers are bound via texture slots, not edited here. Returns true
// when it changed the material (so the caller can mark it dirty).
bool drawUniformOverride(rb::MaterialAsset& material, const rb::ShaderUniform& uniform) {
    ImGui::PushID(uniform.name.c_str());
    if (rb::isSamplerType(uniform.type)) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s  (sampler)", uniform.name.c_str());
        ImGui::PopID();
        return false;
    }

    rb::MaterialUniform* existing = nullptr;
    for (rb::MaterialUniform& value : material.uniforms) {
        if (value.name == uniform.name) {
            existing = &value;
            break;
        }
    }

    if (existing == nullptr) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s  (%s)", uniform.name.c_str(),
                            std::string(rb::uniformTypeName(uniform.type)).c_str());
        ImGui::SameLine(ui::labelColumnWidth());
        bool added = false;
        if (ImGui::SmallButton("Override")) {
            rb::MaterialUniform created;
            created.name = uniform.name;
            created.type = uniform.type;
            created.vec = glm::vec4(1.0f); // neutral starting point (white / 1.0)
            material.uniforms.push_back(created);
            added = true;
        }
        ImGui::PopID();
        return added;
    }

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(uniform.name.c_str());
    ImGui::SameLine(ui::labelColumnWidth());
    const float removeWidth = 22.0f + ImGui::GetStyle().ItemInnerSpacing.x;
    ImGui::SetNextItemWidth(std::max(60.0f, ImGui::GetContentRegionAvail().x - removeWidth));

    bool changed = false;
    switch (uniform.type) {
    case rb::UniformType::Float:
        changed = ImGui::DragFloat("##value", &existing->vec.x, 0.01f);
        break;
    case rb::UniformType::Vec2:
        changed = ImGui::DragFloat2("##value", &existing->vec.x, 0.01f);
        break;
    case rb::UniformType::Vec3:
        changed = isColorName(uniform.name) ? ImGui::ColorEdit3("##value", &existing->vec.x)
                                            : ImGui::DragFloat3("##value", &existing->vec.x, 0.01f);
        break;
    case rb::UniformType::Vec4:
        changed = ImGui::ColorEdit4("##value", &existing->vec.x);
        break;
    case rb::UniformType::Int:
        changed = ImGui::DragInt("##value", &existing->integer);
        break;
    case rb::UniformType::Bool:
        changed = ImGui::Checkbox("##value", &existing->boolean);
        break;
    default:
        break;
    }
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    if (ImGui::Button(icon::kX, ImVec2(22.0f, 0.0f))) {
        const std::string name = uniform.name;
        std::erase_if(material.uniforms,
                      [&name](const rb::MaterialUniform& value) { return value.name == name; });
        changed = true;
    }
    ImGui::SetItemTooltip("Remove override");
    ImGui::PopID();
    return changed;
}

// A row-aligned combo over the catalogued Texture assets (plus "(none)") that writes the chosen
// uuid. This is how terrain assigns its heightmap / layer albedos / splat: multi-slot assignment
// is clearer inline than routing a single Assets-panel action to a hidden "active slot".
bool textureSlot(const char* label, rb::AssetDatabase& database, rb::Uuid& target) {
    std::string current = "(none)";
    if (target.valid()) {
        if (const rb::AssetDatabase::Record* record = database.find(target)) {
            current = record->name;
        } else {
            current = target.toString().substr(0, 8); // assigned but not catalogued
        }
    }
    bool changed = false;
    if (ui::beginCombo(label, current.c_str())) {
        if (ImGui::Selectable("(none)", !target.valid())) {
            target = rb::Uuid{};
            changed = true;
        }
        for (const rb::AssetDatabase::Record& record : database.records()) {
            if (record.type != rb::AssetType::Texture) {
                continue;
            }
            const bool selected = record.id == target;
            const std::string item = record.name + "##" + record.id.toString();
            if (ImGui::Selectable(item.c_str(), selected)) {
                target = record.id;
                changed = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    // Drop a Texture asset from the browser straight onto the slot.
    if (const AssetDragPayload dropped = acceptAssetDropTarget();
        dropped.valid() && dropped.type == rb::AssetType::Texture) {
        target = dropped.id;
        changed = true;
    }
    return changed;
}

} // namespace

void registerComponentDrawers(rb::ComponentRegistry& registry) {
    registry.setDrawer("Name", &drawName);
    registry.setDrawer("Transform", &drawTransform);
    registry.setDrawer("Camera", &drawCamera);
    registry.setDrawer("DirectionalLight", &drawDirectionalLight);
    registry.setDrawer("PointLight", &drawPointLight);
    registry.setDrawer("SpotLight", &drawSpotLight);
    registry.setDrawer("Primitive", &drawPrimitive);
    registry.setDrawer("ModelRenderer", &drawModelRenderer);
    registry.setDrawer("ScriptComponent", &drawScript);
    registry.setDrawer("RigidBody", &drawRigidBody);
    registry.setDrawer("BoxCollider", &drawBoxCollider);
    registry.setDrawer("SphereCollider", &drawSphereCollider);
    registry.setDrawer("SoundEmitter", &drawSoundEmitter);
    registry.setDrawer("ParticleEmitter", &drawParticleEmitter);
    registry.setDrawer("WaterComponent", &drawWater);
    registry.setDrawer("PostProcess", &drawPostProcess);
}

void drawMaterialInspector(EditorContext& context, rb::Entity e) {
    rb::Scene& scene = context.runtime.scene();
    rb::MaterialComponent& component = scene.get<rb::MaterialComponent>(e);

    const ui::SlotResult slot =
        ui::assetSlot("Material", rb::AssetType::Material, component.material,
                      component.handle.valid());
    if (slot.dropped.valid()) {
        component.material = slot.dropped.id;
        component.handle = {}; // MaterialAssetResolveSystem repopulates it from the uuid
        return;
    }
    if (slot.cleared) {
        component.material = rb::Uuid{};
        component.handle = {};
        return;
    }
    if (!component.material.valid()) {
        return;
    }

    rb::AssetManager* assets = context.runtime.tryResource<rb::AssetManager>();
    if (assets == nullptr) {
        return;
    }
    rb::MaterialAsset* material = assets->get<rb::MaterialAsset>(component.handle);
    if (material == nullptr) {
        return;
    }

    // Inspector edits live in memory; persist them to the .material.json explicitly. Built-in
    // materials have no file, so there is nothing to save.
    const bool fileBacked = !material->path.empty();
    ImGui::BeginDisabled(!fileBacked || !material->dirty);
    if (ImGui::Button((std::string(icon::kSave) + "  Save Material").c_str())) {
        if (rb::saveMaterialAsset(*assets, component.handle)) {
            rb::log::info("material: saved '{}'", material->path.string());
        }
    }
    ImGui::EndDisabled();
    if (!fileBacked && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Built-in materials have no file to save.");
    }
    if (material->dirty) {
        ImGui::SameLine();
        ImGui::TextDisabled("(unsaved)");
    }

    ImGui::SeparatorText("Shader");
    if (!material->shader.valid()) {
        ImGui::TextDisabled("No shader");
        return;
    }
    rb::ShaderAsset* shader = assets->get<rb::ShaderAsset>(material->shaderHandle);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s  %s", material->shader.toString().substr(0, 8).c_str(),
                        shader != nullptr ? "resolved" : "unresolved");

    const bool hasFile = shader != nullptr && !shader->path.empty();
    ImGui::SameLine(ui::labelColumnWidth());
    ImGui::BeginDisabled(!hasFile);
    if (ImGui::SmallButton((std::string(icon::kRotateCcw) + "  Reload").c_str()) &&
        shader != nullptr) {
        shader->sourceTimestamp = 0; // force the resolve poll to re-read + recompile
    }
    ImGui::EndDisabled();
    if (!hasFile && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Built-in shaders have no file to reload.");
    }

    if (shader != nullptr) {
        ImGui::SeparatorText("Uniforms");
        if (shader->uniforms.empty()) {
            ImGui::TextDisabled("(no material-editable uniforms reflected yet)");
        }
        bool changed = false;
        for (const rb::ShaderUniform& uniform : shader->uniforms) {
            changed |= drawUniformOverride(*material, uniform);
        }
        if (changed) {
            material->dirty = true;
        }
    }

    if (!material->textures.empty()) {
        ImGui::SeparatorText("Textures");
        for (const rb::MaterialTexture& texture : material->textures) {
            ImGui::Text("%s: %s", texture.name.c_str(),
                        texture.handle.valid() ? "resolved" : "unresolved");
        }
    }
}

void drawPrefabInspector(EditorContext& context, rb::Entity e) {
    rb::Scene& scene = context.runtime.scene();
    rb::PrefabInstance& instance = scene.get<rb::PrefabInstance>(e);

    const ui::SlotResult slot = ui::assetSlot("Prefab", rb::AssetType::Prefab, instance.prefab,
                                              instance.handle.valid(), false);
    if (slot.cleared) {
        instance.prefab = rb::Uuid{};
        instance.handle = {};
        return;
    }
    if (!instance.prefab.valid()) {
        return;
    }

    rb::AssetManager* assets = context.runtime.tryResource<rb::AssetManager>();
    rb::PrefabAsset* prefab =
        assets != nullptr ? assets->get<rb::PrefabAsset>(instance.handle) : nullptr;

    ImGui::BeginDisabled(prefab == nullptr);
    if (ImGui::Button((std::string(icon::kRotateCcw) + "  Revert to Prefab").c_str()) &&
        prefab != nullptr) {
        // Re-applies the prefab's component data, discarding this instance's overrides.
        rb::applyPrefab(scene, context.registry, e, *prefab);
    }
    ImGui::EndDisabled();
}

void drawSkyboxInspector(EditorContext& context, rb::Entity e) {
    rb::Scene& scene = context.runtime.scene();
    rb::SkyboxComponent& sky = scene.get<rb::SkyboxComponent>(e);
    rb::AssetDatabase* database = context.runtime.tryResource<rb::AssetDatabase>();
    if (database == nullptr) {
        ImGui::TextDisabled("No asset database (faces cannot be assigned)");
        return;
    }

    // Cubemap face order, which is also the order the skybox folders on disk use.
    static const std::array<const char*, rb::SkyboxComponent::kFaceCount> kLabels = {
        "Right (+X)", "Left (-X)", "Top (+Y)", "Bottom (-Y)", "Front (+Z)", "Back (-Z)"};
    for (std::size_t i = 0; i < kLabels.size(); ++i) {
        textureSlot(kLabels[i], *database, sky.faces[i]);
    }
    ImGui::TextDisabled("Six textures, swapped in as soon as all of them resolve.");
}

void drawTerrainInspector(EditorContext& context, rb::Entity e) {
    rb::Scene& scene = context.runtime.scene();
    rb::TerrainComponent& t = scene.get<rb::TerrainComponent>(e);
    rb::AssetDatabase* database = context.runtime.tryResource<rb::AssetDatabase>();
    if (database == nullptr) {
        ImGui::TextDisabled("No asset database (textures cannot be assigned)");
    }

    ImGui::SeparatorText("Shape");
    ui::dragFloat("Size", &t.size, 0.5f, 1.0f, 4096.0f);
    ui::dragInt("Resolution", &t.resolution, 1.0f, 2, 512);
    ImGui::SetItemTooltip("Vertices per side. The mesh rebuilds shortly after you stop editing.");
    ui::dragFloat("Height Scale", &t.heightScale, 0.1f, 0.0f, 1000.0f);

    ImGui::SeparatorText("Height Source");
    enumCombo("Source", t.source);
    if (t.source == rb::TerrainHeightSource::Heightmap) {
        if (database != nullptr) {
            textureSlot("Heightmap", *database, t.heightmap);
        }
        ImGui::TextDisabled("Grayscale image; read on the CPU for the heightfield.");
    } else if (t.source == rb::TerrainHeightSource::Noise) {
        int seed = static_cast<int>(t.seed);
        if (ui::dragInt("Seed", &seed, 1.0f, 0, 1000000)) {
            t.seed = static_cast<std::uint32_t>(seed < 0 ? 0 : seed);
        }
        ui::dragInt("Octaves", &t.octaves, 1.0f, 1, 12);
        ui::dragFloat("Frequency", &t.frequency, 0.05f, 0.1f, 64.0f);
        ui::dragFloat("Lacunarity", &t.lacunarity, 0.01f, 1.0f, 4.0f);
        ui::dragFloat("Gain", &t.gain, 0.01f, 0.0f, 1.0f);
    }

    ImGui::SeparatorText("Layers");
    ui::dragInt("Layer Count", &t.layerCount, 0.1f, 1, rb::TerrainComponent::kMaxLayers);
    // CTRL+click typed entry bypasses the drag bounds; unclamped, the loop below would
    // index past the fixed layers array.
    t.layerCount = std::clamp(t.layerCount, 1, rb::TerrainComponent::kMaxLayers);
    for (int i = 0; i < t.layerCount; ++i) {
        ImGui::PushID(i);
        rb::TerrainLayer& layer = t.layers[static_cast<std::size_t>(i)];
        ImGui::Text("Layer %d", i);
        if (database != nullptr) {
            textureSlot("Albedo", *database, layer.albedo);
        }
        ui::dragFloat("Tiling", &layer.tiling, 0.25f, 0.1f, 256.0f);
        ui::dragFloat2("Height Range", &layer.heightRange.x, 0.01f, 0.0f, 1.0f);
        ui::dragFloat2("Slope Range", &layer.slopeRange.x, 0.01f, 0.0f, 1.0f);
        ui::dragFloat("Sharpness", &layer.sharpness, 0.005f, 0.0f, 0.5f);
        ImGui::PopID();
    }

    ImGui::SeparatorText("Blend");
    enumCombo("Mode", t.blend);
    if (t.blend == rb::TerrainBlend::Splatmap) {
        if (database != nullptr) {
            textureSlot("Splat Map", *database, t.splat);
        }
        ImGui::TextDisabled("RGBA channels -> layers 0..3");
    } else {
        ImGui::TextDisabled("Auto-blend from each layer's height + slope range.");
    }
}

} // namespace rb::editor
