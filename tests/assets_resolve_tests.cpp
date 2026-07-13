#include "rabbet/assets/AssetManager.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/render/AssetResolveSystem.h"
#include "rabbet/render/ModelAsset.h"
#include "rabbet/render/ModelRenderer.h"
#include "tests/Test.h"

#include <filesystem>

static void resolvesKnownAndFlagsMissing() {
    rb::Runtime runtime;
    rb::AssetManager& assets = runtime.addResource<rb::AssetManager>();
    const rb::Uuid id = rb::Uuid::generate();
    const rb::AssetHandle<rb::ModelAsset> asset = assets.add<rb::ModelAsset>(rb::ModelAsset{}, id);

    const rb::Entity known = runtime.scene().create();
    runtime.scene().add<rb::ModelRenderer>(known, rb::ModelRenderer{id, {}});

    const rb::Entity missing = runtime.scene().create();
    runtime.scene().add<rb::ModelRenderer>(missing, rb::ModelRenderer{rb::Uuid::generate(), {}});

    rb::AssetResolveSystem system;
    system.onUpdate(runtime, 0.016f);

    CHECK(runtime.scene().get<rb::ModelRenderer>(known).handle == asset);
    CHECK(runtime.scene().get<rb::ModelRenderer>(known).handle.valid());
    CHECK(!runtime.scene().get<rb::ModelRenderer>(missing).handle.valid());
}

static void reresolvesAfterReload() {
    rb::Runtime runtime;
    rb::AssetManager& assets = runtime.addResource<rb::AssetManager>();
    const rb::Uuid id = rb::Uuid::generate();
    const rb::AssetHandle<rb::ModelAsset> first = assets.add<rb::ModelAsset>(rb::ModelAsset{}, id);

    const rb::Entity e = runtime.scene().create();
    runtime.scene().add<rb::ModelRenderer>(e, rb::ModelRenderer{id, {}});

    rb::AssetResolveSystem system;
    system.onUpdate(runtime, 0.016f);
    CHECK(runtime.scene().get<rb::ModelRenderer>(e).handle == first);

    // Reload the asset: the old handle goes stale, a fresh one takes the same uuid.
    assets.erase<rb::ModelAsset>(first);
    const rb::AssetHandle<rb::ModelAsset> second = assets.add<rb::ModelAsset>(rb::ModelAsset{}, id);
    CHECK(!(second == first));

    system.onUpdate(runtime, 0.016f);
    CHECK(runtime.scene().get<rb::ModelRenderer>(e).handle == second);
}

// Models lazy-import from their registered source like every other resolve. Headless the
// import can only be exercised up to the file read (a real model upload needs GL), so a
// registered-but-missing source must leave the handle invalid without crashing or
// littering ghost sidecars, and keep getting retried safely.
static void modelResolveLazyLoadsFromSource() {
    rb::Runtime runtime;
    rb::AssetManager& assets = runtime.addResource<rb::AssetManager>();
    const rb::Uuid id = rb::Uuid::generate();
    const std::filesystem::path missing =
        std::filesystem::temp_directory_path() / "rabbet_resolve_missing.gltf";
    assets.registerSource(id, missing, rb::AssetType::Model);

    const rb::Entity e = runtime.scene().create();
    runtime.scene().add<rb::ModelRenderer>(e, rb::ModelRenderer{id, {}});

    rb::AssetResolveSystem system;
    system.onUpdate(runtime, 0.016f);
    system.onUpdate(runtime, 0.016f);

    CHECK(!runtime.scene().get<rb::ModelRenderer>(e).handle.valid());
    CHECK(!std::filesystem::exists(std::filesystem::path(missing.string() + ".import")));
}

int main() {
    resolvesKnownAndFlagsMissing();
    reresolvesAfterReload();
    modelResolveLazyLoadsFromSource();
    return rbtest::summary("assets_resolve");
}
