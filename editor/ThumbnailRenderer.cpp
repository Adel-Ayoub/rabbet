#include "editor/ThumbnailRenderer.h"

#include "rabbet/assets/AssetManager.h"
#include "rabbet/render/Geometry.h"
#include "rabbet/render/MaterialAsset.h"
#include "rabbet/render/MaterialImport.h"
#include "rabbet/render/ModelAsset.h"
#include "rabbet/render/ModelImport.h"
#include "rabbet/render/TextureAsset.h"
#include "rabbet/render/TextureImport.h"
#include "rabbet/util/Log.h"

#include <glad/glad.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace rb::editor {
namespace {

constexpr const char* kThumbVert = R"(#version 410 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
uniform mat4 uModel;
uniform mat3 uNormalMatrix;
uniform mat4 uViewProjection;
out vec3 vNormal;
out vec2 vUv;
void main() {
    vNormal = uNormalMatrix * aNormal;
    vUv = aUv;
    gl_Position = uViewProjection * uModel * vec4(aPosition, 1.0);
}
)";

constexpr const char* kThumbFrag = R"(#version 410 core
in vec3 vNormal;
in vec2 vUv;
uniform sampler2D uAlbedoTex;
uniform vec3 uBaseColor;
uniform int uHasTexture;
out vec4 FragColor;
void main() {
    vec3 albedo = uBaseColor;
    if (uHasTexture == 1) {
        albedo *= texture(uAlbedoTex, vUv).rgb;
    }
    vec3 n = normalize(vNormal);
    vec3 l = normalize(vec3(0.45, 0.8, 0.55));
    float diff = max(dot(n, l), 0.0);
    vec3 lit = albedo * (0.30 + 0.85 * diff);
    FragColor = vec4(pow(lit, vec3(1.0 / 2.2)), 1.0); // display-encode for the LDR ImGui pass
}
)";

// A representative base colour for a material preview: the first colour-like uniform override, or a
// neutral grey when the material has none (e.g. the built-in default).
glm::vec3 materialColor(const MaterialAsset& material) {
    for (const MaterialUniform& uniform : material.uniforms) {
        if (uniform.name.find("Color") != std::string::npos ||
            uniform.name.find("Albedo") != std::string::npos ||
            uniform.name.find("Tint") != std::string::npos) {
            return glm::vec3(uniform.vec);
        }
    }
    return glm::vec3(0.78f);
}

} // namespace

void ThumbnailRenderer::init() {
    m_fbo = gl::Framebuffer::create(kSize, kSize, false);
    m_shader = gl::Shader::fromSource(kThumbVert, kThumbFrag);
    if (!m_shader.has_value()) {
        rb::log::error("thumbnails: preview shader failed to compile");
    }
    m_sphere = gl::Mesh::create(geometry::sphere());
    m_white = gl::Texture::solid(255, 255, 255);
}

void ThumbnailRenderer::invalidate(const Uuid& id) {
    m_cache.invalidate(id);
    m_textures.erase(id);
}

unsigned int ThumbnailRenderer::thumbnail(AssetManager& assets,
                                          const AssetDatabase::Record& record) {
    if (!m_fbo.has_value() || !m_shader.has_value()) {
        return 0;
    }
    switch (record.type) {
    case AssetType::Texture:
        return textureThumbnail(assets, record);
    case AssetType::Model:
        return modelThumbnail(assets, record);
    case AssetType::Material:
        return materialThumbnail(assets, record);
    default:
        return 0;
    }
}

unsigned int ThumbnailRenderer::textureThumbnail(AssetManager& assets,
                                                 const AssetDatabase::Record& record) {
    // The AssetManager is the cache here: once imported, the live gl::Texture is shown directly.
    AssetHandle<TextureAsset> handle = assets.find<TextureAsset>(record.id);
    if (!handle.valid()) {
        if (m_budget <= 0) {
            return 0; // decoding is costly; defer the first import to a later frame
        }
        --m_budget;
        handle = loadTextureAsset(assets, record.path);
        if (!handle.valid()) {
            return 0;
        }
    }
    const TextureAsset* texture = assets.get<TextureAsset>(handle);
    return texture != nullptr ? texture->texture.id() : 0;
}

