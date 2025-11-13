#include "examples/common/SceneLoader.h"

#include "rabbet/render/Image.h"
#include "rabbet/render/ImageLoader.h"
#include "rabbet/render/MeshData.h"
#include "rabbet/render/Model.h"
#include "rabbet/render/ModelLoader.h"
#include "rabbet/render/PbrMaterial.h"
#include "rabbet/render/gl/Mesh.h"
#include "rabbet/render/gl/Texture.h"
#include "rabbet/util/Log.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <utility>

namespace ex {
namespace {

struct Group {
    rb::MeshData mesh;
    std::string texture;
    glm::vec3 color{1.0f};
};

rb::gl::Texture albedoTexture(const std::filesystem::path& path, const glm::vec3& fallback) {
    if (std::optional<rb::Image> image = rb::loadImage(path)) {
        rb::gl::TextureConfig config;
        config.srgb = true;
        return rb::gl::Texture::fromPixels(image->pixels, image->width, image->height,
                                           image->channels, config);
    }
    return rb::gl::Texture::solid(static_cast<std::uint8_t>(glm::clamp(fallback.r, 0.0f, 1.0f) * 255.0f),
                                  static_cast<std::uint8_t>(glm::clamp(fallback.g, 0.0f, 1.0f) * 255.0f),
                                  static_cast<std::uint8_t>(glm::clamp(fallback.b, 0.0f, 1.0f) * 255.0f));
}

} // namespace

void spawnModel(rb::Runtime& runtime, const std::filesystem::path& modelPath,
                const rb::Transform& base, float metallic, float roughness) {
    const std::optional<rb::Model> model = rb::loadModel(modelPath);
    if (!model) {
        return;
    }
    const std::filesystem::path dir = modelPath.parent_path();

    std::unordered_map<std::string, Group> groups;
    for (const rb::ModelMesh& part : model->meshes) {
        const rb::ModelMaterial* material =
            (part.materialIndex >= 0 &&
             static_cast<std::size_t>(part.materialIndex) < model->materials.size())
                ? &model->materials[static_cast<std::size_t>(part.materialIndex)]
                : nullptr;

        std::string key;
        glm::vec3 color{1.0f};
        std::string texture;
        if (material != nullptr && !material->baseColorTexture.empty()) {
            texture = material->baseColorTexture;
            key = texture;
        } else {
            color = material != nullptr ? material->baseColor : glm::vec3(1.0f);
            key = "#" + std::to_string(part.materialIndex);
        }

        Group& group = groups[key];
        group.texture = texture;
        group.color = color;
        const auto baseIndex = static_cast<std::uint32_t>(group.mesh.vertices.size());
        group.mesh.vertices.insert(group.mesh.vertices.end(), part.data.vertices.begin(),
                                   part.data.vertices.end());
        for (std::uint32_t idx : part.data.indices) {
            group.mesh.indices.push_back(baseIndex + idx);
        }
    }

    for (auto& [key, group] : groups) {
        const rb::Entity entity = runtime.scene().create();
        runtime.scene().add<rb::Transform>(entity, base);
        runtime.scene().add<rb::gl::Mesh>(entity, rb::gl::Mesh::create(group.mesh));
        const bool textured = !group.texture.empty();
        rb::gl::Texture texture = textured ? albedoTexture(dir / group.texture, group.color)
                                           : rb::gl::Texture::solid(255, 255, 255);
        const glm::vec3 baseColor = textured ? glm::vec3(1.0f) : group.color;
        runtime.scene().add<rb::PbrMaterial>(
            entity, rb::PbrMaterial{std::move(texture), baseColor, metallic, roughness, 1.0f});
    }

    rb::log::info("spawned '{}' as {} draw group(s)", modelPath.string(), groups.size());
}

rb::gl::Cubemap loadCubemap(const std::filesystem::path& dir,
                            const std::array<std::string, 6>& faces) {
    std::array<rb::Image, 6> images;
    for (std::size_t i = 0; i < 6; ++i) {
        if (std::optional<rb::Image> image = rb::loadImage(dir / faces[i], false)) {
            images[i] = std::move(*image);
        } else {
            rb::Image fallback;
            fallback.width = 1;
            fallback.height = 1;
            fallback.channels = 3;
            fallback.pixels = {std::byte{90}, std::byte{120}, std::byte{170}};
            images[i] = std::move(fallback);
        }
    }
    return rb::gl::Cubemap::fromFaces(images);
}

} // namespace ex
