#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d12.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace {

std::wstring widen(const char* text)
{
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (needed <= 0) {
        return L"AXRB_Windows_D3D12_TestTexture";
    }
    std::wstring out(static_cast<size_t>(needed - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, out.data(), needed);
    return out;
}

void print_hr(const char* label, HRESULT hr)
{
    std::fprintf(stderr, "%s failed: 0x%08lx\n", label, static_cast<unsigned long>(hr));
}

template <typename T>
class ComPtr {
public:
    ~ComPtr()
    {
        if (ptr_ != nullptr) {
            ptr_->Release();
        }
    }

    T* get() const { return ptr_; }
    T** put()
    {
        if (ptr_ != nullptr) {
            ptr_->Release();
            ptr_ = nullptr;
        }
        return &ptr_;
    }

private:
    T* ptr_ = nullptr;
};

} // namespace

int main(int argc, char** argv)
{
    const char* name_arg = argc >= 2 ? argv[1] : "AXRB_Windows_D3D12_TestTexture";
    const int hold_seconds = argc >= 3 ? std::atoi(argv[2]) : 60;
    const std::wstring name = widen(name_arg);

    ComPtr<ID3D12Device> device;
    HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(device.put()));
    if (FAILED(hr)) {
        print_hr("D3D12CreateDevice", hr);
        return 1;
    }

    D3D12_HEAP_PROPERTIES heap_properties{};
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heap_properties.CreationNodeMask = 1;
    heap_properties.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 64;
    desc.Height = 64;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clear_value{};
    clear_value.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    clear_value.Color[0] = 0.8f;
    clear_value.Color[1] = 0.1f;
    clear_value.Color[2] = 0.1f;
    clear_value.Color[3] = 1.0f;

    ComPtr<ID3D12Resource> resource;
    hr = device.get()->CreateCommittedResource(&heap_properties,
                                               D3D12_HEAP_FLAG_SHARED,
                                               &desc,
                                               D3D12_RESOURCE_STATE_COMMON,
                                               &clear_value,
                                               IID_PPV_ARGS(resource.put()));
    if (FAILED(hr)) {
        print_hr("CreateCommittedResource", hr);
        return 1;
    }

    HANDLE shared_handle = nullptr;
    hr = device.get()->CreateSharedHandle(resource.get(), nullptr, GENERIC_ALL, name.c_str(), &shared_handle);
    if (FAILED(hr)) {
        print_hr("CreateSharedHandle", hr);
        return 1;
    }

    std::fprintf(stderr,
                 "AXRB Windows D3D12 producer READY name=%s handle=%p hold_seconds=%d\n",
                 name_arg,
                 shared_handle,
                 hold_seconds);
    std::this_thread::sleep_for(std::chrono::seconds(hold_seconds));
    CloseHandle(shared_handle);
    return 0;
}
