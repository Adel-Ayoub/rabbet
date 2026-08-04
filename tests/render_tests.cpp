#include "rabbet/assets/AssetManager.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/render/Geometry.h"
#include "rabbet/render/ImageLoader.h"
#include "rabbet/render/MaterialAsset.h"
#include "rabbet/render/MaterialAssetResolveSystem.h"
#include "rabbet/render/MaterialComponent.h"
#include "rabbet/render/MaterialImport.h"
#include "rabbet/render/ModelLoader.h"
#include "rabbet/render/PostProcess.h"
#include "rabbet/render/ShaderAsset.h"
#include "rabbet/render/ShaderImport.h"
#include "rabbet/render/ShaderUniform.h"
#include "rabbet/render/Sky.h"
#include "rabbet/render/Tonemap.h"
#include "rabbet/render/WaterComponent.h"
#include "rabbet/serialize/BuiltinComponents.h"
#include "rabbet/serialize/ComponentRegistry.h"
#include "rabbet/serialize/SceneSerializer.h"

#include "tests/Test.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <glad/glad.h>
#include <ios>
#include <limits>
#include <optional>
#include <string>
#include <system_error>

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

static void sphereVerticesLieOnRadius() {
    const rb::MeshData sphere = rb::geometry::sphere(8, 16);
    CHECK(sphere.vertices.size() == 9u * 17u);
    CHECK(sphere.indices.size() == 8u * 16u * 6u);
    bool onSurface = true;
    for (const rb::Vertex& v : sphere.vertices) {
        const float radius = std::sqrt(v.position.x * v.position.x + v.position.y * v.position.y +
                                       v.position.z * v.position.z);
        onSurface = onSurface && std::fabs(radius - 0.5f) <= 1.0e-4f;
    }
    CHECK(onSurface);
}

void renderGeometrySuite() {
    primitiveCounts();
    cubeIndicesAreInRange();
    quadIsUnitCentered();
    sphereVerticesLieOnRadius();
}

namespace {

std::filesystem::path writeTempPpm() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "rabbet_test_image.ppm";
    std::ofstream out(path, std::ios::binary);
    out << "P6\n2 2\n255\n";
    const unsigned char pixels[] = {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255};
    out.write(reinterpret_cast<const char*>(pixels), static_cast<std::streamsize>(sizeof(pixels)));
    return path;
}

std::filesystem::path writeTempObj() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "rabbet_test_triangle.obj";
    std::ofstream out(path);
    out << "v 0 0 0\n"
           "v 1 0 0\n"
           "v 0 1 0\n"
           "vn 0 0 1\n"
           "vt 0 0\n"
           "vt 1 0\n"
           "vt 0 1\n"
           "f 1/1/1 2/2/1 3/3/1\n";
    return path;
}

} // namespace

static void loadsPpm() {
    const std::filesystem::path path = writeTempPpm();
    const std::optional<rb::Image> image = rb::loadImage(path, false);
    std::filesystem::remove(path);

    CHECK(image.has_value());
    if (image) {
        CHECK(image->width == 2);
        CHECK(image->height == 2);
        CHECK(image->channels == 3);
        CHECK(image->pixels.size() == 12u);
    }
}

static void loadsTriangleObj() {
    const std::filesystem::path path = writeTempObj();
    const std::optional<rb::Model> model = rb::loadModel(path);
    std::filesystem::remove(path);

    CHECK(model.has_value());
    if (model) {
        CHECK(model->meshes.size() == 1u);
        CHECK(model->meshes.front().data.vertices.size() == 3u);
        CHECK(model->meshes.front().data.indices.size() == 3u);
        CHECK(!model->materials.empty());
    }
}

static void missingFilesReturnNullopt() {
    CHECK(!rb::loadImage("/no/such/rabbet_missing_image.ppm").has_value());
    CHECK(!rb::loadModel("/no/such/rabbet_missing_model.obj").has_value());
}

void renderLoaderSuite() {
    loadsPpm();
    loadsTriangleObj();
    missingFilesReturnNullopt();
}

namespace {

bool inUnitRange(const glm::vec3& c) {
    return c.x >= 0.0f && c.x <= 1.0f && c.y >= 0.0f && c.y <= 1.0f && c.z >= 0.0f && c.z <= 1.0f;
}

} // namespace

