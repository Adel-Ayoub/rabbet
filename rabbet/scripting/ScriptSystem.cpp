#include "rabbet/scripting/ScriptSystem.h"

#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <sol/sol.hpp>

#include "rabbet/assets/AssetManager.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/platform/Input.h"
#include "rabbet/scene/Name.h"
#include "rabbet/scene/Transform.h"
#include "rabbet/scripting/ScriptAsset.h"
#include "rabbet/scripting/ScriptComponent.h"
#include "rabbet/util/Log.h"

namespace rb {
namespace {

// A live handle to one entity, handed to its script as `self`. It stores the Scene and
// Entity (not a Transform*), resolving the component on each call so a sparse-set realloc
// between frames can never leave the script holding a dangling pointer.
struct ScriptEntity {
    Scene* scene = nullptr;
    Entity entity;

    Transform* transform() { return scene != nullptr ? scene->tryGet<Transform>(entity) : nullptr; }

    void translate(double dx, double dy, double dz) {
        if (Transform* t = transform()) {
            t->position +=
                glm::vec3(static_cast<float>(dx), static_cast<float>(dy), static_cast<float>(dz));
        }
    }

    void rotate(double dx, double dy, double dz) {
        if (Transform* t = transform()) {
            const glm::vec3 euler = glm::radians(
                glm::vec3(static_cast<float>(dx), static_cast<float>(dy), static_cast<float>(dz)));
            t->rotation = glm::normalize(t->rotation * glm::quat(euler));
        }
    }

    std::tuple<double, double, double> position() {
        if (Transform* t = transform()) {
            return {static_cast<double>(t->position.x), static_cast<double>(t->position.y),
                    static_cast<double>(t->position.z)};
        }
        return {0.0, 0.0, 0.0};
    }

    void setPosition(double x, double y, double z) {
        if (Transform* t = transform()) {
            t->position =
                glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
        }
    }

    std::tuple<double, double, double> getScale() {
        if (Transform* t = transform()) {
            return {static_cast<double>(t->scale.x), static_cast<double>(t->scale.y),
                    static_cast<double>(t->scale.z)};
        }
        return {1.0, 1.0, 1.0};
    }

    void setScale(double x, double y, double z) {
        if (Transform* t = transform()) {
            t->scale = glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
        }
    }

    std::string name() {
        if (scene != nullptr) {
            if (const Name* n = scene->tryGet<Name>(entity)) {
                return n->value;
            }
        }
        return std::string{};
    }
};

std::optional<Key> keyFromName(std::string_view name) {
    if (name.size() == 1) {
        const char c = name[0];
        if (c >= 'A' && c <= 'Z') {
            return static_cast<Key>(static_cast<int>(Key::A) + (c - 'A'));
        }
        if (c >= 'a' && c <= 'z') {
            return static_cast<Key>(static_cast<int>(Key::A) + (c - 'a'));
        }
        if (c >= '0' && c <= '9') {
            return static_cast<Key>(static_cast<int>(Key::Num0) + (c - '0'));
        }
    }
    if (name == "Space") return Key::Space;
    if (name == "Escape") return Key::Escape;
    if (name == "Enter") return Key::Enter;
    if (name == "Tab") return Key::Tab;
    if (name == "Backspace") return Key::Backspace;
    if (name == "Left") return Key::Left;
    if (name == "Right") return Key::Right;
    if (name == "Up") return Key::Up;
    if (name == "Down") return Key::Down;
    if (name == "LeftShift") return Key::LeftShift;
    if (name == "LeftControl") return Key::LeftControl;
    if (name == "LeftAlt") return Key::LeftAlt;
    return std::nullopt;
}

// Reconcile `fields` with a script's declared `fields` table: keep an existing override
// when its name and type still match, add newly declared fields, drop removed ones.
void mergeDeclaredFields(std::vector<ScriptField>& fields, const sol::object& declared) {
    if (!declared.is<sol::table>()) {
        fields.clear();
        return;
    }
    std::vector<ScriptField> merged;
    sol::table table = declared.as<sol::table>();
    table.for_each([&](const sol::object& key, const sol::object& value) {
        if (key.get_type() != sol::type::string) {
            return;
        }
        ScriptField field;
        field.name = key.as<std::string>();
        switch (value.get_type()) {
        case sol::type::boolean:
            field.type = ScriptField::Type::Boolean;
            field.boolean = value.as<bool>();
            break;
        case sol::type::number:
            field.type = ScriptField::Type::Number;
            field.number = value.as<double>();
            break;
        case sol::type::string:
            field.type = ScriptField::Type::String;
            field.text = value.as<std::string>();
            break;
        default:
            return; // unsupported field type — skip it
        }
        for (const ScriptField& existing : fields) {
            if (existing.name == field.name && existing.type == field.type) {
                field = existing;
                break;
            }
        }
        merged.push_back(std::move(field));
    });
    fields = std::move(merged);
}

} // namespace

struct ScriptSystem::Impl {
    struct Instance {
        sol::environment env;
        sol::protected_function onStart;
        sol::protected_function onUpdate;
        sol::object self;
        Uuid script;
        std::uint32_t revision = 0;
        bool compiled = false;    // a compile was attempted for (script, revision)
        bool ok = false;          // the attempt produced runnable hooks
        bool started = false;     // on_start has run this play session
        bool errorLogged = false; // a runtime error was already reported (throttle)
    };

    sol::state lua;
    std::unordered_map<Entity, Instance> instances;
    Input* input = nullptr;

    Impl() {
        lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);

