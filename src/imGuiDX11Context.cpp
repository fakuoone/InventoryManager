#include "userInterface/ImGuiDX11Context.hpp"

#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

// #pragma comment(lib, "d3d11.lib")

// Forward declaration from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK ImGuiDX11Context::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;

    ImGuiDX11Context* ctx = reinterpret_cast<ImGuiDX11Context*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

    switch (msg) {
    case WM_SIZE:
        if (ctx && wParam != SIZE_MINIMIZED) {
            ctx->resizeWidth_ = LOWORD(lParam);
            ctx->resizeHeight_ = HIWORD(lParam);
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

    io_ = &ImGui::GetIO();
    style_ = &ImGui::GetStyle();

    ImGui_ImplWin32_EnableDpiAwareness();
    float scale = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY));

    wc_ = {};
    wc_.cbSize = sizeof(WNDCLASSEXW);
    wc_.style = CS_CLASSDC;
    wc_.lpfnWndProc = ImGuiDX11Context::WndProc;
    wc_.hInstance = GetModuleHandle(nullptr);
    wc_.lpszClassName = L"ImGuiDX11Context";

    RegisterClassExW(&wc_);

    hwnd_ = CreateWindowW(wc_.lpszClassName,
                          L"Inventory Manager",
                          WS_OVERLAPPEDWINDOW,
                          100,
                          100,
                          int(1280 * scale),
                          int(800 * scale),
                          nullptr,
                          nullptr,
                          wc_.hInstance,
                          nullptr);

    SetWindowLongPtr(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    createDeviceD3D(hwnd_);

    ShowWindow(hwnd_, SW_SHOWDEFAULT);
    UpdateWindow(hwnd_);

    io_->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io_->ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    ImGui::StyleColorsDark();
    style_->ScaleAllSizes(scale);
    style_->FontScaleDpi = scale;

    ImGui_ImplWin32_Init(hwnd_);
    ImGui_ImplDX11_Init(device_, deviceContext_);
}

ImGuiDX11Context::~ImGuiDX11Context() {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    cleanupDeviceD3D();
    DestroyWindow(hwnd_);
    UnregisterClassW(wc_.lpszClassName, wc_.hInstance);
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
    if (swapChainOccluded_ && swapChain_->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
        Sleep(10);
        return false;
    }
    swapChainOccluded_ = false;

    if (resizeWidth_ && resizeHeight_) {
        cleanupRenderTarget();
        swapChain_->ResizeBuffers(0, resizeWidth_, resizeHeight_, DXGI_FORMAT_UNKNOWN, 0);
        resizeWidth_ = resizeHeight_ = 0;
        createRenderTarget();
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                             ImGuiWindowFlags_NoDocking;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    ImGui::Begin("Inventory Manager", nullptr, flags);

    return true;
}

void ImGuiDX11Context::endFrame() {
    ImGui::PopStyleVar(3);
    ImGui::End();

    ImGui::Render();

    float clear[4] = {clearColor_.x * clearColor_.w, clearColor_.y * clearColor_.w, clearColor_.z * clearColor_.w, clearColor_.w};

    deviceContext_->OMSetRenderTargets(1, &mainRTV_, nullptr);
    deviceContext_->ClearRenderTargetView(mainRTV_, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    HRESULT hr = swapChain_->Present(1, 0);
    swapChainOccluded_ = (hr == DXGI_STATUS_OCCLUDED);
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

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd, &swapChain_, &device_, &level, &deviceContext_);

    if (FAILED(hr)) return false;

    createRenderTarget();
    return true;
}

void ImGuiDX11Context::cleanupDeviceD3D() {
    cleanupRenderTarget();
    if (swapChain_) swapChain_->Release();
    if (deviceContext_) deviceContext_->Release();
    if (device_) device_->Release();
}

void ImGuiDX11Context::createRenderTarget() {
    ID3D11Texture2D* backBuffer = nullptr;
    swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    device_->CreateRenderTargetView(backBuffer, nullptr, &mainRTV_);
    backBuffer->Release();
}

void ImGuiDX11Context::cleanupRenderTarget() {
    if (mainRTV_) {
        mainRTV_->Release();
        mainRTV_ = nullptr;
    }
}