static void colorsStayInRange() {
    CHECK(inUnitRange(rb::skyColor(glm::vec3(0.0f, 1.0f, 0.0f))));
    CHECK(inUnitRange(rb::skyColor(glm::vec3(0.0f, -1.0f, 0.0f))));
    CHECK(inUnitRange(rb::skyColor(glm::vec3(1.0f, 0.0f, 0.0f))));
}

static void zenithIsBlueAndBrighterThanGround() {
    const glm::vec3 up = rb::skyColor(glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 down = rb::skyColor(glm::vec3(0.0f, -1.0f, 0.0f));
    CHECK(up.z > up.x);
    CHECK(up.z > down.z);
}

static void sunDirectionIsBright() {
    const glm::vec3 sun = rb::skyColor(glm::vec3(0.5f, 0.8f, 0.4f));
    const glm::vec3 away = rb::skyColor(glm::vec3(-0.5f, 0.8f, -0.4f));
    CHECK(sun.x + sun.y + sun.z > away.x + away.y + away.z);
}

void renderSkySuite() {
    colorsStayInRange();
    zenithIsBlueAndBrighterThanGround();
    sunDirectionIsBright();
}

namespace {

bool approx(float a, float b, float eps = 1.0e-4f) { return std::fabs(a - b) <= eps; }

// The surface takes its world POSITION (including a parent's contribution) but never a parent's
// rotation or scale: water stays horizontal and only `extent` sizes it.
void modelTakesPositionOnly() {
    glm::mat4 world = glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, -2.0f, 7.0f));
    world = glm::rotate(world, glm::radians(40.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    world = glm::scale(world, glm::vec3(5.0f));

    const glm::mat4 model = rb::waterSurfaceModel(world, glm::vec2(10.0f, 4.0f));
    CHECK(approx(model[3][0], 3.0f));
    CHECK(approx(model[3][1], -2.0f));
    CHECK(approx(model[3][2], 7.0f));

    // Extent maps x -> world X and y -> world Z; a swapped axis would break a non-square lake.
    CHECK(approx(model[0][0], 10.0f));
    CHECK(approx(model[2][2], 4.0f));
    CHECK(approx(model[1][1], 1.0f)); // height is never scaled

    // The parent's 40-degree tilt and 5x scale are gone: the basis stays axis-aligned.
    CHECK(approx(model[0][1], 0.0f));
    CHECK(approx(model[1][2], 0.0f));
    CHECK(approx(model[2][1], 0.0f));
}

// Serialized junk must never reach the rasterizer: a NaN survives std::clamp (every comparison
// is false), so the guard has to test finiteness first.
void modelRefusesNonFiniteInput() {
    const glm::mat4 world = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 3.0f));
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    const glm::mat4 a = rb::waterSurfaceModel(world, glm::vec2(nan, 4.0f));
    CHECK(std::isfinite(a[0][0]));
    CHECK(approx(a[2][2], 4.0f)); // the healthy axis is untouched

    const glm::mat4 b = rb::waterSurfaceModel(world, glm::vec2(inf, inf));
    CHECK(std::isfinite(b[0][0]));
    CHECK(std::isfinite(b[2][2]));

    glm::mat4 nanWorld = world;
    nanWorld[3][1] = nan;
    const glm::mat4 c = rb::waterSurfaceModel(nanWorld, glm::vec2(2.0f, 2.0f));
    CHECK(std::isfinite(c[3][1]));

    // Zero and negative extents collapse to a minimum rather than a degenerate quad.
    const glm::mat4 d = rb::waterSurfaceModel(world, glm::vec2(0.0f, -8.0f));
    CHECK(d[0][0] > 0.0f);
    CHECK(d[2][2] > 0.0f);
}

