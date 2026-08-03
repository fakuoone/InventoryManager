#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan_core.h>

#include "logger.hpp"

#include "imgui.h"

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;
    VkPhysicalDevice physicalDevice;
    bool isComplete() { return graphicsFamily.has_value() && presentFamily.has_value(); }
};

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

class ImGuiRenderContext {
  public:
    ImGuiRenderContext();
    ~ImGuiRenderContext();

    static void setLogger(Logger* cLogger);
    static VKAPI_ATTR VkBool32 VKAPI_CALL
    debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                  VkDebugUtilsMessageTypeFlagsEXT messageType,
                  const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                  void* userData);
    bool pollEvents();
    bool beginFrame();
    void endFrame();
    void renderFrame();
    void presentFrame();

  private:
    static inline Logger* logger_ = nullptr;
#ifdef NDEBUG
    static constexpr inline bool debug_ = false;
#else
    static constexpr inline bool debug_ = true;
#endif

    GLFWwindow* window_ = nullptr;
    std::vector<const char*> glfwRequiredExtensions_;

    // Vulkan
    VkInstance instance_;
    static constexpr std::array<const char*, 1> validationLayers_ = {"VK_LAYER_KHRONOS_validation"};
    static constexpr std::array<const char*, 1> deviceExtensions_ = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
    VkDebugUtilsMessengerEXT debugMessenger_;

    VkSurfaceKHR surface_;

    VkPhysicalDevice physicalDevice_;
    VkDevice device_;
    QueueFamilyIndices queueFamilyIndices_;

    uint32_t graphicsQueueFamily_ = 0;
    VkQueue graphicsQueue_;
    VkQueue presentQueue_;

    // Swapchain
    VkSwapchainKHR swapchain_;
    VkFormat swapchainFormat_;
    VkExtent2D swapchainExtent_;
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainImageViews_;
    std::vector<VkFramebuffer> swapchainFramebuffers_;

    // Rendering
    VkRenderPass renderPass_;
    uint32_t currentImageIndex_ = 0;
    uint32_t currentFrame_ = 0;
    bool frameBufferResized_;

    // Commands
    VkCommandPool commandPool_;
    std::array<VkCommandBuffer, MAX_FRAMES_IN_FLIGHT> commandBuffers_;

    // Sync
    std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> imageAvailableS_;
    std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> renderFinishedS_;
    std::array<VkFence, MAX_FRAMES_IN_FLIGHT> inFlightFenceF_;

    VkDescriptorPool descriptorPool_;

    // ImGui
    ImGuiIO* io_ = nullptr;
    ImGuiStyle* style_ = nullptr;
    ImVec4 clearColor_ = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

  private:
    // Init
    void initGlfw();
    void initVulkan();

    // Debug
    bool checkValidationLayerSupport();
    VkDebugUtilsMessengerCreateInfoEXT createDebugMessengerCreateInfo();
    VkResult createDebugUtilsMessengerExt(const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
                                          VkDebugUtilsMessengerEXT* pDebugMessenger);
    void destroyDebugUtilsMessengerExt();
    void getRequiredExtensions();
    void setupDebugMessenger();

    // Instance
    void createInstance();
    void createSurface();

    // Physical device
    void pickPhysicalDevice();
    uint32_t rateDeviceSuitability(VkPhysicalDevice device);
    void findQueueFamilies(VkPhysicalDevice device);
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);

    // Logical device
    void createLogicalDevice();

    // Swapchain
    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);
    VkSurfaceFormatKHR
    chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR
    chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
    void createSwapchain();
    void createImageViews();

    void createGraphicsPipeline();
    void createRenderPass();

    void createFramebuffers();
    void createCommandPool();
    void createCommandBuffers();
    void recordCommandBuffers(VkCommandBuffer commandBuffer);

    void createSyncObjects();

    void createDescriptorPool();

    void initImGui();

    void recreateSwapchain();
    void cleanupSwapchain();

    static void errorCallback(int error, const char* description);
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
};