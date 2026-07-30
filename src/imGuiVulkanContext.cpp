#include <stdexcept>
#include <vector>

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include "imgui.h"
#include "userInterface/imGuiVulkanContext.hpp"

void ImGuiRenderContext::createInstance() {}

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
    if (!glfwInit()) { throw std::runtime_error("Failed to initialize glfw."); }
    glfwSetErrorCallback(ImGuiRenderContext::errorCallback);

    window_ = glfwCreateWindow(1920, 1080, "Inventory Manager", NULL, NULL);
    if (!window_) { throw std::runtime_error("Could't create window."); }

    glfwMakeContextCurrent(window_);
}
ImGuiRenderContext::~ImGuiRenderContext() {
    glfwDestroyWindow(window_);
    glfwTerminate();
}

void ImGuiRenderContext::setLogger(Logger* logger) {
    logger_ = logger;
}

bool ImGuiRenderContext::pollEvents() {
    return !glfwWindowShouldClose(window_);
}

bool ImGuiRenderContext::beginFrame() {
    return false;
}

void ImGuiRenderContext::endFrame() {}