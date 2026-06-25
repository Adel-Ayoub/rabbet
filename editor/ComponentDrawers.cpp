#include "editor/ComponentDrawers.h"

#include "editor/AssetAssign.h"
#include "editor/EditorContext.h"

#include "rabbet/assets/AssetDatabase.h"
#include "rabbet/assets/AssetManager.h"
#include "rabbet/assets/AssetType.h"
#include "rabbet/audio/SoundEmitter.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/core/Uuid.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/particle/ParticleEmitter.h"
#include "rabbet/render/PostProcess.h"
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

#include <cstdint>
#include <cstdio>
#include <string>
#include <type_traits>
#include <vector>

namespace rb::editor {
namespace {

// One dropdown for any enum: the labels come straight from magic_enum, so adding an
// enumerator needs no editor change (no hand-written name table to keep in sync).
template <typename T>
void enumCombo(const char* label, T& value) {
    static_assert(std::is_enum_v<T>, "enumCombo requires an enum type");
    if (ImGui::BeginCombo(label, magic_enum::enum_name(value).data())) {
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
    if (ImGui::InputText("Value", buffer, sizeof(buffer))) {
        n.value = buffer;
    }
}

void drawTransform(rb::Scene& scene, rb::Entity e) {
    rb::Transform& t = scene.get<rb::Transform>(e);
    ImGui::DragFloat3("Position", &t.position.x, 0.05f);
    glm::vec3 euler = glm::degrees(glm::eulerAngles(t.rotation));
    if (ImGui::DragFloat3("Rotation", &euler.x, 0.5f)) {
        t.rotation = glm::quat(glm::radians(euler));
    }
    ImGui::DragFloat3("Scale", &t.scale.x, 0.05f, 0.01f, 100.0f);
}

void drawCamera(rb::Scene& scene, rb::Entity e) {
    rb::Camera& c = scene.get<rb::Camera>(e);
    float fovDeg = glm::degrees(c.fovY);
    if (ImGui::DragFloat("FOV (Y)", &fovDeg, 0.5f, 10.0f, 170.0f)) {
        c.fovY = glm::radians(fovDeg);
    }
    ImGui::DragFloat("Near", &c.nearPlane, 0.01f, 0.001f, c.farPlane);
    ImGui::DragFloat("Far", &c.farPlane, 0.5f, c.nearPlane, 10000.0f);
}

void drawDirectionalLight(rb::Scene& scene, rb::Entity e) {
    rb::DirectionalLight& l = scene.get<rb::DirectionalLight>(e);
    ImGui::ColorEdit3("Color", &l.color.x);
    ImGui::DragFloat("Intensity", &l.intensity, 0.05f, 0.0f, 20.0f);
    ImGui::DragFloat3("Direction", &l.direction.x, 0.02f);
}

void drawPointLight(rb::Scene& scene, rb::Entity e) {
    rb::PointLight& l = scene.get<rb::PointLight>(e);
    ImGui::ColorEdit3("Color", &l.color.x);
    ImGui::DragFloat("Intensity", &l.intensity, 0.05f, 0.0f, 50.0f);
    ImGui::DragFloat("Constant", &l.constant, 0.01f, 0.0f, 5.0f);
    ImGui::DragFloat("Linear", &l.linear, 0.001f, 0.0f, 2.0f);
    ImGui::DragFloat("Quadratic", &l.quadratic, 0.001f, 0.0f, 2.0f);
}

void drawSpotLight(rb::Scene& scene, rb::Entity e) {
    rb::SpotLight& l = scene.get<rb::SpotLight>(e);
    ImGui::ColorEdit3("Color", &l.color.x);
    ImGui::DragFloat("Intensity", &l.intensity, 0.05f, 0.0f, 50.0f);
    ImGui::DragFloat3("Direction", &l.direction.x, 0.02f);
    ImGui::DragFloat("Inner Angle", &l.innerAngle, 0.5f, 0.0f, l.outerAngle);
    ImGui::DragFloat("Outer Angle", &l.outerAngle, 0.5f, 0.0f, 89.9f);
    ImGui::DragFloat("Constant", &l.constant, 0.01f, 0.0f, 5.0f);
    ImGui::DragFloat("Linear", &l.linear, 0.001f, 0.0f, 2.0f);
    ImGui::DragFloat("Quadratic", &l.quadratic, 0.001f, 0.0f, 2.0f);
}

void drawPrimitive(rb::Scene& scene, rb::Entity e) {
    rb::Primitive& p = scene.get<rb::Primitive>(e);
    enumCombo("Shape", p.shape);
    ImGui::ColorEdit3("Color", &p.color.x);
    ImGui::ColorEdit3("Emissive", &p.emissive.x,
                      ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
    ImGui::DragFloat("Metallic", &p.metallic, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Roughness", &p.roughness, 0.01f, 0.0f, 1.0f);
}

// The asset reference itself is assigned from the Assets panel (it needs the asset
// database); here we show the bound uuid and whether it resolved this frame.
void drawModelRenderer(rb::Scene& scene, rb::Entity e) {
    rb::ModelRenderer& r = scene.get<rb::ModelRenderer>(e);
    if (!r.model.valid()) {
        ImGui::TextDisabled("No model assigned");
        ImGui::TextDisabled("Assign one from the Assets panel.");
        return;
    }
    ImGui::Text("Model %s", r.model.toString().c_str());
    if (r.handle.valid()) {
        ImGui::TextDisabled("resolved");
    } else {
        ImGui::TextDisabled("unresolved (import or re-assign)");
    }
    if (ImGui::Button("Clear")) {
        r.model = rb::Uuid{};
        r.handle = {};
    }
}

// The .lua reference is assigned from the Assets panel (it needs the asset database);
// here we show the bound script and edit its exposed fields, which drive the running
// script and serialize with the component.
void drawScript(rb::Scene& scene, rb::Entity e) {
    rb::ScriptComponent& s = scene.get<rb::ScriptComponent>(e);
    if (!s.script.valid()) {
        ImGui::TextDisabled("No script assigned");
        ImGui::TextDisabled("Assign a .lua from the Assets panel.");
        return;
    }
    ImGui::Text("Script %s", s.script.toString().c_str());
    ImGui::TextDisabled("%s", s.handle.valid() ? "resolved" : "unresolved (re-assign or import)");
    if (ImGui::Button("Clear")) {
        s.script = rb::Uuid{};
        s.handle = {};
        s.fields.clear();
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
            if (ImGui::DragFloat(f.name.c_str(), &value, 0.05f)) {
                f.number = static_cast<double>(value);
            }
            break;
        }
        case rb::ScriptField::Type::Boolean:
            ImGui::Checkbox(f.name.c_str(), &f.boolean);
            break;
        case rb::ScriptField::Type::String: {
            char buffer[256];
            std::snprintf(buffer, sizeof(buffer), "%s", f.text.c_str());
            if (ImGui::InputText(f.name.c_str(), buffer, sizeof(buffer))) {
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
    ImGui::DragFloat("Mass", &b.mass, 0.05f, 0.0f, 1000.0f);
    ImGui::DragFloat("Friction", &b.friction, 0.01f, 0.0f, 2.0f);
    ImGui::DragFloat("Restitution", &b.restitution, 0.01f, 0.0f, 1.0f);
    ImGui::Checkbox("Gravity", &b.gravity);
}

void drawBoxCollider(rb::Scene& scene, rb::Entity e) {
    rb::BoxCollider& c = scene.get<rb::BoxCollider>(e);
    ImGui::DragFloat3("Half Extents", &c.halfExtents.x, 0.05f, 0.01f, 1000.0f);
    ImGui::DragFloat3("Offset", &c.offset.x, 0.05f);
}

void drawSphereCollider(rb::Scene& scene, rb::Entity e) {
    rb::SphereCollider& c = scene.get<rb::SphereCollider>(e);
    ImGui::DragFloat("Radius", &c.radius, 0.05f, 0.01f, 1000.0f);
    ImGui::DragFloat3("Offset", &c.offset.x, 0.05f);
}

// The clip is assigned from the Assets panel (it needs the asset database); here we show the
// bound clip and edit playback properties, which drive the running voice and serialize.
void drawSoundEmitter(rb::Scene& scene, rb::Entity e) {
    rb::SoundEmitter& s = scene.get<rb::SoundEmitter>(e);
    if (!s.sound.valid()) {
        ImGui::TextDisabled("No clip assigned");
        ImGui::TextDisabled("Assign an audio clip from the Assets panel.");
    } else {
        ImGui::Text("Clip %s", s.sound.toString().c_str());
        ImGui::TextDisabled("%s", s.handle.valid() ? "resolved" : "unresolved (re-assign or import)");
        if (ImGui::Button("Clear")) {
            s.sound = rb::Uuid{};
            s.handle = {};
        }
    }
    ImGui::DragFloat("Volume", &s.volume, 0.01f, 0.0f, 2.0f);
    ImGui::DragFloat("Pitch", &s.pitch, 0.01f, 0.1f, 4.0f);
    ImGui::Checkbox("Loop", &s.loop);
    ImGui::Checkbox("Spatial", &s.spatial);
    ImGui::Checkbox("Play on start", &s.playOnStart);
    ImGui::Checkbox("Stream", &s.stream);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Stream from disk instead of decoding up front (for long music tracks).");
    }
}

// All-data drawer: the sprite reference is assigned from the Assets panel (it needs the asset
// database), so here we show the bound uuid and edit every simulation/appearance field live.
void drawParticleEmitter(rb::Scene& scene, rb::Entity e) {
    rb::ParticleEmitter& p = scene.get<rb::ParticleEmitter>(e);

    ImGui::DragFloat("Emission Rate", &p.emissionRate, 0.5f, 0.0f, 10000.0f);
    ImGui::DragInt("Max Particles", &p.maxParticles, 1.0f, 0, 100000);
    enumCombo("Blend Mode", p.blendMode);
    ImGui::Checkbox("Looping", &p.looping);
    if (!p.looping) {
        ImGui::DragFloat("Duration", &p.duration, 0.1f, 0.0f, 600.0f);
    }

    ImGui::SeparatorText("Life");
    ImGui::DragFloat("Lifetime", &p.lifetime, 0.05f, 0.01f, 600.0f);
    ImGui::DragFloat("Lifetime Jitter", &p.lifetimeJitter, 0.05f, 0.0f, 600.0f);

    ImGui::SeparatorText("Motion");
    ImGui::DragFloat3("Velocity", &p.velocity.x, 0.05f);
    ImGui::DragFloat("Cone Angle", &p.coneAngle, 0.5f, 0.0f, 89.9f);
    ImGui::DragFloat("Speed Jitter", &p.speedJitter, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat3("Gravity", &p.gravity.x, 0.05f);

    ImGui::SeparatorText("Appearance");
    ImGui::DragFloat("Start Size", &p.startSize, 0.01f, 0.0f, 100.0f);
    ImGui::DragFloat("End Size", &p.endSize, 0.01f, 0.0f, 100.0f);
    constexpr ImGuiColorEditFlags colorFlags =
        ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_AlphaPreviewHalf;
    ImGui::ColorEdit4("Start Color", &p.startColor.x, colorFlags);
    ImGui::ColorEdit4("End Color", &p.endColor.x, colorFlags);

    int seed = static_cast<int>(p.seed);
    if (ImGui::DragInt("Seed", &seed, 1.0f, 0, 1000000)) {
        p.seed = static_cast<std::uint32_t>(seed < 0 ? 0 : seed);
    }

    ImGui::SeparatorText("Sprite");
    if (!p.sprite.valid()) {
        ImGui::TextDisabled("No sprite (soft dot)");
    } else {
        ImGui::Text("Sprite %s", p.sprite.toString().c_str());
        ImGui::TextDisabled("%s", p.handle.valid() ? "resolved" : "unresolved (re-assign or import)");
        if (ImGui::Button("Clear Sprite")) {
            p.sprite = rb::Uuid{};
            p.handle = {};
        }
    }
    // Drop a texture from the browser to set the sprite (AssetResolveSystem imports + resolves it;
    // a non-texture simply fails to resolve and falls back to the soft dot).
    ImGui::Selectable("(drop a texture here)##spritedrop");
    if (const rb::Uuid dropped = acceptAssetDropTarget(); dropped.valid()) {
        p.sprite = dropped;
        p.handle = {};
    }
}

// Post-processing settings for the scene (a single enabled instance drives the editor's view).
void drawPostProcess(rb::Scene& scene, rb::Entity e) {
    rb::PostProcess& p = scene.get<rb::PostProcess>(e);
    ImGui::Checkbox("Enabled", &p.enabled);

    ImGui::SeparatorText("Tonemap");
    enumCombo("Operator", p.tonemap);
    ImGui::DragFloat("Exposure (EV)", &p.exposure, 0.05f, -8.0f, 8.0f);
    ImGui::DragFloat("Gamma", &p.gamma, 0.01f, 1.0f, 3.0f);

    ImGui::SeparatorText("Bloom");
    ImGui::Checkbox("Bloom", &p.bloom);
    ImGui::DragFloat("Threshold", &p.bloomThreshold, 0.02f, 0.0f, 20.0f);
    ImGui::DragFloat("Knee", &p.bloomKnee, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Intensity", &p.bloomIntensity, 0.005f, 0.0f, 2.0f);

    ImGui::SeparatorText("Grade");
    ImGui::DragFloat("Contrast", &p.contrast, 0.01f, 0.0f, 2.0f);
    ImGui::DragFloat("Saturation", &p.saturation, 0.01f, 0.0f, 2.0f);
    ImGui::DragFloat("Vignette", &p.vignette, 0.01f, 0.0f, 1.0f);

    ImGui::SeparatorText("Anti-aliasing");
    ImGui::Checkbox("FXAA", &p.fxaa);
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
        ImGui::TextDisabled("%s  (%s)", uniform.name.c_str(),
                            std::string(rb::uniformTypeName(uniform.type)).c_str());
        ImGui::SameLine();
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

    bool changed = false;
    switch (uniform.type) {
    case rb::UniformType::Float:
        changed = ImGui::DragFloat(uniform.name.c_str(), &existing->vec.x, 0.01f);
        break;
    case rb::UniformType::Vec2:
        changed = ImGui::DragFloat2(uniform.name.c_str(), &existing->vec.x, 0.01f);
        break;
    case rb::UniformType::Vec3:
        changed = isColorName(uniform.name)
                      ? ImGui::ColorEdit3(uniform.name.c_str(), &existing->vec.x)
                      : ImGui::DragFloat3(uniform.name.c_str(), &existing->vec.x, 0.01f);
        break;
    case rb::UniformType::Vec4:
        changed = ImGui::ColorEdit4(uniform.name.c_str(), &existing->vec.x);
        break;
    case rb::UniformType::Int:
        changed = ImGui::DragInt(uniform.name.c_str(), &existing->integer);
        break;
    case rb::UniformType::Bool:
        changed = ImGui::Checkbox(uniform.name.c_str(), &existing->boolean);
        break;
    default:
        break;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("x")) {
        const std::string name = uniform.name;
        std::erase_if(material.uniforms,
                      [&name](const rb::MaterialUniform& value) { return value.name == name; });
        changed = true;
    }
    ImGui::PopID();
    return changed;
}

// A combo over the catalogued Texture assets (plus "(none)") that writes the chosen uuid. This is
// how terrain assigns its heightmap / layer albedos / splat: multi-slot assignment is clearer inline
// than routing a single Assets-panel action to a hidden "active slot".
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
    if (ImGui::BeginCombo(label, current.c_str())) {
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
    if (const rb::Uuid dropped = acceptAssetDropTarget(); dropped.valid()) {
        if (const rb::AssetDatabase::Record* record = database.find(dropped);
            record != nullptr && record->type == rb::AssetType::Texture) {
            target = dropped;
            changed = true;
        }
    }
    return changed;
}

// Rebinds a MaterialComponent when a Material asset is dropped on the previous item.
bool acceptMaterialDrop(EditorContext& context, rb::MaterialComponent& component) {
    const rb::Uuid dropped = acceptAssetDropTarget();
    if (!dropped.valid()) {
        return false;
    }
    rb::AssetDatabase* database = context.runtime.tryResource<rb::AssetDatabase>();
    const rb::AssetDatabase::Record* record = database != nullptr ? database->find(dropped) : nullptr;
    if (record == nullptr || record->type != rb::AssetType::Material) {
        return false;
    }
    component.material = dropped;
    component.handle = {}; // MaterialAssetResolveSystem repopulates it from the uuid
    return true;
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
    registry.setDrawer("PostProcess", &drawPostProcess);
}

void drawMaterialInspector(EditorContext& context, rb::Entity e) {
    rb::Scene& scene = context.runtime.scene();
    rb::MaterialComponent& component = scene.get<rb::MaterialComponent>(e);

    if (!component.material.valid()) {
        ImGui::TextDisabled("No material assigned");
        ImGui::Selectable("(drop a Material asset here)##matdrop0");
        if (acceptMaterialDrop(context, component)) {
            return;
        }
        ImGui::TextDisabled("...or assign one from the Assets panel.");
        return;
    }
    ImGui::Text("Material %s", component.material.toString().c_str());
    ImGui::TextDisabled("%s",
                        component.handle.valid() ? "resolved" : "unresolved (re-assign or import)");
    if (ImGui::Button("Clear")) {
        component.material = rb::Uuid{};
        component.handle = {};
        return;
    }
    ImGui::SameLine();
    ImGui::Selectable("(drop a Material to rebind)##matdrop1", false, ImGuiSelectableFlags_None,
                      ImVec2(220.0f, 0.0f));
    if (acceptMaterialDrop(context, component)) {
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
    if (ImGui::Button("Save Material")) {
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
    ImGui::Text("Shader %s", material->shader.toString().c_str());
    rb::ShaderAsset* shader = assets->get<rb::ShaderAsset>(material->shaderHandle);
    ImGui::TextDisabled("%s", shader != nullptr ? "resolved" : "unresolved");

    const bool hasFile = shader != nullptr && !shader->path.empty();
    ImGui::BeginDisabled(!hasFile);
    if (ImGui::Button("Reload Shader") && shader != nullptr) {
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

    if (!instance.prefab.valid()) {
        ImGui::TextDisabled("Not linked to a prefab");
        return;
    }
    ImGui::Text("Prefab %s", instance.prefab.toString().c_str());
    ImGui::TextDisabled("%s", instance.handle.valid() ? "resolved" : "unresolved");

    rb::AssetManager* assets = context.runtime.tryResource<rb::AssetManager>();
    rb::PrefabAsset* prefab =
        assets != nullptr ? assets->get<rb::PrefabAsset>(instance.handle) : nullptr;

    ImGui::BeginDisabled(prefab == nullptr);
    if (ImGui::Button("Revert to Prefab") && prefab != nullptr) {
        // Re-applies the prefab's component data, discarding this instance's overrides.
        rb::applyPrefab(scene, context.registry, e, *prefab);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Unlink")) {
        instance.prefab = rb::Uuid{};
        instance.handle = {};
    }
}

void drawTerrainInspector(EditorContext& context, rb::Entity e) {
    rb::Scene& scene = context.runtime.scene();
    rb::TerrainComponent& t = scene.get<rb::TerrainComponent>(e);
    rb::AssetDatabase* database = context.runtime.tryResource<rb::AssetDatabase>();
    if (database == nullptr) {
        ImGui::TextDisabled("No asset database (textures cannot be assigned)");
    }

    ImGui::SeparatorText("Shape");
    ImGui::DragFloat("Size", &t.size, 0.5f, 1.0f, 4096.0f);
    ImGui::DragInt("Resolution", &t.resolution, 1.0f, 2, 512);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Vertices per side. The mesh rebuilds shortly after you stop editing.");
    }
    ImGui::DragFloat("Height Scale", &t.heightScale, 0.1f, 0.0f, 1000.0f);

    ImGui::SeparatorText("Height Source");
    enumCombo("Source", t.source);
    if (t.source == rb::TerrainHeightSource::Heightmap) {
        if (database != nullptr) {
            textureSlot("Heightmap", *database, t.heightmap);
        }
        ImGui::TextDisabled("Grayscale image; read on the CPU for the heightfield.");
    } else if (t.source == rb::TerrainHeightSource::Noise) {
        int seed = static_cast<int>(t.seed);
        if (ImGui::DragInt("Seed", &seed, 1.0f, 0, 1000000)) {
            t.seed = static_cast<std::uint32_t>(seed < 0 ? 0 : seed);
        }
        ImGui::DragInt("Octaves", &t.octaves, 1.0f, 1, 12);
        ImGui::DragFloat("Frequency", &t.frequency, 0.05f, 0.1f, 64.0f);
        ImGui::DragFloat("Lacunarity", &t.lacunarity, 0.01f, 1.0f, 4.0f);
        ImGui::DragFloat("Gain", &t.gain, 0.01f, 0.0f, 1.0f);
    }

    ImGui::SeparatorText("Layers");
    ImGui::DragInt("Layer Count", &t.layerCount, 0.1f, 1, rb::TerrainComponent::kMaxLayers);
    for (int i = 0; i < t.layerCount; ++i) {
        ImGui::PushID(i);
        rb::TerrainLayer& layer = t.layers[static_cast<std::size_t>(i)];
        ImGui::Text("Layer %d", i);
        if (database != nullptr) {
            textureSlot("Albedo", *database, layer.albedo);
        }
        ImGui::DragFloat("Tiling", &layer.tiling, 0.25f, 0.1f, 256.0f);
        ImGui::DragFloat2("Height Range", &layer.heightRange.x, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat2("Slope Range", &layer.slopeRange.x, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("Sharpness", &layer.sharpness, 0.005f, 0.0f, 0.5f);
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
