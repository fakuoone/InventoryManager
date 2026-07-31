#pragma once

#include <array>
#include <vector>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "logger.hpp"

#include "imgui.h"

class ImGuiRenderContext {
  public:
    ImGuiRenderContext();
    ~ImGuiRenderContext();

    static void setLogger(Logger* cLogger);
    bool pollEvents();
    bool beginFrame();
    void endFrame();

  private:
    static inline Logger* logger_ = nullptr;
    GLFWwindow* window_ = nullptr;
    std::vector<std::string> glfwRequiredExtensions_;

    // Vulkan
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;

    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;

    uint32_t graphicsQueueFamily_ = 0;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;

    static constexpr std::array<const char*, 1> validationLayers_ = {"VK_LAYER_KHRONOS_validation"};

    // Swapchain
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;

    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainImageViews_;
    std::vector<VkFramebuffer> framebuffers_;

    VkFormat swapchainFormat_;
    VkExtent2D swapchainExtent_;

    // Rendering
    VkRenderPass renderPass_ = VK_NULL_HANDLE;

    // Commands
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers_;

    // Sync
    VkSemaphore imageAvailable_;
    VkSemaphore renderFinished_;
    VkFence inFlightFence_;

    // ImGui
    VkDescriptorPool descriptorPool_;

    ImGuiIO* io_ = nullptr;
    ImGuiStyle* style_ = nullptr;
    ImVec4 clearColor_{0.45f, 0.55f, 0.60f, 1.0f};

  private:
    void initGlfwTest();
    void initVulkan();
    bool checkValidationLayerSupport();
    void createInstance();
    void createSurface();
    void pickPhysicalDevice();
    void createLogicalDevice();

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