// The same finiteness discipline for the shader-bound floats, which would otherwise paint NaN
// into the scene target and spread it through the bloom chain.
void sanitizeReplacesNonFiniteFields() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    rb::WaterComponent w;
    w.waveTileScale = nan;
    w.waveStrength = std::numeric_limits<float>::infinity();
    w.waveSpeed = -nan;
    w.smoothness = 40.0f; // finite but out of range
    w.deepColor = glm::vec4(nan, 0.5f, 0.5f, 1.0f);
    rb::sanitizeWater(w);

    CHECK(std::isfinite(w.waveTileScale));
    CHECK(std::isfinite(w.waveStrength));
    CHECK(std::isfinite(w.waveSpeed));
    CHECK(approx(w.smoothness, 1.0f)); // clamped into 0..1
    CHECK(std::isfinite(w.deepColor.r));
    CHECK(approx(w.deepColor.g, 0.5f)); // healthy channels survive

    rb::WaterComponent healthy;
    healthy.waveSpeed = 2.5f;
    healthy.smoothness = 0.25f;
    rb::sanitizeWater(healthy);
    CHECK(approx(healthy.waveSpeed, 2.5f)); // a sane component is left alone
    CHECK(approx(healthy.smoothness, 0.25f));
    CHECK(approx(healthy.extent.x, 30.0f));
}

} // namespace

void renderWaterSuite() {
    modelTakesPositionOnly();
    modelRefusesNonFiniteInput();
    sanitizeReplacesNonFiniteFields();
}

