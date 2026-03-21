#include "rabbet/assets/AssetManager.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/render/ModelAsset.h"
#include "rabbet/render/ModelRenderer.h"
#include "rabbet/scene/Camera.h"
#include "rabbet/scene/Light.h"
#include "rabbet/scene/Name.h"
#include "rabbet/serialize/BuiltinComponents.h"
#include "rabbet/serialize/ComponentRegistry.h"
#include "rabbet/serialize/SceneSerializer.h"
#include "tests/Test.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

// A whole scene authored as a JSON file (no C++ entity building) loads into live
// entities, and its model reference resolves through the asset manager.
static void loadsSceneAuthoredAsData() {
    const rb::Uuid modelId = rb::Uuid::generate();

    const std::string sceneText = std::string(R"({
  "version": 1,
  "entities": [
    { "id": 0, "components": {
        "Name": { "value": "camera" },
        "Transform": { "position": [0.0, 1.0, 4.0], "rotation": [1.0, 0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0] },
        "Camera": { "fovY": 1.0, "nearPlane": 0.1, "farPlane": 100.0 } } },
    { "id": 1, "components": {
        "Name": { "value": "sun" },
        "DirectionalLight": { "direction": [0.0, -1.0, -0.2], "color": [1.0, 1.0, 1.0], "intensity": 3.0 } } },
    { "id": 2, "components": {
        "Name": { "value": "prop" },
        "Transform": { "position": [0.0, 0.0, 0.0], "rotation": [1.0, 0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0] },
        "ModelRenderer": { "model": ")") +
                                  modelId.toString() + R"(" } } }
  ]
})";

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "rabbet_showcase.scene.json";
    {
        std::ofstream out(path);
        out << sceneText;
    }

    rb::AssetManager assets;
    assets.add<rb::ModelAsset>(rb::ModelAsset{}, modelId); // stands in for a loaded model file

    rb::ComponentRegistry registry;
    rb::registerBuiltinComponents(registry);

    rb::Scene scene;
    CHECK(rb::SceneSerializer::loadFromFile(scene, registry, path));
    CHECK(scene.aliveCount() == 3u);
    CHECK(scene.count<rb::Camera>() == 1u);
    CHECK(scene.count<rb::DirectionalLight>() == 1u);
    CHECK(scene.count<rb::ModelRenderer>() == 1u);
    CHECK(scene.count<rb::Name>() == 3u);

    bool resolved = false;
    scene.each<rb::ModelRenderer>([&](rb::Entity, rb::ModelRenderer& renderer) {
        CHECK(renderer.model == modelId);
        renderer.handle = assets.find<rb::ModelAsset>(renderer.model);
        resolved = renderer.handle.valid();
    });
    CHECK(resolved);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

int main() {
    loadsSceneAuthoredAsData();
    return rbtest::summary("assets_scene_file");
}