unsigned int ThumbnailRenderer::modelThumbnail(AssetManager& assets,
                                               const AssetDatabase::Record& record) {
    if (!m_cache.needsRender(record.id, 0u)) {
        const auto it = m_textures.find(record.id);
        return it != m_textures.end() ? it->second.id() : 0;
    }
    if (m_budget <= 0) {
        return 0;
    }
    const AssetHandle<ModelAsset> handle = loadModelAsset(assets, record.path);
    const ModelAsset* model = assets.get<ModelAsset>(handle);
    if (model == nullptr || model->submeshes.empty()) {
        m_cache.markRendered(record.id, 0u); // a broken model should not retry every frame
        return 0;
    }
    --m_budget;

    const float radius = std::max(model->boundsRadius, 1.0e-3f);
    const float fov = glm::radians(35.0f);
    const float dist = radius / std::sin(fov * 0.5f) * 1.15f;
    const glm::vec3 eye = glm::normalize(glm::vec3(0.65f, 0.5f, 1.0f)) * dist;
    const glm::mat4 view = glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 projection =
        glm::perspective(fov, 1.0f, std::max(0.01f, dist - radius * 1.5f), dist + radius * 1.5f);
    const glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), -model->boundsCenter);

    const unsigned int id = renderInto(record.id, [&] {
        m_shader->bind();
        m_shader->setMat4("uModel", modelMatrix);
        m_shader->setMat4("uViewProjection", projection * view);
        m_shader->setMat3("uNormalMatrix", glm::mat3(1.0f));
        m_shader->setInt("uAlbedoTex", 0);
        for (const ModelAsset::Submesh& submesh : model->submeshes) {
            bool hasTexture = false;
            if (const TextureAsset* texture = assets.get<TextureAsset>(submesh.albedo)) {
                texture->texture.bind(0);
                hasTexture = true;
            } else {
                m_white->bind(0);
            }
            m_shader->setInt("uHasTexture", hasTexture ? 1 : 0);
            m_shader->setVec3("uBaseColor", submesh.baseColor);
            submesh.mesh.draw();
        }
    });
    m_cache.markRendered(record.id, 0u);
    return id;
}

unsigned int ThumbnailRenderer::materialThumbnail(AssetManager& assets,
                                                  const AssetDatabase::Record& record) {
    AssetHandle<MaterialAsset> handle = assets.find<MaterialAsset>(record.id);
    const MaterialAsset* material = handle.valid() ? assets.get<MaterialAsset>(handle) : nullptr;
    const std::uint32_t revision = material != nullptr ? material->revision : 0u;
    if (material != nullptr && !m_cache.needsRender(record.id, revision)) {
        const auto it = m_textures.find(record.id);
        return it != m_textures.end() ? it->second.id() : 0;
    }
    if (m_budget <= 0) {
        return 0;
    }
    if (material == nullptr) {
        handle = loadMaterialAsset(assets, record.path);
        material = assets.get<MaterialAsset>(handle);
        if (material == nullptr) {
            return 0;
        }
    }
    --m_budget;

    const glm::vec3 color = materialColor(*material);
    const std::uint32_t builtRevision = material->revision;
    const float radius = 0.5f; // the preview sphere
    const float fov = glm::radians(35.0f);
    const float dist = radius / std::sin(fov * 0.5f) * 1.18f;
    const glm::vec3 eye = glm::normalize(glm::vec3(0.4f, 0.35f, 1.0f)) * dist;
    const glm::mat4 view = glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 projection = glm::perspective(fov, 1.0f, 0.05f, dist + 2.0f);

    const unsigned int id = renderInto(record.id, [&] {
        m_shader->bind();
        m_shader->setMat4("uModel", glm::mat4(1.0f));
        m_shader->setMat4("uViewProjection", projection * view);
        m_shader->setMat3("uNormalMatrix", glm::mat3(1.0f));
        m_white->bind(0);
        m_shader->setInt("uAlbedoTex", 0);
        m_shader->setInt("uHasTexture", 0);
        m_shader->setVec3("uBaseColor", color);
        m_sphere->draw();
    });
    m_cache.markRendered(record.id, builtRevision);
    return id;
}

template <typename Draw>
unsigned int ThumbnailRenderer::renderInto(const Uuid& id, Draw&& draw) {
    // Save every GL state the preview pass touches so the main viewport render is unperturbed.
    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    GLint prevViewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    const GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean prevBlend = glIsEnabled(GL_BLEND);
    const GLboolean prevCull = glIsEnabled(GL_CULL_FACE);
    const GLboolean prevScissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean prevDepthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);
    const auto setEnabled = [](GLenum cap, GLboolean on) {
        if (on) {
            glEnable(cap);
        } else {
            glDisable(cap);
        }
    };

    m_fbo->bind();
    glViewport(0, 0, kSize, kSize);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glClearColor(0.18f, 0.19f, 0.22f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    draw();

    std::vector<std::byte> pixels(static_cast<std::size_t>(kSize) * static_cast<std::size_t>(kSize) *
                                  4u);
    glReadPixels(0, 0, kSize, kSize, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    gl::TextureConfig config;
    config.srgb = false;
    config.generateMipmaps = false;
    config.linearFilter = true;
    config.repeat = false;
    const auto result =
        m_textures.insert_or_assign(id, gl::Texture::fromPixels(pixels, kSize, kSize, 4, config));

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    setEnabled(GL_DEPTH_TEST, prevDepth);
    glDepthMask(prevDepthMask);
    setEnabled(GL_BLEND, prevBlend);
    setEnabled(GL_CULL_FACE, prevCull);
    setEnabled(GL_SCISSOR_TEST, prevScissor);

    return result.first->second.id();
}

} // namespace rb::editor
