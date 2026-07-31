#include <cstdint>
#include <cstring>
#include <print>
#include <stdexcept>
#include <vector>

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

#include "imgui.h"
#include "userInterface/imGuiVulkanContext.hpp"

void ImGuiRenderContext::initGlfwTest() {
    if (!glfwInit()) { throw std::runtime_error("Failed to initialize glfw."); }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwSetErrorCallback(ImGuiRenderContext::errorCallback);

    window_ = glfwCreateWindow(1920, 1080, "Inventory Manager", NULL, NULL);
    if (!window_) { throw std::runtime_error("Could't create window."); }

    glfwMakeContextCurrent(window_);
}

void ImGuiRenderContext::initVulkan() {
    createInstance();
}

bool ImGuiRenderContext::checkValidationLayerSupport() {
    // TODO: Only debug
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> availableLayers{layerCount};
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char* layerName : validationLayers_) {
        bool layerFound = false;

        for (const auto& layerProperties : availableLayers) {
            if (strcmp(layerName, layerProperties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }

        if (!layerFound) { return false; }
    }

    return true;
}

void ImGuiRenderContext::createInstance() {
    if (!checkValidationLayerSupport()) { throw std::runtime_error("Validation layers requested, but not available."); }
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Inventorymanager";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers_.size()); // TODO only debug
    createInfo.ppEnabledLayerNames = validationLayers_.data();

    // Get required extensions
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    glfwRequiredExtensions_.assign(glfwExtensions, glfwExtensions + glfwExtensionCount);
    glfwRequiredExtensions_.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME); // TODO only debug

    // Get available extensions
    uint32_t deviceExtensionCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &deviceExtensionCount, nullptr);
    std::vector<VkExtensionProperties> deviceExtensions{deviceExtensionCount};

    // TOOD: Compare extensions
    for (const auto& extension : deviceExtensions) {
        logger_->pushLog(Log{std::format("Device provides extension {}.", extension.extensionName)});
    }

    createInfo.enabledExtensionCount = glfwExtensionCount;
    createInfo.ppEnabledExtensionNames = glfwExtensions;
    createInfo.enabledLayerCount = 0;
    VkResult result = vkCreateInstance(&createInfo, nullptr, &instance_);
    if (result != VK_SUCCESS) { throw std::runtime_error("Failed to create vulkan instance."); }
}

void ImGuiRenderContext::createSurface() {}

void ImGuiRenderContext::pickPhysicalDevice() {}

void ImGuiRenderContext::createLogicalDevice() {}

void ImGuiRenderContext::createSwapchain() {}

void ImGuiRenderContext::createImageViews() {}

void ImGuiRenderContext::createRenderPass() {}

void ImGuiRenderContext::createFramebuffers() {}

void ImGuiRenderContext::createCommandPool() {}

void ImGuiRenderContext::createCommandBuffers() {}

void ImGuiRenderContext::createSyncObjects() {}

void ImGuiRenderContext::createDescriptorPool() {}

void ImGuiRenderContext::initImGui() {}

void ImGuiRenderContext::cleanupSwapchain() {}

void ImGuiRenderContext::errorCallback(int error, const char* description) {
    logger_->pushLog(Log{std::string{description}});
}

ImGuiRenderContext::ImGuiRenderContext() {
    if (!logger_) { throw std::runtime_error("Logger needs to be specified."); }
    initGlfwTest();
}
ImGuiRenderContext::~ImGuiRenderContext() {
    vkDestroyInstance(instance_, nullptr);

    glfwDestroyWindow(window_);
    glfwTerminate();
}

void ImGuiRenderContext::setLogger(Logger* logger) {
    logger_ = logger;
}

bool ImGuiRenderContext::pollEvents() {
    bool running = !glfwWindowShouldClose(window_);
    if (running) { glfwPollEvents(); }
    return running;
}

bool ImGuiRenderContext::beginFrame() {

    return false;
}

void ImGuiRenderContext::endFrame() {}