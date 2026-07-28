#pragma once

#include "rabbet/core/System.h"

namespace rb {

class CameraShakeSystem final : public System {
public:
    void onPlayBegin(Runtime& runtime) override;
    void onUpdate(Runtime& runtime, float dt) override;
    void onPlayEnd(Runtime& runtime) override;
};

} // namespace rb
