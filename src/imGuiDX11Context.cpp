#include "userInterface/ImGuiDX11Context.hpp"

#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

// #pragma comment(lib, "d3d11.lib")

// Forward declaration from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK ImGuiDX11Context::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;

    ImGuiDX11Context* ctx = reinterpret_cast<ImGuiDX11Context*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

    switch (msg) {
        case WM_SIZE:
            if (ctx && wParam != SIZE_MINIMIZED) {
                ctx->resizeWidth = LOWORD(lParam);
                ctx->resizeHeight = HIWORD(lParam);
            }
            return 0;

        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

ImGuiDX11Context::ImGuiDX11Context() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    io = &ImGui::GetIO();
    style = &ImGui::GetStyle();

    ImGui_ImplWin32_EnableDpiAwareness();
    float scale = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY));

    wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = ImGuiDX11Context::WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"ImGuiDX11Context";

    RegisterClassExW(&wc);

    hwnd = CreateWindowW(wc.lpszClassName, L"Dear ImGui DX11", WS_OVERLAPPEDWINDOW, 100, 100, int(1280 * scale), int(800 * scale), nullptr, nullptr, wc.hInstance, nullptr);

    SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    createDeviceD3D(hwnd);

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io->ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    ImGui::StyleColorsDark();
    style->ScaleAllSizes(scale);
    style->FontScaleDpi = scale;

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(device, deviceContext);
}

ImGuiDX11Context::~ImGuiDX11Context() {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    cleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
}

bool ImGuiDX11Context::pollEvents() {
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        if (msg.message == WM_QUIT) return false;
    }
    return true;
}

bool ImGuiDX11Context::beginFrame() {
    if (swapChainOccluded && swapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
        Sleep(10);
        return false;
    }
    swapChainOccluded = false;

    if (resizeWidth && resizeHeight) {
        cleanupRenderTarget();
        swapChain->ResizeBuffers(0, resizeWidth, resizeHeight, DXGI_FORMAT_UNKNOWN, 0);
        resizeWidth = resizeHeight = 0;
        createRenderTarget();
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    return true;
}

void ImGuiDX11Context::endFrame() {
    ImGui::Render();

    float clear[4] = {clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w};

    deviceContext->OMSetRenderTargets(1, &mainRTV, nullptr);
    deviceContext->ClearRenderTargetView(mainRTV, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    HRESULT hr = swapChain->Present(1, 0);
    swapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
}

bool ImGuiDX11Context::createDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL level;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};

    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd, &swapChain, &device, &level, &deviceContext);

    if (FAILED(hr)) return false;

    createRenderTarget();
    return true;
}

void ImGuiDX11Context::cleanupDeviceD3D() {
    cleanupRenderTarget();
    if (swapChain) swapChain->Release();
    if (deviceContext) deviceContext->Release();
    if (device) device->Release();
}

void ImGuiDX11Context::createRenderTarget() {
    ID3D11Texture2D* backBuffer = nullptr;
    swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    device->CreateRenderTargetView(backBuffer, nullptr, &mainRTV);
    backBuffer->Release();
}

void ImGuiDX11Context::cleanupRenderTarget() {
    if (mainRTV) {
        mainRTV->Release();
        mainRTV = nullptr;
    }
}
