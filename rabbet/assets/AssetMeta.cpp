#include "rabbet/assets/AssetMeta.h"

#include <chrono>
#include <fstream>
#include <string>
#include <system_error>

#include <nlohmann/json.hpp>

#include "rabbet/util/Log.h"

namespace rb::assetmeta {
namespace {

std::int64_t toTicks(std::filesystem::file_time_type time) {
    return std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch()).count();
}

std::int64_t sourceTicks(const std::filesystem::path& path) {
    std::error_code ec;
    const std::filesystem::file_time_type time = std::filesystem::last_write_time(path, ec);
    return ec ? 0 : toTicks(time);
}

Metadata freshMetadata(const std::filesystem::path& assetPath, AssetType type) {
    Metadata meta;
    meta.id = Uuid::generate();
    meta.type = type;
    meta.name = assetPath.stem().string();
    return meta;
}

} // namespace

std::filesystem::path sidecarPath(const std::filesystem::path& assetPath) {
    std::filesystem::path meta = assetPath;
    meta += ".import";
    return meta;
}

Metadata loadOrCreate(const std::filesystem::path& assetPath, AssetType type) {
    const std::filesystem::path metaPath = sidecarPath(assetPath);

    std::error_code ec;
    const bool exists = std::filesystem::exists(metaPath, ec);
    if (exists) {
        std::ifstream in(metaPath);
        if (in) {
            const nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
            if (!doc.is_discarded()) {
                const auto idIt = doc.find("uuid");
                if (idIt != doc.end() && idIt->is_string()) {
                    const Uuid id = Uuid::fromString(idIt->get<std::string>());
                    if (id.valid()) {
                        Metadata meta;
                        meta.id = id;
                        meta.type = assetTypeFromName(doc.value("type", std::string{"Unknown"}));
                        meta.name = doc.value("name", std::string{});
                        if (const auto depsIt = doc.find("dependencies");
                            depsIt != doc.end() && depsIt->is_array()) {
                            for (const nlohmann::json& dep : *depsIt) {
                                if (dep.is_string()) {
                                    const Uuid depId = Uuid::fromString(dep.get<std::string>());
                                    if (depId.valid()) {
                                        meta.dependencies.push_back(depId);
                                    }
                                }
                            }
                        }
                        return meta;
                    }
                }
            }
        }
        // The sidecar exists but is unreadable or corrupt. Never overwrite it: doing
        // so would reassign the asset's identity and silently break every reference.
        // Hand back a temporary id and leave the file untouched for the user to repair.
        log::error("asset meta: '{}' exists but is unreadable; not overwriting", metaPath.string());
        return freshMetadata(assetPath, type);
    }

    Metadata meta = freshMetadata(assetPath, type);
    write(assetPath, meta);
    return meta;
}

void write(const std::filesystem::path& assetPath, const Metadata& meta) {
    nlohmann::json doc;
    doc["version"] = 1;
    doc["uuid"] = meta.id.toString();
    doc["type"] = std::string(assetTypeName(meta.type));
    doc["name"] = meta.name;
    nlohmann::json deps = nlohmann::json::array();
    for (const Uuid& dep : meta.dependencies) {
        deps.push_back(dep.toString());
    }
    doc["dependencies"] = std::move(deps);

    std::ofstream out(sidecarPath(assetPath));
    if (out) {
        out << doc.dump(2) << '\n';
    } else {
        log::warn("asset meta: could not write sidecar for '{}'", assetPath.string());
    }
}

bool needsReimport(const std::filesystem::path& assetPath) {
    std::error_code ec;
    if (!std::filesystem::exists(assetPath, ec)) {
        return false;
    }
    // A missing sidecar ticks 0, so an asset that was never imported always qualifies.
    return sourceTicks(assetPath) > sourceTicks(sidecarPath(assetPath));
}

Uuid loadOrCreateUuid(const std::filesystem::path& assetPath) {
    return loadOrCreate(assetPath, AssetType::Unknown).id;
}

} // namespace rb::assetmeta
