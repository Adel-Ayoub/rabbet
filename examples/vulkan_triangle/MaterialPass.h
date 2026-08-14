#pragma once

#include "rabbet/render/vulkan/Device.h"

#include <string>

struct MaterialPassPaths {
    std::string vertexSpv;
    std::string pbrFragmentSpv;
    std::string phongFragmentSpv;
    std::string depthVertexSpv;
    std::string depthFragmentSpv;
    std::string skyboxVertexSpv;
    std::string skyboxFragmentSpv;
    std::string convolveVertexSpv;
    std::string convolveFragmentSpv;
    std::string fullscreenVertexSpv;
    std::string prefilterFragmentSpv;
    std::string downsampleFragmentSpv;
    std::string upsampleFragmentSpv;
    std::string compositeFragmentSpv;
    std::string fxaaFragmentSpv;
    std::string waterVertexSpv;
    std::string waterFragmentSpv;
    std::string baselinePpm;
    std::string outputDirectory;
};

bool runMaterialPass(const rb::vulkan::Device& device, const MaterialPassPaths& paths);
