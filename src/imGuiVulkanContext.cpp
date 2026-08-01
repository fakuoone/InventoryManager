#include <cstdint>
#include <cstring>
#include <format>
#include <map>
#include <stdexcept>
#include <strings.h>
#include <vector>

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

#include "userInterface/imGuiVulkanContext.hpp"

void ImGuiRenderContext::initGlfw() {
    if (!glfwInit()) { throw std::runtime_error("Failed to initialize glfw."); }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwSetErrorCallback(ImGuiRenderContext::errorCallback);

    window_ = glfwCreateWindow(1920, 1080, "Inventory Manager", NULL, NULL);
    if (!window_) { throw std::runtime_error("Could't create window."); }

    // glfwMakeContextCurrent(window_);
}

bool ImGuiRenderContext::checkValidationLayerSupport() {
    if (!debug_) { return true; }
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> availableLayers(layerCount);
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

void ImGuiRenderContext::initVulkan() {
    createInstance();
    setupDebugMessenger();
    pickPhysicalDevice();
    createLogicalDevice();
}

VkDebugUtilsMessengerCreateInfoEXT ImGuiRenderContext::createDebugMessengerCreateInfo() {
    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;
    return createInfo;
}

VkResult ImGuiRenderContext::createDebugUtilsMessengerExt(const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
                                                          VkDebugUtilsMessengerEXT* pDebugMessenger) {
    auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
    if (func != nullptr) {
        return func(instance_, pCreateInfo, nullptr, pDebugMessenger);
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

void ImGuiRenderContext::destroyDebugUtilsMessengerExt() {
    auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
    if (func != nullptr) { func(instance_, debugMessenger_, nullptr); }
}

void ImGuiRenderContext::setupDebugMessenger() {
    auto createInfo = createDebugMessengerCreateInfo();
    if (createDebugUtilsMessengerExt(&createInfo, &debugMessenger_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to set up debug messenger.");
    }
}

void ImGuiRenderContext::getRequiredExtensions() {
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    glfwRequiredExtensions_ = std::vector<const char*>(glfwExtensions, glfwExtensions + glfwExtensionCount);
    if (debug_) { glfwRequiredExtensions_.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME); }
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
    createInfo.ppEnabledLayerNames = 0;

    // Add debug functionality
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (debug_) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers_.size());
        createInfo.ppEnabledLayerNames = validationLayers_.data();
        auto debugCreateInfo = createDebugMessengerCreateInfo();
        createInfo.pNext = static_cast<VkDebugUtilsMessengerCreateInfoEXT*>(&debugCreateInfo);
    }

    // Get required extensions
    getRequiredExtensions();

    // Get available extensions
    uint32_t deviceExtensionCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &deviceExtensionCount, nullptr);
    std::vector<VkExtensionProperties> deviceExtensions(deviceExtensionCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &deviceExtensionCount, deviceExtensions.data());

    // TOOD: Compare extensions
    for (const auto& extension : deviceExtensions) {
        logger_->pushLog(Log{std::format("Device provides extension {}.", std::string{extension.extensionName})});
    }

    createInfo.enabledExtensionCount = glfwRequiredExtensions_.size();
    createInfo.ppEnabledExtensionNames = glfwRequiredExtensions_.data();
    createInfo.enabledLayerCount = 0;
    VkResult result = vkCreateInstance(&createInfo, nullptr, &instance_);
    if (result != VK_SUCCESS) { throw std::runtime_error("Failed to create vulkan instance."); }
}

void ImGuiRenderContext::pickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr);
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data());

    std::multimap<int, VkPhysicalDevice> candidates;

    for (const auto& device : devices) {
        int score = rateDeviceSuitability(device);
        candidates.insert(std::make_pair(score, device));
    }

    if (candidates.rbegin()->first > 0) {
        physicalDevice_ = candidates.rbegin()->second;
    } else {
        throw std::runtime_error("failed to find a suitable GPU!");
    }
}

uint32_t ImGuiRenderContext::rateDeviceSuitability(const VkPhysicalDevice& device) {
    VkPhysicalDeviceProperties deviceProperties;
    VkPhysicalDeviceFeatures deviceFeatures;
    vkGetPhysicalDeviceProperties(device, &deviceProperties);
    vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

    if (!deviceFeatures.geometryShader) { return 0; }

    int score = 0;
    if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) { score += 1000; }
    score += deviceProperties.limits.maxImageDimension2D;
    QueueFamilyIndices indices = findQueueFamilies(device);

    if (!indices.isComplete()) { return 0; } // TODO

    return score;
}

QueueFamilyIndices ImGuiRenderContext::findQueueFamilies(const VkPhysicalDevice& device) {
    QueueFamilyIndices indices;
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    for (uint32_t i = 0; i < queueFamilies.size(); i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { indices.graphicsFamily = i; }
        if (indices.isComplete()) { break; }
    }

    return indices;
}

void ImGuiRenderContext::createLogicalDevice() {
    QueueFamilyIndices indices = findQueueFamilies(physicalDevice_);
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = indices.graphicsFamily.value();
    queueCreateInfo.queueCount = 1;
    float queuePrio = 1.0f;
    queueCreateInfo.pQueuePriorities = &queuePrio;

    VkPhysicalDeviceFeatures deviceFeatures{};
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = 0;
    createInfo.enabledLayerCount = 0;
    if (debug_) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers_.size());
        createInfo.ppEnabledLayerNames = validationLayers_.data();
    }

    if (vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create logical device.");
    }

    vkGetDeviceQueue(device_, indices.graphicsFamily.value(), 0, &graphicsQueue_);
}

void ImGuiRenderContext::createSurface() {}

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
    initGlfw();
    initVulkan();
}
ImGuiRenderContext::~ImGuiRenderContext() {
    if (debug_) { destroyDebugUtilsMessengerExt(); }
    vkDestroyDevice(device_, nullptr);
    vkDestroyInstance(instance_, nullptr);

    glfwDestroyWindow(window_);
    glfwTerminate();
}

void ImGuiRenderContext::setLogger(Logger* logger) {
    logger_ = logger;
}

VKAPI_ATTR VkBool32 VKAPI_CALL ImGuiRenderContext::debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                                 VkDebugUtilsMessageTypeFlagsEXT messageType,
                                                                 const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                                                 void* userData) {
    logger_->pushLog(Log{std::format("Validation layer: {}", pCallbackData->pMessage)});
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    return VK_FALSE;
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