#include "rabbet/render/vulkan/Instance.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <optional>
#include <utility>
#include <vector>

namespace rb::vulkan {

namespace {

constexpr const char* validationLayerName = "VK_LAYER_KHRONOS_validation";
constexpr std::size_t enumerationAttempts = 4;
constexpr std::uint32_t synchronizationValidationRevision = 4;
#ifdef VK_EXT_layer_settings
constexpr std::uint32_t publicLayerSettingsRevision = 2;
constexpr const char* synchronizationSettingName = "validate_sync";
#endif

std::optional<std::vector<VkLayerProperties>> enumerateLayers() {
    for (std::size_t attempt = 0; attempt < enumerationAttempts; ++attempt) {
        std::uint32_t count = 0;
        if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS) {
            return std::nullopt;
        }
        std::vector<VkLayerProperties> layers(count);
        const VkResult result = vkEnumerateInstanceLayerProperties(&count, layers.data());
        if (result == VK_SUCCESS) {
            layers.resize(count);
            return layers;
        }
        if (result != VK_INCOMPLETE) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<std::vector<VkExtensionProperties>> enumerateExtensions(const char* layerName) {
    for (std::size_t attempt = 0; attempt < enumerationAttempts; ++attempt) {
        std::uint32_t count = 0;
        if (vkEnumerateInstanceExtensionProperties(layerName, &count, nullptr) != VK_SUCCESS) {
            return std::nullopt;
        }
        std::vector<VkExtensionProperties> extensions(count);
        const VkResult result =
            vkEnumerateInstanceExtensionProperties(layerName, &count, extensions.data());
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

bool hasLayer(const std::vector<VkLayerProperties>& layers, const char* name) {
    return std::any_of(layers.begin(), layers.end(), [name](const VkLayerProperties& layer) {
        return std::strcmp(layer.layerName, name) == 0;
    });
}

bool hasExtension(const std::vector<VkExtensionProperties>& extensions, const char* name,
                  std::uint32_t minimumRevision = 0) {
    return std::any_of(extensions.begin(), extensions.end(),
                       [name, minimumRevision](const VkExtensionProperties& extension) {
                           return std::strcmp(extension.extensionName, name) == 0 &&
                                  extension.specVersion >= minimumRevision;
                       });
}

bool containsName(const std::vector<const char*>& names, const char* name) {
    return std::any_of(names.begin(), names.end(),
                       [name](const char* existing) { return std::strcmp(existing, name) == 0; });
}

VKAPI_ATTR VkBool32 VKAPI_CALL
validationCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
                   const VkDebugUtilsMessengerCallbackDataEXT* callbackData, void* userData) {
    auto* counter = static_cast<ValidationCounter*>(userData);
    counter->record(severity);

    if ((severity & (VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)) != 0U) {
        const char* level =
            (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0U ? "error" : "warning";
        const char* message = callbackData != nullptr && callbackData->pMessage != nullptr
                                  ? callbackData->pMessage
                                  : "message unavailable";
        std::fprintf(stderr, "[Vulkan validation %s] %s\n", level, message);
    }
    return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT debugMessengerInfo(ValidationCounter& validation) {
    VkDebugUtilsMessengerCreateInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = validationCallback;
    info.pUserData = &validation;
    return info;
}

}

void ValidationCounter::record(VkDebugUtilsMessageSeverityFlagBitsEXT severity) noexcept {
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0U) {
        m_errors.fetch_add(1, std::memory_order_relaxed);
    } else if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0U) {
        m_warnings.fetch_add(1, std::memory_order_relaxed);
    } else if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) != 0U) {
        m_info.fetch_add(1, std::memory_order_relaxed);
    } else {
        m_verbose.fetch_add(1, std::memory_order_relaxed);
    }
}

std::uint32_t ValidationCounter::verbose() const noexcept {
    return m_verbose.load(std::memory_order_relaxed);
}

std::uint32_t ValidationCounter::info() const noexcept {
    return m_info.load(std::memory_order_relaxed);
}

std::uint32_t ValidationCounter::warnings() const noexcept {
    return m_warnings.load(std::memory_order_relaxed);
}

std::uint32_t ValidationCounter::errors() const noexcept {
    return m_errors.load(std::memory_order_relaxed);
}

std::uint32_t ValidationCounter::total() const noexcept {
    return verbose() + info() + warnings() + errors();
}

std::unique_ptr<Instance> Instance::create(GLFWwindow* window,
                                           std::shared_ptr<ValidationCounter> validation) {
    if (window == nullptr) {
        std::fprintf(stderr, "Vulkan instance creation received no window\n");
        return nullptr;
    }
    if (!validation) {
        std::fprintf(stderr, "Vulkan instance creation received no validation counter\n");
        return nullptr;
    }

    const auto layers = enumerateLayers();
    if (!layers.has_value()) {
        std::fprintf(stderr, "Vulkan instance layer enumeration failed\n");
        return nullptr;
    }
    if (!hasLayer(*layers, validationLayerName)) {
        std::fprintf(stderr, "Vulkan validation layer is unavailable\n");
        return nullptr;
    }

    const auto availableExtensions = enumerateExtensions(nullptr);
    if (!availableExtensions.has_value()) {
        std::fprintf(stderr, "Vulkan instance extension enumeration failed\n");
        return nullptr;
    }
    const auto validationExtensions = enumerateExtensions(validationLayerName);
    const bool validationFeatureSynchronization =
        validationExtensions.has_value() &&
        hasExtension(*validationExtensions, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME,
                     synchronizationValidationRevision);
    bool layerSettingSynchronization = false;
#ifdef VK_EXT_layer_settings
    layerSettingSynchronization =
        validationExtensions.has_value() &&
        hasExtension(*validationExtensions, VK_EXT_LAYER_SETTINGS_EXTENSION_NAME,
                     publicLayerSettingsRevision);
#endif
    const bool synchronizationValidation =
        layerSettingSynchronization || validationFeatureSynchronization;

    std::uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    if (glfwExtensions == nullptr || glfwExtensionCount == 0U) {
        std::fprintf(stderr, "GLFW reported no Vulkan instance extensions\n");
        return nullptr;
    }
    std::vector<const char*> enabledExtensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    if (!hasExtension(*availableExtensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
        std::fprintf(stderr, "Vulkan debug utilities extension is unavailable\n");
        return nullptr;
    }
    if (!containsName(enabledExtensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
        enabledExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    if (!layerSettingSynchronization && validationFeatureSynchronization) {
        enabledExtensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
    }
#ifdef VK_EXT_layer_settings
    if (layerSettingSynchronization) {
        enabledExtensions.push_back(VK_EXT_LAYER_SETTINGS_EXTENSION_NAME);
    }
#endif
    if (!hasExtension(*availableExtensions, VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME)) {
        std::fprintf(stderr, "Vulkan surface capability extension is unavailable\n");
        return nullptr;
    }
    if (!containsName(enabledExtensions, VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME)) {
        enabledExtensions.push_back(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
    }
    bool khrSurfaceMaintenance = false;
#ifdef VK_KHR_surface_maintenance1
    if (hasExtension(*availableExtensions, VK_KHR_SURFACE_MAINTENANCE_1_EXTENSION_NAME)) {
        enabledExtensions.push_back(VK_KHR_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
        khrSurfaceMaintenance = true;
    }
#endif
    bool extSurfaceMaintenance = false;
    if (hasExtension(*availableExtensions, VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME)) {
        enabledExtensions.push_back(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
        extSurfaceMaintenance = true;
    }
    if (!khrSurfaceMaintenance && !extSurfaceMaintenance) {
        std::fprintf(stderr, "Vulkan surface maintenance extension is unavailable\n");
        return nullptr;
    }

    VkInstanceCreateFlags flags = 0;
    if (hasExtension(*availableExtensions, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        enabledExtensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }

    VkApplicationInfo applicationInfo{};
    applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    applicationInfo.pApplicationName = "Rabbet Vulkan Triangle";
    applicationInfo.applicationVersion = 1;
    applicationInfo.pEngineName = "Rabbet";
    applicationInfo.engineVersion = 1;
    applicationInfo.apiVersion = VK_API_VERSION_1_3;

    auto messengerInfo = debugMessengerInfo(*validation);
    const VkValidationFeatureEnableEXT validationFeature =
        VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    VkValidationFeaturesEXT validationFeatures{};
#ifdef VK_EXT_layer_settings
    const VkBool32 synchronizationSettingValue = VK_TRUE;
    const VkLayerSettingEXT synchronizationSetting{
        validationLayerName, synchronizationSettingName, VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1,
        &synchronizationSettingValue};
    VkLayerSettingsCreateInfoEXT layerSettings{};
#endif
    const char* enabledLayers[] = {validationLayerName};
    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    const void* instanceCreateChain = &messengerInfo;
#ifdef VK_EXT_layer_settings
    if (layerSettingSynchronization) {
        layerSettings.sType = VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT;
        layerSettings.pNext = &messengerInfo;
        layerSettings.settingCount = 1;
        layerSettings.pSettings = &synchronizationSetting;
        instanceCreateChain = &layerSettings;
    }
#endif
    if (!layerSettingSynchronization && validationFeatureSynchronization) {
        validationFeatures.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
        validationFeatures.pNext = &messengerInfo;
        validationFeatures.enabledValidationFeatureCount = 1;
        validationFeatures.pEnabledValidationFeatures = &validationFeature;
        instanceCreateChain = &validationFeatures;
    }
    createInfo.pNext = instanceCreateChain;
    createInfo.flags = flags;
    createInfo.pApplicationInfo = &applicationInfo;
    createInfo.enabledLayerCount = 1;
    createInfo.ppEnabledLayerNames = enabledLayers;
    createInfo.enabledExtensionCount = static_cast<std::uint32_t>(enabledExtensions.size());
    createInfo.ppEnabledExtensionNames = enabledExtensions.data();

    VkInstance instance = VK_NULL_HANDLE;
    const VkResult instanceResult = vkCreateInstance(&createInfo, nullptr, &instance);
    if (instanceResult != VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan instance creation failed with result %d\n",
                     static_cast<int>(instanceResult));
        return nullptr;
    }

    const auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
    if (createMessenger == nullptr) {
        std::fprintf(stderr, "Vulkan debug messenger function is unavailable\n");
        vkDestroyInstance(instance, nullptr);
        return nullptr;
    }

    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    const VkResult messengerResult = createMessenger(instance, &messengerInfo, nullptr, &messenger);
    if (messengerResult != VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan debug messenger creation failed with result %d\n",
                     static_cast<int>(messengerResult));
        vkDestroyInstance(instance, nullptr);
        return nullptr;
    }

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    const VkResult surfaceResult = glfwCreateWindowSurface(instance, window, nullptr, &surface);
    if (surfaceResult != VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan window surface creation failed with result %d\n",
                     static_cast<int>(surfaceResult));
        const auto destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroyMessenger != nullptr) {
            destroyMessenger(instance, messenger, nullptr);
        }
        vkDestroyInstance(instance, nullptr);
        return nullptr;
    }

    return std::unique_ptr<Instance>(new Instance(
        std::move(validation), instance, messenger, surface, synchronizationValidation,
        khrSurfaceMaintenance, extSurfaceMaintenance));
}

Instance::Instance(std::shared_ptr<ValidationCounter> validation, VkInstance instance,
                   VkDebugUtilsMessengerEXT messenger, VkSurfaceKHR surface,
                   bool synchronizationValidation, bool khrSurfaceMaintenance,
                   bool extSurfaceMaintenance) noexcept
    : m_validation(std::move(validation)), m_instance(instance), m_messenger(messenger),
      m_surface(surface), m_synchronizationValidation(synchronizationValidation),
      m_khrSurfaceMaintenance(khrSurfaceMaintenance),
      m_extSurfaceMaintenance(extSurfaceMaintenance) {}

Instance::~Instance() {
    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    }
    if (m_messenger != VK_NULL_HANDLE) {
        const auto destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroyMessenger != nullptr) {
            destroyMessenger(m_instance, m_messenger, nullptr);
        }
    }
    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
    }
}

VkInstance Instance::handle() const noexcept {
    return m_instance;
}

VkSurfaceKHR Instance::surface() const noexcept {
    return m_surface;
}

bool Instance::synchronizationValidationEnabled() const noexcept {
    return m_synchronizationValidation;
}

bool Instance::supportsKhrSurfaceMaintenance() const noexcept {
    return m_khrSurfaceMaintenance;
}

bool Instance::supportsExtSurfaceMaintenance() const noexcept {
    return m_extSurfaceMaintenance;
}

}
