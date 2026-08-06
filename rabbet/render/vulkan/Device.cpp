#include "rabbet/render/vulkan/Device.h"

#include "rabbet/render/vulkan/Instance.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <optional>
#include <vector>

namespace rb::vulkan {

namespace {

constexpr const char* portabilitySubsetName = "VK_KHR_portability_subset";
constexpr std::size_t enumerationAttempts = 4;

struct QueueFamilies {
    std::uint32_t graphics{0};
    std::uint32_t present{0};
};

struct Candidate {
    VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
    QueueFamilies queues{};
    VkPhysicalDeviceProperties properties{};
    bool portabilitySubset{false};
    const char* swapchainMaintenanceExtension{nullptr};
    int score{0};
};

bool hasExtension(const std::vector<VkExtensionProperties>& extensions, const char* name) {
    return std::any_of(extensions.begin(), extensions.end(),
                       [name](const VkExtensionProperties& extension) {
                           return std::strcmp(extension.extensionName, name) == 0;
                       });
}

const char* findSwapchainMaintenanceExtension(
    const std::vector<VkExtensionProperties>& extensions, bool khrSurfaceMaintenance,
    bool extSurfaceMaintenance) {
#ifndef VK_KHR_swapchain_maintenance1
    static_cast<void>(khrSurfaceMaintenance);
#endif
#ifdef VK_KHR_swapchain_maintenance1
    if (khrSurfaceMaintenance &&
        hasExtension(extensions, VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME)) {
        return VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME;
    }
#endif
    if (extSurfaceMaintenance &&
        hasExtension(extensions, VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME)) {
        return VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME;
    }
    return nullptr;
}

std::optional<std::vector<VkPhysicalDevice>> enumeratePhysicalDevices(VkInstance instance) {
    for (std::size_t attempt = 0; attempt < enumerationAttempts; ++attempt) {
        std::uint32_t count = 0;
        if (vkEnumeratePhysicalDevices(instance, &count, nullptr) != VK_SUCCESS) {
            return std::nullopt;
        }
        std::vector<VkPhysicalDevice> devices(count);
        const VkResult result = vkEnumeratePhysicalDevices(instance, &count, devices.data());
        if (result == VK_SUCCESS) {
            devices.resize(count);
            return devices;
        }
        if (result != VK_INCOMPLETE) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<std::vector<VkExtensionProperties>>
enumerateDeviceExtensions(VkPhysicalDevice physicalDevice) {
    for (std::size_t attempt = 0; attempt < enumerationAttempts; ++attempt) {
        std::uint32_t count = 0;
        if (vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, nullptr) !=
            VK_SUCCESS) {
            return std::nullopt;
        }
        std::vector<VkExtensionProperties> extensions(count);
        const VkResult result = vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr,
                                                                     &count, extensions.data());
        if (result == VK_SUCCESS) {
            extensions.resize(count);
            return extensions;
        }
        if (result != VK_INCOMPLETE) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<std::vector<VkSurfaceFormatKHR>>
enumerateSurfaceFormats(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface) {
    for (std::size_t attempt = 0; attempt < enumerationAttempts; ++attempt) {
        std::uint32_t count = 0;
        if (vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &count, nullptr) !=
            VK_SUCCESS) {
            return std::nullopt;
        }
        std::vector<VkSurfaceFormatKHR> formats(count);
        const VkResult result =
            vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &count, formats.data());
        if (result == VK_SUCCESS) {
            formats.resize(count);
            return formats;
        }
        if (result != VK_INCOMPLETE) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<std::vector<VkPresentModeKHR>> enumeratePresentModes(VkPhysicalDevice physicalDevice,
                                                                   VkSurfaceKHR surface) {
    for (std::size_t attempt = 0; attempt < enumerationAttempts; ++attempt) {
        std::uint32_t count = 0;
        if (vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &count, nullptr) !=
            VK_SUCCESS) {
            return std::nullopt;
        }
        std::vector<VkPresentModeKHR> modes(count);
        const VkResult result = vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface,
                                                                          &count, modes.data());
        if (result == VK_SUCCESS) {
            modes.resize(count);
            return modes;
        }
        if (result != VK_INCOMPLETE) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<QueueFamilies> findQueueFamilies(VkPhysicalDevice physicalDevice,
                                               VkSurfaceKHR surface) {
    std::uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, nullptr);
    std::vector<VkQueueFamilyProperties> properties(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, properties.data());
    properties.resize(count);

    std::optional<std::uint32_t> graphics;
    std::optional<std::uint32_t> present;
    for (std::uint32_t index = 0; index < count; ++index) {
        const bool supportsGraphics = (properties[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0U;
        VkBool32 supportsPresent = VK_FALSE;
        const VkResult presentResult =
            vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, index, surface, &supportsPresent);
        if (presentResult != VK_SUCCESS) {
            continue;
        }
        if (supportsGraphics && supportsPresent == VK_TRUE) {
            return QueueFamilies{index, index};
        }
        if (supportsGraphics && !graphics.has_value()) {
            graphics = index;
        }
        if (supportsPresent == VK_TRUE && !present.has_value()) {
            present = index;
        }
    }
    if (!graphics.has_value() || !present.has_value()) {
        return std::nullopt;
    }
    return QueueFamilies{*graphics, *present};
}

bool supportsRequiredSurface(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface) {
    const auto formats = enumerateSurfaceFormats(physicalDevice, surface);
    const auto presentModes = enumeratePresentModes(physicalDevice, surface);
    if (!formats.has_value() || formats->empty() || !presentModes.has_value() ||
        presentModes->empty()) {
        return false;
    }

    const bool acceptsRequiredFormat =
        (formats->size() == 1U && formats->front().format == VK_FORMAT_UNDEFINED &&
         formats->front().colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) ||
        std::any_of(formats->begin(), formats->end(), [](const VkSurfaceFormatKHR& format) {
            return format.format == VK_FORMAT_B8G8R8A8_UNORM &&
                   format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        });
    return acceptsRequiredFormat && std::find(presentModes->begin(), presentModes->end(),
                                              VK_PRESENT_MODE_FIFO_KHR) != presentModes->end();
}

std::optional<Candidate> inspectDevice(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
                                       bool khrSurfaceMaintenance,
                                       bool extSurfaceMaintenance) {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);
    if (properties.apiVersion < VK_API_VERSION_1_3) {
        return std::nullopt;
    }

    const auto queues = findQueueFamilies(physicalDevice, surface);
    const auto extensions = enumerateDeviceExtensions(physicalDevice);
    if (!queues.has_value() || !extensions.has_value()) {
        return std::nullopt;
    }
    const char* swapchainMaintenanceExtension = findSwapchainMaintenanceExtension(
        *extensions, khrSurfaceMaintenance, extSurfaceMaintenance);
    if (!hasExtension(*extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME) ||
        swapchainMaintenanceExtension == nullptr ||
        !supportsRequiredSurface(physicalDevice, surface)) {
        return std::nullopt;
    }

    VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT swapchainMaintenance{};
    swapchainMaintenance.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT;
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.pNext = &swapchainMaintenance;
    VkPhysicalDeviceFeatures2 features{};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = &features13;
    vkGetPhysicalDeviceFeatures2(physicalDevice, &features);
    if (features13.dynamicRendering != VK_TRUE || features13.synchronization2 != VK_TRUE ||
        swapchainMaintenance.swapchainMaintenance1 != VK_TRUE) {
        return std::nullopt;
    }

    int score = 0;
    if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        score = 2;
    } else if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
        score = 1;
    }

