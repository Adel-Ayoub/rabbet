#include "rabbet/ecs/Scene.h"
#include "rabbet/render/Image.h"
#include "rabbet/serialize/BuiltinComponents.h"
#include "rabbet/serialize/ComponentRegistry.h"
#include "rabbet/serialize/SceneSerializer.h"
#include "rabbet/terrain/TerrainComponent.h"
#include "rabbet/terrain/TerrainHeightField.h"
#include "rabbet/terrain/TerrainMesh.h"
#include "tests/Test.h"

#include <glm/glm.hpp>

#include <cmath>
#include <cstddef>
#include <vector>

namespace {

bool approx(float a, float b, float eps = 1.0e-4f) {
    return std::fabs(a - b) <= eps;
}

rb::ComponentRegistry makeRegistry() {
    rb::ComponentRegistry registry;
    rb::registerBuiltinComponents(registry);
    return registry;
}

// A ramp rising linearly along +x at the given resolution: height(x,z) = x / (res-1).
rb::TerrainHeightField rampField(int res) {
    rb::TerrainHeightField field;
    field.resolution = res;
    field.heights.resize(static_cast<std::size_t>(res) * static_cast<std::size_t>(res));
    for (int z = 0; z < res; ++z) {
        for (int x = 0; x < res; ++x) {
            field.heights[static_cast<std::size_t>(z) * static_cast<std::size_t>(res) +
                          static_cast<std::size_t>(x)] =
                static_cast<float>(x) / static_cast<float>(res - 1);
        }
    }
    return field;
}

static void flatFieldIsZero() {
    const rb::TerrainHeightField field = rb::flatHeightField(8);
    CHECK(field.resolution == 8);
    CHECK(field.heights.size() == 64u);
    bool allZero = true;
    for (const float h : field.heights) {
        allZero = allZero && approx(h, 0.0f);
    }
    CHECK(allZero);
}

static void noiseIsDeterministicAndBounded() {
    const rb::TerrainHeightField a = rb::noiseHeightField(1337u, 32, 5, 2.5f, 2.0f, 0.5f);
    const rb::TerrainHeightField b = rb::noiseHeightField(1337u, 32, 5, 2.5f, 2.0f, 0.5f);
    const rb::TerrainHeightField c = rb::noiseHeightField(7u, 32, 5, 2.5f, 2.0f, 0.5f);

    CHECK(a.heights.size() == b.heights.size());
    bool identical = a.heights.size() == b.heights.size();
    for (std::size_t i = 0; i < a.heights.size() && identical; ++i) {
        identical = approx(a.heights[i], b.heights[i], 0.0f);
    }
    CHECK(identical); // same seed + params -> identical field, every run

    bool differs = false;
    for (std::size_t i = 0; i < a.heights.size() && i < c.heights.size(); ++i) {
        differs = differs || !approx(a.heights[i], c.heights[i], 1.0e-3f);
    }
    CHECK(differs); // a different seed produces a different field

    float lo = 1.0f;
    float hi = 0.0f;
    bool inRange = true;
    for (const float h : a.heights) {
        inRange = inRange && h >= -1.0e-4f && h <= 1.0f + 1.0e-4f;
        lo = std::fmin(lo, h);
        hi = std::fmax(hi, h);
    }
    CHECK(inRange);
    CHECK(approx(lo, 0.0f, 1.0e-3f)); // normalized to span the full [0,1]
    CHECK(approx(hi, 1.0f, 1.0e-3f));
}

static void imageFieldSamplesLuminance() {
    // A 2x2 single-channel image; at resolution 2 each grid corner maps exactly to one pixel.
    rb::Image image;
    image.width = 2;
    image.height = 2;
    image.channels = 1;
    image.pixels = {std::byte{0}, std::byte{85}, std::byte{170}, std::byte{255}};
    const rb::TerrainHeightField field = rb::imageHeightField(image, 2);
    CHECK(field.resolution == 2);
    CHECK(approx(field.at(0, 0), 0.0f));
    CHECK(approx(field.at(1, 0), 85.0f / 255.0f));
    CHECK(approx(field.at(0, 1), 170.0f / 255.0f));
    CHECK(approx(field.at(1, 1), 1.0f));
}

static void meshHasGridCountsAndBounds() {
    const int res = 5;
    const float size = 8.0f;
    const rb::MeshData mesh = rb::buildTerrainMesh(rb::flatHeightField(res), size, 3.0f);

    CHECK(mesh.vertices.size() == static_cast<std::size_t>(res * res));
    CHECK(mesh.indices.size() == static_cast<std::size_t>((res - 1) * (res - 1) * 6));

    float minX = 1.0e9f;
    float maxX = -1.0e9f;
    float minZ = 1.0e9f;
    float maxZ = -1.0e9f;
    for (const rb::Vertex& v : mesh.vertices) {
        minX = std::fmin(minX, v.position.x);
        maxX = std::fmax(maxX, v.position.x);
        minZ = std::fmin(minZ, v.position.z);
        maxZ = std::fmax(maxZ, v.position.z);
    }
    CHECK(approx(minX, -size * 0.5f)); // spans [-size/2, size/2] about the origin
    CHECK(approx(maxX, size * 0.5f));
    CHECK(approx(minZ, -size * 0.5f));
    CHECK(approx(maxZ, size * 0.5f));
}

static void meshSamplesHeightAtCells() {
    // A 3x3 field with a single raised centre cell -> the centre vertex sits at height*heightScale.
    rb::TerrainHeightField field = rb::flatHeightField(3);
    field.heights[4] = 0.5f; // centre (x=1, z=1)
    const rb::MeshData mesh = rb::buildTerrainMesh(field, 4.0f, 10.0f);
    CHECK(mesh.vertices.size() == 9u);
    CHECK(approx(mesh.vertices[4].position.y, 5.0f)); // 0.5 * 10.0
    CHECK(approx(mesh.vertices[4].position.x, 0.0f)); // centre column -> world x 0
    CHECK(approx(mesh.vertices[0].position.y, 0.0f));
}

static void flatMeshNormalsPointUp() {
    const rb::MeshData mesh = rb::buildTerrainMesh(rb::flatHeightField(4), 6.0f, 5.0f);
    bool allUp = true;
    for (const rb::Vertex& v : mesh.vertices) {
        allUp = allUp && approx(v.normal.x, 0.0f) && approx(v.normal.y, 1.0f) &&
                approx(v.normal.z, 0.0f);
    }
    CHECK(allUp); // a flat field yields exactly +Y everywhere
}

static void rampMeshNormalsTilt() {
    const rb::MeshData mesh = rb::buildTerrainMesh(rampField(5), 8.0f, 4.0f);
    // Interior vertex (x=2, z=2): the surface rises along +x, so its normal leans toward -x while
    // staying upward and unit length.
    const rb::Vertex& v = mesh.vertices[2 * 5 + 2];
    CHECK(v.normal.x < -0.01f);
    CHECK(v.normal.y > 0.0f);
    CHECK(approx(v.normal.z, 0.0f));
    CHECK(approx(glm::length(v.normal), 1.0f));
}

static void degenerateFieldYieldsEmptyMesh() {
    CHECK(rb::buildTerrainMesh(rb::flatHeightField(1), 4.0f, 2.0f).vertices.empty());
    CHECK(rb::buildTerrainMesh(rb::flatHeightField(0), 4.0f, 2.0f).indices.empty());
}

static void componentRoundTrips() {
    const rb::ComponentRegistry registry = makeRegistry();

    rb::Scene source;
    const rb::Entity e = source.create();
    rb::TerrainComponent terrain;
    terrain.size = 96.0f;
    terrain.resolution = 64;
    terrain.heightScale = 18.5f;
    terrain.source = rb::TerrainHeightSource::Heightmap;
    terrain.heightmap = rb::Uuid::fromString("0123456789abcdef0123456789abcdef");
    terrain.seed = 99u;
    terrain.octaves = 6;
    terrain.frequency = 3.25f;
    terrain.lacunarity = 2.1f;
    terrain.gain = 0.45f;
    terrain.layerCount = 2;
    terrain.layers[0].albedo = rb::Uuid::fromString("aaaaaaaaaaaaaaaabbbbbbbbbbbbbbbb");
    terrain.layers[0].tiling = 24.0f;
    terrain.layers[0].heightRange = {0.0f, 0.4f};
    terrain.layers[0].slopeRange = {0.0f, 0.5f};
    terrain.layers[0].sharpness = 0.2f;
    terrain.layers[1].albedo = rb::Uuid::fromString("ccccccccccccccccdddddddddddddddd");
    terrain.layers[1].tiling = 10.0f;
    terrain.layers[1].heightRange = {0.3f, 1.0f};
    terrain.layers[1].slopeRange = {0.2f, 1.0f};
    terrain.layers[1].sharpness = 0.15f;
    terrain.blend = rb::TerrainBlend::Splatmap;
    terrain.splat = rb::Uuid::fromString("eeeeeeeeeeeeeeeeffffffffffffffff");
    source.add<rb::TerrainComponent>(e, terrain);

    const nlohmann::json doc = rb::SceneSerializer::toJson(source, registry);
    rb::Scene loaded;
    rb::SceneSerializer::fromJson(doc, loaded, registry);

    CHECK(loaded.count<rb::TerrainComponent>() == 1u);
    rb::TerrainComponent* out = nullptr;
    loaded.each<rb::TerrainComponent>([&](rb::Entity, rb::TerrainComponent& t) { out = &t; });
    CHECK(out != nullptr);
    if (out == nullptr) {
        return;
    }
    CHECK(approx(out->size, 96.0f));
    CHECK(out->resolution == 64);
    CHECK(approx(out->heightScale, 18.5f));
    CHECK(out->source == rb::TerrainHeightSource::Heightmap);
    CHECK(out->heightmap == terrain.heightmap);
    CHECK(out->seed == 99u);
    CHECK(out->octaves == 6);
    CHECK(approx(out->frequency, 3.25f));
    CHECK(approx(out->lacunarity, 2.1f));
    CHECK(approx(out->gain, 0.45f));
    CHECK(out->layerCount == 2);
    CHECK(out->layers[0].albedo == terrain.layers[0].albedo);
    CHECK(approx(out->layers[0].tiling, 24.0f));
    CHECK(approx(out->layers[0].heightRange.y, 0.4f));
    CHECK(approx(out->layers[0].slopeRange.y, 0.5f));
    CHECK(approx(out->layers[0].sharpness, 0.2f));
    CHECK(out->layers[1].albedo == terrain.layers[1].albedo);
    CHECK(approx(out->layers[1].tiling, 10.0f));
    CHECK(out->blend == rb::TerrainBlend::Splatmap);
    CHECK(out->splat == terrain.splat);
    // Runtime handles never serialize: they come back invalid, to be re-resolved.
    CHECK(!out->layers[0].handle.valid());
    CHECK(!out->splatHandle.valid());
}

static void partialJsonUsesDefaults() {
    const rb::ComponentRegistry registry = makeRegistry();
    rb::Scene scene;
    const rb::ComponentRegistry::Entry* entry = registry.find("TerrainComponent");
    CHECK(entry != nullptr);
    if (entry == nullptr) {
        return;
    }
    // A near-empty object must load every field at its default (tolerant reads).
    const rb::Entity e = scene.create();
    const nlohmann::json partial = nlohmann::json::object({{"size", 12.0f}});
    entry->load(scene, e, partial);
    rb::TerrainComponent& t = scene.get<rb::TerrainComponent>(e);
    CHECK(approx(t.size, 12.0f));
    CHECK(t.resolution == 128);    // default
    CHECK(t.layerCount >= 1);      // always at least the base layer
    CHECK(t.source == rb::TerrainHeightSource::Noise);
}

} // namespace

int main() {
    flatFieldIsZero();
    noiseIsDeterministicAndBounded();
    imageFieldSamplesLuminance();
    meshHasGridCountsAndBounds();
    meshSamplesHeightAtCells();
    flatMeshNormalsPointUp();
    rampMeshNormalsTilt();
    degenerateFieldYieldsEmptyMesh();
    componentRoundTrips();
    partialJsonUsesDefaults();
    return rbtest::summary("terrain");
}
