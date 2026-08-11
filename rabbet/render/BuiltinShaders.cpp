#include "rabbet/render/BuiltinShaders.h"

#include <utility>

#include "rabbet/assets/AssetManager.h"
#include "rabbet/render/MaterialAsset.h"
#include "rabbet/render/ShaderAsset.h"
#include "rabbet/render/shaders/GlShaderSources.h"

namespace rb {

const std::string& builtinLitVertexSource() {
    static const std::string source = shaders::kLitVertex;
    return source;
}

const std::string& builtinPhongFragmentSource() {
    static const std::string source = shaders::kPhongFragment;
    return source;
}

const std::string& builtinPbrFragmentSource() {
    // The PBR and terrain fragments compose the same pbr_functions.glsl chunk so their
    // BRDFs cannot drift apart. Inside it, brdf() reads the global uRoughness and
    // uMetallic uniforms every user declares, and the NDF keeps its alpha squared term
    // (roughness to the fourth power) away from zero because an exactly zero roughness
    // would otherwise divide zero by zero when the half vector lines up with the normal.
    static const std::string source = shaders::kPbrFragment;
    return source;
}

const std::string& builtinTerrainVertexSource() {
    // Beyond the lit vertex outputs, terrain.vert hands the fragment the local height
    // normalized by the height scale, so elevation banding survives whatever transform
    // the entity carries.
    static const std::string source = shaders::kTerrainVertex;
    return source;
}

const std::string& builtinTerrainFragmentSource() {
    // terrain.frag weighs up to four albedo layers by height and slope bands or by an
    // RGBA splat map, then runs the shared light loop and honours the uHdrOutput seam
    // like the PBR fragment. The band and noise mechanics are commented in the file.
    static const std::string source = shaders::kTerrainFragment;
    return source;
}

const std::string& builtinWaterVertexSource() {
    // The surface mesh is one unit quad placed by uModel. Every wave lives in the
    // fragment stage, so four vertices cover any extent.
    static const std::string source = shaders::kWaterVertex;
    return source;
}

const std::string& builtinWaterFragmentSource() {
    // Waves are summed sine octaves with analytic slopes instead of normal map
    // assets, blended toward the scene's own skybox by fresnel, and the quad edge
    // fades out so a surface never stops in a hard line.
    static const std::string source = shaders::kWaterFragment;
    return source;
}

void registerDefaultRenderAssets(AssetManager& assets) {
    if (assets.find<ShaderAsset>(builtin::kPbrShader).valid()) {
        return; // already registered
    }

    ShaderAsset pbr;
    pbr.vertexSource = builtinLitVertexSource();
    pbr.fragmentSource = builtinPbrFragmentSource();
    assets.add<ShaderAsset>(std::move(pbr), builtin::kPbrShader);

    ShaderAsset phong;
    phong.vertexSource = builtinLitVertexSource();
    phong.fragmentSource = builtinPhongFragmentSource();
    assets.add<ShaderAsset>(std::move(phong), builtin::kPhongShader);

    MaterialAsset material;
    material.shader = builtin::kPbrShader;
    assets.add<MaterialAsset>(std::move(material), builtin::kDefaultMaterial);
}

} // namespace rb
