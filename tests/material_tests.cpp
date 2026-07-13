#include "rabbet/assets/AssetManager.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/render/MaterialAsset.h"
#include "rabbet/render/MaterialAssetResolveSystem.h"
#include "rabbet/render/MaterialComponent.h"
#include "rabbet/render/MaterialImport.h"
#include "rabbet/render/ShaderAsset.h"
#include "rabbet/render/ShaderImport.h"
#include "rabbet/render/ShaderUniform.h"
#include "rabbet/serialize/BuiltinComponents.h"
#include "rabbet/serialize/ComponentRegistry.h"
#include "rabbet/serialize/SceneSerializer.h"
#include "tests/Test.h"

#include <glad/glad.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>

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

// saveMaterialAsset writes the material to its file and clears the dirty flag; loading it back
// recovers the shader ref and uniform overrides — so inspector edits can be persisted.
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

int main() {
    shaderSourceParsing();
    glTypeMapping();
    engineUniformClassification();
    materialJsonRoundTrip();
    resolveLinksMaterialAndShader();
    materialComponentSceneRoundTrip();
    materialSaveLoadFileRoundTrip();
    dirtyMaterialSurvivesReload();
    return rbtest::summary("material");
}
