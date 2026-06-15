#pragma once

#include <string>
#include <string_view>

namespace rb {

// The value kinds a material can drive on a shader. Mirrors the subset of GLSL uniform
// types the inspector knows how to edit; matrices are reflected but engine-driven, samplers
// are bound as textures rather than edited as values.
enum class UniformType { Unknown, Int, Float, Vec2, Vec3, Vec4, Mat3, Mat4, Bool, Sampler2D };

[[nodiscard]] constexpr std::string_view uniformTypeName(UniformType type) noexcept {
    switch (type) {
    case UniformType::Int:
        return "Int";
    case UniformType::Float:
        return "Float";
    case UniformType::Vec2:
        return "Vec2";
    case UniformType::Vec3:
        return "Vec3";
    case UniformType::Vec4:
        return "Vec4";
    case UniformType::Mat3:
        return "Mat3";
    case UniformType::Mat4:
        return "Mat4";
    case UniformType::Bool:
        return "Bool";
    case UniformType::Sampler2D:
        return "Sampler2D";
    case UniformType::Unknown:
        break;
    }
    return "Unknown";
}

[[nodiscard]] constexpr UniformType uniformTypeFromName(std::string_view name) noexcept {
    if (name == "Int") {
        return UniformType::Int;
    }
    if (name == "Float") {
        return UniformType::Float;
    }
    if (name == "Vec2") {
        return UniformType::Vec2;
    }
    if (name == "Vec3") {
        return UniformType::Vec3;
    }
    if (name == "Vec4") {
        return UniformType::Vec4;
    }
    if (name == "Mat3") {
        return UniformType::Mat3;
    }
    if (name == "Mat4") {
        return UniformType::Mat4;
    }
    if (name == "Bool") {
        return UniformType::Bool;
    }
    if (name == "Sampler2D") {
        return UniformType::Sampler2D;
    }
    return UniformType::Unknown;
}

// A material-editable uniform exposed by a shader program, learned by reflection. `name` is
// the GLSL identifier; `type` the mapped value kind. Engine-driven uniforms (model/view/
// lights/shadows) are filtered out by the reflector, so this lists only what a material sets.
struct ShaderUniform {
    std::string name;
    UniformType type = UniformType::Unknown;
};

// Maps an OpenGL active-uniform type enum (GL_FLOAT, GL_FLOAT_VEC3, ...) to a UniformType.
// Unsupported GL types map to Unknown. Pure: takes the raw GLenum value, calls no GL.
[[nodiscard]] UniformType uniformTypeFromGlType(unsigned int glType) noexcept;

// True for uniforms the render system sets itself every frame (transforms, camera, lights,
// shadows). Those must never be exposed as material-editable values; everything else is.
[[nodiscard]] bool isEngineUniform(std::string_view name) noexcept;

[[nodiscard]] constexpr bool isSamplerType(UniformType type) noexcept {
    return type == UniformType::Sampler2D;
}

} // namespace rb
