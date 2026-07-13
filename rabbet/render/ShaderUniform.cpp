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
    // Arrays reflect as "uPointPosition[0]": compare the base name so every indexed form
    // matches without a prefix test, which would also swallow a custom shader's own
    // "uPointSize"-style uniforms and hide them from the material inspector. (uEmissive is
    // deliberately absent: like uBaseColor it is a per-surface value a material may
    // override, not engine-driven.)
    if (const std::size_t bracket = name.find('['); bracket != std::string_view::npos) {
        name = name.substr(0, bracket);
    }
    static constexpr std::array<std::string_view, 25> kReserved = {
        "uModel",          "uNormalMatrix",   "uViewProjection",
        "uViewPosition",   "uLightSpace",     "uShadowMap",
        "uHasShadowMap",   "uAmbient",        "uIrradiance",
        "uHasEnvironment", "uEnvironmentIntensity", "uHdrOutput",
        "uDirectionalCount", "uDirectionalColor", "uDirectionalDirection",
        "uPointCount",     "uPointColor",     "uPointPosition",
        "uPointAttenuation", "uSpotCount",    "uSpotColor",
        "uSpotDirection",  "uSpotPosition",   "uSpotAttenuation",
        "uSpotCone"};
    for (const std::string_view reserved : kReserved) {
        if (name == reserved) {
            return true;
        }
    }
    return false;
}

} // namespace rb
