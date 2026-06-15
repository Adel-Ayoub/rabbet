#include "rabbet/ecs/Scene.h"
#include "rabbet/scene/Transform.h"
#include "rabbet/serialize/BuiltinComponents.h"
#include "rabbet/serialize/ComponentRegistry.h"
#include "tests/Test.h"

#include <string_view>

namespace {

rb::ComponentRegistry makeRegistry() {
    rb::ComponentRegistry registry;
    rb::registerBuiltinComponents(registry);
    return registry;
}

// Every builtin carries the full set of reflection hooks the editor relies on, and
// no draw hook until the editor attaches one (the core stays UI-free).
static void builtinsExposeEditorHooks() {
    const rb::ComponentRegistry registry = makeRegistry();
    constexpr std::string_view names[] = {
        "Name",          "Transform",       "Camera",          "DirectionalLight",
        "PointLight",    "ModelRenderer",   "MaterialComponent", "Primitive",
        "ScriptComponent", "RigidBody",     "BoxCollider",     "SphereCollider",
        "SoundEmitter",  "PrefabInstance"};
    for (const std::string_view name : names) {
        const rb::ComponentRegistry::Entry* entry = registry.find(name);
        CHECK(entry != nullptr);
        if (entry == nullptr) {
            continue;
        }
        CHECK(entry->has != nullptr);
        CHECK(entry->addDefault != nullptr);
        CHECK(entry->remove != nullptr);
        CHECK(entry->drawInspector == nullptr);
    }
    CHECK(registry.entries().size() == 14u);
}

// addDefault attaches a default-constructed component; it is idempotent.
static void addDefaultAddsThenIsIdempotent() {
    const rb::ComponentRegistry registry = makeRegistry();
    rb::Scene scene;
    const rb::Entity e = scene.create();
    const rb::ComponentRegistry::Entry* transform = registry.find("Transform");
    CHECK(transform != nullptr);

    CHECK(!transform->has(scene, e));
    transform->addDefault(scene, e);
    CHECK(transform->has(scene, e));

    scene.get<rb::Transform>(e).position.x = 4.0f;
    transform->addDefault(scene, e); // present already: must not overwrite
    CHECK(scene.get<rb::Transform>(e).position.x == 4.0f);
}

// remove takes the component away and is safe to call when it is absent.
static void removeClearsComponent() {
    const rb::ComponentRegistry registry = makeRegistry();
    rb::Scene scene;
    const rb::Entity e = scene.create();
    const rb::ComponentRegistry::Entry* point = registry.find("PointLight");
    CHECK(point != nullptr);

    point->remove(scene, e); // absent: no-op, must not crash
    point->addDefault(scene, e);
    CHECK(point->has(scene, e));
    point->remove(scene, e);
    CHECK(!point->has(scene, e));
}

// setDrawer attaches an editor hook by name and ignores unknown names.
static void setDrawerAttachesByName() {
    rb::ComponentRegistry registry = makeRegistry();
    const rb::ComponentRegistry::DrawFn drawer = +[](rb::Scene&, rb::Entity) {};

    registry.setDrawer("Transform", drawer);
    const rb::ComponentRegistry::Entry* transform = registry.find("Transform");
    CHECK(transform != nullptr);
    CHECK(transform->drawInspector == drawer);

    registry.setDrawer("Nonexistent", drawer); // unknown: no-op, must not crash
}

} // namespace

int main() {
    builtinsExposeEditorHooks();
    addDefaultAddsThenIsIdempotent();
    removeClearsComponent();
    setDrawerAttachesByName();
    return rbtest::summary("serialize_registry");
}
