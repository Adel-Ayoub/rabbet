#pragma once

#include "rabbet/core/System.h"

namespace rb {

class CameraSystem final : public System {
public:
    void onUpdate(Runtime& runtime, float dt) override;
};

} // namespace rb
