#pragma once

#include <algorithm>
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
#include "rabbet/terrain/TerrainComponent.h"
#include "rabbet/physics/BoxCollider.h"
#include "rabbet/physics/RigidBody.h"
#include "rabbet/physics/SphereCollider.h"
#include "rabbet/render/PostProcess.h"
#include "rabbet/render/SkyboxComponent.h"
#include "rabbet/scripting/ScriptComponent.h"
#include "rabbet/scripting/ScriptField.h"
#include "rabbet/serialize/PrefabInstance.h"

namespace rb {

class ComponentRegistry;

// Unset asset refs serialize as "": the null uuid would otherwise round-trip as 32 zeros and
// be rejected as malformed on load, silently dropping the whole component.
inline std::string uuidText(const Uuid& id) {
    return id.valid() ? id.toString() : std::string{};
}
// Exactly the canonical null spelling (32 zero nibbles, dashes ignored); anything shorter or
// longer stays malformed so corruption is still surfaced.
inline bool isNullUuidText(const std::string& text) {
    std::size_t zeros = 0;
    for (const char c : text) {
        if (c == '-') {
            continue;
        }
        if (c != '0') {
            return false;
        }
        ++zeros;
    }
    return zeros == 32;
}
inline Uuid parseUuid(const std::string& text, const char* what) {
    const Uuid id = Uuid::fromString(text);
    if (!id.valid() && !text.empty() && !isNullUuidText(text)) {
        throw std::runtime_error(std::string(what) + " uuid '" + text + "'");
    }
    return id;
}

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
    j = nlohmann::json{{"model", uuidText(r.model)}};
}
inline void from_json(const nlohmann::json& j, ModelRenderer& r) {
    r.model = parseUuid(j.at("model").get<std::string>(), "ModelRenderer: malformed model");
    r.handle = {};
}

inline void to_json(nlohmann::json& j, const MaterialComponent& m) {
    j = nlohmann::json{{"material", uuidText(m.material)}};
}
inline void from_json(const nlohmann::json& j, MaterialComponent& m) {
    m.material =
        parseUuid(j.at("material").get<std::string>(), "MaterialComponent: malformed material");
    m.handle = {};
}

inline void to_json(nlohmann::json& j, const PrefabInstance& p) {
    j = nlohmann::json{{"prefab", uuidText(p.prefab)}};
}
inline void from_json(const nlohmann::json& j, PrefabInstance& p) {
    p.prefab = parseUuid(j.at("prefab").get<std::string>(), "PrefabInstance: malformed prefab");
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
    j = nlohmann::json{{"script", uuidText(s.script)}, {"fields", fields}};
}
inline void from_json(const nlohmann::json& j, ScriptComponent& s) {
    s.script = parseUuid(j.at("script").get<std::string>(), "ScriptComponent: malformed script");
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
    j = nlohmann::json{{"sound", uuidText(s.sound)},
                       {"volume", s.volume},
                       {"pitch", s.pitch},
                       {"loop", s.loop},
                       {"spatial", s.spatial},
                       {"playOnStart", s.playOnStart},
                       {"stream", s.stream}};
}
inline void from_json(const nlohmann::json& j, SoundEmitter& s) {
    s.sound = parseUuid(j.at("sound").get<std::string>(), "SoundEmitter: malformed sound");
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
                       {"sprite", uuidText(e.sprite)}};
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
    e.sprite = parseUuid(j.value("sprite", std::string{}), "ParticleEmitter: malformed sprite");
    e.handle = {};
}

NLOHMANN_JSON_SERIALIZE_ENUM(TonemapOperator, {
                                                  {TonemapOperator::ACES, "ACES"},
                                                  {TonemapOperator::Reinhard, "Reinhard"},
                                                  {TonemapOperator::Filmic, "Filmic"},
                                              })

inline void to_json(nlohmann::json& j, const PostProcess& p) {
    j = nlohmann::json{{"enabled", p.enabled},
                       {"tonemap", p.tonemap},
                       {"exposure", p.exposure},
                       {"gamma", p.gamma},
                       {"bloom", p.bloom},
                       {"bloomThreshold", p.bloomThreshold},
                       {"bloomKnee", p.bloomKnee},
                       {"bloomIntensity", p.bloomIntensity},
                       {"contrast", p.contrast},
                       {"saturation", p.saturation},
                       {"vignette", p.vignette},
                       {"fxaa", p.fxaa}};
}
inline void from_json(const nlohmann::json& j, PostProcess& p) {
    // Tolerant reads: each field falls back to its default so partial / older settings load cleanly.
    p.enabled = j.value("enabled", p.enabled);
    p.tonemap = j.value("tonemap", p.tonemap);
    p.exposure = j.value("exposure", p.exposure);
    p.gamma = j.value("gamma", p.gamma);
    p.bloom = j.value("bloom", p.bloom);
    p.bloomThreshold = j.value("bloomThreshold", p.bloomThreshold);
    p.bloomKnee = j.value("bloomKnee", p.bloomKnee);
    p.bloomIntensity = j.value("bloomIntensity", p.bloomIntensity);
    p.contrast = j.value("contrast", p.contrast);
    p.saturation = j.value("saturation", p.saturation);
    p.vignette = j.value("vignette", p.vignette);
    p.fxaa = j.value("fxaa", p.fxaa);
}