    return Candidate{physicalDevice,
                     *queues,
                     properties,
                     hasExtension(*extensions, portabilitySubsetName),
                     swapchainMaintenanceExtension,
                     score};
}

}

std::unique_ptr<Device> Device::create(const Instance& instance) {
    const auto physicalDevices = enumeratePhysicalDevices(instance.handle());
    if (!physicalDevices.has_value() || physicalDevices->empty()) {
        std::fprintf(stderr, "No Vulkan physical device is available\n");
        return nullptr;
    }

    std::optional<Candidate> selected;
    for (const VkPhysicalDevice physicalDevice : *physicalDevices) {
        auto candidate = inspectDevice(physicalDevice, instance.surface(),
                                       instance.supportsKhrSurfaceMaintenance(),
                                       instance.supportsExtSurfaceMaintenance());
        if (candidate.has_value() &&
            (!selected.has_value() || candidate->score > selected->score)) {
            selected = candidate;
        }
    }
    if (!selected.has_value()) {
        std::fprintf(stderr, "No Vulkan device satisfies the required features\n");
        return nullptr;
    }

    const float queuePriority = 1.0F;
    std::array<VkDeviceQueueCreateInfo, 2> queueInfos{};
    queueInfos[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfos[0].queueFamilyIndex = selected->queues.graphics;
    queueInfos[0].queueCount = 1;
    queueInfos[0].pQueuePriorities = &queuePriority;
    std::uint32_t queueInfoCount = 1;
    if (selected->queues.present != selected->queues.graphics) {
        queueInfos[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfos[1].queueFamilyIndex = selected->queues.present;
        queueInfos[1].queueCount = 1;
        queueInfos[1].pQueuePriorities = &queuePriority;
        queueInfoCount = 2;
    }

    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.synchronization2 = VK_TRUE;
    features13.dynamicRendering = VK_TRUE;
    VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT swapchainMaintenance{};
    swapchainMaintenance.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT;
    swapchainMaintenance.swapchainMaintenance1 = VK_TRUE;
    features13.pNext = &swapchainMaintenance;

    std::vector<const char*> enabledExtensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                                               selected->swapchainMaintenanceExtension};
    if (selected->portabilitySubset) {
        enabledExtensions.push_back(portabilitySubsetName);
    }

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = &features13;
    createInfo.queueCreateInfoCount = queueInfoCount;
    createInfo.pQueueCreateInfos = queueInfos.data();
    createInfo.enabledExtensionCount = static_cast<std::uint32_t>(enabledExtensions.size());
    createInfo.ppEnabledExtensionNames = enabledExtensions.data();

    VkDevice device = VK_NULL_HANDLE;
    const VkResult deviceResult =
        vkCreateDevice(selected->physicalDevice, &createInfo, nullptr, &device);
    if (deviceResult != VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan device creation failed with result %d\n",
                     static_cast<int>(deviceResult));
        return nullptr;
    }

    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, selected->queues.graphics, 0, &graphicsQueue);
    vkGetDeviceQueue(device, selected->queues.present, 0, &presentQueue);
    return std::unique_ptr<Device>(new Device(selected->physicalDevice, device, graphicsQueue,
                                              presentQueue, selected->queues.graphics,
                                              selected->queues.present, selected->properties));
}

