#pragma once

#include <vulkan/vulkan.h>

#include <atomic>
#include <cstdint>
#include <memory>

struct GLFWwindow;

namespace rb::vulkan {

class ValidationCounter {
public:
    void record(VkDebugUtilsMessageSeverityFlagBitsEXT severity) noexcept;

    [[nodiscard]] std::uint32_t verbose() const noexcept;
    [[nodiscard]] std::uint32_t info() const noexcept;
    [[nodiscard]] std::uint32_t warnings() const noexcept;
    [[nodiscard]] std::uint32_t errors() const noexcept;
    [[nodiscard]] std::uint32_t total() const noexcept;

private:
    std::atomic_uint32_t m_verbose{0};
    std::atomic_uint32_t m_info{0};
    std::atomic_uint32_t m_warnings{0};
    std::atomic_uint32_t m_errors{0};
};

class Instance {
public:
    static std::unique_ptr<Instance> create(GLFWwindow* window,
                                            std::shared_ptr<ValidationCounter> validation);

    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;
    ~Instance();

    [[nodiscard]] VkInstance handle() const noexcept;
    [[nodiscard]] VkSurfaceKHR surface() const noexcept;
    [[nodiscard]] bool supportsKhrSurfaceMaintenance() const noexcept;
    [[nodiscard]] bool supportsExtSurfaceMaintenance() const noexcept;

private:
    Instance(std::shared_ptr<ValidationCounter> validation, VkInstance instance,
             VkDebugUtilsMessengerEXT messenger, VkSurfaceKHR surface,
             bool khrSurfaceMaintenance, bool extSurfaceMaintenance) noexcept;

    std::shared_ptr<ValidationCounter> m_validation;
    VkInstance m_instance{VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT m_messenger{VK_NULL_HANDLE};
    VkSurfaceKHR m_surface{VK_NULL_HANDLE};
    bool m_khrSurfaceMaintenance{false};
    bool m_extSurfaceMaintenance{false};
};

}
