#pragma once

#include "rabbet/render/MeshData.h"

namespace rb::geometry {

[[nodiscard]] MeshData triangle();
[[nodiscard]] MeshData quad();
[[nodiscard]] MeshData cube();
[[nodiscard]] MeshData sphere(unsigned int rings = 16, unsigned int sectors = 32);

} // namespace rb::geometry
