#pragma once

#include <nlohmann/json.hpp>

#include "rabbet/scene/Camera.h"
#include "rabbet/scene/Light.h"
#include "rabbet/scene/Name.h"
#include "rabbet/scene/Transform.h"
#include "rabbet/serialize/JsonGlm.h"
#include "rabbet/core/Uuid.h"
#include "rabbet/render/ModelRenderer.h"

namespace rb {

class ComponentRegistry;

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

inline void to_json(nlohmann::json& j, const ModelRenderer& r) {
    j = nlohmann::json{{"model", r.model.toString()}};
}
inline void from_json(const nlohmann::json& j, ModelRenderer& r) {
    r.model = Uuid::fromString(j.at("model").get<std::string>());
    r.handle = {};
}

void registerBuiltinComponents(ComponentRegistry& registry);

} // namespace rb
