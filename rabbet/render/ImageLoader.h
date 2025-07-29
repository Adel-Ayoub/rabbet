#pragma once

#include <filesystem>
#include <optional>

#include "rabbet/render/Image.h"

namespace rb {

[[nodiscard]] std::optional<Image> loadImage(const std::filesystem::path& path,
                                             bool flipVertically = true);

} // namespace rb
