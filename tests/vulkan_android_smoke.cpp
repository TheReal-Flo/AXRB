// Run on the x86_64 emulator. Uses a real Vulkan device, not mocked GPU calls.
#include "vulkan_backend.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace axrb::runtime;
#define REQUIRE(condition) do { if (!(condition)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); return 1; } } while (0)
int main() {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; instanceInfo.pApplicationInfo = &app;
    VkInstance instance{}; REQUIRE(vkCreateInstance(&instanceInfo, nullptr, &instance) == VK_SUCCESS);
    VkPhysicalDevice physical = VulkanBackend::choose_device(instance); REQUIRE(physical);
    uint32_t count = 0; vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count); vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());
    uint32_t family = 0; while (family < count && !(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) ++family;
    REQUIRE(family < count);
    float priority = 1;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = family; queueInfo.queueCount = 1; queueInfo.pQueuePriorities = &priority;
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; deviceInfo.queueCreateInfoCount = 1; deviceInfo.pQueueCreateInfos = &queueInfo;
    VkDevice device{}; REQUIRE(vkCreateDevice(physical, &deviceInfo, nullptr, &device) == VK_SUCCESS);
    VkQueue queue{}; vkGetDeviceQueue(device, family, 0, &queue);
    VulkanBackend backend;
    XrGraphicsBindingVulkanKHR binding{XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR, nullptr, instance, physical, device, family, 0};
    REQUIRE(backend.initialize(binding));
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; poolInfo.queueFamilyIndex = family;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool pool{}; REQUIRE(vkCreateCommandPool(device, &poolInfo, nullptr, &pool) == VK_SUCCESS);
    VkCommandBufferAllocateInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmdInfo.commandPool = pool; cmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cmdInfo.commandBufferCount = 1;
    VkCommandBuffer cmd{}; REQUIRE(vkAllocateCommandBuffers(device, &cmdInfo, &cmd) == VK_SUCCESS);
    constexpr uint32_t width = 1024, height = 1024;
    constexpr VkDeviceSize bytes = width * height * 4 * 2;
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bufferInfo.size = bytes; bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VkBuffer buffer{}; REQUIRE(vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) == VK_SUCCESS);
    VkMemoryRequirements req{}; vkGetBufferMemoryRequirements(device, buffer, &req);
    VkPhysicalDeviceMemoryProperties memoryProps{}; vkGetPhysicalDeviceMemoryProperties(physical, &memoryProps);
    uint32_t memoryType = UINT32_MAX;
    for (uint32_t i = 0; i < memoryProps.memoryTypeCount; ++i)
        if ((req.memoryTypeBits & (1u << i)) && (memoryProps.memoryTypes[i].propertyFlags &
            (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
            (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) { memoryType = i; break; }
    REQUIRE(memoryType != UINT32_MAX);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; alloc.allocationSize = req.size; alloc.memoryTypeIndex = memoryType;
    VkDeviceMemory memory{}; REQUIRE(vkAllocateMemory(device, &alloc, nullptr, &memory) == VK_SUCCESS);
    REQUIRE(vkBindBufferMemory(device, buffer, memory, 0) == VK_SUCCESS);
    void* mapped{}; REQUIRE(vkMapMemory(device, memory, 0, bytes, 0, &mapped) == VK_SUCCESS);
    auto* pixels = static_cast<uint8_t*>(mapped);
    for (uint32_t layer = 0; layer < 2; ++layer) for (uint32_t y = 0; y < height; ++y) for (uint32_t x = 0; x < width; ++x) {
        const size_t p = ((layer * height + y) * width + x) * 4;
        pixels[p] = !layer && y < height/2 ? 255 : 0;
        pixels[p+1] = layer ? 255 : 0;
        pixels[p+2] = !layer && y >= height/2 ? 255 : 0; pixels[p+3] = 255;
    }
    vkUnmapMemory(device, memory);
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.width = width; info.height = height; info.arraySize = 2; info.faceCount = info.mipCount = info.sampleCount = 1;
    info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    VulkanSwapchain sc;
    info.format = VK_FORMAT_D32_SFLOAT;
    REQUIRE(backend.create(sc, info) == XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED);
    info.format = VK_FORMAT_R8G8B8A8_UNORM; info.sampleCount = 4;
    REQUIRE(backend.create(sc, info) == XR_ERROR_FEATURE_UNSUPPORTED); info.sampleCount = 1;
    for (auto format : {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_SRGB}) {
        info.format = format; REQUIRE(backend.create(sc, info) == XR_SUCCESS);
        for (uint32_t frame = 0; frame < 6; ++frame) {
            const uint32_t index = frame % 3;
            REQUIRE(vkResetCommandBuffer(cmd, 0) == VK_SUCCESS);
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; REQUIRE(vkBeginCommandBuffer(cmd, &begin) == VK_SUCCESS);
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; barrier.image = sc.images[index];
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 2};
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            VkBufferImageCopy copy{}; copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 2}; copy.imageExtent = {width, height, 1};
            vkCmdCopyBufferToImage(cmd, buffer, sc.images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            REQUIRE(vkEndCommandBuffer(cmd) == VK_SUCCESS);
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount = 1; submit.pCommandBuffers = &cmd;
            REQUIRE(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS);
            // No app-side queue wait: backend must correctly synchronize its readback.
            XrSwapchainSubImage left{nullptr, {{128, 256}, {768, 512}}, 0};
            XrSwapchainSubImage right = left; right.imageArrayIndex = 1;
            const VulkanSwapchain* chains[] = {&sc, &sc}; const uint32_t indices[] = {index, index};
            const XrSwapchainSubImage* subimages[] = {&left, &right}; std::vector<uint8_t> result[2];
            REQUIRE(backend.readback(chains, indices, subimages, result));
            REQUIRE(result[0].size() == 512*512*4 && result[1].size() == result[0].size());
            // AXRI bottom-up convention: blue bottom, red top, green second layer.
            REQUIRE(result[0][0] == 0 && result[0][2] == 255);
            REQUIRE(result[0][511*512*4] == 255 && result[0][511*512*4+2] == 0);
            for (size_t p = 0; p < result[1].size(); p += 4)
                REQUIRE(result[1][p] == 0 && result[1][p+1] == 255 && result[1][p+2] == 0 && result[1][p+3] == 255);
        }
        backend.destroy(sc);
    }
    backend.shutdown(); vkDestroyBuffer(device, buffer, nullptr); vkFreeMemory(device, memory, nullptr);
    vkDestroyCommandPool(device, pool, nullptr); vkDestroyDevice(device, nullptr); vkDestroyInstance(instance, nullptr);
    std::puts("PASS: real Vulkan images, UNORM/sRGB, array eyes, cropping/downscale, row orientation, synchronization and reuse");
    return 0;
}
