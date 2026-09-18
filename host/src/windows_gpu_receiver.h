#pragma once
#if defined(_WIN32)
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <chrono>
#include "windows_gpu_frame.h"
#include "gpu_completion.h"

namespace axrb::host {
class WindowsGpuReceiver {
public:
    WindowsGpuReceiver() = default;
    WindowsGpuReceiver(const WindowsGpuReceiver&) = delete;
    WindowsGpuReceiver& operator=(const WindowsGpuReceiver&) = delete;
    ~WindowsGpuReceiver() { if (cacheHandle_) CloseHandle(cacheHandle_); }
    // Keep a host-owned GPU copy so the guest can reuse shared images as soon
    // as this copy finishes, even when OpenXR rotates through swapchain images.
    bool enqueue_receive(ID3D11Device* device, ID3D11DeviceContext* context,
                 const protocol::WindowsGpuFrame& frame, uint64_t sequence,
                 UINT width, UINT height, DXGI_FORMAT targetFormat) {
        if (session_ == frame.session && sequence_ == sequence && cached_) return true;
        if (!frame.session || (frame.formats[1] && frame.formats[0] != frame.formats[1])) return false;
        const UINT eyeCount = frame.formats[1] ? 2u : 1u;
        if (session_ != frame.session || width_ != width || height_ != height || eyeCount_ != eyeCount) {
            cached_.Reset(); renderCache_.Reset(); shared_[0].Reset(); shared_[1].Reset();
            if (cacheHandle_) { CloseHandle(cacheHandle_); cacheHandle_ = nullptr; }
            Microsoft::WRL::ComPtr<ID3D11Device1> device1;
            if (FAILED(device->QueryInterface(IID_PPV_ARGS(&device1)))) return false;
            for (UINT eye = 0; eye < eyeCount; ++eye) {
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
            desc.MipLevels = 1; desc.ArraySize = eyeCount; desc.SampleDesc.Count = 1; desc.Format = targetFormat;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED;
            if (FAILED(device->CreateTexture2D(&desc, nullptr, &cached_))) return false;
            Microsoft::WRL::ComPtr<IDXGIResource1> resource;
            if (FAILED(cached_.As(&resource)) || FAILED(resource->CreateSharedHandle(nullptr,
                    DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &cacheHandle_))) return false;
            session_ = frame.session; width_ = width; height_ = height; eyeCount_ = eyeCount;
            static bool reportedMono = false;
            if (eyeCount == 1 && !reportedMono) {
                std::fprintf(stderr, "AXRB GPU: mono layers use one shared image and one receive copy\n");
                reportedMono = true;
            }
        }
        for (UINT eye = 0; eye < eyeCount; ++eye) context->CopySubresourceRegion(cached_.Get(), eye, 0, 0, 0, shared_[eye].Get(), 0, nullptr);
        return true;
    }
    void commit_receive(uint64_t sequence) { sequence_ = sequence; }
    bool receive(ID3D11Device* device, ID3D11DeviceContext* context,
                 const protocol::WindowsGpuFrame& frame, uint64_t sequence,
                 UINT width, UINT height, DXGI_FORMAT targetFormat) {
        if (!enqueue_receive(device, context, frame, sequence, width, height, targetFormat) ||
            !receiveCompletion_.wait(device, context)) return false;
        commit_receive(sequence);
        return true;
    }
    // Caller must keep this frame pinned until a context-wide completion fence
    // finishes, including when a later layer fails to enqueue.
    bool enqueue_copy_to(ID3D11DeviceContext* context, ID3D11Texture2D* destination, UINT firstSlice = 0, UINT sliceCount = 2) {
        D3D11_TEXTURE2D_DESC destinationInfo{}; destination->GetDesc(&destinationInfo);
        if (sliceCount < 1 || sliceCount > 2 || firstSlice + sliceCount > destinationInfo.ArraySize) return false;
        if (!render_texture(context)) return false;
        for (UINT eye = 0; eye < sliceCount; ++eye) context->CopySubresourceRegion(destination, firstSlice + eye, 0, 0, 0, renderCache_.Get(), eyeCount_ == 1 ? 0 : eye, nullptr);
        return true;
    }
    ID3D11Texture2D* render_texture(ID3D11DeviceContext* context) {
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        context->GetDevice(&device);
        if (!renderCache_) {
            Microsoft::WRL::ComPtr<ID3D11Device1> device1;
            if (FAILED(device.As(&device1)) || FAILED(device1->OpenSharedResource1(
                    cacheHandle_, IID_PPV_ARGS(&renderCache_)))) return nullptr;
        }
        return renderCache_.Get();
    }
    bool copy_to(ID3D11DeviceContext* context, ID3D11Texture2D* destination, UINT firstSlice = 0, UINT sliceCount = 2) {
        if (!enqueue_copy_to(context, destination, firstSlice, sliceCount)) return false;
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        context->GetDevice(&device);
        return renderCompletion_.wait(device.Get(), context);
    }
private:
    uint64_t session_ = 0, sequence_ = UINT64_MAX;
    UINT width_ = 0, height_ = 0, eyeCount_ = 2;
    HANDLE cacheHandle_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> shared_[2], cached_, renderCache_;
    GpuCompletion receiveCompletion_, renderCompletion_;
};
}
#endif
