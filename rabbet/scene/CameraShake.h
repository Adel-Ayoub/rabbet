#pragma once

#include "rabbet/render/RenderView.h"

namespace rb {

// Written by world.shake, decayed by CameraShakeSystem, applied to the game view by
// whoever builds it. All zeroes means inert, so consumers can apply unconditionally.
struct CameraShake {
    float amplitude = 0.0f;
    float duration = 0.0f;
    float remaining = 0.0f;
    float time = 0.0f;
};

void applyCameraShake(RenderView& view, const CameraShake& shake);

} // namespace rb
