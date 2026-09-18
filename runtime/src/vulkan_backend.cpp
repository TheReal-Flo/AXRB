#include "vulkan_backend.h"
#if defined(__ANDROID__)
#include "perf_stats.h"
#include "image_frame.h"
#include <android/log.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <chrono>
#include <sys/system_properties.h>

namespace axrb::runtime {
namespace {
bool ok(VkResult result, const char* operation) {
    if (result == VK_SUCCESS) return true;
    __android_log_print(ANDROID_LOG_ERROR, "AXRB.Vulkan", "%s failed: %d", operation, result);
    return false;
}
}
VkPhysicalDevice VulkanBackend::choose_device(VkInstance instance) {
    if (!instance) return VK_NULL_HANDLE;
    uint32_t count = 0;
    if (vkEnumeratePhysicalDevices(instance, &count, nullptr) != VK_SUCCESS) return VK_NULL_HANDLE;
    std::vector<VkPhysicalDevice> devices(count);
    if (vkEnumeratePhysicalDevices(instance, &count, devices.data()) != VK_SUCCESS) return VK_NULL_HANDLE;
    VkPhysicalDevice fallback = VK_NULL_HANDLE;
    for (auto device : devices) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(device, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) continue;
        if (props.vendorID == 0x10de) return device;
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ||
            props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) fallback = device;
    }
    return fallback;
}
uint32_t VulkanBackend::memory_type(uint32_t bits, VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties props{};
    vkGetPhysicalDeviceMemoryProperties(physical_, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (props.memoryTypes[i].propertyFlags & flags) == flags) return i;
    return UINT32_MAX;
}
bool VulkanBackend::initialize(const XrGraphicsBindingVulkanKHR& binding) {
    if (active() || !binding.instance || !binding.device || !binding.physicalDevice ||
        binding.physicalDevice != choose_device(binding.instance)) return false;
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(binding.physicalDevice, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(binding.physicalDevice, &count, families.data());
    if (binding.queueFamilyIndex >= count || binding.queueIndex >= families[binding.queueFamilyIndex].queueCount ||
        !(families[binding.queueFamilyIndex].queueFlags & VK_QUEUE_GRAPHICS_BIT)) return false;
    queueFamily_ = binding.queueFamilyIndex;
    device_ = binding.device; physical_ = binding.physicalDevice;
    vkGetDeviceQueue(device_, binding.queueFamilyIndex, binding.queueIndex, &queue_);
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = binding.queueFamilyIndex;
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; alloc.commandBufferCount = 1;
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (!ok(vkCreateCommandPool(device_, &pool, nullptr, &pool_), "create pool")) { shutdown(); return false; }
    alloc.commandPool = pool_;
    if (!ok(vkAllocateCommandBuffers(device_, &alloc, &cmd_), "allocate commands") ||
        !ok(vkCreateFence(device_, &fence, nullptr, &fence_), "create fence") ||
        !ensure_buffer(sizeof(gpuMarker_))) { shutdown(); return false; }
    VkPhysicalDeviceProperties props{}; vkGetPhysicalDeviceProperties(physical_, &props);
    __android_log_print(ANDROID_LOG_INFO, "AXRB.GPU", "Vulkan device=%s vendor=0x%x type=%u", props.deviceName, props.vendorID, props.deviceType);
    char gpuMode[PROP_VALUE_MAX]{};
    __system_property_get("debug.axrb.gpu_share", gpuMode);
    gpuExportEnabled_ = std::strcmp(gpuMode, "1") == 0;
    nextExportSession_ = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    batchSlots_.clear();
    return true;
}
bool VulkanBackend::ensure_buffer(VkDeviceSize bytes) {
    if (mapped_ && bufferBytes_ >= bytes) return true;
    // All previous readback commands have completed their fence before reuse.
    if (mapped_) vkUnmapMemory(device_, bufferMemory_);
    if (buffer_) vkDestroyBuffer(device_, buffer_, nullptr);
    if (bufferMemory_) vkFreeMemory(device_, bufferMemory_, nullptr);
    mapped_ = nullptr; buffer_ = VK_NULL_HANDLE; bufferMemory_ = VK_NULL_HANDLE; bufferBytes_ = 0;
    VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer.size = bytes; buffer.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (!ok(vkCreateBuffer(device_, &buffer, nullptr, &buffer_), "create staging buffer")) return false;
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device_, buffer_, &req);
    VkMemoryAllocateInfo memory{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    memory.allocationSize = req.size;
    memory.memoryTypeIndex = memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
    if (memory.memoryTypeIndex == UINT32_MAX)
        memory.memoryTypeIndex = memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    __android_log_print(ANDROID_LOG_INFO, "AXRB.Vulkan", "staging memory type=%u", memory.memoryTypeIndex);
    if (memory.memoryTypeIndex == UINT32_MAX ||
        !ok(vkAllocateMemory(device_, &memory, nullptr, &bufferMemory_), "allocate staging memory") ||
        !ok(vkBindBufferMemory(device_, buffer_, bufferMemory_, 0), "bind staging memory") ||
        !ok(vkMapMemory(device_, bufferMemory_, 0, bytes, 0, &mapped_), "map staging memory")) return false;
    bufferBytes_ = bytes;
    return true;
}
void VulkanBackend::shutdown() {
    if (!device_) return;
    vkQueueWaitIdle(queue_);
    if (mapped_) vkUnmapMemory(device_, bufferMemory_);
    if (buffer_) vkDestroyBuffer(device_, buffer_, nullptr);
    if (bufferMemory_) vkFreeMemory(device_, bufferMemory_, nullptr);
    for (auto& formatImages : scaled_) for (auto& scaled : formatImages) {
        if (scaled.image) vkDestroyImage(device_, scaled.image, nullptr);
        if (scaled.memory) vkFreeMemory(device_, scaled.memory, nullptr);
    }
    if (fence_) vkDestroyFence(device_, fence_, nullptr);
    if (pool_) vkDestroyCommandPool(device_, pool_, nullptr);
    *this = {};
}
bool VulkanBackend::allocate_image(VkImage& image, VkDeviceMemory& memory, VkFormat format,
                                  uint32_t width, uint32_t height, uint32_t layers, VkImageUsageFlags usage, uint32_t mips) {
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D; info.format = format; info.extent = {width, height, 1};
    info.mipLevels = mips; info.arrayLayers = layers; info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL; info.usage = usage;
    if (!ok(vkCreateImage(device_, &info, nullptr, &image), "create image")) return false;
    VkMemoryRequirements req{}; vkGetImageMemoryRequirements(device_, image, &req);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size; alloc.memoryTypeIndex = memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (alloc.memoryTypeIndex == UINT32_MAX) alloc.memoryTypeIndex = memory_type(req.memoryTypeBits, 0);
    return alloc.memoryTypeIndex != UINT32_MAX &&
        ok(vkAllocateMemory(device_, &alloc, nullptr, &memory), "allocate image memory") &&
        ok(vkBindImageMemory(device_, image, memory, 0), "bind image memory");
}
bool VulkanBackend::begin() {
    if (!ok(vkResetCommandBuffer(cmd_, 0), "reset commands")) return false;
    VkCommandBufferBeginInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    return ok(vkBeginCommandBuffer(cmd_, &info), "begin commands");
}
bool VulkanBackend::finish() {
    if (!ok(vkEndCommandBuffer(cmd_), "end commands") || !ok(vkResetFences(device_, 1, &fence_), "reset fence")) return false;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount = 1; submit.pCommandBuffers = &cmd_;
    return ok(vkQueueSubmit(queue_, 1, &submit, fence_), "submit") &&
        ok(vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX), "wait fence");
}
void VulkanBackend::barrier(VkImage image, uint32_t layers, VkImageLayout before, VkImageLayout after,
                            VkAccessFlags src, VkAccessFlags dst, VkImageAspectFlags aspect, uint32_t mips) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = before; b.newLayout = after; b.srcAccessMask = src; b.dstAccessMask = dst;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image; b.subresourceRange = {aspect, 0, mips, 0, layers};
    vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
}
std::vector<int64_t> VulkanBackend::formats() const {
    std::vector<int64_t> result;
    for (auto format : {VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R16G16B16A16_SFLOAT,
                       VK_FORMAT_D16_UNORM, VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT}) {
        VkFormatProperties props{}; vkGetPhysicalDeviceFormatProperties(physical_, format, &props);
        const bool depth = format >= VK_FORMAT_D16_UNORM;
        const VkFormatFeatureFlags required = depth ? VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT :
            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        if ((props.optimalTilingFeatures & required) == required) result.push_back(format);
    }
    return result;
}
XrResult VulkanBackend::create(VulkanSwapchain& sc, const XrSwapchainCreateInfo& info) {
    __android_log_print(ANDROID_LOG_INFO, "AXRB.Swapchain", "create %ux%u layers=%u mips=%u samples=%u faces=%u format=%lld flags=%llu usage=%llu", info.width, info.height, info.arraySize, info.mipCount, info.sampleCount, info.faceCount, (long long)info.format, (unsigned long long)info.createFlags, (unsigned long long)info.usageFlags);
    const bool depth = info.format == VK_FORMAT_D16_UNORM || info.format == VK_FORMAT_D32_SFLOAT ||
        info.format == VK_FORMAT_D24_UNORM_S8_UINT || info.format == VK_FORMAT_D32_SFLOAT_S8_UINT;
    if (!depth && info.format != VK_FORMAT_R8G8B8A8_UNORM && info.format != VK_FORMAT_R8G8B8A8_SRGB &&
        info.format != VK_FORMAT_R16G16B16A16_SFLOAT) return XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED;
    if ((depth && (info.usageFlags & XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT)) ||
        (!depth && (info.usageFlags & XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))) return XR_ERROR_VALIDATION_FAILURE;
    uint32_t maxMips = 0;
    for (uint32_t dimension = std::max(info.width, info.height); dimension; dimension >>= 1) ++maxMips;
    constexpr XrFlags64 supportedUsage = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT |
        XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (info.sampleCount != 1 || !info.mipCount || info.mipCount > maxMips || info.faceCount != 1 || (info.createFlags & ~XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT) ||
        info.width > 16384 || info.height > 16384 || info.arraySize > 4 || (info.usageFlags & ~supportedUsage)) return XR_ERROR_FEATURE_UNSUPPORTED;
    sc.format = static_cast<VkFormat>(info.format); sc.layers = info.arraySize;
    VkFormatProperties props{}; vkGetPhysicalDeviceFormatProperties(physical_, sc.format, &props);
    VkFormatFeatureFlags required = depth ? VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
    if (info.usageFlags & XR_SWAPCHAIN_USAGE_SAMPLED_BIT) required |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if ((props.optimalTilingFeatures & required) != required) return XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED;
    // Engines also clear color swapchains with vkCmdClearColorImage. Permit
    // transfer clears even when the application requested color attachment only.
    VkImageUsageFlags usage = (depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (info.usageFlags & XR_SWAPCHAIN_USAGE_SAMPLED_BIT) usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (info.usageFlags & XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT) usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    const uint32_t imageCount = (info.createFlags & XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT) ? 1 : 3;
    for (uint32_t i = 0; i < imageCount; ++i) {
        if (!allocate_image(sc.images[i], sc.memory[i], sc.format, info.width, info.height, sc.layers, usage, info.mipCount)) { destroy(sc); return XR_ERROR_RUNTIME_FAILURE; }
    }
    // Initialize before handing images to the app; xrWaitSwapchainImage never touches its queue.
    if (!begin()) { destroy(sc); return XR_ERROR_RUNTIME_FAILURE; }
    VkImageAspectFlags aspect = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    if (info.format == VK_FORMAT_D24_UNORM_S8_UINT || info.format == VK_FORMAT_D32_SFLOAT_S8_UINT) aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
    for (auto image : sc.images) if (image) barrier(image, sc.layers, VK_IMAGE_LAYOUT_UNDEFINED,
        depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0,
        depth ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT :
                VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, aspect, info.mipCount);
    if (!finish()) { destroy(sc); return XR_ERROR_RUNTIME_FAILURE; }
    return XR_SUCCESS;
}
void VulkanBackend::destroy(VulkanSwapchain& sc) {
    if (device_) {
        vkQueueWaitIdle(queue_);
        if (sc.surfaceImage) vkDestroyImage(device_, sc.surfaceImage, nullptr);
        if (sc.surfaceMemory) vkFreeMemory(device_, sc.surfaceMemory, nullptr);
        for (auto image : sc.images) if (image) vkDestroyImage(device_, image, nullptr);
        for (auto memory : sc.memory) if (memory) vkFreeMemory(device_, memory, nullptr);
    }
    sc = {};
}
bool VulkanBackend::ensure_scratch(VkFormat format, uint32_t eye, uint32_t width, uint32_t height) {
    auto& scaled = scaled_[format == VK_FORMAT_R8G8B8A8_SRGB ? 1 : 0][eye];
    if (scaled.image && scaled.width >= width && scaled.height >= height) return true;
            const uint32_t allocationWidth = std::max(width, scaled.width);
            const uint32_t allocationHeight = std::max(height, scaled.height);
            if (scaled.image) vkDestroyImage(device_, scaled.image, nullptr);
            if (scaled.memory) vkFreeMemory(device_, scaled.memory, nullptr);
            scaled = {};
            if (!allocate_image(scaled.image, scaled.memory, format, allocationWidth, allocationHeight, 1,
                                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) return false;
            scaled.format = format;
            scaled.width = allocationWidth; scaled.height = allocationHeight;
            ++scratchAllocations_;
            __android_log_print(ANDROID_LOG_INFO, "AXRB.GPU", "Scratch allocation %llu: eye=%u format=%d capacity=%ux%u",
                static_cast<unsigned long long>(scratchAllocations_), eye, int(format), scaled.width, scaled.height);
    return true;
}
bool VulkanBackend::export_batch(const std::vector<VulkanExportRequest>& requests,
                                 std::vector<axrb::protocol::WindowsGpuFrame>& frames) {
    static axrb::protocol::PerfStats stats("vulkan-batch-export");
    axrb::protocol::PerfScope scope(stats);
    frames.clear();
    if (!gpuExportEnabled_ || batchFailed_ || batchInFlight_ || requests.size() < 2 || requests.size() > axrb::protocol::kMaxWireCompositionLayers) return false;
    // Allocate every scratch capacity before recording. A later larger panel
    // must never destroy an image referenced by an earlier command in this batch.
    for (const auto& request : requests) {
        if (!axrb::protocol::valid_render_extent(request.width, request.height)) return false;
        for (uint32_t eye = 0; eye < 2; ++eye) {
            auto* sc = request.swapchains[eye];
            if (!sc || request.indices[eye] >= sc->images.size() || !sc->images[request.indices[eye]] ||
                (sc->format != VK_FORMAT_R8G8B8A8_UNORM && sc->format != VK_FORMAT_R8G8B8A8_SRGB && sc->format != VK_FORMAT_R16G16B16A16_SFLOAT)) return false;
            const auto format = sc->format == VK_FORMAT_R8G8B8A8_SRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
            if ((!request.mono() || eye == 0) && !ensure_scratch(format, eye, request.width, request.height)) return false;
        }
    }
    if (!ensure_buffer(requests.size() * sizeof(axrb::protocol::WindowsGpuMarker)) || !begin()) return false;
    gpuMarker_.session = 0;
    batchSlots_.resize(requests.size());
    batchInFlight_ = true;
    for (uint32_t i = 0; i < requests.size(); ++i) {
        const auto& request = requests[i];
        auto& marker = batchSlots_[i];
        uint32_t formats[2];
        for (uint32_t eye = 0; eye < 2; ++eye)
            formats[eye] = request.swapchains[eye]->format == VK_FORMAT_R8G8B8A8_SRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        if (request.mono()) formats[1] = 0;
        if (!marker.session || marker.width != request.width || marker.height != request.height ||
            marker.formats[0] != formats[0] || marker.formats[1] != formats[1]) marker.session = ++nextExportSession_;
        marker.width = request.width; marker.height = request.height;
        marker.formats[0] = formats[0]; marker.formats[1] = formats[1];
        marker.status = 0; ++marker.sequence;
        marker.reserved[0] = i; marker.reserved[1] = static_cast<uint32_t>(requests.size()); marker.reserved[4] = axrb::protocol::kGpuBatchSlotTag;
        vkCmdUpdateBuffer(cmd_, buffer_, i * sizeof(marker), sizeof(marker), &marker);
        for (uint32_t eye = 0; eye < (request.mono() ? 1u : 2u); ++eye) {
            const auto& sc = *request.swapchains[eye]; const auto& sub = request.subimages[eye];
            auto& scaled = scaled_[formats[eye] == VK_FORMAT_R8G8B8A8_SRGB ? 1 : 0][eye];
            const auto image = sc.images[request.indices[eye]];
            barrier(image, sc.layers, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
            barrier(scaled.image, 1, scaled.initialized ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, scaled.initialized ? VK_ACCESS_TRANSFER_READ_BIT : 0, VK_ACCESS_TRANSFER_WRITE_BIT);
            VkImageBlit blit{};
            blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, sub.imageArrayIndex, 1};
            blit.srcOffsets[0] = {sub.imageRect.offset.x, sub.imageRect.offset.y, 0};
            blit.srcOffsets[1] = {sub.imageRect.offset.x + sub.imageRect.extent.width, sub.imageRect.offset.y + sub.imageRect.extent.height, 1};
            if (request.verticalFlip[eye]) std::swap(blit.srcOffsets[0].y, blit.srcOffsets[1].y);
            blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            blit.dstOffsets[1] = {static_cast<int32_t>(request.width), static_cast<int32_t>(request.height), 1};
            vkCmdBlitImage(cmd_, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, scaled.image,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
            barrier(scaled.image, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
            scaled.initialized = true;
            barrier(image, sc.layers, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        }
    }
    VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    host.buffer = buffer_; host.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &host, 0, nullptr);
    if (!finish()) { batchFailed_ = true; return false; }
    for (uint32_t i = 0; i < requests.size(); ++i) {
        axrb::protocol::WindowsGpuMarker reply{};
        std::memcpy(&reply, static_cast<const uint8_t*>(mapped_) + i * sizeof(reply), sizeof(reply));
        const auto& expected = batchSlots_[i];
        if (reply.magic != expected.magic || reply.session != expected.session || reply.sequence != expected.sequence || reply.status != 1) {
            batchFailed_ = true;
            __android_log_print(ANDROID_LOG_ERROR, "AXRB.GPU", "Incomplete batch export at layer %u; refusing reuse", i);
            return false;
        }
        frames.push_back({reply.session, {reply.formats[0], reply.formats[1]}});
    }
    return true;
}
bool VulkanBackend::release_batch() {
    if (batchSlots_.empty() && !gpuMarker_.session) return true;
    if (batchInFlight_ || batchFailed_ || !ensure_buffer(sizeof(gpuMarker_)) || !begin()) return false;
    axrb::protocol::WindowsGpuMarker retire{};
    retire.reserved[4] = axrb::protocol::kGpuBatchSlotTag;
    vkCmdUpdateBuffer(cmd_, buffer_, 0, sizeof(retire), &retire);
    if (!finish()) { batchFailed_ = true; return false; }
    batchSlots_.clear(); gpuMarker_.session = 0;
    return true;
}
bool VulkanBackend::readback(const VulkanSwapchain* const swapchains[2], const uint32_t indices[2],
                             const XrSwapchainSubImage* const subimages[2], uint32_t width, uint32_t height, std::vector<uint8_t> rgba[2], const bool* verticalFlip) {
    static axrb::protocol::PerfStats stats("vulkan-frame-copy");
    axrb::protocol::PerfScope scope(stats);
    if ((!batchSlots_.empty() && !release_batch()) || !axrb::protocol::valid_render_extent(width, height)) return false;
    for (auto* sc : {swapchains[0], swapchains[1]}) if (sc->format != VK_FORMAT_R8G8B8A8_SRGB &&
        sc->format != VK_FORMAT_R8G8B8A8_UNORM && sc->format != VK_FORMAT_R16G16B16A16_SFLOAT) return false;
    const VkDeviceSize eyeBytes = VkDeviceSize(width) * height * 4;
    if (!gpuExportEnabled_ && eyeBytes * 2 > 128ull * 1024 * 1024) return false;
    if (!ensure_buffer(gpuExportEnabled_ ? sizeof(gpuMarker_) : eyeBytes * 2)) return false;
    VulkanExportRequest request{};
    for (uint32_t eye = 0; eye < 2; ++eye) {
        request.swapchains[eye] = swapchains[eye]; request.indices[eye] = indices[eye];
        request.subimages[eye] = *subimages[eye]; request.verticalFlip[eye] = verticalFlip && verticalFlip[eye];
    }
    const bool mono = request.mono();
    ScaledImage* scratch[2]{};
    for (uint32_t eye = 0; eye < 2; ++eye) {
        rgba[eye].clear();
        const auto format = swapchains[eye]->format == VK_FORMAT_R8G8B8A8_SRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        auto& scaled = scaled_[format == VK_FORMAT_R8G8B8A8_SRGB ? 1 : 0][eye];
        scratch[eye] = &scaled;
        if (!ensure_scratch(format, eye, width, height)) return false;
    }
    gpuMarker_.status = 0;
    if (gpuExportEnabled_) {
        const uint32_t rightFormat = mono ? 0 : scratch[1]->format;
        if (!gpuMarker_.session || gpuMarker_.width != width || gpuMarker_.height != height ||
            gpuMarker_.formats[0] != scratch[0]->format || gpuMarker_.formats[1] != rightFormat)
            gpuMarker_.session = ++nextExportSession_;
        gpuMarker_.width = width; gpuMarker_.height = height;
        gpuMarker_.formats[0] = scratch[0]->format; gpuMarker_.formats[1] = rightFormat;
        gpuMarker_.reserved[0] = 0; gpuMarker_.reserved[1] = 1;
        gpuMarker_.reserved[4] = axrb::protocol::kGpuBatchSlotTag;
        ++gpuMarker_.sequence;
    }
    // The configuration cap may have disabled export; allocate pixel staging
    // before recording the fallback copy, never into the small marker buffer.
    if (!gpuExportEnabled_ && (eyeBytes * 2 > 128ull * 1024 * 1024 || !ensure_buffer(eyeBytes * 2))) return false;
    if (!begin()) return false;
    if (gpuExportEnabled_) {
        vkCmdUpdateBuffer(cmd_, buffer_, 0, sizeof(gpuMarker_), &gpuMarker_);
    }
    for (uint32_t eye = 0; eye < (gpuExportEnabled_ && mono ? 1u : 2u); ++eye) {
        const auto& sc = *swapchains[eye]; const auto& sub = *subimages[eye];
        const auto index = indices[eye]; auto& scaled = *scratch[eye];
        const uint32_t w = width, h = height;
        barrier(sc.images[index], sc.layers, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        barrier(scaled.image, 1, scaled.initialized ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, scaled.initialized ? VK_ACCESS_TRANSFER_READ_BIT : 0, VK_ACCESS_TRANSFER_WRITE_BIT);
        VkImageBlit blit{}; blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, sub.imageArrayIndex, 1};
        blit.srcOffsets[0] = {sub.imageRect.offset.x, sub.imageRect.offset.y, 0};
        blit.srcOffsets[1] = {sub.imageRect.offset.x + sub.imageRect.extent.width, sub.imageRect.offset.y + sub.imageRect.extent.height, 1};
        if (verticalFlip && verticalFlip[eye]) std::swap(blit.srcOffsets[0].y, blit.srcOffsets[1].y);
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; blit.dstOffsets[1] = {static_cast<int32_t>(w), static_cast<int32_t>(h), 1};
        vkCmdBlitImage(cmd_, sc.images[index], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, scaled.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        barrier(scaled.image, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        VkBufferImageCopy copy{}; copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; copy.imageExtent = {w, h, 1};
        copy.bufferOffset = eye * eyeBytes;
        if (!gpuExportEnabled_) vkCmdCopyImageToBuffer(cmd_, scaled.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer_, 1, &copy);
        barrier(sc.images[index], sc.layers, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    }
    VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    host.buffer = buffer_; host.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &host, 0, nullptr);
    if (!finish()) return false;
    for (uint32_t eye = 0; eye < (gpuExportEnabled_ && mono ? 1u : 2u); ++eye) scratch[eye]->initialized = true;
    if (gpuExportEnabled_) {
        axrb::protocol::WindowsGpuMarker reply{};
        std::memcpy(&reply, mapped_, sizeof(reply));
        if (reply.magic == gpuMarker_.magic && reply.session == gpuMarker_.session && reply.sequence == gpuMarker_.sequence && reply.status == 1) {
            gpuMarker_ = reply;
            return true;
        }
        __android_log_print(ANDROID_LOG_WARN, "AXRB.GPU", "Host export unavailable; falling back to pixel transfer");
        gpuExportEnabled_ = false;
        return readback(swapchains, indices, subimages, width, height, rgba, verticalFlip);
    }
    for (uint32_t eye = 0; eye < 2; ++eye) {
        const auto& sub = *subimages[eye];
        const uint32_t w = width, h = height;
        rgba[eye].resize(static_cast<size_t>(w) * h * 4);
        // AXRI v2 uses the GLES bottom-up row convention; Vulkan rows start at the top.
        for (uint32_t y = 0; y < h; ++y)
            std::memcpy(rgba[eye].data() + static_cast<size_t>(y) * w * 4,
                        static_cast<const uint8_t*>(mapped_) + eye * eyeBytes + static_cast<size_t>(h - 1 - y) * w * 4, w * 4);
    }
    return true;
}
}
#endif
