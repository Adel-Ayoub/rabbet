#include "rabbet/serialize/AssetCreate.h"

#include "rabbet/ecs/Scene.h"
#include "rabbet/serialize/AtomicWrite.h"
#include "rabbet/serialize/SceneSerializer.h"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace rb {
namespace {

// Keeps "<base>_999.material.json" comfortably inside every filesystem's 255-byte name limit.
constexpr std::size_t kMaxAssetBaseLength = 64;

const std::string kScriptTemplate = "fields = {}\n"
                                    "\n"
                                    "function on_start(self)\n"
                                    "end\n"
                                    "\n"
                                    "function on_update(self, dt)\n"
                                    "end\n";

const std::string kMaterialTemplate = "{\n"
                                      "  \"version\": 1,\n"
                                      "  \"shader\": \"\",\n"
                                      "  \"uniforms\": [],\n"
                                      "  \"textures\": []\n"
                                      "}\n";

} // namespace

std::string sanitizeAssetName(std::string name, const std::string& fallback) {
    for (char& c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        const bool ok = std::isalnum(u) != 0 || c == '-' || c == '_' || c == '.' || c == ' ';
        if (!ok) {
            c = '_';
        }
    }
    const auto keep = [](char c) { return c != ' ' && c != '.'; };
    name.erase(name.begin(), std::find_if(name.begin(), name.end(), keep));
    name.erase(std::find_if(name.rbegin(), name.rend(), keep).base(), name.end());
    if (name.size() > kMaxAssetBaseLength) {
        name.resize(kMaxAssetBaseLength);
        // The cut can land on a trailing dot/space, which Windows strips silently; re-trim.
        name.erase(std::find_if(name.rbegin(), name.rend(), keep).base(), name.end());
    }
    if (name.empty()) {
        name = fallback;
    }
    return name;
}

std::filesystem::path assetFilePath(const std::filesystem::path& dir, const std::string& name,
                                    const std::string& suffix, const std::string& fallback) {
    const std::string base = sanitizeAssetName(name, fallback);
    std::error_code ec;
    std::filesystem::path candidate = dir / (base + suffix);
    for (int i = 1; i < 1000 && std::filesystem::exists(candidate, ec); ++i) {
        candidate = dir / (base + "_" + std::to_string(i) + suffix);
    }
    if (std::filesystem::exists(candidate, ec)) {
        return {}; // 1000 same-named siblings: refuse rather than rename over the last candidate
    }
    return candidate;
}

std::filesystem::path createScriptAsset(const std::filesystem::path& dir,
                                        const std::string& name) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path path = assetFilePath(dir, name, ".lua", "Script");
    return writeFileAtomically(path, kScriptTemplate) ? path : std::filesystem::path{};
}

std::filesystem::path createMaterialAsset(const std::filesystem::path& dir,
                                          const std::string& name) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path path = assetFilePath(dir, name, ".material.json", "Material");
    return writeFileAtomically(path, kMaterialTemplate) ? path : std::filesystem::path{};
}

std::filesystem::path createSceneAsset(const std::filesystem::path& dir, const std::string& name,
                                       const ComponentRegistry& registry) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path path = assetFilePath(dir, name, ".scene.json", "Scene");
    // An empty scene through the real serializer: whatever shape the loader's gate expects,
    // this document has it, today and after any future format change.
    Scene empty;
    return SceneSerializer::saveToFile(empty, registry, path) ? path : std::filesystem::path{};
}

} // namespace rb
