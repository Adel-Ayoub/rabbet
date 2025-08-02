#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

#include <glm/glm.hpp>

namespace rb::gl {

class Shader {
public:
    [[nodiscard]] static std::optional<Shader> fromSource(std::string_view vertexSource,
                                                          std::string_view fragmentSource);

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;
    ~Shader();

    void bind() const noexcept;

    void setInt(std::string_view name, int value);
    void setFloat(std::string_view name, float value);
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
    std::unordered_map<std::string, int> m_uniformCache;
};

} // namespace rb::gl
