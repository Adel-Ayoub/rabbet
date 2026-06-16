#pragma once

#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "rabbet/scene/Camera.h"
#include "rabbet/scene/Light.h"
#include "rabbet/scene/Name.h"
#include "rabbet/scene/Transform.h"
#include "rabbet/serialize/JsonGlm.h"
#include "rabbet/core/Uuid.h"
#include "rabbet/audio/SoundEmitter.h"
#include "rabbet/render/MaterialComponent.h"
#include "rabbet/render/ModelRenderer.h"
#include "rabbet/render/Primitive.h"
#include "rabbet/particle/ParticleEmitter.h"
#include "rabbet/physics/BoxCollider.h"
#include "rabbet/physics/RigidBody.h"
#include "rabbet/physics/SphereCollider.h"
#include "rabbet/scripting/ScriptComponent.h"
#include "rabbet/scripting/ScriptField.h"
#include "rabbet/serialize/PrefabInstance.h"

namespace rb {

class ComponentRegistry;

NLOHMANN_JSON_SERIALIZE_ENUM(PrimitiveShape, {
                                                 {PrimitiveShape::Cube, "Cube"},
                                                 {PrimitiveShape::Sphere, "Sphere"},
                                                 {PrimitiveShape::Plane, "Plane"},
                                             })

inline void to_json(nlohmann::json& j, const Transform& t) {
    j = nlohmann::json{{"position", t.position}, {"rotation", t.rotation}, {"scale", t.scale}};
}
inline void from_json(const nlohmann::json& j, Transform& t) {
    j.at("position").get_to(t.position);
    j.at("rotation").get_to(t.rotation);
    j.at("scale").get_to(t.scale);
}

inline void to_json(nlohmann::json& j, const Camera& c) {
    j = nlohmann::json{{"fovY", c.fovY}, {"nearPlane", c.nearPlane}, {"farPlane", c.farPlane}};
}
inline void from_json(const nlohmann::json& j, Camera& c) {
    j.at("fovY").get_to(c.fovY);
    j.at("nearPlane").get_to(c.nearPlane);
    j.at("farPlane").get_to(c.farPlane);
}

inline void to_json(nlohmann::json& j, const Name& n) { j = nlohmann::json{{"value", n.value}}; }
inline void from_json(const nlohmann::json& j, Name& n) { j.at("value").get_to(n.value); }

inline void to_json(nlohmann::json& j, const DirectionalLight& l) {
    j = nlohmann::json{{"direction", l.direction}, {"color", l.color}, {"intensity", l.intensity}};
}
inline void from_json(const nlohmann::json& j, DirectionalLight& l) {
    j.at("direction").get_to(l.direction);
    j.at("color").get_to(l.color);
    j.at("intensity").get_to(l.intensity);
}

inline void to_json(nlohmann::json& j, const PointLight& l) {
    j = nlohmann::json{{"color", l.color},
                       {"intensity", l.intensity},
                       {"constant", l.constant},
                       {"linear", l.linear},
                       {"quadratic", l.quadratic}};
}
inline void from_json(const nlohmann::json& j, PointLight& l) {
    j.at("color").get_to(l.color);
    j.at("intensity").get_to(l.intensity);
    j.at("constant").get_to(l.constant);
    j.at("linear").get_to(l.linear);
    j.at("quadratic").get_to(l.quadratic);
}

inline void to_json(nlohmann::json& j, const SpotLight& l) {
    j = nlohmann::json{{"direction", l.direction},
                       {"color", l.color},
                       {"intensity", l.intensity},
                       {"constant", l.constant},
                       {"linear", l.linear},
                       {"quadratic", l.quadratic},
                       {"innerAngle", l.innerAngle},
                       {"outerAngle", l.outerAngle}};
}
inline void from_json(const nlohmann::json& j, SpotLight& l) {
    j.at("direction").get_to(l.direction);
    j.at("color").get_to(l.color);
    j.at("intensity").get_to(l.intensity);
    j.at("constant").get_to(l.constant);
    j.at("linear").get_to(l.linear);
    j.at("quadratic").get_to(l.quadratic);
    j.at("innerAngle").get_to(l.innerAngle);
    j.at("outerAngle").get_to(l.outerAngle);
}

inline void to_json(nlohmann::json& j, const ModelRenderer& r) {
    j = nlohmann::json{{"model", r.model.toString()}};
}
inline void from_json(const nlohmann::json& j, ModelRenderer& r) {
    const std::string text = j.at("model").get<std::string>();
    r.model = Uuid::fromString(text);
    if (!text.empty() && !r.model.valid()) {
        throw std::runtime_error("ModelRenderer: malformed model uuid '" + text + "'");
    }
    r.handle = {};
}

inline void to_json(nlohmann::json& j, const MaterialComponent& m) {
    j = nlohmann::json{{"material", m.material.toString()}};
}
inline void from_json(const nlohmann::json& j, MaterialComponent& m) {
    const std::string text = j.at("material").get<std::string>();
    m.material = Uuid::fromString(text);
    if (!text.empty() && !m.material.valid()) {
        throw std::runtime_error("MaterialComponent: malformed material uuid '" + text + "'");
    }
    m.handle = {};
}

inline void to_json(nlohmann::json& j, const PrefabInstance& p) {
    j = nlohmann::json{{"prefab", p.prefab.toString()}};
}
inline void from_json(const nlohmann::json& j, PrefabInstance& p) {
    const std::string text = j.at("prefab").get<std::string>();
    p.prefab = Uuid::fromString(text);
    if (!text.empty() && !p.prefab.valid()) {
        throw std::runtime_error("PrefabInstance: malformed prefab uuid '" + text + "'");
    }
    p.handle = {};
}

inline void to_json(nlohmann::json& j, const ScriptComponent& s) {
    nlohmann::json fields = nlohmann::json::object();
    for (const ScriptField& f : s.fields) {
        switch (f.type) {
        case ScriptField::Type::Number:
            fields[f.name] = f.number;
            break;
        case ScriptField::Type::Boolean:
            fields[f.name] = f.boolean;
            break;
        case ScriptField::Type::String:
            fields[f.name] = f.text;
            break;
        }
    }
    j = nlohmann::json{{"script", s.script.toString()}, {"fields", fields}};
}
inline void from_json(const nlohmann::json& j, ScriptComponent& s) {
    const std::string text = j.at("script").get<std::string>();
    s.script = Uuid::fromString(text);
    if (!text.empty() && !s.script.valid()) {
        throw std::runtime_error("ScriptComponent: malformed script uuid '" + text + "'");
    }
    s.handle = {};
    s.fields.clear();
    if (j.contains("fields") && j.at("fields").is_object()) {
        for (const auto& [name, value] : j.at("fields").items()) {
            ScriptField field;
            field.name = name;
            if (value.is_boolean()) {
                field.type = ScriptField::Type::Boolean;
                field.boolean = value.get<bool>();
            } else if (value.is_number()) {
                field.type = ScriptField::Type::Number;
                field.number = value.get<double>();
            } else if (value.is_string()) {
                field.type = ScriptField::Type::String;
                field.text = value.get<std::string>();
            } else {
                continue;
            }
            s.fields.push_back(std::move(field));
        }
    }
}

inline void to_json(nlohmann::json& j, const Primitive& p) {
    j = nlohmann::json{{"shape", p.shape},
                       {"color", p.color},
                       {"emissive", p.emissive},
                       {"metallic", p.metallic},
                       {"roughness", p.roughness}};
}
inline void from_json(const nlohmann::json& j, Primitive& p) {
    j.at("shape").get_to(p.shape);
    j.at("color").get_to(p.color);
    p.emissive = j.value("emissive", glm::vec3(0.0f)); // tolerate scenes written before emissive
    j.at("metallic").get_to(p.metallic);
    j.at("roughness").get_to(p.roughness);
}

NLOHMANN_JSON_SERIALIZE_ENUM(BodyType, {
                                           {BodyType::Static, "Static"},
                                           {BodyType::Dynamic, "Dynamic"},
                                           {BodyType::Kinematic, "Kinematic"},
                                       })

inline void to_json(nlohmann::json& j, const RigidBody& b) {
    j = nlohmann::json{{"type", b.type},
                       {"mass", b.mass},
                       {"friction", b.friction},
                       {"restitution", b.restitution},
                       {"gravity", b.gravity}};
}
inline void from_json(const nlohmann::json& j, RigidBody& b) {
    j.at("type").get_to(b.type);
    j.at("mass").get_to(b.mass);
    j.at("friction").get_to(b.friction);
    j.at("restitution").get_to(b.restitution);
    j.at("gravity").get_to(b.gravity);
}

inline void to_json(nlohmann::json& j, const BoxCollider& c) {
    j = nlohmann::json{{"halfExtents", c.halfExtents}, {"offset", c.offset}};
}
inline void from_json(const nlohmann::json& j, BoxCollider& c) {
    j.at("halfExtents").get_to(c.halfExtents);
    j.at("offset").get_to(c.offset);
}

inline void to_json(nlohmann::json& j, const SphereCollider& c) {
    j = nlohmann::json{{"radius", c.radius}, {"offset", c.offset}};
}
inline void from_json(const nlohmann::json& j, SphereCollider& c) {
    j.at("radius").get_to(c.radius);
    j.at("offset").get_to(c.offset);
}

inline void to_json(nlohmann::json& j, const SoundEmitter& s) {
    j = nlohmann::json{{"sound", s.sound.toString()},
                       {"volume", s.volume},
                       {"pitch", s.pitch},
                       {"loop", s.loop},
                       {"spatial", s.spatial},
                       {"playOnStart", s.playOnStart},
                       {"stream", s.stream}};
}
inline void from_json(const nlohmann::json& j, SoundEmitter& s) {
    const std::string text = j.at("sound").get<std::string>();
    s.sound = Uuid::fromString(text);
    if (!text.empty() && !s.sound.valid()) {
        throw std::runtime_error("SoundEmitter: malformed sound uuid '" + text + "'");
    }
    s.handle = {};
    j.at("volume").get_to(s.volume);
    j.at("pitch").get_to(s.pitch);
    j.at("loop").get_to(s.loop);
    j.at("spatial").get_to(s.spatial);
    j.at("playOnStart").get_to(s.playOnStart);
    s.stream = j.value("stream", false); // tolerate scenes written before `stream` existed
}

NLOHMANN_JSON_SERIALIZE_ENUM(ParticleBlendMode, {
                                                    {ParticleBlendMode::Additive, "Additive"},
                                                    {ParticleBlendMode::Alpha, "Alpha"},
                                                })

inline void to_json(nlohmann::json& j, const ParticleEmitter& e) {
    j = nlohmann::json{{"emissionRate", e.emissionRate},
                       {"maxParticles", e.maxParticles},
                       {"lifetime", e.lifetime},
                       {"lifetimeJitter", e.lifetimeJitter},
                       {"startSize", e.startSize},
                       {"endSize", e.endSize},
                       {"startColor", e.startColor},
                       {"endColor", e.endColor},
                       {"velocity", e.velocity},
                       {"coneAngle", e.coneAngle},
                       {"speedJitter", e.speedJitter},
                       {"gravity", e.gravity},
                       {"blendMode", e.blendMode},
                       {"looping", e.looping},
                       {"duration", e.duration},
                       {"seed", e.seed},
                       {"sprite", e.sprite.toString()}};
}
inline void from_json(const nlohmann::json& j, ParticleEmitter& e) {
    // Tolerant reads: every field falls back to its default, so a partial or hand-edited emitter
    // (or one written before a field was added) loads cleanly instead of throwing.
    e.emissionRate = j.value("emissionRate", e.emissionRate);
    e.maxParticles = j.value("maxParticles", e.maxParticles);
    e.lifetime = j.value("lifetime", e.lifetime);
    e.lifetimeJitter = j.value("lifetimeJitter", e.lifetimeJitter);
    e.startSize = j.value("startSize", e.startSize);
    e.endSize = j.value("endSize", e.endSize);
    e.startColor = j.value("startColor", e.startColor);
    e.endColor = j.value("endColor", e.endColor);
    e.velocity = j.value("velocity", e.velocity);
    e.coneAngle = j.value("coneAngle", e.coneAngle);
    e.speedJitter = j.value("speedJitter", e.speedJitter);
    e.gravity = j.value("gravity", e.gravity);
    e.blendMode = j.value("blendMode", e.blendMode);
    e.looping = j.value("looping", e.looping);
    e.duration = j.value("duration", e.duration);
    e.seed = j.value("seed", e.seed);
    const std::string text = j.value("sprite", std::string{});
    e.sprite = Uuid::fromString(text);
    if (!text.empty() && !e.sprite.valid()) {
        throw std::runtime_error("ParticleEmitter: malformed sprite uuid '" + text + "'");
    }
    e.handle = {};
}

void registerBuiltinComponents(ComponentRegistry& registry);

} // namespace rb
