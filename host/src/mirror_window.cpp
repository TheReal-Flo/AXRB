#include "mirror_window.h"
#if defined(_WIN32)
#include <algorithm>
#include <cstdio>

namespace axrb::host {
MirrorWindow::~MirrorWindow() {
    if (window_) DestroyWindow(window_);
    if (closeRequest_) CloseHandle(closeRequest_);
    if (closeReady_) CloseHandle(closeReady_);
}

bool MirrorWindow::open(ID3D11Device* device, const std::string& gameName) {
    wchar_t eventName[256]{};
    auto length = GetEnvironmentVariableW(L"AXRB_CLOSE_EVENT", eventName, 256);
    if (length && length < 256) {
        closeRequest_ = OpenEventW(EVENT_MODIFY_STATE, FALSE, eventName);
        std::wstring readyName = std::wstring(eventName) + L".ready";
        closeReady_ = OpenEventW(SYNCHRONIZE, FALSE, readyName.c_str());
    }
    device_ = device;
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) ||
        FAILED(dxgiDevice->GetAdapter(&adapter)) ||
        FAILED(adapter->GetParent(IID_PPV_ARGS(&factory_)))) return false;
    WNDCLASSW wc{};
    wc.lpfnWndProc = window_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"AXRBMirror";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(102));
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    std::string title = gameName + " | AXRB";
    length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, title.data(), static_cast<int>(title.size()), nullptr, 0);
    if (!length) return false;
    std::wstring wide(length, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, title.data(), static_cast<int>(title.size()), wide.data(), length);
    window_ = CreateWindowExW(0, wc.lpszClassName, wide.c_str(), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 680, 720, nullptr, nullptr, wc.hInstance, this);
    if (!window_) return false;
    surface_ = CreateWindowExW(0, L"STATIC", nullptr, WS_CHILD | WS_VISIBLE,
        0, 0, 1, 1, window_, nullptr, wc.hInstance, nullptr);
    if (!surface_) return false;
    factory_->MakeWindowAssociation(window_, DXGI_MWA_NO_ALT_ENTER);
    layout();
    ShowWindow(window_, SW_SHOWNORMAL);
    // The launcher hides the console with STARTF_USESHOWWINDOW. Windows uses
    // that startup value for the first ShowWindow call, so explicitly show
    // the requested preview after consuming it.
    ShowWindow(window_, SW_SHOW);
    std::fprintf(stderr, "AXRB Mirror: opened %s\n", title.c_str());
    return true;
}

LRESULT CALLBACK MirrorWindow::window_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* self = reinterpret_cast<MirrorWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<MirrorWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self) {
        if (message == WM_CLOSE) {
            if (self->closeRequest_ && self->closeReady_) {
                if (!self->closeDeadline_) {
                    self->closeDeadline_ = GetTickCount64() + 30000;
                    SetEvent(self->closeRequest_);
                    std::fprintf(stderr, "AXRB Mirror: waiting for Android save/pause before closing\n");
                }
                return 0;
            }
            self->closed_ = true; DestroyWindow(window); return 0;
        }
        if (message == WM_DESTROY) { self->closed_ = true; self->window_ = nullptr; return 0; }
        if (message == WM_SIZE) { self->layout(); return 0; }
    }
    return DefWindowProcW(window, message, w, l);
}

void MirrorWindow::layout() {
    dirty_ = true;
    if (!window_ || !surface_) return;
    RECT rect{}; GetClientRect(window_, &rect);
    int w = rect.right, h = rect.bottom;
    if (w <= 0 || h <= 0) return;
    const double scale = (std::min)(double(w) / width_, double(h) / height_);
    int imageW = (std::max)(1, int(width_ * scale));
    int imageH = (std::max)(1, int(height_ * scale));
    MoveWindow(surface_, (w-imageW)/2, (h-imageH)/2, imageW, imageH, TRUE);
    InvalidateRect(window_, nullptr, TRUE);
}

bool MirrorWindow::pump() {
    if (closeReady_ && (WaitForSingleObject(closeReady_, 0) == WAIT_OBJECT_0 ||
        (closeDeadline_ && GetTickCount64() >= closeDeadline_))) {
        closed_ = true;
        if (window_) DestroyWindow(window_);
    }
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) closed_ = true;
        TranslateMessage(&message); DispatchMessageW(&message);
    }
    return !closed_;
}

void MirrorWindow::present(ID3D11DeviceContext* context, ID3D11Texture2D* source,
                          UINT width, UINT height, DXGI_FORMAT format, uint64_t sequence) {
    if (sequence == sequence_ && !dirty_) return;
    if (closed_ || failed_ || !window_ || IsIconic(window_)) return;
    if (format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) format = DXGI_FORMAT_R8G8B8A8_UNORM;
    if (format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) format = DXGI_FORMAT_B8G8R8A8_UNORM;
    if (!swapchain_ || width != width_ || height != height_ || format != format_) {
        swapchain_.Reset();
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = width; desc.Height = height; desc.Format = format;
        desc.SampleDesc.Count = 1; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2; desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        HRESULT result = factory_->CreateSwapChainForHwnd(device_.Get(), surface_, &desc, nullptr, nullptr, &swapchain_);
        if (FAILED(result)) {
            std::fprintf(stderr, "AXRB Mirror: swapchain failed 0x%lx\n", result); failed_ = true; return;
        }
        width_ = width; height_ = height; format_ = format; layout();
    }
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    if (FAILED(swapchain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) return;
    D3D11_BOX box{0, 0, 0, width, height, 1};
    context->CopySubresourceRegion(backBuffer.Get(), 0, 0, 0, 0, source, 0, &box);
    HRESULT result = swapchain_->Present(0, DXGI_PRESENT_DO_NOT_WAIT);
    if (SUCCEEDED(result)) { sequence_ = sequence; dirty_ = false; }
    if (FAILED(result) && result != DXGI_ERROR_WAS_STILL_DRAWING) {
        std::fprintf(stderr, "AXRB Mirror: present failed 0x%lx\n", result); failed_ = true;
    }
}
}
#endif
