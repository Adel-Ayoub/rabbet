#pragma once

#include <filesystem>

#include <nlohmann/json.hpp>

namespace rb {

class Scene;
class ComponentRegistry;

namespace SceneSerializer {

[[nodiscard]] nlohmann::json toJson(Scene& scene, const ComponentRegistry& registry);
void fromJson(const nlohmann::json& doc, Scene& scene, const ComponentRegistry& registry);

[[nodiscard]] bool saveToFile(Scene& scene, const ComponentRegistry& registry,
                              const std::filesystem::path& path);
[[nodiscard]] bool loadFromFile(Scene& scene, const ComponentRegistry& registry,
                                const std::filesystem::path& path);

} // namespace SceneSerializer

} // namespace rb
