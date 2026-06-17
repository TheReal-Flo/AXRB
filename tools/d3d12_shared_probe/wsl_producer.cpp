#include <wsl/winadapter.h>
#include <wsl/wrladapter.h>
#include <directx/d3d12.h>
#include <directx/dxcore.h>
#include <dxguids/dxguids.h>

#include <chrono>
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

extern "C" HRESULT D3D12CreateDevice(IUnknown* adapter,
                                     D3D_FEATURE_LEVEL minimum_feature_level,
                                     REFIID riid,
                                     void** device);
extern "C" HRESULT DXCoreCreateAdapterFactory(REFIID riid, void** factory);

namespace {

std::wstring widen(const char* text)
{
    std::mbstate_t state{};
    const char* source = text;
    const size_t length = std::mbsrtowcs(nullptr, &source, 0, &state);
    if (length == static_cast<size_t>(-1)) {
        return L"AXRB_WSL_D3D12_TestTexture";
    }

    std::wstring out(length, L'\0');
    state = {};
    source = text;
    std::mbsrtowcs(out.data(), &source, out.size(), &state);
    return out;
}

void print_hr(const char* label, HRESULT hr)
{
    std::fprintf(stderr, "%s failed: 0x%08x\n", label, static_cast<unsigned int>(hr));
}

} // namespace

int main(int argc, char** argv)
{
    std::setlocale(LC_ALL, "");

    const char* name_arg = argc >= 2 ? argv[1] : "AXRB_WSL_D3D12_TestTexture";
    const int hold_seconds = argc >= 3 ? std::atoi(argv[2]) : 60;
    const std::wstring name = widen(name_arg);

    Microsoft::WRL::ComPtr<IDXCoreAdapterFactory> factory;
    HRESULT hr = DXCoreCreateAdapterFactory(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        print_hr("DXCoreCreateAdapterFactory", hr);
        return 1;
    }

    const GUID attributes[] = {DXCORE_ADAPTER_ATTRIBUTE_D3D12_GRAPHICS};
    Microsoft::WRL::ComPtr<IDXCoreAdapterList> adapter_list;
    hr = factory->CreateAdapterList(1, attributes, IID_PPV_ARGS(&adapter_list));
    if (FAILED(hr)) {
        print_hr("CreateAdapterList", hr);
        return 1;
    }

    Microsoft::WRL::ComPtr<ID3D12Device> device;
    const uint32_t adapter_count = adapter_list->GetAdapterCount();
    for (uint32_t i = 0; i < adapter_count; ++i) {
        Microsoft::WRL::ComPtr<IDXCoreAdapter> adapter;
        hr = adapter_list->GetAdapter(i, IID_PPV_ARGS(&adapter));
        if (FAILED(hr)) {
            continue;
        }

        hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
        if (SUCCEEDED(hr)) {
            std::fprintf(stderr, "AXRB WSL D3D12 producer using DXCore adapter %u/%u\n", i + 1, adapter_count);
            break;
        }
    }

    if (!device) {
        print_hr("D3D12CreateDevice(adapter)", hr);
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
    desc.Alignment = 0;
    desc.Width = 64;
    desc.Height = 64;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clear_value{};
    clear_value.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    clear_value.Color[0] = 0.1f;
    clear_value.Color[1] = 0.2f;
    clear_value.Color[2] = 0.9f;
    clear_value.Color[3] = 1.0f;

    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    hr = device->CreateCommittedResource(&heap_properties,
                                         D3D12_HEAP_FLAG_SHARED,
                                         &desc,
                                         D3D12_RESOURCE_STATE_COMMON,
                                         &clear_value,
                                         IID_PPV_ARGS(&resource));
    if (FAILED(hr)) {
        print_hr("CreateCommittedResource", hr);
        return 1;
    }

    HANDLE shared_handle = nullptr;
    hr = device->CreateSharedHandle(resource.Get(), nullptr, GENERIC_ALL, name.c_str(), &shared_handle);
    if (FAILED(hr)) {
        print_hr("CreateSharedHandle", hr);
        return 1;
    }

    std::fprintf(stderr,
                 "AXRB WSL D3D12 producer READY name=%s handle=%p hold_seconds=%d\n",
                 name_arg,
                 shared_handle,
                 hold_seconds);
    std::fflush(stderr);

    std::this_thread::sleep_for(std::chrono::seconds(hold_seconds));
    return 0;
}
