#pragma once

#include <filesystem>

#include <nlohmann/json.hpp>

#include "rabbet/ecs/Entity.h"

namespace rb {

class Scene;
class ComponentRegistry;

namespace SceneSerializer {

[[nodiscard]] nlohmann::json toJson(Scene& scene, const ComponentRegistry& registry);

// Creates a new entity carrying copies of every registered component on `source`,
// by routing each through the registry's save/load hooks. Only serializable
// components are copied (live GL components are not registered), so the clone is
// independent and itself serializable. Returns the new entity.
[[nodiscard]] Entity duplicateEntity(Scene& scene, const ComponentRegistry& registry, Entity source);

// Appends the document's entities into `scene` without clearing it. loadFromFile
// clears first; call this directly only to intentionally merge into a live scene.
void fromJson(const nlohmann::json& doc, Scene& scene, const ComponentRegistry& registry);

[[nodiscard]] bool saveToFile(Scene& scene, const ComponentRegistry& registry,
                              const std::filesystem::path& path);
[[nodiscard]] bool loadFromFile(Scene& scene, const ComponentRegistry& registry,
                                const std::filesystem::path& path);

} // namespace SceneSerializer

} // namespace rb