namespace {

// Exposure scales linearly in stops: +1 EV doubles, -1 EV halves, 0 EV is identity.
void exposureScalesInStops() {
    const glm::vec3 c(0.4f, 0.6f, 0.8f);
    CHECK(approx(rb::applyExposure(c, 0.0f).r, 0.4f));
    CHECK(approx(rb::applyExposure(c, 1.0f).g, 1.2f));
    CHECK(approx(rb::applyExposure(c, -1.0f).b, 0.4f));
}

void luminanceUsesRec709Weights() {
    CHECK(approx(rb::luminance(glm::vec3(1.0f)), 1.0f));      // weights sum to 1
    CHECK(approx(rb::luminance(glm::vec3(0.0f)), 0.0f));
    CHECK(approx(rb::luminance(glm::vec3(0.0f, 1.0f, 0.0f)), 0.7152f)); // green dominates
    CHECK(rb::luminance(glm::vec3(0, 1, 0)) > rb::luminance(glm::vec3(1, 0, 0)));
}

// ACES maps 0 -> 0, is monotonic, and saturates toward (but not past) 1.
void acesIsBlackPreservingAndBounded() {
    CHECK(approx(rb::tonemapAces(glm::vec3(0.0f)).r, 0.0f));
    const float low = rb::tonemapAces(glm::vec3(0.2f)).r;
    const float mid = rb::tonemapAces(glm::vec3(1.0f)).r;
    const float high = rb::tonemapAces(glm::vec3(8.0f)).r;
    CHECK(low < mid);
    CHECK(mid < high);
    CHECK(high <= 1.0f);
    CHECK(high > 0.9f); // bright HDR approaches white
}

void reinhardMatchesClosedForm() {
    CHECK(approx(rb::tonemapReinhard(glm::vec3(0.0f)).r, 0.0f));
    CHECK(approx(rb::tonemapReinhard(glm::vec3(1.0f)).r, 0.5f)); // x/(x+1)
    CHECK(approx(rb::tonemapReinhard(glm::vec3(3.0f)).r, 0.75f));
    CHECK(rb::tonemapReinhard(glm::vec3(1000.0f)).r < 1.0f);
}

void filmicIsBlackPreservingAndBounded() {
    CHECK(approx(rb::tonemapFilmic(glm::vec3(0.0f)).r, 0.0f, 1.0e-3f));
    const float mid = rb::tonemapFilmic(glm::vec3(1.0f)).r;
    const float high = rb::tonemapFilmic(glm::vec3(11.2f)).r;
    CHECK(mid < high);
    CHECK(high <= 1.0f);
    CHECK(high > 0.95f); // the curve's white point
}

void tonemapDispatchSelectsOperator() {
    const glm::vec3 c(1.0f);
    CHECK(approx(rb::tonemap(rb::TonemapOperator::Reinhard, c).r, 0.5f));
    CHECK(approx(rb::tonemap(rb::TonemapOperator::ACES, c).r, rb::tonemapAces(c).r));
    CHECK(approx(rb::tonemap(rb::TonemapOperator::Filmic, c).r, rb::tonemapFilmic(c).r));
}

// Gamma encode then decode round-trips; gamma 1.0 is identity.
void gammaRoundTrips() {
    const glm::vec3 c(0.25f, 0.5f, 0.75f);
    CHECK(approx(rb::gammaCorrect(c, 1.0f).r, 0.25f));
    const glm::vec3 encoded = rb::gammaCorrect(c, 2.2f);
    CHECK(encoded.r > c.r); // 1/2.2 power brightens midtones
    const glm::vec3 decoded = glm::pow(encoded, glm::vec3(2.2f));
    CHECK(approx(decoded.r, 0.25f));
    CHECK(approx(decoded.b, 0.75f));
}

// Contrast pivots around 0.18: identity at 1.0, pushes away from mid-grey above it.
void contrastPivotsAtMidGrey() {
    CHECK(approx(rb::applyContrast(glm::vec3(0.7f), 1.0f).r, 0.7f));
    CHECK(approx(rb::applyContrast(glm::vec3(0.18f), 2.0f).r, 0.18f)); // mid-grey is fixed
    CHECK(rb::applyContrast(glm::vec3(0.5f), 1.5f).r > 0.5f);          // above mid pushed up
    CHECK(rb::applyContrast(glm::vec3(0.05f), 1.5f).r < 0.05f);        // below mid pushed down
}

// Saturation 1.0 is identity; 0.0 collapses to luma grey; values agree on the grey axis.
void saturationInterpolatesToLuma() {
    const glm::vec3 c(0.8f, 0.2f, 0.4f);
    const glm::vec3 ident = rb::applySaturation(c, 1.0f);
    CHECK(approx(ident.r, 0.8f));
    CHECK(approx(ident.g, 0.2f));
    const glm::vec3 grey = rb::applySaturation(c, 0.0f);
    const float l = rb::luminance(c);
    CHECK(approx(grey.r, l));
    CHECK(approx(grey.g, l));
    CHECK(approx(grey.b, l));
}

// activePostProcess returns the first ENABLED instance, skipping disabled ones; none -> nullptr.
void activePostProcessPicksFirstEnabled() {
    rb::Scene scene;
    const rb::Entity a = scene.create();
    rb::PostProcess off;
    off.enabled = false;
    scene.add<rb::PostProcess>(a, off);
    CHECK(rb::activePostProcess(scene) == nullptr); // a disabled one is inert

    const rb::Entity b = scene.create();
    rb::PostProcess on;
    on.enabled = true;
    on.exposure = 1.5f;
    scene.add<rb::PostProcess>(b, on);
    rb::PostProcess* active = rb::activePostProcess(scene);
    CHECK(active != nullptr);
    if (active != nullptr) {
        CHECK(approx(active->exposure, 1.5f));
    }
}

// The PostProcess component round-trips through the scene serializer with every field intact.
void postProcessRoundTrips() {
    rb::ComponentRegistry registry;
    rb::registerBuiltinComponents(registry);

    rb::Scene source;
    const rb::Entity e = source.create();
    rb::PostProcess pp;
    pp.enabled = true;
    pp.tonemap = rb::TonemapOperator::Filmic;
    pp.exposure = 0.75f;
    pp.gamma = 2.4f;
    pp.bloom = true;
    pp.bloomThreshold = 1.3f;
    pp.bloomKnee = 0.6f;
    pp.bloomIntensity = 0.09f;
    pp.contrast = 1.15f;
    pp.saturation = 0.85f;
    pp.vignette = 0.3f;
    pp.fxaa = false;
    source.add<rb::PostProcess>(e, pp);

    const nlohmann::json doc = rb::SceneSerializer::toJson(source, registry);
    rb::Scene loaded;
    rb::SceneSerializer::fromJson(doc, loaded, registry);

    CHECK(loaded.count<rb::PostProcess>() == 1u);
    loaded.each<rb::PostProcess>([&](rb::Entity, rb::PostProcess& l) {
        CHECK(l.enabled == true);
        CHECK(l.tonemap == rb::TonemapOperator::Filmic);
        CHECK(approx(l.exposure, 0.75f));
        CHECK(approx(l.gamma, 2.4f));
        CHECK(approx(l.bloomThreshold, 1.3f));
        CHECK(approx(l.bloomIntensity, 0.09f));
        CHECK(approx(l.contrast, 1.15f));
        CHECK(approx(l.saturation, 0.85f));
        CHECK(approx(l.vignette, 0.3f));
        CHECK(l.fxaa == false);
    });
}

} // namespace

