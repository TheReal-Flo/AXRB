#pragma once
#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstring>

namespace axrb {
using Microsoft::WRL::ComPtr;
struct SharedTexture {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    HANDLE handle = nullptr;
    ComPtr<ID3D11Texture2D> texture;
};

struct SharedDevice {
    VkDevice device{};
    PFN_vkGetDeviceProcAddr gdpa{};
    PFN_vkGetPhysicalDeviceMemoryProperties memoryProperties{};
    VkPhysicalDevice physical{};
    ComPtr<ID3D11Device> d3d;
    ComPtr<ID3D11DeviceContext> context;
    bool initialize(VkInstance instance, VkPhysicalDevice gpu, VkDevice vkDevice,
                    PFN_vkGetInstanceProcAddr gipa, PFN_vkGetDeviceProcAddr getDeviceProc) {
        physical = gpu; device = vkDevice; gdpa = getDeviceProc;
        memoryProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(gipa(instance, "vkGetPhysicalDeviceMemoryProperties"));
        auto getProps = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(gipa(instance, "vkGetPhysicalDeviceProperties2"));
        if (!getProps) getProps = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(gipa(instance, "vkGetPhysicalDeviceProperties2KHR"));
        if (!getProps) return false;
        VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
        VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2}; props.pNext = &id;
        getProps(gpu, &props);
        if (!id.deviceLUIDValid) return false;
        ComPtr<IDXGIFactory1> factory;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return false;
        for (UINT i = 0;; ++i) {
            ComPtr<IDXGIAdapter1> adapter;
            if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_ADAPTER_DESC1 desc{}; adapter->GetDesc1(&desc);
            if (std::memcmp(&desc.AdapterLuid, id.deviceLUID, sizeof(LUID))) continue;
            D3D_FEATURE_LEVEL level;
            HRESULT result = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &d3d, &level, &context);
            std::fprintf(stderr, "AXRB GPU: matching adapter %s, D3D11=0x%lx\n", props.properties.deviceName, result);
            return SUCCEEDED(result);
        }
        return false;
    }
    bool create(SharedTexture& out, UINT width, UINT height, VkFormat format, const wchar_t* name) {
        auto createImage = reinterpret_cast<PFN_vkCreateImage>(gdpa(device, "vkCreateImage"));
        auto getRequirements = reinterpret_cast<PFN_vkGetImageMemoryRequirements>(gdpa(device, "vkGetImageMemoryRequirements"));
        auto allocateMemory = reinterpret_cast<PFN_vkAllocateMemory>(gdpa(device, "vkAllocateMemory"));
        auto bindMemory = reinterpret_cast<PFN_vkBindImageMemory>(gdpa(device, "vkBindImageMemory"));
        auto getHandleProps = reinterpret_cast<PFN_vkGetMemoryWin32HandlePropertiesKHR>(gdpa(device, "vkGetMemoryWin32HandlePropertiesKHR"));
        if (!getHandleProps || !d3d) return false;
        DXGI_FORMAT dxgi;
        switch (format) {
        case VK_FORMAT_R8G8B8A8_UNORM: dxgi = DXGI_FORMAT_R8G8B8A8_UNORM; break;
        case VK_FORMAT_R8G8B8A8_SRGB: dxgi = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; break;
        case VK_FORMAT_B8G8R8A8_UNORM: dxgi = DXGI_FORMAT_B8G8R8A8_UNORM; break;
        case VK_FORMAT_B8G8R8A8_SRGB: dxgi = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB; break;
        default: return false;
        }
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width; desc.Height = height; desc.MipLevels = 1; desc.ArraySize = 1;
        desc.Format = dxgi; desc.SampleDesc.Count = 1;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED;
        HRESULT hr = d3d->CreateTexture2D(&desc, nullptr, &out.texture);
        if (FAILED(hr)) { std::fprintf(stderr, "AXRB GPU: CreateTexture2D=0x%lx\n", hr); return false; }
        ComPtr<IDXGIResource1> resource; out.texture.As(&resource);
        hr = resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, name, &out.handle);
        if (FAILED(hr)) { std::fprintf(stderr, "AXRB GPU: CreateSharedHandle=0x%lx\n", hr); return false; }
        VkExternalMemoryImageCreateInfo external{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
        external.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;
        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; imageInfo.pNext = &external;
        imageInfo.imageType = VK_IMAGE_TYPE_2D; imageInfo.format = format;
        imageInfo.extent = {width, height, 1}; imageInfo.mipLevels = 1; imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT; imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        VkResult result = createImage(device, &imageInfo, nullptr, &out.image);
        if (result != VK_SUCCESS) { std::fprintf(stderr, "AXRB GPU: CreateImage=%d\n", result); return false; }
        VkMemoryRequirements req{}; getRequirements(device, out.image, &req);
        VkMemoryWin32HandlePropertiesKHR handleProps{VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR};
        result = getHandleProps(device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT, out.handle, &handleProps);
        if (result != VK_SUCCESS) return false;
        uint32_t bits = req.memoryTypeBits & handleProps.memoryTypeBits;
        uint32_t index = 0; while (index < 32 && !(bits & (1u << index))) ++index;
        if (index == 32) return false;
        VkImportMemoryWin32HandleInfoKHR import{VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR};
        import.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT; import.handle = out.handle;
        VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
        dedicated.pNext = &import; dedicated.image = out.image;
        VkMemoryAllocateInfo memory{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        memory.pNext = &dedicated; memory.allocationSize = req.size; memory.memoryTypeIndex = index;
        result = allocateMemory(device, &memory, nullptr, &out.memory);
        if (result != VK_SUCCESS) { std::fprintf(stderr, "AXRB GPU: import memory=%d\n", result); return false; }
        result = bindMemory(device, out.image, out.memory, 0);
        std::fprintf(stderr, "AXRB GPU: shared texture %ux%u format=%d bind=%d\n", width, height, format, result);
        return result == VK_SUCCESS;
    }
    void destroy(SharedTexture& texture) {
        if (texture.image) reinterpret_cast<PFN_vkDestroyImage>(gdpa(device, "vkDestroyImage"))(device, texture.image, nullptr);
        if (texture.memory) reinterpret_cast<PFN_vkFreeMemory>(gdpa(device, "vkFreeMemory"))(device, texture.memory, nullptr);
        if (texture.handle) CloseHandle(texture.handle);
        texture = {};
    }
};
}
