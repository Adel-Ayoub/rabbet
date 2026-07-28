#pragma once

#include <filesystem>
#include <string>

namespace rb {

[[nodiscard]] bool writeFileAtomically(const std::filesystem::path& path, const std::string& text);

} // namespace rb
