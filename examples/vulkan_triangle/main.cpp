#include "examples/vulkan_triangle/MeshDepthPass.h"
#include "examples/vulkan_triangle/MaterialPass.h"
#include "examples/vulkan_triangle/ObjectsPass.h"
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
#include <cstring>
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
    bool objectsPassed{false};
    bool meshDepthRan{false};
    bool meshDepthPassed{false};
    bool materialsRan{false};
    bool materialsPassed{false};
    bool synchronizationValidation{false};
    bool deviceLost{false};
};

struct ProbeOptions {
    bool objectsOnly{false};
    bool meshDepthOnly{false};
    bool materialsOnly{false};
    std::string meshDepthBaseline;
    std::string meshDepthOut;
    std::string materialsBaseline;
    std::string materialsOut;
    bool requireSynchronizationValidation{false};
    rb::vulkan::DeviceLossSite forceLoss{rb::vulkan::DeviceLossSite::none};
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
                TriangleMetrics& metrics, const ProbeOptions& options) {
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
    if (options.objectsOnly || options.meshDepthOnly || options.materialsOnly) {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    }
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
    metrics.synchronizationValidation = instance->synchronizationValidationEnabled();
    if (options.requireSynchronizationValidation && !metrics.synchronizationValidation) {
        std::cerr << "Vulkan synchronization validation is unavailable\n";
        return EXIT_FAILURE;
    }
    auto device = rb::vulkan::Device::create(*instance);
    if (!device) {
        return EXIT_FAILURE;
    }

    const ObjectsPassPaths objectsPaths{RB_VULKAN_OBJECTS_VERTEX_SPV,
                                        RB_VULKAN_OBJECTS_FRAGMENT_SPV,
                                        RB_VULKAN_TRIANGLE_CACHE};
    metrics.objectsPassed = runObjectsPass(*device, objectsPaths);
    if (!metrics.objectsPassed || options.objectsOnly) {
        return metrics.objectsPassed ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    const MeshDepthPassPaths meshDepthPaths{
        RB_VULKAN_FLAT_VERTEX_SPV,  RB_VULKAN_FLAT_FRAGMENT_SPV,
        RB_VULKAN_DEPTH_VERTEX_SPV, RB_VULKAN_DEPTH_FRAGMENT_SPV,
        RB_VULKAN_PICK_VERTEX_SPV,  RB_VULKAN_PICK_FRAGMENT_SPV,
        options.meshDepthBaseline,  options.meshDepthOut};
    metrics.meshDepthRan = true;
    metrics.meshDepthPassed = runMeshDepthPass(*device, meshDepthPaths);
    if (!metrics.meshDepthPassed || options.meshDepthOnly) {
        return metrics.meshDepthPassed ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    const MaterialPassPaths materialPaths{RB_VULKAN_LIT_VERTEX_SPV,
                                          RB_VULKAN_PBR_FRAGMENT_SPV,
                                          RB_VULKAN_PHONG_FRAGMENT_SPV,
                                          RB_VULKAN_DEPTH_VERTEX_SPV,
                                          RB_VULKAN_DEPTH_FRAGMENT_SPV,
                                          RB_VULKAN_SKYBOX_VERTEX_SPV,
                                          RB_VULKAN_SKYBOX_FRAGMENT_SPV,
                                          RB_VULKAN_CONVOLVE_VERTEX_SPV,
                                          RB_VULKAN_CONVOLVE_FRAGMENT_SPV,
                                          RB_VULKAN_FULLSCREEN_VERTEX_SPV,
                                          RB_VULKAN_PREFILTER_FRAGMENT_SPV,
                                          RB_VULKAN_DOWNSAMPLE_FRAGMENT_SPV,
                                          RB_VULKAN_UPSAMPLE_FRAGMENT_SPV,
                                          RB_VULKAN_COMPOSITE_FRAGMENT_SPV,
                                          RB_VULKAN_FXAA_FRAGMENT_SPV,
                                          RB_VULKAN_WATER_VERTEX_SPV,
                                          RB_VULKAN_WATER_FRAGMENT_SPV,
                                          options.materialsBaseline,
                                          options.materialsOut};
    metrics.materialsRan = true;
    metrics.materialsPassed = runMaterialPass(*device, materialPaths);
    if (!metrics.materialsPassed || options.materialsOnly) {
        return metrics.materialsPassed ? EXIT_SUCCESS : EXIT_FAILURE;
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

    const auto vertexCode = loadSpirvFile(RB_VULKAN_TRIANGLE_VERTEX_SPV);
    const auto fragmentCode = loadSpirvFile(RB_VULKAN_TRIANGLE_FRAGMENT_SPV);
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
    bool lossArmed = false;
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

        if (options.forceLoss != rb::vulkan::DeviceLossSite::none && !lossArmed &&
            frameLoop->frameCount() >= 20U) {
            frameLoop->simulateDeviceLoss(options.forceLoss);
            lossArmed = true;
        }

        const rb::vulkan::FrameResult frameResult = frameLoop->draw(*swapchain);
        if (frameResult == rb::vulkan::FrameResult::deviceLost) {
            metrics.deviceLost = true;
            result = EXIT_FAILURE;
            break;
        }
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

int main(int argc, char** argv) {
    constexpr const char* baselinePrefix = "--mesh-depth-baseline=";
    constexpr const char* outPrefix = "--mesh-depth-out=";
    constexpr const char* materialsBaselinePrefix = "--materials-baseline=";
    constexpr const char* materialsOutPrefix = "--materials-out=";
    ProbeOptions options;
    for (int index = 1; index < argc; ++index) {
        const char* argument = argv[index];
        if (std::strcmp(argument, "--objects-only") == 0) {
            options.objectsOnly = true;
        } else if (std::strcmp(argument, "--mesh-depth-only") == 0) {
            options.meshDepthOnly = true;
        } else if (std::strcmp(argument, "--materials-only") == 0) {
            options.materialsOnly = true;
        } else if (std::strcmp(argument, "--require-sync-validation") == 0) {
            options.requireSynchronizationValidation = true;
        } else if (std::strncmp(argument, baselinePrefix, std::strlen(baselinePrefix)) == 0) {
            options.meshDepthBaseline = argument + std::strlen(baselinePrefix);
        } else if (std::strncmp(argument, outPrefix, std::strlen(outPrefix)) == 0) {
            options.meshDepthOut = argument + std::strlen(outPrefix);
        } else if (std::strncmp(argument, materialsBaselinePrefix,
                                std::strlen(materialsBaselinePrefix)) == 0) {
            options.materialsBaseline = argument + std::strlen(materialsBaselinePrefix);
        } else if (std::strncmp(argument, materialsOutPrefix,
                                std::strlen(materialsOutPrefix)) == 0) {
            options.materialsOut = argument + std::strlen(materialsOutPrefix);
        } else if (std::strcmp(argument, "--force-device-loss=submit") == 0) {
            options.forceLoss = rb::vulkan::DeviceLossSite::submit;
        } else if (std::strcmp(argument, "--force-device-loss=present") == 0) {
            options.forceLoss = rb::vulkan::DeviceLossSite::present;
        } else {
            std::cerr << "usage vulkan_triangle [--objects-only] [--mesh-depth-only]"
                         " [--materials-only]"
                         " [--mesh-depth-baseline=<depth.raw>] [--mesh-depth-out=<dir>]"
                         " [--materials-baseline=<materials.ppm>] [--materials-out=<dir>]"
                         " [--require-sync-validation]"
                         " [--force-device-loss=submit|present]\n";
            return EXIT_FAILURE;
        }
    }
    const int passFlagCount = static_cast<int>(options.objectsOnly) +
                              static_cast<int>(options.meshDepthOnly) +
                              static_cast<int>(options.materialsOnly);
    if (passFlagCount > 1) {
        std::cerr << "pass-only flags are mutually exclusive\n";
        return EXIT_FAILURE;
    }
    if ((options.objectsOnly || options.meshDepthOnly || options.materialsOnly) &&
        options.forceLoss != rb::vulkan::DeviceLossSite::none) {
        std::cerr << "--force-device-loss needs the window loop and excludes the pass flags\n";
        return EXIT_FAILURE;
    }

    auto validation = std::make_shared<rb::vulkan::ValidationCounter>();
    TriangleMetrics metrics;
    int result = runTriangle(validation, metrics, options);
    if (validation->errors() != 0U) {
        result = EXIT_FAILURE;
    }

    std::cout << std::fixed << std::setprecision(3)
              << "Vulkan triangle summary frames=" << metrics.frames
              << " resize_events=" << metrics.resizeEvents << " recreates=" << metrics.recreates
              << " minimize_waits=" << metrics.minimizeWaits
              << " recreate_total_ms=" << metrics.recreateTotalMilliseconds
              << " recreate_max_ms=" << metrics.recreateMaximumMilliseconds
              << " objects=" << (metrics.objectsPassed ? "pass" : "fail")
              << " mesh_depth="
              << (metrics.meshDepthRan ? (metrics.meshDepthPassed ? "pass" : "fail") : "skipped")
              << " materials="
              << (metrics.materialsRan ? (metrics.materialsPassed ? "pass" : "fail") : "skipped")
              << " sync_validation=" << (metrics.synchronizationValidation ? 1 : 0)
              << " device_lost=" << (metrics.deviceLost ? 1 : 0)
              << " validation_total=" << validation->total()
              << " validation_verbose=" << validation->verbose()
              << " validation_info=" << validation->info()
              << " validation_warnings=" << validation->warnings()
              << " validation_errors=" << validation->errors() << '\n';
    return result;
}
