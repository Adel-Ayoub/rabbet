#include "rabbet/assets/AssetMeta.h"

#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "rabbet/util/Log.h"

namespace rb::assetmeta {

std::filesystem::path sidecarPath(const std::filesystem::path& assetPath) {
    std::filesystem::path meta = assetPath;
    meta += ".import";
    return meta;
}

Uuid loadOrCreateUuid(const std::filesystem::path& assetPath) {
    const std::filesystem::path meta = sidecarPath(assetPath);

    std::ifstream in(meta);
    if (in) {
        const nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
        if (!doc.is_discarded()) {
            const auto it = doc.find("uuid");
            if (it != doc.end() && it->is_string()) {
                const Uuid id = Uuid::fromString(it->get<std::string>());
                if (id.valid()) {
                    return id;
                }
            }
        }
        log::warn("asset meta: '{}' is unreadable; reassigning a uuid", meta.string());
    }

    const Uuid id = Uuid::generate();
    nlohmann::json doc;
    doc["version"] = 1;
    doc["uuid"] = id.toString();
    std::ofstream out(meta);
    if (out) {
        out << doc.dump(2) << '\n';
    } else {
        log::warn("asset meta: could not write sidecar '{}'", meta.string());
    }
    return id;
}

} // namespace rb::assetmeta