Device::Device(VkPhysicalDevice physicalDevice, VkDevice device, VkQueue graphicsQueue,
               VkQueue presentQueue, std::uint32_t graphicsQueueFamily,
               std::uint32_t presentQueueFamily,
               const VkPhysicalDeviceProperties& properties) noexcept
    : m_physicalDevice(physicalDevice), m_device(device), m_graphicsQueue(graphicsQueue),
      m_presentQueue(presentQueue), m_graphicsQueueFamily(graphicsQueueFamily),
      m_presentQueueFamily(presentQueueFamily), m_properties(properties),
      m_limits{properties.limits.maxPushConstantsSize, properties.limits.maxBoundDescriptorSets,
               properties.limits.minUniformBufferOffsetAlignment} {}

Device::~Device() {
    if (m_device != VK_NULL_HANDLE) {
        vkDestroyDevice(m_device, nullptr);
    }
}

VkPhysicalDevice Device::physicalHandle() const noexcept {
    return m_physicalDevice;
}

VkDevice Device::handle() const noexcept {
    return m_device;
}

VkQueue Device::graphicsQueue() const noexcept {
    return m_graphicsQueue;
}

VkQueue Device::presentQueue() const noexcept {
    return m_presentQueue;
}

std::uint32_t Device::graphicsQueueFamily() const noexcept {
    return m_graphicsQueueFamily;
}

std::uint32_t Device::presentQueueFamily() const noexcept {
    return m_presentQueueFamily;
}

const VkPhysicalDeviceProperties& Device::properties() const noexcept {
    return m_properties;
}

const DeviceLimits& Device::limits() const noexcept {
    return m_limits;
}

bool Device::waitIdle() const noexcept {
    return vkDeviceWaitIdle(m_device) == VK_SUCCESS;
}

}
