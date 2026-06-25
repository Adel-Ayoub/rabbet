#pragma once

#include <nlohmann/json.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace nlohmann {

template <>
struct adl_serializer<glm::vec2> {
    static void to_json(json& j, const glm::vec2& v) { j = json{v.x, v.y}; }
    static void from_json(const json& j, glm::vec2& v) {
        v.x = j.at(0).get<float>();
        v.y = j.at(1).get<float>();
    }
};

template <>
struct adl_serializer<glm::vec3> {
    static void to_json(json& j, const glm::vec3& v) { j = json{v.x, v.y, v.z}; }
    static void from_json(const json& j, glm::vec3& v) {
        v.x = j.at(0).get<float>();
        v.y = j.at(1).get<float>();
        v.z = j.at(2).get<float>();
    }
};

template <>
struct adl_serializer<glm::vec4> {
    static void to_json(json& j, const glm::vec4& v) { j = json{v.x, v.y, v.z, v.w}; }
    static void from_json(const json& j, glm::vec4& v) {
        v.x = j.at(0).get<float>();
        v.y = j.at(1).get<float>();
        v.z = j.at(2).get<float>();
        v.w = j.at(3).get<float>();
    }
};

template <>
struct adl_serializer<glm::quat> {
    // stored as [w, x, y, z]
    static void to_json(json& j, const glm::quat& q) { j = json{q.w, q.x, q.y, q.z}; }
    static void from_json(const json& j, glm::quat& q) {
        q.w = j.at(0).get<float>();
        q.x = j.at(1).get<float>();
        q.y = j.at(2).get<float>();
        q.z = j.at(3).get<float>();
    }
};

} // namespace nlohmann
