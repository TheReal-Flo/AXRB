#pragma once
#if defined(_WIN32)
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <chrono>
#include "windows_gpu_frame.h"

namespace axrb::host {
class WindowsGpuReceiver {
public:
    ~WindowsGpuReceiver() { if (cacheHandle_) CloseHandle(cacheHandle_); }
    // Keep a host-owned GPU copy so the guest can reuse shared images as soon
    // as this copy finishes, even when OpenXR rotates through swapchain images.
    bool receive(ID3D11Device* device, ID3D11DeviceContext* context,
                 const protocol::WindowsGpuFrame& frame, uint64_t sequence,
                 UINT width, UINT height, DXGI_FORMAT targetFormat) {
        if (session_ == frame.session && sequence_ == sequence && cached_) return true;
        if (!frame.session || frame.formats[0] != frame.formats[1]) return false;
        if (session_ != frame.session || width_ != width || height_ != height) {
            cached_.Reset(); renderCache_.Reset(); shared_[0].Reset(); shared_[1].Reset();
            if (cacheHandle_) { CloseHandle(cacheHandle_); cacheHandle_ = nullptr; }
            Microsoft::WRL::ComPtr<ID3D11Device1> device1;
            if (FAILED(device->QueryInterface(IID_PPV_ARGS(&device1)))) return false;
            for (UINT eye = 0; eye < 2; ++eye) {
                wchar_t name[96]; swprintf_s(name, L"Local\\AXRB_GPU_%016llx_%u", frame.session, eye);
                if (FAILED(device1->OpenSharedResourceByName(name, DXGI_SHARED_RESOURCE_READ, IID_PPV_ARGS(&shared_[eye])))) return false;
                D3D11_TEXTURE2D_DESC source{}; shared_[eye]->GetDesc(&source);
                if (source.Width != width || source.Height != height || source.ArraySize != 1 || source.SampleDesc.Count != 1) return false;
                bool rgba = source.Format == DXGI_FORMAT_R8G8B8A8_UNORM || source.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
                bool targetRgba = targetFormat == DXGI_FORMAT_R8G8B8A8_UNORM || targetFormat == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
                bool bgra = source.Format == DXGI_FORMAT_B8G8R8A8_UNORM || source.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
                bool targetBgra = targetFormat == DXGI_FORMAT_B8G8R8A8_UNORM || targetFormat == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
                if (!(rgba && targetRgba) && !(bgra && targetBgra)) return false;
            }
            D3D11_TEXTURE2D_DESC desc{}; desc.Width = width; desc.Height = height;
            desc.MipLevels = 1; desc.ArraySize = 2; desc.SampleDesc.Count = 1; desc.Format = targetFormat;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED;
            if (FAILED(device->CreateTexture2D(&desc, nullptr, &cached_))) return false;
            Microsoft::WRL::ComPtr<IDXGIResource1> resource;
            if (FAILED(cached_.As(&resource)) || FAILED(resource->CreateSharedHandle(nullptr,
                    DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &cacheHandle_))) return false;
            session_ = frame.session; width_ = width; height_ = height;
        }
        for (UINT eye = 0; eye < 2; ++eye) context->CopySubresourceRegion(cached_.Get(), eye, 0, 0, 0, shared_[eye].Get(), 0, nullptr);
        if (!wait_copy(device, context, receiveCompletion_)) return false;
        sequence_ = sequence; return true;
    }
    bool copy_to(ID3D11DeviceContext* context, ID3D11Texture2D* destination, UINT firstSlice = 0, UINT sliceCount = 2) {
        D3D11_TEXTURE2D_DESC destinationInfo{}; destination->GetDesc(&destinationInfo);
        if (sliceCount < 1 || sliceCount > 2 || firstSlice + sliceCount > destinationInfo.ArraySize) return false;
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        context->GetDevice(&device);
        if (!renderCache_) {
            Microsoft::WRL::ComPtr<ID3D11Device1> device1;
            if (FAILED(device.As(&device1)) || FAILED(device1->OpenSharedResource1(
                    cacheHandle_, IID_PPV_ARGS(&renderCache_)))) return false;
        }
        for (UINT eye = 0; eye < sliceCount; ++eye) context->CopySubresourceRegion(destination, firstSlice + eye, 0, 0, 0, renderCache_.Get(), eye, nullptr);
        // The caller holds the cache mutex until this cross-device read is
        // finished. The receiver may then safely overwrite the shared cache.
        return wait_copy(device.Get(), context, renderCompletion_);
    }
private:
    static bool wait_copy(ID3D11Device* device, ID3D11DeviceContext* context,
                          Microsoft::WRL::ComPtr<ID3D11Query>& completion) {
        Microsoft::WRL::ComPtr<ID3D11Device> queryDevice;
        if (completion) completion->GetDevice(&queryDevice);
        if (queryDevice.Get() != device) completion.Reset();
        D3D11_QUERY_DESC desc{D3D11_QUERY_EVENT, 0};
        if (!completion && FAILED(device->CreateQuery(&desc, &completion))) return false;
        context->End(completion.Get()); context->Flush();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        HRESULT result;
        while ((result = context->GetData(completion.Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH)) == S_FALSE) {
            if (std::chrono::steady_clock::now() > deadline) { completion.Reset(); return false; }
            SwitchToThread();
        }
        return SUCCEEDED(result);
    }
    uint64_t session_ = 0, sequence_ = UINT64_MAX;
    UINT width_ = 0, height_ = 0;
    HANDLE cacheHandle_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> shared_[2], cached_, renderCache_;
    Microsoft::WRL::ComPtr<ID3D11Query> receiveCompletion_, renderCompletion_;
};
}
#endif
