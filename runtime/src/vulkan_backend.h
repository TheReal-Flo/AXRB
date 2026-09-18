#pragma once
#if defined(__ANDROID__)
#include "openxr_dispatch/openxr_minimal.h"
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>
#include <android/hardware_buffer.h>
#include <array>
#include <vector>
#include <map>
#include "windows_gpu_frame.h"

struct XrGraphicsBindingVulkanKHR {
    XrStructureType type; const void* next;
    VkInstance instance; VkPhysicalDevice physicalDevice; VkDevice device;
    uint32_t queueFamilyIndex; uint32_t queueIndex;
};
struct XrVulkanInstanceCreateInfoKHR {
    XrStructureType type; const void* next; XrSystemId systemId; XrFlags64 createFlags;
    PFN_vkGetInstanceProcAddr pfnGetInstanceProcAddr;
    const VkInstanceCreateInfo* vulkanCreateInfo; const VkAllocationCallbacks* vulkanAllocator;
};
struct XrVulkanDeviceCreateInfoKHR {
    XrStructureType type; const void* next; XrSystemId systemId; XrFlags64 createFlags;
    PFN_vkGetInstanceProcAddr pfnGetInstanceProcAddr; VkPhysicalDevice vulkanPhysicalDevice;
    const VkDeviceCreateInfo* vulkanCreateInfo; const VkAllocationCallbacks* vulkanAllocator;
};
struct XrVulkanGraphicsDeviceGetInfoKHR {
    XrStructureType type; const void* next; XrSystemId systemId; VkInstance vulkanInstance;
};
namespace axrb::runtime {
struct VulkanSwapchain {
    std::array<VkImage, 3> images{};
    std::array<VkDeviceMemory, 3> memory{};
    VkImage surfaceImage = VK_NULL_HANDLE;
    VkDeviceMemory surfaceMemory = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t layers = 0;
};
struct VulkanExportRequest {
    const VulkanSwapchain* swapchains[2]{};
    uint32_t indices[2]{};
    XrSwapchainSubImage subimages[2]{};
    uint32_t width = 0, height = 0;
    bool verticalFlip[2]{};
    bool mono() const {
        const auto& a = subimages[0]; const auto& b = subimages[1];
        return swapchains[0] == swapchains[1] && indices[0] == indices[1] &&
            a.imageArrayIndex == b.imageArrayIndex && verticalFlip[0] == verticalFlip[1] &&
            a.imageRect.offset.x == b.imageRect.offset.x && a.imageRect.offset.y == b.imageRect.offset.y &&
            a.imageRect.extent.width == b.imageRect.extent.width && a.imageRect.extent.height == b.imageRect.extent.height;
    }
};
class VulkanBackend {
public:
    const axrb::protocol::WindowsGpuMarker& gpu_marker() const { return gpuMarker_; }
    bool release_batch();
    bool gpu_export_enabled() const { return gpuExportEnabled_; }
    bool export_batch(const std::vector<VulkanExportRequest>& requests, std::vector<axrb::protocol::WindowsGpuFrame>& frames);
    void acknowledge_batch(bool success) { if (success) batchInFlight_ = false; else batchFailed_ = true; }
    void disable_gpu_export() { gpuExportEnabled_ = false; }
    uint64_t scratch_allocation_count() const { return scratchAllocations_; }
    bool active() const { return device_ != VK_NULL_HANDLE; }
    static VkPhysicalDevice choose_device(VkInstance instance);
    bool initialize(const XrGraphicsBindingVulkanKHR& binding);
    void shutdown();
    std::vector<int64_t> formats() const;
    XrResult create(VulkanSwapchain& sc, const XrSwapchainCreateInfo& info);
    void destroy(VulkanSwapchain& sc);
    bool import_surface(VulkanSwapchain& sc, AHardwareBuffer* buffer, uint32_t width, uint32_t height);
    bool copy_surface(VulkanSwapchain& sc, uint32_t width, uint32_t height);
    bool readback(const VulkanSwapchain* const swapchains[2], const uint32_t indices[2],
                  const XrSwapchainSubImage* const subimages[2], uint32_t width, uint32_t height, std::vector<uint8_t> rgba[2], const bool* verticalFlip = nullptr);
private:
    bool ensure_buffer(VkDeviceSize bytes);
    bool ensure_scratch(VkFormat format, uint32_t eye, uint32_t width, uint32_t height);
    std::vector<axrb::protocol::WindowsGpuMarker> batchSlots_;
    bool batchInFlight_ = false, batchFailed_ = false;
    uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags flags);
    bool allocate_image(VkImage& image, VkDeviceMemory& memory, VkFormat format,
                        uint32_t width, uint32_t height, uint32_t layers, VkImageUsageFlags usage, uint32_t mips = 1);
    bool begin();
    bool finish();
    void barrier(VkImage image, uint32_t layers, VkImageLayout before, VkImageLayout after,
                 VkAccessFlags src, VkAccessFlags dst, VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT, uint32_t mips = 1);
    bool gpuExportEnabled_ = false;
    axrb::protocol::WindowsGpuMarker gpuMarker_{};
    uint64_t nextExportSession_ = 0;
    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = 0;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    VkCommandBuffer cmd_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory bufferMemory_ = VK_NULL_HANDLE;
    void* mapped_ = nullptr;
    VkDeviceSize bufferBytes_ = 0;
    struct ScaledImage {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        bool initialized = false;
        uint32_t width = 0, height = 0;
    };
    // Two eyes for each supported output format. Grow only, so alternating
    // scene/panel extents reuse storage without an unbounded size-keyed cache.
    std::array<std::array<ScaledImage, 2>, 2> scaled_{};
    uint64_t scratchAllocations_ = 0;
};
}
#endif
