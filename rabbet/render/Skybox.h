#pragma once

#include "rabbet/render/gl/Cubemap.h"
#include "rabbet/render/gl/Mesh.h"

namespace rb {

struct Skybox {
    gl::Cubemap cubemap;
    gl::Mesh mesh;
};

} // namespace rb
