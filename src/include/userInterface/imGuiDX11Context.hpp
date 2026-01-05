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
    WNDCLASSEXW wc{};
    HWND hwnd = nullptr;

    // D3D11
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* deviceContext = nullptr;
    IDXGISwapChain* swapChain = nullptr;
    ID3D11RenderTargetView* mainRTV = nullptr;
    bool swapChainOccluded = false;
    UINT resizeWidth = 0;
    UINT resizeHeight = 0;

    // ImGui
    ImGuiIO* io = nullptr;
    ImGuiStyle* style = nullptr;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

   private:
    bool createDeviceD3D(HWND hWnd);
    void cleanupDeviceD3D();
    void createRenderTarget();
    void cleanupRenderTarget();

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
};
