#pragma once

#include <d3d11.h>

#include "imgui.h"

class ImGuiDX11Context {
  public:
    ImGuiDX11Context(const ImGuiDX11Context&) = delete;
    ImGuiDX11Context& operator=(const ImGuiDX11Context&) = delete;

    ImGuiDX11Context();
    ~ImGuiDX11Context();

    bool pollEvents();
    bool beginFrame();
    void endFrame();

  private:
    // Win32
    WNDCLASSEXW wc_{};
    HWND hwnd_ = nullptr;

    // D3D11
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* deviceContext_ = nullptr;
    IDXGISwapChain* swapChain_ = nullptr;
    ID3D11RenderTargetView* mainRTV_ = nullptr;
    bool swapChainOccluded_ = false;
    UINT resizeWidth_ = 0;
    UINT resizeHeight_ = 0;

    // ImGui
    ImGuiIO* io_ = nullptr;
    ImGuiStyle* style_ = nullptr;
    ImVec4 clearColor_ = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

  private:
    bool createDeviceD3D(HWND hWnd);
    void cleanupDeviceD3D();
    void createRenderTarget();
    void cleanupRenderTarget();

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
};
