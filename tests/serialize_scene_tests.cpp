#include "rabbet/ecs/Scene.h"
#include "rabbet/scene/Camera.h"
#include "rabbet/scene/Light.h"
#include "rabbet/scene/Name.h"
#include "rabbet/scene/Transform.h"
#include "rabbet/serialize/BuiltinComponents.h"
#include "rabbet/serialize/ComponentRegistry.h"
#include "rabbet/serialize/SceneSerializer.h"
#include "tests/Test.h"

#include <filesystem>
#include <string>
#include <system_error>

namespace {

rb::ComponentRegistry makeRegistry() {
    rb::ComponentRegistry registry;
    rb::registerBuiltinComponents(registry);
    return registry;
}

rb::Entity spawnHero(rb::Scene& scene) {
    const rb::Entity e = scene.create();
    scene.add<rb::Name>(e, rb::Name{"hero"});
    rb::Transform t;
    t.position = {1.0f, 2.0f, 3.0f};
    t.scale = {2.0f, 2.0f, 2.0f};
    scene.add<rb::Transform>(e, t);
    return e;
}

} // namespace

static void registryExposesBuiltins() {
    const rb::ComponentRegistry registry = makeRegistry();
    CHECK(registry.entries().size() == 11u);
    CHECK(registry.find("Transform") != nullptr);
    CHECK(registry.find("Camera") != nullptr);
    CHECK(registry.find("Primitive") != nullptr);
    CHECK(registry.find("Missing") == nullptr);
}

static void roundTripPreservesComponents() {
    const rb::ComponentRegistry registry = makeRegistry();

    rb::Scene source;
    spawnHero(source);
    const rb::Entity sun = source.create();
    source.add<rb::DirectionalLight>(sun,
                                     rb::DirectionalLight{{0.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, 3.0f});

    const nlohmann::json doc = rb::SceneSerializer::toJson(source, registry);

    rb::Scene loaded;
    rb::SceneSerializer::fromJson(doc, loaded, registry);

    CHECK(loaded.aliveCount() == 2u);
    CHECK(loaded.count<rb::Transform>() == 1u);
    CHECK(loaded.count<rb::Name>() == 1u);
    CHECK(loaded.count<rb::DirectionalLight>() == 1u);

    bool foundHero = false;
    loaded.each<rb::Transform>([&](rb::Entity, rb::Transform& t) {
        foundHero = true;
        CHECK(t.position.x == 1.0f);
        CHECK(t.position.y == 2.0f);
        CHECK(t.position.z == 3.0f);
        CHECK(t.scale.x == 2.0f);
    });
    CHECK(foundHero);
}

static void saveLoadSaveIsIdempotent() {
    const rb::ComponentRegistry registry = makeRegistry();

    rb::Scene source;
    spawnHero(source);
    const rb::Entity lamp = source.create();
    source.add<rb::PointLight>(lamp, rb::PointLight{});
    const rb::Entity eye = source.create();
    source.add<rb::Camera>(eye, rb::Camera{});

    const std::string first = rb::SceneSerializer::toJson(source, registry).dump(2);

    rb::Scene loaded;
    rb::SceneSerializer::fromJson(nlohmann::json::parse(first), loaded, registry);
    const std::string second = rb::SceneSerializer::toJson(loaded, registry).dump(2);

    CHECK(first == second);
}

static void saveAndLoadFile() {
    const rb::ComponentRegistry registry = makeRegistry();

    rb::Scene source;
    spawnHero(source);

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "rabbet_scene_roundtrip.scene.json";

    CHECK(rb::SceneSerializer::saveToFile(source, registry, path));

    rb::Scene loaded;
    CHECK(rb::SceneSerializer::loadFromFile(loaded, registry, path));
    CHECK(loaded.count<rb::Transform>() == 1u);
    CHECK(loaded.count<rb::Name>() == 1u);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

static void malformedDataLoadsGracefully() {
    const rb::ComponentRegistry registry = makeRegistry();
    const nlohmann::json doc = nlohmann::json::parse(R"({
      "version": 1,
      "entities": [
        { "id": 0, "components": { "Transform": { "position": [1.0, 2.0, 3.0] } } },
        { "id": 1, "components": { "Bogus": { "x": 1 } } },
        { "id": 2 },
        { "id": 3, "components": { "ModelRenderer": { "model": "not-a-uuid" } } }
      ]
    })");

    rb::Scene scene;
    rb::SceneSerializer::fromJson(doc, scene, registry); // must not throw
    CHECK(scene.aliveCount() == 4u);               // every record still became an entity
    CHECK(scene.count<rb::Transform>() == 0u);     // partial Transform skipped, not crashed
    CHECK(scene.count<rb::ModelRenderer>() == 0u); // malformed model uuid skipped
}

static void loadFromFileReplaces() {
    const rb::ComponentRegistry registry = makeRegistry();

    rb::Scene source;
    spawnHero(source);
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "rabbet_replace.scene.json";
    CHECK(rb::SceneSerializer::saveToFile(source, registry, path));

    rb::Scene target;
    spawnHero(target);
    spawnHero(target);
    CHECK(target.aliveCount() == 2u);

    CHECK(rb::SceneSerializer::loadFromFile(target, registry, path));
    CHECK(target.aliveCount() == 1u); // replaced, not appended
    CHECK(target.count<rb::Transform>() == 1u);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

int main() {
    registryExposesBuiltins();
    roundTripPreservesComponents();
    saveLoadSaveIsIdempotent();
    saveAndLoadFile();
    malformedDataLoadsGracefully();
    loadFromFileReplaces();
    return rbtest::summary("serialize_scene");
}
