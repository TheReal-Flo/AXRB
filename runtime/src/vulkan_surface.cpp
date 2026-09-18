#include "vulkan_backend.h"
#include "perf_stats.h"
#if defined(__ANDROID__)
#include <android/log.h>
namespace axrb::runtime {
bool VulkanBackend::import_surface(VulkanSwapchain &sc, AHardwareBuffer *buffer, uint32_t width,
                                   uint32_t height) {
    auto properties = reinterpret_cast<PFN_vkGetAndroidHardwareBufferPropertiesANDROID>(
        vkGetDeviceProcAddr(device_, "vkGetAndroidHardwareBufferPropertiesANDROID"));
    if (!properties) {
        __android_log_print(ANDROID_LOG_ERROR, "AXRB.Surface",
                            "Vulkan device lacks Android hardware buffer import");
        return false;
    }
    VkAndroidHardwareBufferPropertiesANDROID props{
        VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID};
    if (properties(device_, buffer, &props) != VK_SUCCESS)
        return false;
    VkExternalMemoryImageCreateInfo external{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
    external.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.pNext = &external;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = VK_FORMAT_R8G8B8A8_UNORM;
    info.extent = {width, height, 1};
    info.mipLevels = info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage =
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (vkCreateImage(device_, &info, nullptr, &sc.surfaceImage) != VK_SUCCESS)
        return false;
    VkImportAndroidHardwareBufferInfoANDROID import{
        VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID};
    import.buffer = buffer;
    VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
    dedicated.pNext = &import;
    dedicated.image = sc.surfaceImage;
    VkMemoryAllocateInfo memory{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    memory.pNext = &dedicated;
    memory.allocationSize = props.allocationSize;
    memory.memoryTypeIndex = memory_type(props.memoryTypeBits, 0);
    return memory.memoryTypeIndex != UINT32_MAX &&
           vkAllocateMemory(device_, &memory, nullptr, &sc.surfaceMemory) == VK_SUCCESS &&
           vkBindImageMemory(device_, sc.surfaceImage, sc.surfaceMemory, 0) == VK_SUCCESS;
}
bool VulkanBackend::copy_surface(VulkanSwapchain &sc, uint32_t width, uint32_t height) {
    static axrb::protocol::PerfStats stats("surface-vulkan-copy");
    axrb::protocol::PerfScope scope(stats);
    if (!sc.surfaceImage || !begin())
        return false;
    VkImageMemoryBarrier acquire{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    acquire.image = sc.surfaceImage;
    acquire.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    acquire.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    acquire.srcQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT;
    acquire.dstQueueFamilyIndex = queueFamily_;
    acquire.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    acquire.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &acquire);
    barrier(sc.images[0], 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
    VkImageCopy copy{};
    copy.srcSubresource = copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.extent = {width, height, 1};
    vkCmdCopyImage(cmd_, sc.surfaceImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, sc.images[0],
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    barrier(sc.images[0], 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    auto release = acquire;
    release.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    release.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    release.srcQueueFamilyIndex = queueFamily_;
    release.dstQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT;
    release.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    release.dstAccessMask = 0;
    vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &release);
    return finish();
}
} // namespace axrb::runtime
#endif