void tonemapSuite() {
    exposureScalesInStops();
    luminanceUsesRec709Weights();
    acesIsBlackPreservingAndBounded();
    reinhardMatchesClosedForm();
    filmicIsBlackPreservingAndBounded();
    tonemapDispatchSelectsOperator();
    gammaRoundTrips();
    contrastPivotsAtMidGrey();
    saturationInterpolatesToLuma();
    activePostProcessPicksFirstEnabled();
    postProcessRoundTrips();
}

namespace {

// The shader file format splits into a vertex stage (between #VERTEX and #FRAGMENT) and a
// fragment stage (after #FRAGMENT); the markers themselves never reach the GLSL.
static void shaderSourceParsing() {
    const std::string text =
        "#VERTEX\nvoid vmain() {}\n#FRAGMENT\nvoid fmain() {}\n";
    const std::optional<rb::ShaderSourceParts> parts = rb::parseShaderSource(text);
    CHECK(parts.has_value());
    if (parts.has_value()) {
        CHECK(parts->vertex.find("vmain") != std::string::npos);
        CHECK(parts->vertex.find("#VERTEX") == std::string::npos);
        CHECK(parts->vertex.find("fmain") == std::string::npos); // fragment did not bleed in
        CHECK(parts->fragment.find("fmain") != std::string::npos);
        CHECK(parts->fragment.find("#FRAGMENT") == std::string::npos);
    }

    CHECK(!rb::parseShaderSource("#VERTEX\nonly vertex, no fragment\n").has_value());
    CHECK(!rb::parseShaderSource("no markers at all").has_value());
    CHECK(!rb::parseShaderSource("#VERTEX\n\n#FRAGMENT\n").has_value()); // empty stages
    CHECK(!rb::parseShaderSource("#FRAGMENT\nx\n#VERTEX\ny\n").has_value()); // out of order
}

// glGetActiveUniform reports a GL type enum; the reflector maps it to a UniformType. The
// mapping is pure (no context), so the GL constants can be passed directly.
static void glTypeMapping() {
    CHECK(rb::uniformTypeFromGlType(GL_FLOAT) == rb::UniformType::Float);
    CHECK(rb::uniformTypeFromGlType(GL_FLOAT_VEC2) == rb::UniformType::Vec2);
    CHECK(rb::uniformTypeFromGlType(GL_FLOAT_VEC3) == rb::UniformType::Vec3);
    CHECK(rb::uniformTypeFromGlType(GL_FLOAT_VEC4) == rb::UniformType::Vec4);
    CHECK(rb::uniformTypeFromGlType(GL_INT) == rb::UniformType::Int);
    CHECK(rb::uniformTypeFromGlType(GL_BOOL) == rb::UniformType::Bool);
    CHECK(rb::uniformTypeFromGlType(GL_FLOAT_MAT4) == rb::UniformType::Mat4);
    CHECK(rb::uniformTypeFromGlType(GL_SAMPLER_2D) == rb::UniformType::Sampler2D);
    CHECK(rb::uniformTypeFromGlType(GL_FLOAT_MAT2x3) == rb::UniformType::Unknown); // unsupported
}

// Engine-driven uniforms (transforms, camera, lights, shadows) are filtered out of the
// material-editable set; light arrays match by base name, so a custom shader's own
// uPoint*/uSpot* uniforms stay inspectable.
static void engineUniformClassification() {
    CHECK(rb::isEngineUniform("uModel"));
    CHECK(rb::isEngineUniform("uViewProjection"));
    CHECK(rb::isEngineUniform("uDirectionalColor"));
    CHECK(rb::isEngineUniform("uPointPosition"));
    CHECK(rb::isEngineUniform("uPointPosition[3]")); // reflected array element
    CHECK(rb::isEngineUniform("uSpotCone[0]"));
    CHECK(rb::isEngineUniform("uShadowMap"));
    CHECK(!rb::isEngineUniform("uBaseColor"));
    CHECK(!rb::isEngineUniform("uMetallic"));
    CHECK(!rb::isEngineUniform("uAlbedoTex"));
    CHECK(!rb::isEngineUniform("uTint"));
    CHECK(!rb::isEngineUniform("uPointSize"));      // custom, not the engine's light family
    CHECK(!rb::isEngineUniform("uSpotlightTint")); // custom, must reach the inspector
}

// A material round-trips through its .material.json form: shader uuid, every uniform value
// kind, and texture bindings survive write -> read unchanged.
static void materialJsonRoundTrip() {
    rb::MaterialAsset source;
    source.shader = rb::Uuid::generate();

    rb::MaterialUniform baseColor;
    baseColor.name = "uBaseColor";
    baseColor.type = rb::UniformType::Vec3;
    baseColor.vec = {0.5f, 0.25f, 0.75f, 0.0f};
    source.uniforms.push_back(baseColor);

    rb::MaterialUniform metallic;
    metallic.name = "uMetallic";
    metallic.type = rb::UniformType::Float;
    metallic.vec.x = 0.25f;
    source.uniforms.push_back(metallic);

    rb::MaterialUniform layer;
    layer.name = "uLayer";
    layer.type = rb::UniformType::Int;
    layer.integer = 3;
    source.uniforms.push_back(layer);

    rb::MaterialUniform unlit;
    unlit.name = "uUnlit";
    unlit.type = rb::UniformType::Bool;
    unlit.boolean = true;
    source.uniforms.push_back(unlit);

    rb::MaterialTexture albedo;
    albedo.name = "uAlbedoTex";
    albedo.texture = rb::Uuid::generate();
    source.textures.push_back(albedo);

    rb::MaterialAsset loaded;
    rb::materialFromJson(rb::materialToJson(source), loaded);

    CHECK(loaded.shader == source.shader);
    CHECK(loaded.uniforms.size() == 4u);
    CHECK(loaded.textures.size() == 1u);
    if (loaded.uniforms.size() == 4u) {
        CHECK(loaded.uniforms[0].name == "uBaseColor");
        CHECK(loaded.uniforms[0].type == rb::UniformType::Vec3);
        CHECK(loaded.uniforms[0].vec.x == 0.5f);
        CHECK(loaded.uniforms[0].vec.y == 0.25f);
        CHECK(loaded.uniforms[0].vec.z == 0.75f);
        CHECK(loaded.uniforms[1].vec.x == 0.25f);
        CHECK(loaded.uniforms[2].integer == 3);
        CHECK(loaded.uniforms[3].boolean == true);
    }
    if (loaded.textures.size() == 1u) {
        CHECK(loaded.textures[0].name == "uAlbedoTex");
        CHECK(loaded.textures[0].texture == albedo.texture);
    }
    // Handles are runtime-only and never serialised.
    CHECK(!loaded.shaderHandle.valid());
}

// The resolve system links a MaterialComponent's uuid to its asset handle and cascades into the
// material to link its shader handle; an unknown material stays unresolved.
static void resolveLinksMaterialAndShader() {
    rb::Runtime runtime;
    rb::AssetManager& assets = runtime.addResource<rb::AssetManager>();

    const rb::Uuid shaderId = rb::Uuid::generate();
    const rb::AssetHandle<rb::ShaderAsset> shader =
        assets.add<rb::ShaderAsset>(rb::ShaderAsset{}, shaderId);

    const rb::Uuid materialId = rb::Uuid::generate();
    rb::MaterialAsset material;
    material.shader = shaderId;
    const rb::AssetHandle<rb::MaterialAsset> materialHandle =
        assets.add<rb::MaterialAsset>(std::move(material), materialId);

    const rb::Entity known = runtime.scene().create();
    runtime.scene().add<rb::MaterialComponent>(known, rb::MaterialComponent{materialId, {}});

    const rb::Entity missing = runtime.scene().create();
    runtime.scene().add<rb::MaterialComponent>(missing,
                                               rb::MaterialComponent{rb::Uuid::generate(), {}});

    rb::MaterialAssetResolveSystem system;
    system.onUpdate(runtime, 0.016f);

    CHECK(runtime.scene().get<rb::MaterialComponent>(known).handle == materialHandle);
    CHECK(runtime.scene().get<rb::MaterialComponent>(known).handle.valid());
    if (rb::MaterialAsset* resolved = assets.get<rb::MaterialAsset>(materialHandle)) {
        CHECK(resolved->shaderHandle == shader);
        CHECK(resolved->shaderHandle.valid());
    }
    CHECK(!runtime.scene().get<rb::MaterialComponent>(missing).handle.valid());
}

// A MaterialComponent's uuid reference survives a scene save/load through the registry, and the
// runtime handle is never serialised (it re-resolves on load).
static void materialComponentSceneRoundTrip() {
    rb::ComponentRegistry registry;
    rb::registerBuiltinComponents(registry);

    rb::Scene source;
    const rb::Entity e = source.create();
    const rb::Uuid id = rb::Uuid::generate();
    source.add<rb::MaterialComponent>(e, rb::MaterialComponent{id, {}});

    const nlohmann::json doc = rb::SceneSerializer::toJson(source, registry);
    rb::Scene loaded;
    rb::SceneSerializer::fromJson(doc, loaded, registry);

    CHECK(loaded.count<rb::MaterialComponent>() == 1u);
    bool found = false;
    loaded.each<rb::MaterialComponent>([&](rb::Entity, rb::MaterialComponent& material) {
        found = true;
        CHECK(material.material == id);
        CHECK(!material.handle.valid());
    });
    CHECK(found);
}

} // namespace

