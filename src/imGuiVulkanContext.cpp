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

ImGuiRenderContext::ImGuiRenderContext() {}
ImGuiRenderContext::~ImGuiRenderContext() {}

bool ImGuiRenderContext::pollEvents() {
    return false;
}

bool ImGuiRenderContext::beginFrame() {
    return false;
}

void ImGuiRenderContext::endFrame() {}