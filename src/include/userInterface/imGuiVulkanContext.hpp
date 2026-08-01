#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include <vulkan/vulkan_core.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "logger.hpp"

#include "imgui.h"

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    bool isComplete() { return graphicsFamily.has_value(); }
};

class ImGuiRenderContext {
  public:
    ImGuiRenderContext();
    ~ImGuiRenderContext();

    static void setLogger(Logger* cLogger);
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                        VkDebugUtilsMessageTypeFlagsEXT messageType,
                                                        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                                        void* userData);
    bool pollEvents();
    bool beginFrame();
    void endFrame();

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
    VkDebugUtilsMessengerEXT debugMessenger_;

    VkSurfaceKHR surface_;

    VkPhysicalDevice physicalDevice_;
    VkDevice device_;

    uint32_t graphicsQueueFamily_ = 0;
    VkQueue graphicsQueue_;
    VkQueue presentQueue_;

    // Swapchain
    VkSwapchainKHR swapchain_;

    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainImageViews_;
    std::vector<VkFramebuffer> framebuffers_;

    VkFormat swapchainFormat_;
    VkExtent2D swapchainExtent_;

    // Rendering
    VkRenderPass renderPass_;

    // Commands
    VkCommandPool commandPool_;
    std::vector<VkCommandBuffer> commandBuffers_;

    // Sync
    VkSemaphore imageAvailable_;
    VkSemaphore renderFinished_;
    VkFence inFlightFence_;

    VkDescriptorPool descriptorPool_;

  private:
    // Init
    void initGlfw();
    void initVulkan();

    // Debug
    bool checkValidationLayerSupport();
    VkDebugUtilsMessengerCreateInfoEXT createDebugMessengerCreateInfo();
    VkResult createDebugUtilsMessengerExt(const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, VkDebugUtilsMessengerEXT* pDebugMessenger);
    void destroyDebugUtilsMessengerExt();
    void getRequiredExtensions();
    void setupDebugMessenger();

    // Instance
    void createInstance();

    // Physical device
    void pickPhysicalDevice();
    static uint32_t rateDeviceSuitability(const VkPhysicalDevice& device);
    static QueueFamilyIndices findQueueFamilies(const VkPhysicalDevice& device);

    // Logical device
    void createLogicalDevice();

    void createSurface();

    void createSwapchain();
    void createImageViews();

    void createRenderPass();
    void createFramebuffers();

    void createCommandPool();
    void createCommandBuffers();

    void createSyncObjects();

    void createDescriptorPool();

    void initImGui();

    void cleanupSwapchain();

    static void errorCallback(int error, const char* description);
};