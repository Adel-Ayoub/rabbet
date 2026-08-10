#pragma once

#include "rabbet/render/vulkan/Device.h"

#include <cstdint>
#include <string>
#include <vector>

struct ObjectsPassPaths {
    std::string vertexSpv;
    std::string fragmentSpv;
    std::string pipelineCache;
};

std::vector<std::uint32_t> loadSpirvFile(const std::string& path);
bool runObjectsPass(const rb::vulkan::Device& device, const ObjectsPassPaths& paths);
