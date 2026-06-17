#include "rabbet/render/ShaderUniform.h"

#include <array>

#include <glad/glad.h>

namespace rb {

UniformType uniformTypeFromGlType(unsigned int glType) noexcept {
    switch (glType) {
    case GL_INT:
        return UniformType::Int;
    case GL_FLOAT:
        return UniformType::Float;
    case GL_FLOAT_VEC2:
        return UniformType::Vec2;
    case GL_FLOAT_VEC3:
        return UniformType::Vec3;
    case GL_FLOAT_VEC4:
        return UniformType::Vec4;
    case GL_FLOAT_MAT3:
        return UniformType::Mat3;
    case GL_FLOAT_MAT4:
        return UniformType::Mat4;
    case GL_BOOL:
        return UniformType::Bool;
    case GL_SAMPLER_2D:
        return UniformType::Sampler2D;
    default:
        return UniformType::Unknown;
    }
}

bool isEngineUniform(std::string_view name) noexcept {
    // Light and primitive arrays reflect as "uPointPosition[0]" etc.; the prefix match below
    // covers the indexed forms without listing each element. (uEmissive is deliberately absent:
    // like uBaseColor it is a per-surface value a material may override, not engine-driven.)
    if (name.starts_with("uDirectional") || name.starts_with("uPoint") ||
        name.starts_with("uSpot")) {
        return true;
    }
    static constexpr std::array<std::string_view, 12> kReserved = {
        "uModel",      "uNormalMatrix",   "uViewProjection",      "uViewPosition",
        "uLightSpace", "uShadowMap",      "uHasShadowMap",        "uAmbient",
        "uIrradiance", "uHasEnvironment", "uEnvironmentIntensity", "uHdrOutput"};
    for (const std::string_view reserved : kReserved) {
        if (name == reserved) {
            return true;
        }
    }
    return false;
}

} // namespace rb
