#include "rabbet/assets/AssetManager.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/render/ModelAsset.h"
#include "rabbet/render/ModelRenderer.h"
#include "rabbet/serialize/BuiltinComponents.h"
#include "rabbet/serialize/ComponentRegistry.h"
#include "rabbet/serialize/SceneSerializer.h"
#include "tests/Test.h"

// An empty ModelAsset needs no GL context, so the full reference -> serialize ->
// resolve loop can be exercised headlessly.
static void modelRendererRefSurvivesAndResolves() {
    rb::AssetManager assets;
    const rb::Uuid modelId = rb::Uuid::generate();
    const rb::AssetHandle<rb::ModelAsset> asset =
        assets.add<rb::ModelAsset>(rb::ModelAsset{}, modelId);
    CHECK(asset.valid());

    rb::ComponentRegistry registry;
    rb::registerBuiltinComponents(registry);
    CHECK(registry.find("ModelRenderer") != nullptr);

    rb::Scene source;
    const rb::Entity e = source.create();
    source.add<rb::ModelRenderer>(e, rb::ModelRenderer{modelId, {}});

    const nlohmann::json doc = rb::SceneSerializer::toJson(source, registry);

    rb::Scene loaded;
    rb::SceneSerializer::fromJson(doc, loaded, registry);
    CHECK(loaded.count<rb::ModelRenderer>() == 1u);

    bool visited = false;
    loaded.each<rb::ModelRenderer>([&](rb::Entity, rb::ModelRenderer& renderer) {
        visited = true;
        CHECK(renderer.model == modelId);
        CHECK(!renderer.handle.valid());
        renderer.handle = assets.find<rb::ModelAsset>(renderer.model);
        CHECK(renderer.handle == asset);
        CHECK(assets.get<rb::ModelAsset>(renderer.handle) != nullptr);
    });
    CHECK(visited);
}

int main() {
    modelRendererRefSurvivesAndResolves();
    return rbtest::summary("assets_render");
}
