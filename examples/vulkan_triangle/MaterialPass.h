#pragma once

#include "rabbet/render/vulkan/Device.h"

#include <string>

struct MaterialPassPaths {
    std::string vertexSpv;
    std::string pbrFragmentSpv;
    std::string phongFragmentSpv;
    std::string baselinePpm;
    std::string outputDirectory;
};

bool runMaterialPass(const rb::vulkan::Device& device, const MaterialPassPaths& paths);
