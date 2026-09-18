// Run on the x86_64 emulator. Uses a real Vulkan device, not mocked GPU calls.
#include "vulkan_backend.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace axrb::runtime;
#define REQUIRE(condition) do { if (!(condition)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); return 1; } } while (0)
#include "gpu_frame_packet.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
static bool batch_test(axrb::runtime::VulkanBackend& backend, const axrb::runtime::VulkanSwapchain& sc,
                       uint32_t index, XrSwapchainSubImage left, XrSwapchainSubImage right, bool host = false) {
    using namespace axrb::protocol;
    int socket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket < 0) return false;
    timeval timeout{5,0};
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(host ? 38491 : 38506);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    if (connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address))) { close(socket); return false; }
    const uint32_t counts[] = {5,17,40,2,24,3};
    uint64_t sequence = 100;
    for (uint32_t iteration = 0; iteration < 6; ++iteration) {
        const uint32_t count = counts[iteration];
        std::vector<axrb::runtime::VulkanExportRequest> requests(count);
        for (uint32_t i = 0; i < count; ++i) {
            auto& r = requests[i]; r.width = iteration % 2 ? 768 : 512; r.height = iteration % 2 ? 512 : 384;
            r.swapchains[0] = r.swapchains[1] = &sc; r.indices[0] = r.indices[1] = index;
            r.subimages[0] = left; r.subimages[1] = right; r.verticalFlip[0] = (iteration+i)%2 != 0;
            if ((iteration+i)%3 == 0) { r.subimages[1] = left; r.verticalFlip[1] = r.verticalFlip[0]; }
        }
        std::vector<WindowsGpuFrame> exports;
        if (!backend.export_batch(requests, exports)) { close(socket); return false; }
        for (uint32_t i = 0; i < count; ++i)
            if ((exports[i].formats[1] == 0) != requests[i].mono()) { close(socket); return false; }
        std::vector<WindowsGpuFrame> denied;
        if (backend.export_batch(requests, denied)) { close(socket); return false; } // No reuse before ACK.
        std::vector<GpuBatchPart> parts(count);
        for (uint32_t i = 0; i < count; ++i) {
            auto& p = parts[i]; auto& h = p.header;
            h.version = kMixedQuadGpuFrameVersion; h.type = kWindowsGpuFrameType;
            h.header_size += sizeof(ImageProjection); h.width = requests[i].width; h.height = requests[i].height; h.layers = 2;
            h.reserved = (count<<16)|i; h.sequence = sequence++; h.monotonic_time_ns = iteration;
            h.payload_size = sizeof(WindowsGpuFrame); p.gpu = exports[i];
            p.projection.view_count = kQuadCompositionBit | 1;
            p.projection.quads[0] = {{0,0,-2,0,0,0,1},2,1,0,7};
            if (host && i == 0 && iteration != 0 && iteration != 3) {
                h.version = kMixedProjectionGpuFrameVersion;
                p.projection = {}; p.projection.view_count = 2;
                for (auto& view : p.projection.views) {
                    view.pose.qw = 1; view.angle_left = view.angle_down = -.78539816f;
                    view.angle_right = view.angle_up = .78539816f;
                }
            }
            if (host && iteration == 4 && i == count-1) {
                h.version = kMixedEquirectGpuFrameVersion;
                p.projection = {}; p.projection.view_count = kEquirectComposition;
                p.projection.equirect = {{0,0,0,0,0,0,1},0,6.2831853f,1.5707963f,-1.5707963f,0,7};
            }
        }
        auto h = parts[0].header; h.version = kGpuBatchFrameVersion; h.reserved = count;
        h.sequence = parts.back().header.sequence; h.payload_size = parts.size()*sizeof(GpuBatchPart);
        std::vector<uint8_t> packet(h.header_size + h.payload_size);
        std::memcpy(packet.data(), &h, sizeof(h));
        std::memcpy(packet.data()+sizeof(h), &parts[0].projection, sizeof(ImageProjection));
        std::memcpy(packet.data()+h.header_size, parts.data(), h.payload_size);
        size_t sent=0;
        while (sent<packet.size()) { auto n=send(socket,packet.data()+sent,packet.size()-sent,0); if(n<=0){close(socket);return false;}sent+=n; }
        uint64_t ack=0; size_t got=0;
        while (got<sizeof(ack)) {auto n=recv(socket,reinterpret_cast<uint8_t*>(&ack)+got,sizeof(ack)-got,0);if(n<=0){close(socket);return false;}got+=n;}
        const bool accepted = ack == h.sequence;
        backend.acknowledge_batch(accepted);
        if (host ? !accepted : iteration < 5 ? !accepted : ack != UINT64_MAX) { close(socket); return false; }
        if (!host && iteration == 5 && backend.export_batch(requests, denied)) { close(socket); return false; }
        if (host) usleep(1500000);
    }
    if (host) {
        if (!backend.release_batch()) { close(socket); return false; }
        ImageFrameHeader empty{}; empty.version = kEmptyImageFrameVersion;
        empty.type = kWindowsGpuFrameType; empty.sequence = sequence++;
        if (send(socket,&empty,sizeof(empty),0) != sizeof(empty)) { close(socket); return false; }
        uint64_t ack = 0;
        if (recv(socket,&ack,sizeof(ack),MSG_WAITALL) != sizeof(ack) || ack != empty.sequence) { close(socket); return false; }
        usleep(1000000);
    }
    close(socket); return true;
}
int main(int argc, char** argv) {
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
    // Unity uses dynamic uniform-buffer descriptor pools during startup.
    VkDescriptorPoolSize descriptorSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 256};
    VkDescriptorPoolCreateInfo descriptorInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    descriptorInfo.maxSets = 128; descriptorInfo.poolSizeCount = 1; descriptorInfo.pPoolSizes = &descriptorSize;
    VkDescriptorPool descriptors{};
    REQUIRE(vkCreateDescriptorPool(device, &descriptorInfo, nullptr, &descriptors) == VK_SUCCESS);
    vkDestroyDescriptorPool(device, descriptors, nullptr);
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
    REQUIRE(backend.create(sc, info) == XR_ERROR_VALIDATION_FAILURE);
    for (auto format : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_R16G16B16A16_SFLOAT}) {
        info.format = format;
        info.usageFlags = (format == VK_FORMAT_R16G16B16A16_SFLOAT ? XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT :
            XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        REQUIRE(backend.create(sc, info) == XR_SUCCESS);
        for (auto image : sc.images) REQUIRE(image != VK_NULL_HANDLE);
        backend.destroy(sc);
    }
    info.format = VK_FORMAT_R8G8B8A8_SRGB; info.width = info.height = 2; info.mipCount = 2;
    info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    REQUIRE(backend.create(sc, info) == XR_SUCCESS); backend.destroy(sc);
    info.mipCount = 3; REQUIRE(backend.create(sc, info) == XR_ERROR_FEATURE_UNSUPPORTED);
    info.width = width; info.height = height; info.mipCount = 1;
    info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    info.format = VK_FORMAT_R8G8B8A8_UNORM; info.sampleCount = 4;
    REQUIRE(backend.create(sc, info) == XR_ERROR_FEATURE_UNSUPPORTED); info.sampleCount = 1;
    for (auto format : {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_SRGB}) {
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
            if (argc > 1) {
                REQUIRE(batch_test(backend, sc, index, left, right, std::strcmp(argv[1], "--host-batch") == 0));
                backend.destroy(sc); backend.shutdown();
                std::puts("PASS: whole-frame export, equal-size layer isolation, slot resizing, ACK gating and rejection poisoning");
                return 0;
            }
            backend.disable_gpu_export(); // Exercise real pixel fallback and staging growth.
            const uint32_t outWidth = index == 1 ? 1536 : 512;
            const uint32_t outHeight = index == 1 ? 1024 : 384;
            const auto allocationsBefore = backend.scratch_allocation_count();
            REQUIRE(backend.readback(chains, indices, subimages, outWidth, outHeight, result));
            if (frame >= 2) REQUIRE(backend.scratch_allocation_count() == allocationsBefore);
            REQUIRE(result[0].size() == outWidth*outHeight*4 && result[1].size() == result[0].size());
            // AXRI bottom-up convention: blue bottom, red top, green second layer.
            REQUIRE(result[0][0] == 0 && result[0][2] == 255);
            REQUIRE(result[0][(outHeight-1)*outWidth*4] == 255 && result[0][(outHeight-1)*outWidth*4+2] == 0);
            for (size_t p = 0; p < result[1].size(); p += 4)
                REQUIRE(result[1][p] == 0 && result[1][p+1] == 255 && result[1][p+2] == 0 && result[1][p+3] == 255);
            const bool flips[] = {true, false};
            REQUIRE(backend.readback(chains, indices, subimages, outWidth, outHeight, result, flips));
            REQUIRE(result[0][0] == 255 && result[0][2] == 0);
            REQUIRE(result[0][(outHeight-1)*outWidth*4] == 0 && result[0][(outHeight-1)*outWidth*4+2] == 255);
            REQUIRE(result[1][0] == 0 && result[1][1] == 255);
        }
        backend.destroy(sc);
    }
    REQUIRE(backend.scratch_allocation_count() == 8); // Two eyes, two formats, one growth each; revisits allocate nothing.
    backend.shutdown(); vkDestroyBuffer(device, buffer, nullptr); vkFreeMemory(device, memory, nullptr);
    vkDestroyCommandPool(device, pool, nullptr); vkDestroyDevice(device, nullptr); vkDestroyInstance(instance, nullptr);
    std::puts("PASS: real Vulkan images, UNORM/sRGB, array eyes, cropping/downscale, row orientation, synchronization and reuse");
    return 0;
}
