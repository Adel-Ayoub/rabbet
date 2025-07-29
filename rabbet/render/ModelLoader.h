#pragma once

#include <filesystem>
#include <optional>

#include "rabbet/render/Model.h"

namespace rb {

[[nodiscard]] std::optional<Model> loadModel(const std::filesystem::path& path);

} // namespace rb
