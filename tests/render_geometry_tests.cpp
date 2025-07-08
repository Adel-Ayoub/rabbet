#include "rabbet/render/Geometry.h"
#include "tests/Test.h"

#include <cstdint>

static void primitiveCounts() {
    const rb::MeshData tri = rb::geometry::triangle();
    CHECK(tri.vertices.size() == 3u);
    CHECK(tri.indices.size() == 3u);

    const rb::MeshData quad = rb::geometry::quad();
    CHECK(quad.vertices.size() == 4u);
    CHECK(quad.indices.size() == 6u);

    const rb::MeshData cube = rb::geometry::cube();
    CHECK(cube.vertices.size() == 24u);
    CHECK(cube.indices.size() == 36u);
}

static void cubeIndicesAreInRange() {
    const rb::MeshData cube = rb::geometry::cube();
    bool inRange = true;
    for (const std::uint32_t index : cube.indices) {
        inRange = inRange && index < cube.vertices.size();
    }
    CHECK(inRange);
}

static void quadIsUnitCentered() {
    const rb::MeshData quad = rb::geometry::quad();
    bool bounded = true;
    for (const rb::Vertex& v : quad.vertices) {
        bounded = bounded && v.position.x >= -0.5f && v.position.x <= 0.5f && v.position.y >= -0.5f &&
                  v.position.y <= 0.5f;
    }
    CHECK(bounded);
}

int main() {
    primitiveCounts();
    cubeIndicesAreInRange();
    quadIsUnitCentered();
    return rbtest::summary("render_geometry");
}