NLOHMANN_JSON_SERIALIZE_ENUM(TerrainHeightSource, {
                                                      {TerrainHeightSource::Flat, "Flat"},
                                                      {TerrainHeightSource::Heightmap, "Heightmap"},
                                                      {TerrainHeightSource::Noise, "Noise"},
                                                  })

NLOHMANN_JSON_SERIALIZE_ENUM(TerrainBlend, {
                                               {TerrainBlend::HeightSlope, "HeightSlope"},
                                               {TerrainBlend::Splatmap, "Splatmap"},
                                           })

inline void to_json(nlohmann::json& j, const TerrainLayer& layer) {
    j = nlohmann::json{{"albedo", uuidText(layer.albedo)},
                       {"tiling", layer.tiling},
                       {"heightRange", layer.heightRange},
                       {"slopeRange", layer.slopeRange},
                       {"sharpness", layer.sharpness}};
}
inline void from_json(const nlohmann::json& j, TerrainLayer& layer) {
    layer.albedo = parseUuid(j.value("albedo", std::string{}), "TerrainLayer: malformed albedo");
    layer.handle = {};
    layer.tiling = j.value("tiling", layer.tiling);
    layer.heightRange = j.value("heightRange", layer.heightRange);
    layer.slopeRange = j.value("slopeRange", layer.slopeRange);
    layer.sharpness = j.value("sharpness", layer.sharpness);
}

inline void to_json(nlohmann::json& j, const SkyboxComponent& s) {
    nlohmann::json faces = nlohmann::json::array();
    for (const Uuid& face : s.faces) {
        faces.push_back(uuidText(face));
    }
    j = nlohmann::json{{"faces", faces}};
}
inline void from_json(const nlohmann::json& j, SkyboxComponent& s) {
    s.faces = {};
    const auto it = j.find("faces");
    if (it == j.end() || !it->is_array()) {
        return; // an unset sky is legal: it just leaves the current one alone
    }
    const std::size_t count =
        std::min<std::size_t>(it->size(), static_cast<std::size_t>(SkyboxComponent::kFaceCount));
    for (std::size_t i = 0; i < count; ++i) {
        const nlohmann::json& face = (*it)[i];
        if (face.is_string()) {
            s.faces[i] = parseUuid(face.get<std::string>(), "SkyboxComponent: malformed face");
        }
    }
}

inline void to_json(nlohmann::json& j, const TerrainComponent& t) {
    const int count =
        t.layerCount < 0 ? 0 : (t.layerCount > TerrainComponent::kMaxLayers
                                    ? TerrainComponent::kMaxLayers : t.layerCount);
    nlohmann::json layers = nlohmann::json::array();
    for (int i = 0; i < count; ++i) {
        layers.push_back(t.layers[static_cast<std::size_t>(i)]);
    }
    j = nlohmann::json{{"size", t.size},
                       {"resolution", t.resolution},
                       {"heightScale", t.heightScale},
                       {"source", t.source},
                       {"heightmap", uuidText(t.heightmap)},
                       {"seed", t.seed},
                       {"octaves", t.octaves},
                       {"frequency", t.frequency},
                       {"lacunarity", t.lacunarity},
                       {"gain", t.gain},
                       {"layers", layers},
                       {"blend", t.blend},
                       {"splat", uuidText(t.splat)}};
}
inline void from_json(const nlohmann::json& j, TerrainComponent& t) {
    // Tolerant reads: each field falls back to its default so partial / older terrains load cleanly.
    t.size = j.value("size", t.size);
    t.resolution = j.value("resolution", t.resolution);
    t.heightScale = j.value("heightScale", t.heightScale);
    t.source = j.value("source", t.source);
    t.heightmap = parseUuid(j.value("heightmap", std::string{}),
                            "TerrainComponent: malformed heightmap");
    t.seed = j.value("seed", t.seed);
    t.octaves = j.value("octaves", t.octaves);
    t.frequency = j.value("frequency", t.frequency);
    t.lacunarity = j.value("lacunarity", t.lacunarity);
    t.gain = j.value("gain", t.gain);
    t.layers = {};
    int count = 0;
    if (j.contains("layers") && j.at("layers").is_array()) {
        for (const auto& entry : j.at("layers")) {
            if (count >= TerrainComponent::kMaxLayers) {
                break;
            }
            t.layers[static_cast<std::size_t>(count)] = entry.get<TerrainLayer>();
            ++count;
        }
    }
    t.layerCount = count > 0 ? count : 1; // always keep at least the base layer
    t.blend = j.value("blend", t.blend);
    t.splat = parseUuid(j.value("splat", std::string{}), "TerrainComponent: malformed splat");
    t.splatHandle = {};
}

void registerBuiltinComponents(ComponentRegistry& registry);

} // namespace rb
