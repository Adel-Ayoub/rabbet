#include "rabbet/ecs/Scene.h"
#include "rabbet/scene/Camera.h"
#include "rabbet/scene/Hierarchy.h"
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

rb::Entity spawnNamed(rb::Scene& scene, const std::string& name) {
    const rb::Entity e = scene.create();
    scene.add<rb::Name>(e, rb::Name{name});
    return e;
}

rb::Entity firstNamed(rb::Scene& scene, const std::string& name) {
    rb::Entity found;
    scene.each<rb::Name>([&](rb::Entity e, rb::Name& n) {
        if (!found.valid() && n.value == name) {
            found = e;
        }
    });
    return found;
}

} // namespace

static void registryExposesBuiltins() {
    const rb::ComponentRegistry registry = makeRegistry();
    CHECK(registry.entries().size() == 18u);
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

static void parentLinksRoundTrip() {
    const rb::ComponentRegistry registry = makeRegistry();

    rb::Scene source;
    const rb::Entity root = spawnNamed(source, "root");
    const rb::Entity child = spawnNamed(source, "child");
    const rb::Entity grand = spawnNamed(source, "grand");
    spawnNamed(source, "loner");
    CHECK(rb::setParent(source, child, root));
    CHECK(rb::setParent(source, grand, child));

    const nlohmann::json doc = rb::SceneSerializer::toJson(source, registry);

    rb::Scene loaded;
    rb::SceneSerializer::fromJson(doc, loaded, registry);
    CHECK(loaded.aliveCount() == 4u);

    const rb::Entity lroot = firstNamed(loaded, "root");
    const rb::Entity lchild = firstNamed(loaded, "child");
    const rb::Entity lgrand = firstNamed(loaded, "grand");
    const rb::Entity lloner = firstNamed(loaded, "loner");
    CHECK(rb::parentOf(loaded, lchild) == lroot);
    CHECK(rb::parentOf(loaded, lgrand) == lchild);
    CHECK(rb::parentOf(loaded, lroot) == rb::kNullEntity);
    CHECK(rb::parentOf(loaded, lloner) == rb::kNullEntity);
}

static void parentedSaveLoadSaveIsIdempotent() {
    const rb::ComponentRegistry registry = makeRegistry();

    rb::Scene source;
    const rb::Entity root = spawnHero(source);
    const rb::Entity child = spawnNamed(source, "shield");
    CHECK(rb::setParent(source, child, root));

    const std::string first = rb::SceneSerializer::toJson(source, registry).dump(2);
    rb::Scene loaded;
    rb::SceneSerializer::fromJson(nlohmann::json::parse(first), loaded, registry);
    const std::string second = rb::SceneSerializer::toJson(loaded, registry).dump(2);
    CHECK(first == second);
}

static void childRecordBeforeParentRecordResolves() {
    const rb::ComponentRegistry registry = makeRegistry();
    const nlohmann::json doc = nlohmann::json::parse(R"({
      "version": 1,
      "entities": [
        { "id": 0, "parent": 1, "components": { "Name": { "value": "child" } } },
        { "id": 1, "components": { "Name": { "value": "root" } } }
      ]
    })");

    rb::Scene scene;
    rb::SceneSerializer::fromJson(doc, scene, registry);
    const rb::Entity child = firstNamed(scene, "child");
    const rb::Entity root = firstNamed(scene, "root");
    CHECK(rb::parentOf(scene, child) == root);
}

static void malformedParentIsSkipped() {
    const rb::ComponentRegistry registry = makeRegistry();
    const nlohmann::json doc = nlohmann::json::parse(R"({
      "version": 1,
      "entities": [
        { "id": 0, "parent": "bogus", "components": { "Name": { "value": "a" } } },
        { "id": 1, "parent": 99, "components": { "Name": { "value": "b" } } },
        { "id": 2, "parent": 2, "components": { "Name": { "value": "c" } } },
        { "id": 3, "parent": 4, "components": { "Name": { "value": "d" } } },
        { "id": 4, "parent": 3, "components": { "Name": { "value": "e" } } }
      ]
    })");

    rb::Scene scene;
    rb::SceneSerializer::fromJson(doc, scene, registry); // must not throw
    CHECK(scene.aliveCount() == 5u);
    CHECK(rb::parentOf(scene, firstNamed(scene, "a")) == rb::kNullEntity);
    CHECK(rb::parentOf(scene, firstNamed(scene, "b")) == rb::kNullEntity);
    CHECK(rb::parentOf(scene, firstNamed(scene, "c")) == rb::kNullEntity);

    // The authored cycle degrades to a single accepted link, never a loop.
    const bool dUnderE = rb::parentOf(scene, firstNamed(scene, "d")) == firstNamed(scene, "e");
    const bool eUnderD = rb::parentOf(scene, firstNamed(scene, "e")) == firstNamed(scene, "d");
    CHECK(dUnderE != eUnderD);
}

static void staleParentIsNotSaved() {
    const rb::ComponentRegistry registry = makeRegistry();

    rb::Scene scene;
    const rb::Entity parent = spawnNamed(scene, "gone");
    const rb::Entity child = spawnNamed(scene, "kept");
    CHECK(rb::setParent(scene, child, parent));
    scene.destroy(parent);

    const nlohmann::json doc = rb::SceneSerializer::toJson(scene, registry);
    CHECK(doc["entities"].size() == 1u);
    CHECK(!doc["entities"][0].contains("parent"));
}

int main() {
    registryExposesBuiltins();
    roundTripPreservesComponents();
    saveLoadSaveIsIdempotent();
    saveAndLoadFile();
    malformedDataLoadsGracefully();
    loadFromFileReplaces();
    parentLinksRoundTrip();
    parentedSaveLoadSaveIsIdempotent();
    childRecordBeforeParentRecordResolves();
    malformedParentIsSkipped();
    staleParentIsNotSaved();
    return rbtest::summary("serialize_scene");
}
