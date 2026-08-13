#pragma once

#include "rabbet/render/vulkan/Device.h"

#include <string>

struct MeshDepthPassPaths {
    std::string vertexSpv;
    std::string fragmentSpv;
    std::string depthVertexSpv;
    std::string depthFragmentSpv;
    std::string pickVertexSpv;
    std::string pickFragmentSpv;
    // Optional GL depth capture of the same scene, raw float32 rows bottom to top.
    // Empty skips the comparison.
    std::string baselineDepthRaw;
    // Optional directory receiving the rendered color and depth. Empty skips the dump.
    std::string outputDirectory;
};

bool runMeshDepthPass(const rb::vulkan::Device& device, const MeshDepthPassPaths& paths);
