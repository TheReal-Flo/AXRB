#pragma once
#if defined(_WIN32)
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <string>
#include <cstdint>

namespace axrb::host {
// The child surface preserves aspect ratio while DXGI scales the GPU image.
class MirrorWindow {
public:
    ~MirrorWindow();
    bool open(ID3D11Device* device, const std::string& gameName);
    bool pump();
    void present(ID3D11DeviceContext* context, ID3D11Texture2D* source,
                 UINT width, UINT height, DXGI_FORMAT format, uint64_t sequence);
private:
    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM w, LPARAM l);
    void layout();
    HWND window_ = nullptr, surface_ = nullptr;
    uint64_t sequence_ = UINT64_MAX;
    bool dirty_ = true;
    bool closed_ = false, failed_ = false;
    HANDLE closeRequest_ = nullptr, closeReady_ = nullptr;
    ULONGLONG closeDeadline_ = 0;
    UINT width_ = 1, height_ = 1;
    DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
    Microsoft::WRL::ComPtr<IDXGIFactory2> factory_;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapchain_;
};
}
#endif
