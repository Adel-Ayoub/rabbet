#include "rabbet/render/MaterialImport.h"

#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>

#include "rabbet/assets/AssetManager.h"
#include "rabbet/render/MaterialAsset.h"

namespace rb {
namespace {

std::optional<std::string> readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::int64_t mtimeOf(const std::filesystem::path& path) {
    std::error_code ec;
    const std::filesystem::file_time_type time = std::filesystem::last_write_time(path, ec);
    if (ec) {
        return 0;
    }
    return static_cast<std::int64_t>(time.time_since_epoch().count());
}

float numberAt(const nlohmann::json& array, std::size_t index) {
    return index < array.size() && array[index].is_number() ? array[index].get<float>() : 0.0f;
}

nlohmann::json uniformValueToJson(const MaterialUniform& uniform) {
    switch (uniform.type) {
    case UniformType::Float:
        return uniform.vec.x;
    case UniformType::Int:
        return uniform.integer;
    case UniformType::Bool:
        return uniform.boolean;
    case UniformType::Vec2:
        return nlohmann::json::array({uniform.vec.x, uniform.vec.y});
    case UniformType::Vec3:
        return nlohmann::json::array({uniform.vec.x, uniform.vec.y, uniform.vec.z});
    case UniformType::Vec4:
        return nlohmann::json::array({uniform.vec.x, uniform.vec.y, uniform.vec.z, uniform.vec.w});
    default:
        return nullptr;
    }
}

void readUniformValue(const nlohmann::json& value, MaterialUniform& uniform) {
    switch (uniform.type) {
    case UniformType::Float:
        uniform.vec.x = value.is_number() ? value.get<float>() : 0.0f;
        break;
    case UniformType::Int:
        uniform.integer = value.is_number() ? value.get<int>() : 0;
        break;
    case UniformType::Bool:
        uniform.boolean = value.is_boolean() && value.get<bool>();
        break;
    case UniformType::Vec2:
        uniform.vec = {numberAt(value, 0), numberAt(value, 1), 0.0f, 0.0f};
        break;
    case UniformType::Vec3:
        uniform.vec = {numberAt(value, 0), numberAt(value, 1), numberAt(value, 2), 0.0f};
        break;
    case UniformType::Vec4:
        uniform.vec = {numberAt(value, 0), numberAt(value, 1), numberAt(value, 2),
                       numberAt(value, 3)};
        break;
    default:
        break;
    }
}

} // namespace

nlohmann::json materialToJson(const MaterialAsset& material) {
    nlohmann::json doc;
    doc["version"] = 1;
    doc["shader"] = material.shader.toString();

    nlohmann::json uniforms = nlohmann::json::array();
    for (const MaterialUniform& uniform : material.uniforms) {
        nlohmann::json value = uniformValueToJson(uniform);
        if (value.is_null()) {
            continue; // Unknown / matrix / sampler kinds carry no editable value
        }
        uniforms.push_back({{"name", uniform.name},
                            {"type", std::string(uniformTypeName(uniform.type))},
                            {"value", std::move(value)}});
    }
    doc["uniforms"] = std::move(uniforms);

    nlohmann::json textures = nlohmann::json::array();
    for (const MaterialTexture& texture : material.textures) {
        textures.push_back({{"name", texture.name}, {"texture", texture.texture.toString()}});
    }
    doc["textures"] = std::move(textures);
    return doc;
}

void materialFromJson(const nlohmann::json& doc, MaterialAsset& material) {
    material.shader = Uuid::fromString(doc.value("shader", std::string{}));
    material.shaderHandle = {};
    material.uniforms.clear();
    material.textures.clear();

    if (const auto it = doc.find("uniforms"); it != doc.end() && it->is_array()) {
        for (const nlohmann::json& entry : *it) {
            if (!entry.is_object() || !entry.contains("name") || !entry.contains("type")) {
                continue;
            }
            MaterialUniform uniform;
            uniform.name = entry.value("name", std::string{});
            uniform.type = uniformTypeFromName(entry.value("type", std::string{}));
            if (uniform.name.empty() || uniform.type == UniformType::Unknown) {
                continue;
            }
            if (entry.contains("value")) {
                readUniformValue(entry.at("value"), uniform);
            }
            material.uniforms.push_back(std::move(uniform));
        }
    }

    if (const auto it = doc.find("textures"); it != doc.end() && it->is_array()) {
        for (const nlohmann::json& entry : *it) {
            if (!entry.is_object() || !entry.contains("name") || !entry.contains("texture")) {
                continue;
            }
            MaterialTexture texture;
            texture.name = entry.value("name", std::string{});
            texture.texture = Uuid::fromString(entry.value("texture", std::string{}));
            if (texture.name.empty()) {
                continue;
            }
            material.textures.push_back(std::move(texture));
        }
    }
}

AssetHandle<MaterialAsset> loadMaterialAsset(AssetManager& assets,
                                             const std::filesystem::path& path) {
    return assets.load<MaterialAsset>(
        path, [](const std::filesystem::path& p) -> std::optional<MaterialAsset> {
            const std::optional<std::string> text = readFile(p);
            if (!text.has_value()) {
                return std::nullopt;
            }
            const nlohmann::json doc = nlohmann::json::parse(*text, nullptr, false);
            if (doc.is_discarded()) {
                return std::nullopt;
            }
            MaterialAsset material;
            materialFromJson(doc, material);
            material.path = p;
            material.sourceTimestamp = mtimeOf(p);
            return material;
        });
}

bool saveMaterialAsset(AssetManager& assets, AssetHandle<MaterialAsset> handle) {
    MaterialAsset* material = assets.get<MaterialAsset>(handle);
    if (material == nullptr || material->path.empty()) {
        return false;
    }
    std::ofstream out(material->path, std::ios::binary);
    if (!out) {
        return false;
    }
    out << materialToJson(*material).dump(2) << '\n';
    material->sourceTimestamp = mtimeOf(material->path);
    material->dirty = false;
    return true;
}

bool reloadMaterialIfChanged(AssetManager& assets, AssetHandle<MaterialAsset> handle) {
    MaterialAsset* material = assets.get<MaterialAsset>(handle);
    if (material == nullptr || material->path.empty()) {
        return false;
    }
    if (material->dirty) {
        return false; // keep unsaved inspector edits; a disk change must not clobber them
    }
    const std::int64_t current = mtimeOf(material->path);
    if (current == 0 || current == material->sourceTimestamp) {
        return false;
    }
    const std::optional<std::string> text = readFile(material->path);
    if (!text.has_value()) {
        return false;
    }
    const nlohmann::json doc = nlohmann::json::parse(*text, nullptr, false);
    if (doc.is_discarded()) {
        return false;
    }
    materialFromJson(doc, *material);
    material->sourceTimestamp = current;
    ++material->revision;
    return true;
}

} // namespace rb
