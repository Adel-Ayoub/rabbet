#pragma once

#include "rabbet/render/MeshData.h"
#include "rabbet/terrain/TerrainHeightField.h"

namespace rb {

// Builds a heightfield grid mesh from a normalized field: positions span [-size/2, size/2] in x/z
// about the origin with y = height * heightScale, uvs run 0..1 across the grid, and normals come
// from the height gradient (central difference, exact on a uniform grid). Pure CPU and GL-free, so
// it is the headless-testable unit. A field smaller than 2x2 yields an empty mesh.
[[nodiscard]] MeshData buildTerrainMesh(const TerrainHeightField& field, float size,
                                        float heightScale);

} // namespace rb
