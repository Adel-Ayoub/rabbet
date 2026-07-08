#pragma once

#include "rabbet/ecs/Entity.h"

namespace rb {

// Links this entity beneath another: its Transform reads as local to the parent's world
// pose. Deliberately NOT registered in the ComponentRegistry: the scene format stores the
// link structurally (a "parent" field on the entity record, remapped on load), and
// re-parenting is a Hierarchy-panel gesture, so it must not appear in the add-component
// menu or the generic component save loop.
struct Parent {
    Entity entity;
};

} // namespace rb