// Inspector edits persist only if saveMaterialAsset really writes the file and clears the
// dirty flag, and a fresh load recovers the same shader ref and uniform overrides.
static void materialSaveLoadFileRoundTrip() {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path path = fs::temp_directory_path() / "rabbet_material_save.material.json";
    fs::remove(path, ec);

    rb::AssetManager assets;
    const rb::Uuid shaderId = rb::Uuid::generate();
    rb::MaterialAsset mat;
    mat.shader = shaderId;
    mat.path = path;
    mat.dirty = true;
    rb::MaterialUniform tint;
    tint.name = "uTint";
    tint.type = rb::UniformType::Vec3;
    tint.vec = {0.5f, 0.25f, 0.75f, 0.0f};
    mat.uniforms.push_back(tint);
    const rb::AssetHandle<rb::MaterialAsset> handle =
        assets.add<rb::MaterialAsset>(std::move(mat), rb::Uuid::generate());

    CHECK(rb::saveMaterialAsset(assets, handle));
    CHECK(assets.get<rb::MaterialAsset>(handle)->dirty == false); // save clears dirty

    rb::AssetManager other; // fresh manager avoids the path cache
    rb::MaterialAsset* loaded = other.get<rb::MaterialAsset>(rb::loadMaterialAsset(other, path));
    CHECK(loaded != nullptr);
    if (loaded != nullptr) {
        CHECK(loaded->shader == shaderId);
        CHECK(loaded->uniforms.size() == 1u);
        if (loaded->uniforms.size() == 1u) {
            CHECK(loaded->uniforms[0].name == "uTint");
            CHECK(loaded->uniforms[0].vec.x == 0.5f);
        }
    }
    fs::remove(path, ec);
}

