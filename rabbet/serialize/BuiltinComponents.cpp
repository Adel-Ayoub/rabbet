#include "rabbet/serialize/BuiltinComponents.h"

#include "rabbet/serialize/ComponentRegistry.h"

namespace rb {

void registerBuiltinComponents(ComponentRegistry& registry) {
    registry.add<Name>("Name");
    registry.add<Transform>("Transform");
    registry.add<Camera>("Camera");
    registry.add<DirectionalLight>("DirectionalLight");
    registry.add<PointLight>("PointLight");
}

} // namespace rb
