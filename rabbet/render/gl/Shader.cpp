#include "rabbet/render/gl/Shader.h"

#include "rabbet/util/Log.h"

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <utility>

namespace rb::gl {
namespace {

std::optional<unsigned int> compileStage(GLenum type, std::string_view source, std::string_view label) {
    const unsigned int shader = glCreateShader(type);
    const char* sourcePtr = source.data();
    const auto length = static_cast<GLint>(source.size());
    glShaderSource(shader, 1, &sourcePtr, &length);
    glCompileShader(shader);

    GLint status = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
        GLint capacity = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &capacity);
        std::string infoLog(static_cast<std::size_t>(capacity), '\0');
        GLsizei written = 0;
        glGetShaderInfoLog(shader, capacity, &written, infoLog.data());
        infoLog.resize(static_cast<std::size_t>(written));
        log::error("{} shader compilation failed: {}", label, infoLog);
        glDeleteShader(shader);
        return std::nullopt;
    }
    return shader;
}

} // namespace

std::optional<Shader> Shader::fromSource(std::string_view vertexSource,
                                         std::string_view fragmentSource) {
    const std::optional<unsigned int> vertex = compileStage(GL_VERTEX_SHADER, vertexSource, "vertex");
    if (!vertex) {
        return std::nullopt;
    }
    const std::optional<unsigned int> fragment =
        compileStage(GL_FRAGMENT_SHADER, fragmentSource, "fragment");
    if (!fragment) {
        glDeleteShader(*vertex);
        return std::nullopt;
    }

    const unsigned int program = glCreateProgram();
    glAttachShader(program, *vertex);
    glAttachShader(program, *fragment);
    glLinkProgram(program);
    glDeleteShader(*vertex);
    glDeleteShader(*fragment);

    GLint status = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status != GL_TRUE) {
        GLint capacity = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &capacity);
        std::string infoLog(static_cast<std::size_t>(capacity), '\0');
        GLsizei written = 0;
        glGetProgramInfoLog(program, capacity, &written, infoLog.data());
        infoLog.resize(static_cast<std::size_t>(written));
        log::error("shader program link failed: {}", infoLog);
        glDeleteProgram(program);
        return std::nullopt;
    }
    return Shader(program);
}

Shader::Shader(Shader&& other) noexcept
    : m_program(std::exchange(other.m_program, 0)), m_uniformCache(std::move(other.m_uniformCache)) {}

Shader& Shader::operator=(Shader&& other) noexcept {
    if (this != &other) {
        destroy();
        m_program = std::exchange(other.m_program, 0);
        m_uniformCache = std::move(other.m_uniformCache);
    }
    return *this;
}

Shader::~Shader() {
    destroy();
}

void Shader::destroy() noexcept {
    if (m_program != 0) {
        glDeleteProgram(m_program);
        m_program = 0;
    }
}

void Shader::bind() const noexcept {
    glUseProgram(m_program);
}

std::vector<Shader::ActiveUniform> Shader::activeUniforms() const {
    std::vector<ActiveUniform> result;
    GLint count = 0;
    glGetProgramiv(m_program, GL_ACTIVE_UNIFORMS, &count);
    if (count <= 0) {
        return result;
    }
    GLint maxLength = 0;
    glGetProgramiv(m_program, GL_ACTIVE_UNIFORM_MAX_LENGTH, &maxLength);
    std::string nameBuffer(static_cast<std::size_t>(std::max(maxLength, 1)), '\0');
    result.reserve(static_cast<std::size_t>(count));
    for (GLint i = 0; i < count; ++i) {
        GLsizei written = 0;
        GLint size = 0;
        GLenum type = 0;
        glGetActiveUniform(m_program, static_cast<GLuint>(i),
                           static_cast<GLsizei>(nameBuffer.size()), &written, &size, &type,
                           nameBuffer.data());
        std::string name(nameBuffer.data(), static_cast<std::size_t>(written));
        // Drivers append "[0]" to array uniforms; trim it so the name matches the source.
        if (const std::size_t bracket = name.find('['); bracket != std::string::npos) {
            name.resize(bracket);
        }
        result.push_back(
            ActiveUniform{std::move(name), static_cast<unsigned int>(type), static_cast<int>(size)});
    }
    return result;
}

int Shader::uniformLocation(std::string_view name) {
    if (const auto it = m_uniformCache.find(name); it != m_uniformCache.end()) {
        return it->second;
    }
    std::string key{name};
    const int location = glGetUniformLocation(m_program, key.c_str());
    m_uniformCache.emplace(std::move(key), location);
    return location;
}

void Shader::setInt(std::string_view name, int value) {
    glUniform1i(uniformLocation(name), value);
}

void Shader::setFloat(std::string_view name, float value) {
    glUniform1f(uniformLocation(name), value);
}

void Shader::setVec2(std::string_view name, const glm::vec2& value) {
    glUniform2fv(uniformLocation(name), 1, glm::value_ptr(value));
}

void Shader::setVec2Array(std::string_view name, std::span<const glm::vec2> values) {
    if (values.empty()) {
        return;
    }
    glUniform2fv(uniformLocation(name), static_cast<GLsizei>(values.size()),
                 glm::value_ptr(values.front()));
}

void Shader::setVec3(std::string_view name, const glm::vec3& value) {
    glUniform3fv(uniformLocation(name), 1, glm::value_ptr(value));
}

void Shader::setVec3Array(std::string_view name, std::span<const glm::vec3> values) {
    if (values.empty()) {
        return;
    }
    glUniform3fv(uniformLocation(name), static_cast<GLsizei>(values.size()),
                 glm::value_ptr(values.front()));
}

void Shader::setVec4(std::string_view name, const glm::vec4& value) {
    glUniform4fv(uniformLocation(name), 1, glm::value_ptr(value));
}

void Shader::setMat3(std::string_view name, const glm::mat3& value) {
    glUniformMatrix3fv(uniformLocation(name), 1, GL_FALSE, glm::value_ptr(value));
}

void Shader::setMat4(std::string_view name, const glm::mat4& value) {
    glUniformMatrix4fv(uniformLocation(name), 1, GL_FALSE, glm::value_ptr(value));
}

} // namespace rb::gl
