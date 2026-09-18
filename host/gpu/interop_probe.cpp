#include "shared_texture.h"
#include <vector>
#include <cstdlib>
#define REQUIRE(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while(0)
#define INSTANCE_FN(n) auto n = reinterpret_cast<PFN_##n>(gipa(instance, #n))
#define DEVICE_FN(n) auto n = reinterpret_cast<PFN_##n>(gdpa(device, #n))
int main(int argc, char** argv) {
    uint32_t vendor = 0;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    if (argc > 3) { std::fprintf(stderr, "Usage: axrb_gpu_interop_probe [vendor-id, e.g. 0x1002] [format: 37,43,44,50]\n"); return 2; }
    if (argc >= 2) {
        char* end = nullptr;
        vendor = static_cast<uint32_t>(std::strtoul(argv[1], &end, 0));
        if (!vendor || vendor > 0xffff || !end || *end) return 2;
    }
    if (argc == 3) {
        char* end = nullptr;
        format = static_cast<VkFormat>(std::strtoul(argv[2], &end, 10));
        if (!end || *end || (format != VK_FORMAT_R8G8B8A8_UNORM && format != VK_FORMAT_R8G8B8A8_SRGB &&
            format != VK_FORMAT_B8G8R8A8_UNORM && format != VK_FORMAT_B8G8R8A8_SRGB)) return 2;
    }
    auto loader = LoadLibraryW(L"vulkan-1.dll"); REQUIRE(loader);
    auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader, "vkGetInstanceProcAddr")); REQUIRE(gipa);
    auto createInstance = reinterpret_cast<PFN_vkCreateInstance>(gipa(nullptr, "vkCreateInstance"));
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; instanceInfo.pApplicationInfo = &app;
    VkInstance instance; REQUIRE(createInstance(&instanceInfo, nullptr, &instance) == VK_SUCCESS);
    INSTANCE_FN(vkEnumeratePhysicalDevices); INSTANCE_FN(vkGetPhysicalDeviceProperties);
    INSTANCE_FN(vkGetPhysicalDeviceQueueFamilyProperties); INSTANCE_FN(vkCreateDevice);
    uint32_t count = 0; vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> physicals(count); vkEnumeratePhysicalDevices(instance, &count, physicals.data());
    VkPhysicalDevice physical{};
    for (auto gpu : physicals) {
        VkPhysicalDeviceProperties props{}; vkGetPhysicalDeviceProperties(gpu, &props);
        std::fprintf(stderr, "Available GPU: %s (vendor=0x%x, type=%u)\n", props.deviceName, props.vendorID, props.deviceType);
        if (!physical && (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU || props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) && (!vendor || props.vendorID == vendor)) physical = gpu;
    }
    REQUIRE(physical);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count); vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());
    uint32_t family = 0; while (family < count && !(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) ++family;
    REQUIRE(family < count);
    float priority = 1; VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = family; queueInfo.queueCount = 1; queueInfo.pQueuePriorities = &priority;
    const char* extensions[] = {VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME};
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.queueCreateInfoCount = 1; deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = 1; deviceInfo.ppEnabledExtensionNames = extensions;
    VkDevice device; REQUIRE(vkCreateDevice(physical, &deviceInfo, nullptr, &device) == VK_SUCCESS);
    auto gdpa = reinterpret_cast<PFN_vkGetDeviceProcAddr>(gipa(instance, "vkGetDeviceProcAddr"));
    axrb::SharedDevice shared; REQUIRE(shared.initialize(instance, physical, device, gipa, gdpa));
    axrb::SharedTexture texture; REQUIRE(shared.create(texture, 64, 64, format, L"Local\\AXRB_Interop_Probe"));
    DEVICE_FN(vkGetDeviceQueue); DEVICE_FN(vkCreateCommandPool); DEVICE_FN(vkAllocateCommandBuffers);
    DEVICE_FN(vkBeginCommandBuffer); DEVICE_FN(vkEndCommandBuffer); DEVICE_FN(vkCmdPipelineBarrier);
    DEVICE_FN(vkCmdClearColorImage); DEVICE_FN(vkQueueSubmit); DEVICE_FN(vkQueueWaitIdle);
    std::fprintf(stderr, "probe: device functions loaded\n");
    VkQueue queue; vkGetDeviceQueue(device, family, 0, &queue);
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; poolInfo.queueFamilyIndex = family;
    std::fprintf(stderr, "probe: queue acquired\n");
    VkCommandPool pool; REQUIRE(vkCreateCommandPool(device, &poolInfo, nullptr, &pool) == VK_SUCCESS);
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; alloc.commandPool = pool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; alloc.commandBufferCount = 1;
    std::fprintf(stderr, "probe: command pool ready\n");
    VkCommandBuffer cmd; REQUIRE(vkAllocateCommandBuffers(device, &alloc, &cmd) == VK_SUCCESS);
    std::fprintf(stderr, "probe: command buffer ready\n");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; REQUIRE(vkBeginCommandBuffer(cmd, &begin) == VK_SUCCESS); std::fprintf(stderr, "probe: began\n");
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; barrier.image = texture.image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL; barrier.dstQueueFamilyIndex = family;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkClearColorValue color{{0.25f, 0.5f, 0.75f, 1}};
    vkCmdClearColorImage(cmd, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &barrier.subresourceRange);
    barrier.srcQueueFamilyIndex = family; barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = 0;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    REQUIRE(vkEndCommandBuffer(cmd) == VK_SUCCESS); std::fprintf(stderr, "probe: ended\n");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount = 1; submit.pCommandBuffers = &cmd;
    REQUIRE(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS); REQUIRE(vkQueueWaitIdle(queue) == VK_SUCCESS);
    std::fprintf(stderr, "probe: GPU finished\n");
    // Independently open the named resource as the AXRB host will.
    axrb::ComPtr<IDXGIDevice> producerDxgi; REQUIRE(SUCCEEDED(shared.d3d.As(&producerDxgi)));
    axrb::ComPtr<IDXGIAdapter> adapter; REQUIRE(SUCCEEDED(producerDxgi->GetAdapter(&adapter)));
    axrb::ComPtr<ID3D11Device> receiver;
    axrb::ComPtr<ID3D11DeviceContext> receiverContext;
    REQUIRE(SUCCEEDED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &receiver, nullptr, &receiverContext)));
    axrb::ComPtr<ID3D11Device1> d3d1; REQUIRE(SUCCEEDED(receiver.As(&d3d1)));
    axrb::ComPtr<ID3D11Texture2D> opened;
    REQUIRE(SUCCEEDED(d3d1->OpenSharedResourceByName(L"Local\\AXRB_Interop_Probe", DXGI_SHARED_RESOURCE_READ, IID_PPV_ARGS(&opened))));
    D3D11_TEXTURE2D_DESC desc{}; opened->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.MiscFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    axrb::ComPtr<ID3D11Texture2D> staging; REQUIRE(SUCCEEDED(receiver->CreateTexture2D(&desc, nullptr, &staging)));
    receiverContext->CopyResource(staging.Get(), opened.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{}; REQUIRE(SUCCEEDED(receiverContext->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
    auto bytes = static_cast<unsigned char*>(mapped.pData);
    std::printf("Shared Vulkan -> D3D11 pixel: %u %u %u %u\n", bytes[0], bytes[1], bytes[2], bytes[3]);
    const bool srgb = format == VK_FORMAT_R8G8B8A8_SRGB || format == VK_FORMAT_B8G8R8A8_SRGB;
    const bool bgra = format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB;
    const int expected[] = { srgb ? 137 : 64, srgb ? 188 : 128, srgb ? 225 : 191, 255 };
    for (UINT y = 0; y < 64; ++y) for (UINT x = 0; x < 64; ++x) {
        const auto pixel = bytes + y * mapped.RowPitch + x * 4;
        for (int channel = 0; channel < 4; ++channel) {
            const int index = bgra && channel < 3 ? 2 - channel : channel;
            REQUIRE(std::abs(int(pixel[channel]) - expected[index]) <= (channel == 3 ? 0 : 1));
        }
    }
    receiverContext->Unmap(staging.Get(), 0);
    shared.destroy(texture);
    DEVICE_FN(vkDestroyCommandPool); DEVICE_FN(vkDestroyDevice); INSTANCE_FN(vkDestroyInstance);
    vkDestroyCommandPool(device, pool, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    std::fprintf(stderr, "GPU interoperability probe passed\n");
}
