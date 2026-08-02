#include <cstdint>
#include <cstring>
#include <format>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

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
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createSwapchain();
}

VkDebugUtilsMessengerCreateInfoEXT ImGuiRenderContext::createDebugMessengerCreateInfo() {
    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;
    return createInfo;
}

VkResult ImGuiRenderContext::createDebugUtilsMessengerExt(
    const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
    VkDebugUtilsMessengerEXT* pDebugMessenger) {
    auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
    if (func != nullptr) {
        return func(instance_, pCreateInfo, nullptr, pDebugMessenger);
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

void ImGuiRenderContext::destroyDebugUtilsMessengerExt() {
    auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
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

    glfwRequiredExtensions_ =
        std::vector<const char*>(glfwExtensions, glfwExtensions + glfwExtensionCount);
    if (debug_) { glfwRequiredExtensions_.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME); }
}

void ImGuiRenderContext::createInstance() {
    if (!checkValidationLayerSupport()) {
        throw std::runtime_error("Validation layers requested, but not available.");
    }
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

    createInfo.enabledExtensionCount = glfwRequiredExtensions_.size();
    createInfo.ppEnabledExtensionNames = glfwRequiredExtensions_.data();
    VkResult result = vkCreateInstance(&createInfo, nullptr, &instance_);
    if (result != VK_SUCCESS) { throw std::runtime_error("Failed to create vulkan instance."); }
}

void ImGuiRenderContext::createSurface() {
    if (glfwCreateWindowSurface(instance_, window_, nullptr, &surface_)) {
        throw std::runtime_error("Failed to create winodw surface.");
    }
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
    if (!checkDeviceExtensionSupport(device)) { return 0; }

    // Assumes swapchain extension is supported which is covered above
    bool swapChainAdequate = false;
    SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
    swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();

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
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &presentSupport);
        if (presentSupport) { indices.presentFamily = i; }
        if (indices.isComplete()) { break; }
    }

    return indices;
}

bool ImGuiRenderContext::checkDeviceExtensionSupport(const VkPhysicalDevice& device) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(
        device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(deviceExtensions_.begin(), deviceExtensions_.end());

    for (const auto& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
}

void ImGuiRenderContext::createLogicalDevice() {
    QueueFamilyIndices indices = findQueueFamilies(physicalDevice_);
    float queuePrio = 1.0f;
    std::set<uint32_t> uniqueQueueFamilies = {indices.graphicsFamily.value(),
                                              indices.presentFamily.value()};

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    queueCreateInfos.reserve(uniqueQueueFamilies.size());
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePrio;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures{};
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.queueCreateInfoCount = queueCreateInfos.size();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions_.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions_.data();
    createInfo.enabledLayerCount = 0;
    // if (debug_) { // deprecated
    //    createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers_.size());
    //    createInfo.ppEnabledLayerNames = validationLayers_.data();
    // }

    if (vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create logical device.");
    }

    vkGetDeviceQueue(device_, indices.graphicsFamily.value(), 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, indices.presentFamily.value(), 0, &presentQueue_);
}

SwapChainSupportDetails ImGuiRenderContext::querySwapChainSupport(const VkPhysicalDevice& device) {
    SwapChainSupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface_, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &formatCount, nullptr);

    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(
            device, surface_, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &presentModeCount, nullptr);

    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(
            device, surface_, &presentModeCount, details.presentModes.data());
    }

    return details;
}

VkSurfaceFormatKHR ImGuiRenderContext::chooseSwapSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR>& availableFormats) {
    if (availableFormats.empty()) { throw std::runtime_error("No swap-chain formats available."); }
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
            availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormat;
        }
    }
    return availableFormats[0];
}

VkPresentModeKHR ImGuiRenderContext::chooseSwapPresentMode(
    const std::vector<VkPresentModeKHR>& availablePresentModes) {
    if (availablePresentModes.empty()) {
        throw std::runtime_error("No swap-chain present modes available.");
    }
    for (const auto& presentMode : availablePresentModes) {
        if (presentMode == VK_PRESENT_MODE_MAILBOX_KHR) { return presentMode; }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D ImGuiRenderContext::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }
    int width, height;
    glfwGetFramebufferSize(window_, &width, &height);

    VkExtent2D actualExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
    actualExtent.width = std::clamp(
        actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    actualExtent.height = std::clamp(actualExtent.height,
                                     capabilities.minImageExtent.height,
                                     capabilities.maxImageExtent.height);
    return actualExtent;
}

void ImGuiRenderContext::createSwapchain() {
    SwapChainSupportDetails swapChainSupport = querySwapChainSupport(physicalDevice_);
    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

    uint32_t imageCount = std::clamp(swapChainSupport.capabilities.minImageCount + 1,
                                     swapChainSupport.capabilities.minImageCount,
                                     swapChainSupport.capabilities.maxImageCount == 0
                                         ? std::numeric_limits<uint32_t>::max()
                                         : swapChainSupport.capabilities.maxImageCount);

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface_;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    QueueFamilyIndices indices = findQueueFamilies(physicalDevice_);
    std::array<uint32_t, 2> queueFamilyIndices = {indices.graphicsFamily.value(),
                                                  indices.presentFamily.value()};

    if (indices.graphicsFamily != indices.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices.data();
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapchain_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create swap chain.");
    }
}

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
    vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    vkDestroySurfaceKHR(instance_, surface_, nullptr);
    vkDestroyDevice(device_, nullptr);
    vkDestroyInstance(instance_, nullptr);

    glfwDestroyWindow(window_);
    glfwTerminate();
}

void ImGuiRenderContext::setLogger(Logger* logger) {
    logger_ = logger;
}

VKAPI_ATTR VkBool32 VKAPI_CALL
ImGuiRenderContext::debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
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