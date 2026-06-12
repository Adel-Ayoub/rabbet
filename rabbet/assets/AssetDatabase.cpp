#include "rabbet/assets/AssetDatabase.h"

#include "rabbet/assets/AssetManager.h"
#include "rabbet/assets/AssetMeta.h"
#include "rabbet/util/Log.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace rb {

namespace {

std::string toLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool endsWith(const std::string& text, std::string_view suffix) {
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace

AssetType assetTypeFromExtension(const std::filesystem::path& path) {
    const std::string filename = toLower(path.filename().string());
    if (endsWith(filename, ".scene.json")) {
        return AssetType::Scene;
    }
    if (endsWith(filename, ".prefab.json") || endsWith(filename, ".prefab")) {
        return AssetType::Prefab;
    }

    const std::string ext = toLower(path.extension().string());
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp" ||
        ext == ".hdr" || ext == ".dds" || ext == ".ktx") {
        return AssetType::Texture;
    }
    if (ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx" || ext == ".dae" ||
        ext == ".ply" || ext == ".stl") {
        return AssetType::Model;
    }
    if (ext == ".lua") {
        return AssetType::Script;
    }
    if (ext == ".wav" || ext == ".ogg" || ext == ".mp3") {
        return AssetType::Audio;
    }
    return AssetType::Unknown;
}

std::size_t AssetDatabase::scan(const std::filesystem::path& root, AssetManager* manager) {
    m_root = root;
    m_records.clear();
    m_byId.clear();
    m_byPath.clear();

    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) {
        log::warn("asset database: '{}' is not a directory", root.string());
        return 0;
    }

    // Collect candidate paths in one pass first; loadOrCreate writes ".import"
    // sidecars, so processing inline would mutate the directory mid-iteration.
    std::vector<std::pair<std::filesystem::path, AssetType>> candidates;
    std::filesystem::recursive_directory_iterator it(
        root, std::filesystem::directory_options::skip_permission_denied, ec);
    const std::filesystem::recursive_directory_iterator end;
    for (; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) {
            continue;
        }
        const std::filesystem::path& path = it->path();
        if (path.extension() == ".import") {
            continue;
        }
        const AssetType type = assetTypeFromExtension(path);
        if (type != AssetType::Unknown) {
            candidates.emplace_back(path, type);
        }
    }

    for (const auto& [path, type] : candidates) {
        const assetmeta::Metadata meta = assetmeta::loadOrCreate(path, type);
        if (!meta.id.valid()) {
            log::warn("asset database: '{}' has no usable uuid; skipped", path.string());
            continue;
        }

        const std::size_t index = m_records.size();
        m_records.push_back(Record{meta.id, path, type, meta.name});
        m_byId[meta.id] = index;
        m_byPath[path.lexically_normal().string()] = index;
        if (manager != nullptr) {
            manager->registerSource(meta.id, path, type);
        }
    }

    log::info("asset database: catalogued {} assets under '{}'", m_records.size(), root.string());
    return m_records.size();
}

const AssetDatabase::Record* AssetDatabase::find(const Uuid& id) const noexcept {
    const auto it = m_byId.find(id);
    return it != m_byId.end() ? &m_records[it->second] : nullptr;
}

const AssetDatabase::Record* AssetDatabase::findByPath(const std::filesystem::path& path) const noexcept {
    const auto it = m_byPath.find(path.lexically_normal().string());
    return it != m_byPath.end() ? &m_records[it->second] : nullptr;
}

} // namespace rb
