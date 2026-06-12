#include "editor/ComponentDrawers.h"

#include "rabbet/audio/SoundEmitter.h"
#include "rabbet/core/Uuid.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/physics/BoxCollider.h"
#include "rabbet/physics/RigidBody.h"
#include "rabbet/physics/SphereCollider.h"
#include "rabbet/render/ModelRenderer.h"
#include "rabbet/render/Primitive.h"
#include "rabbet/scene/Camera.h"
#include "rabbet/scene/Light.h"
#include "rabbet/scene/Name.h"
#include "rabbet/scene/Transform.h"
#include "rabbet/scripting/ScriptComponent.h"
#include "rabbet/scripting/ScriptField.h"
#include "rabbet/serialize/ComponentRegistry.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>
#include <imgui.h>
#include <magic_enum/magic_enum.hpp>

#include <cstdio>
#include <type_traits>

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

void drawPrimitive(rb::Scene& scene, rb::Entity e) {
    rb::Primitive& p = scene.get<rb::Primitive>(e);
    enumCombo("Shape", p.shape);
    ImGui::ColorEdit3("Color", &p.color.x);
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
}

} // namespace

void registerComponentDrawers(rb::ComponentRegistry& registry) {
    registry.setDrawer("Name", &drawName);
    registry.setDrawer("Transform", &drawTransform);
    registry.setDrawer("Camera", &drawCamera);
    registry.setDrawer("DirectionalLight", &drawDirectionalLight);
    registry.setDrawer("PointLight", &drawPointLight);
    registry.setDrawer("Primitive", &drawPrimitive);
    registry.setDrawer("ModelRenderer", &drawModelRenderer);
    registry.setDrawer("ScriptComponent", &drawScript);
    registry.setDrawer("RigidBody", &drawRigidBody);
    registry.setDrawer("BoxCollider", &drawBoxCollider);
    registry.setDrawer("SphereCollider", &drawSphereCollider);
    registry.setDrawer("SoundEmitter", &drawSoundEmitter);
}

} // namespace rb::editor
