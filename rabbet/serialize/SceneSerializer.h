#pragma once

#include <filesystem>

#include <nlohmann/json.hpp>

namespace rb {

class Scene;
class ComponentRegistry;

namespace SceneSerializer {

[[nodiscard]] nlohmann::json toJson(Scene& scene, const ComponentRegistry& registry);

// Appends the document's entities into `scene` without clearing it. loadFromFile
// clears first; call this directly only to intentionally merge into a live scene.
void fromJson(const nlohmann::json& doc, Scene& scene, const ComponentRegistry& registry);

[[nodiscard]] bool saveToFile(Scene& scene, const ComponentRegistry& registry,
                              const std::filesystem::path& path);
[[nodiscard]] bool loadFromFile(Scene& scene, const ComponentRegistry& registry,
                                const std::filesystem::path& path);

} // namespace SceneSerializer

} // namespace rb