        lua.new_usertype<ScriptEntity>(
            "Entity", sol::meta_function::construct, sol::no_constructor, //
            "translate", &ScriptEntity::translate, "rotate", &ScriptEntity::rotate, "position",
            &ScriptEntity::position, "set_position", &ScriptEntity::setPosition, "scale",
            &ScriptEntity::getScale, "set_scale", &ScriptEntity::setScale, "name",
            &ScriptEntity::name);

        sol::table inputTable = lua.create_named_table("input");
        inputTable.set_function("down", [this](const std::string& keyName) {
            const std::optional<Key> key = keyFromName(keyName);
            return input != nullptr && key.has_value() && input->keyDown(*key);
        });
        inputTable.set_function("pressed", [this](const std::string& keyName) {
            const std::optional<Key> key = keyFromName(keyName);
            return input != nullptr && key.has_value() && input->keyPressed(*key);
        });
    }

    void syncFields(Instance& instance, const ScriptComponent& component) {
        sol::object existing = instance.env["fields"];
        sol::table table;
        if (existing.is<sol::table>()) {
            table = existing.as<sol::table>();
        } else if (!component.fields.empty()) {
            table = lua.create_table();
            instance.env["fields"] = table;
        } else {
            return;
        }
        for (const ScriptField& field : component.fields) {
            switch (field.type) {
            case ScriptField::Type::Number:
                table[field.name] = field.number;
                break;
            case ScriptField::Type::Boolean:
                table[field.name] = field.boolean;
                break;
            case ScriptField::Type::String:
                table[field.name] = field.text;
                break;
            }
        }
    }

    void compile(Instance& instance, Scene& scene, Entity entity, const ScriptAsset& asset,
                 ScriptComponent& component) {
        instance.ok = false;
        instance.started = false;
        instance.errorLogged = false;
        const std::string chunkName = "@" + asset.path.string();

        sol::environment env(lua, sol::create, lua.globals());
        sol::load_result loaded = lua.load(asset.source, chunkName, sol::load_mode::text);
        if (!loaded.valid()) {
            const sol::error err = loaded;
            log::error("script: parse error in '{}': {}", asset.path.string(), err.what());
            return;
        }
        sol::protected_function chunk = loaded;
        sol::set_environment(env, chunk);
        const sol::protected_function_result ran = chunk();
        if (!ran.valid()) {
            const sol::error err = ran;
            log::error("script: load error in '{}': {}", asset.path.string(), err.what());
            return;
        }

        mergeDeclaredFields(component.fields, env["fields"]);
        instance.env = std::move(env);
        instance.self = sol::make_object(lua, ScriptEntity{&scene, entity});
        instance.onStart = instance.env["on_start"];
        instance.onUpdate = instance.env["on_update"];
        instance.ok = true;
    }

    void invoke(Instance& instance, sol::protected_function& fn, const char* hook, float dt) {
        if (!fn.valid()) {
            return;
        }
        sol::protected_function_result result =
            (dt < 0.0f) ? fn(instance.self) : fn(instance.self, static_cast<double>(dt));
        if (!result.valid() && !instance.errorLogged) {
            const sol::error err = result;
            log::error("script: {} error: {}", hook, err.what());
            instance.errorLogged = true; // throttle: report once until the script recompiles
        }
    }

    void update(Runtime& runtime, float dt) {
        AssetManager* assets = runtime.tryResource<AssetManager>();
        if (assets == nullptr) {
            return;
        }
        input = runtime.tryResource<Input>();
        Scene& scene = runtime.scene();

        scene.each<ScriptComponent>([&](Entity entity, ScriptComponent& component) {
            if (!component.handle.valid()) {
                return;
            }
            ScriptAsset* asset = assets->get<ScriptAsset>(component.handle);
            if (asset == nullptr) {
                return;
            }
            Instance& instance = instances[entity];
            const bool needCompile = !instance.compiled || instance.script != component.script ||
                                     instance.revision != asset->revision;
            if (needCompile) {
                compile(instance, scene, entity, *asset, component);
                instance.script = component.script;
                instance.revision = asset->revision;
                instance.compiled = true;
            }
            if (!instance.ok) {
                return;
            }
            syncFields(instance, component);
            if (!instance.started) {
                invoke(instance, instance.onStart, "on_start", -1.0f);
                instance.started = true;
            }
            invoke(instance, instance.onUpdate, "on_update", dt);
        });
    }
};

ScriptSystem::ScriptSystem() : m_impl(std::make_unique<Impl>()) {}
ScriptSystem::~ScriptSystem() = default;

void ScriptSystem::onUpdate(Runtime& runtime, float dt) { m_impl->update(runtime, dt); }

// A fresh play session starts every script from a clean state, so on_start runs again.
void ScriptSystem::onPlayBegin(Runtime&) { m_impl->instances.clear(); }
void ScriptSystem::onPlayEnd(Runtime&) {
    m_impl->instances.clear();
    m_impl->input = nullptr;
}

void introspectScriptFields(const std::string& source, std::vector<ScriptField>& fields) {
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);
    sol::environment env(lua, sol::create, lua.globals());
    sol::load_result loaded = lua.load(source, "@<introspect>", sol::load_mode::text);
    if (!loaded.valid()) {
        return; // a script that won't parse keeps whatever fields it already had
    }
    sol::protected_function chunk = loaded;
    sol::set_environment(env, chunk);
    if (!chunk().valid()) {
        return;
    }
    mergeDeclaredFields(fields, env["fields"]);
}

} // namespace rb
