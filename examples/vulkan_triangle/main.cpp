#include "rabbet/render/vulkan/Device.h"
#include "rabbet/render/vulkan/FrameLoop.h"
#include "rabbet/render/vulkan/Instance.h"
#include "rabbet/render/vulkan/Swapchain.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

struct GlfwSession {
    bool active{false};

    ~GlfwSession() {
        if (active) {
            glfwTerminate();
        }
    }
};

struct WindowState {
    bool resizePending{false};
    std::uint64_t resizeEvents{0};
};

struct TriangleMetrics {
    std::uint64_t frames{0};
    std::uint64_t resizeEvents{0};
    std::uint64_t recreates{0};
    std::uint64_t minimizeWaits{0};
    double recreateTotalMilliseconds{0.0};
    double recreateMaximumMilliseconds{0.0};
};

void onGlfwError(int code, const char* description) {
    std::cerr << "GLFW error " << code << " " << description << '\n';
}

void onFramebufferResize(GLFWwindow* window, int, int) {
    auto* state = static_cast<WindowState*>(glfwGetWindowUserPointer(window));
    if (state != nullptr) {
        state->resizePending = true;
        ++state->resizeEvents;
    }
}

std::vector<std::uint32_t> loadSpirv(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open SPIR-V file " << path << '\n';
        return {};
    }
    const std::streamsize size = static_cast<std::streamsize>(file.tellg());
    if (size <= 0 || size % static_cast<std::streamsize>(sizeof(std::uint32_t)) != 0) {
        std::cerr << "SPIR-V file has an invalid size " << path << '\n';
        return {};
    }

    std::vector<std::uint32_t> code(
        static_cast<std::size_t>(size / static_cast<std::streamsize>(sizeof(std::uint32_t))));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(code.data()), size)) {
        std::cerr << "Failed to read SPIR-V file " << path << '\n';
        return {};
    }
    return code;
}

bool framebufferExtent(GLFWwindow* window, VkExtent2D& extent) {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    if (width <= 0 || height <= 0) {
        return false;
    }
    extent.width = static_cast<std::uint32_t>(width);
    extent.height = static_cast<std::uint32_t>(height);
    return true;
}

bool recreateSwapchain(GLFWwindow* window, rb::vulkan::Swapchain& swapchain,
                       rb::vulkan::FrameLoop& frameLoop, WindowState& windowState,
                       TriangleMetrics& metrics) {
    VkExtent2D extent{};
    bool waitedForRestore = false;
    for (;;) {
        while (!framebufferExtent(window, extent)) {
            if (glfwWindowShouldClose(window) == GLFW_TRUE) {
                return true;
            }
            waitedForRestore = true;
            glfwWaitEventsTimeout(0.05);
        }

        const auto start = std::chrono::steady_clock::now();
        const rb::vulkan::SwapchainRecreateResult recreateResult =
            frameLoop.recreateSwapchain(swapchain, extent);
        if (recreateResult == rb::vulkan::SwapchainRecreateResult::unavailable) {
            if (glfwWindowShouldClose(window) == GLFW_TRUE) {
                return true;
            }
            waitedForRestore = true;
            glfwWaitEventsTimeout(0.05);
            continue;
        }
        if (recreateResult == rb::vulkan::SwapchainRecreateResult::fatal) {
            std::cerr << "Vulkan swapchain recreation failed\n";
            return false;
        }
        const auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start);
        metrics.recreateTotalMilliseconds += elapsed.count();
        metrics.recreateMaximumMilliseconds =
            std::max(metrics.recreateMaximumMilliseconds, elapsed.count());
        break;
    }
    if (waitedForRestore) {
        ++metrics.minimizeWaits;
    }
    ++metrics.recreates;
    windowState.resizePending = false;
    return true;
}