// A material with unsaved inspector edits (dirty) is not clobbered by a disk hot-reload; once
// saved/clean, the reload applies.
static void dirtyMaterialSurvivesReload() {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path path = fs::temp_directory_path() / "rabbet_material_reload.material.json";
    {
        std::ofstream out(path);
        out << R"({"version":1,"shader":"","uniforms":[],"textures":[]})" << '\n';
    }

    rb::AssetManager assets;
    rb::MaterialAsset mat;
    mat.path = path;
    mat.sourceTimestamp = 0; // force "changed on disk"
    mat.dirty = true;        // but there are unsaved edits
    const rb::AssetHandle<rb::MaterialAsset> handle =
        assets.add<rb::MaterialAsset>(std::move(mat), rb::Uuid::generate());

    CHECK(!rb::reloadMaterialIfChanged(assets, handle)); // dirty -> skipped, edits kept
    assets.get<rb::MaterialAsset>(handle)->dirty = false;
    CHECK(rb::reloadMaterialIfChanged(assets, handle)); // clean -> reloads from disk

    fs::remove(path, ec);
}

void materialSuite() {
    shaderSourceParsing();
    glTypeMapping();
    engineUniformClassification();
    materialJsonRoundTrip();
    resolveLinksMaterialAndShader();
    materialComponentSceneRoundTrip();
    materialSaveLoadFileRoundTrip();
    dirtyMaterialSurvivesReload();
}

int main() {
    renderGeometrySuite();
    renderLoaderSuite();
    renderSkySuite();
    renderWaterSuite();
    tonemapSuite();
    materialSuite();
    return rbtest::summary("render");
}
