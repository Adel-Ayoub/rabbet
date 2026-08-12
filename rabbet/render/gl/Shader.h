#pragma once

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

namespace rb::gl {

namespace detail {
struct TransparentStringHash {
    using is_transparent = void;
    [[nodiscard]] std::size_t operator()(std::string_view value) const noexcept {
        return std::hash<std::string_view>{}(value);
    }
};
} // namespace detail

class Shader {
public:
    // One active uniform as the driver reports it: the GLSL name (array "[0]" suffix stripped),
    // the raw GL type enum, and the array length (1 for scalars). The render layer maps `type`
    // to a UniformType and filters engine-driven names.
    struct ActiveUniform {
        std::string name;
        unsigned int type = 0;
        int size = 0;
    };

    [[nodiscard]] static std::optional<Shader> fromSource(std::string_view vertexSource,
                                                          std::string_view fragmentSource);

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;
    ~Shader();

    void bind() const noexcept;

    // Reflects every active uniform of the linked program via glGetActiveUniform. Requires the
    // program to be linked (it is, post-fromSource). The order follows the driver's indices.
    [[nodiscard]] std::vector<ActiveUniform> activeUniforms() const;

    // Wires a named uniform block to a buffer binding index. 410 has no binding layout
    // qualifier, so the program side owns this wiring. Missing blocks are ignored the
    // way missing loose uniforms are.
    void bindUniformBlock(std::string_view name, unsigned int binding) const noexcept;

    void setInt(std::string_view name, int value);
    void setFloat(std::string_view name, float value);
    void setVec2(std::string_view name, const glm::vec2& value);
    void setVec2Array(std::string_view name, std::span<const glm::vec2> values);
    void setVec3(std::string_view name, const glm::vec3& value);
    void setVec3Array(std::string_view name, std::span<const glm::vec3> values);
    void setVec4(std::string_view name, const glm::vec4& value);
    void setMat3(std::string_view name, const glm::mat3& value);
    void setMat4(std::string_view name, const glm::mat4& value);

    [[nodiscard]] unsigned int id() const noexcept { return m_program; }

private:
    explicit Shader(unsigned int program) noexcept : m_program(program) {}
    [[nodiscard]] int uniformLocation(std::string_view name);
    void destroy() noexcept;

    unsigned int m_program = 0;
    std::unordered_map<std::string, int, detail::TransparentStringHash, std::equal_to<>>
        m_uniformCache;
};

} // namespace rb::gl
