#pragma once

#include "rabbet/render/MeshData.h"

namespace rb::geometry {

[[nodiscard]] MeshData triangle();
[[nodiscard]] MeshData quad();
[[nodiscard]] MeshData cube();

} // namespace rb::geometry
