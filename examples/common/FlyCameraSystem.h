#pragma once

#include "rabbet/core/System.h"

namespace ex {

// Turns the Input resource into momentum-driven first-person camera movement.
class FlyCameraSystem final : public rb::System {
public:
    void onUpdate(rb::Runtime& runtime, float dt) override;
};

} // namespace ex