int runTriangle(const std::shared_ptr<rb::vulkan::ValidationCounter>& validation,
                TriangleMetrics& metrics) {
    glfwSetErrorCallback(onGlfwError);
    glfwInitVulkanLoader(vkGetInstanceProcAddr);
    GlfwSession glfw;
    if (glfwInit() != GLFW_TRUE) {
        std::cerr << "GLFW initialization failed\n";
        return EXIT_FAILURE;
    }
    glfw.active = true;
    if (glfwVulkanSupported() != GLFW_TRUE) {
        std::cerr << "GLFW cannot load Vulkan\n";
        return EXIT_FAILURE;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    using Window = std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)>;
    Window window(glfwCreateWindow(960, 600, "Rabbet Vulkan Triangle", nullptr, nullptr),
                  glfwDestroyWindow);
    if (!window) {
        std::cerr << "GLFW window creation failed\n";
        return EXIT_FAILURE;
    }

    WindowState windowState;
    glfwSetWindowUserPointer(window.get(), &windowState);
    glfwSetFramebufferSizeCallback(window.get(), onFramebufferResize);

    auto instance = rb::vulkan::Instance::create(window.get(), validation);
    if (!instance) {
        return EXIT_FAILURE;
    }
    auto device = rb::vulkan::Device::create(*instance);
    if (!device) {
        return EXIT_FAILURE;
    }

    VkExtent2D initialExtent{};
    bool waitedForInitialRestore = false;
    while (!framebufferExtent(window.get(), initialExtent)) {
        if (glfwWindowShouldClose(window.get()) == GLFW_TRUE) {
            return EXIT_SUCCESS;
        }
        waitedForInitialRestore = true;
        glfwWaitEventsTimeout(0.05);
    }
    if (waitedForInitialRestore) {
        ++metrics.minimizeWaits;
    }
    auto swapchain = rb::vulkan::Swapchain::create(*device, instance->surface(), initialExtent);
    if (!swapchain) {
        return EXIT_FAILURE;
    }

    const auto vertexCode = loadSpirv(RB_VULKAN_TRIANGLE_VERTEX_SPV);
    const auto fragmentCode = loadSpirv(RB_VULKAN_TRIANGLE_FRAGMENT_SPV);
    auto frameLoop = rb::vulkan::FrameLoop::create(*device, *swapchain, vertexCode, fragmentCode);
    if (!frameLoop) {
        return EXIT_FAILURE;
    }

    const auto& properties = device->properties();
    const auto& limits = device->limits();
    std::cout << "Vulkan device " << properties.deviceName << " API "
              << VK_API_VERSION_MAJOR(properties.apiVersion) << '.'
              << VK_API_VERSION_MINOR(properties.apiVersion) << '.'
              << VK_API_VERSION_PATCH(properties.apiVersion) << '\n';
    std::cout << "Vulkan limits push_constants=" << limits.pushConstantBytes
              << " descriptor_sets=" << limits.descriptorSetCount
              << " uniform_alignment=" << limits.uniformBufferOffsetAlignment << '\n';

    int result = EXIT_SUCCESS;
    while (glfwWindowShouldClose(window.get()) != GLFW_TRUE) {
        glfwPollEvents();
        if (glfwWindowShouldClose(window.get()) == GLFW_TRUE) {
            break;
        }

        if (windowState.resizePending) {
            windowState.resizePending = false;
            if (!recreateSwapchain(window.get(), *swapchain, *frameLoop, windowState, metrics)) {
                result = EXIT_FAILURE;
                break;
            }
            continue;
        }

        const rb::vulkan::FrameResult frameResult = frameLoop->draw(*swapchain);
        if (frameResult == rb::vulkan::FrameResult::fatal) {
            result = EXIT_FAILURE;
            break;
        }
        if (frameResult == rb::vulkan::FrameResult::needsRecreate &&
            !recreateSwapchain(window.get(), *swapchain, *frameLoop, windowState, metrics)) {
            result = EXIT_FAILURE;
            break;
        }
    }

    metrics.frames = frameLoop->frameCount();
    metrics.resizeEvents = windowState.resizeEvents;
    if (!frameLoop->waitIdle()) {
        result = EXIT_FAILURE;
    }
    return result;
}

}

int main() {
    auto validation = std::make_shared<rb::vulkan::ValidationCounter>();
    TriangleMetrics metrics;
    int result = runTriangle(validation, metrics);
    if (validation->errors() != 0U) {
        result = EXIT_FAILURE;
    }

    std::cout << std::fixed << std::setprecision(3)
              << "Vulkan triangle summary frames=" << metrics.frames
              << " resize_events=" << metrics.resizeEvents << " recreates=" << metrics.recreates
              << " minimize_waits=" << metrics.minimizeWaits
              << " recreate_total_ms=" << metrics.recreateTotalMilliseconds
              << " recreate_max_ms=" << metrics.recreateMaximumMilliseconds
              << " validation_total=" << validation->total()
              << " validation_verbose=" << validation->verbose()
              << " validation_info=" << validation->info()
              << " validation_warnings=" << validation->warnings()
              << " validation_errors=" << validation->errors() << '\n';
    return result;
}
