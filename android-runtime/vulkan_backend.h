#pragma once
#if defined(__ANDROID__)
#include "openxr_dispatch/openxr_minimal.h"
#include <vulkan/vulkan.h>
#include <array>
#include <vector>

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
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t layers = 0;
};
class VulkanBackend {
public:
    bool active() const { return device_ != VK_NULL_HANDLE; }
    static VkPhysicalDevice choose_device(VkInstance instance);
    bool initialize(const XrGraphicsBindingVulkanKHR& binding);
    void shutdown();
    XrResult create(VulkanSwapchain& sc, const XrSwapchainCreateInfo& info);
    void destroy(VulkanSwapchain& sc);
    bool readback(const VulkanSwapchain* const swapchains[2], const uint32_t indices[2],
                  const XrSwapchainSubImage* const subimages[2], std::vector<uint8_t> rgba[2]);
private:
    uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags flags);
    bool allocate_image(VkImage& image, VkDeviceMemory& memory, VkFormat format,
                        uint32_t width, uint32_t height, uint32_t layers, VkImageUsageFlags usage);
    bool begin();
    bool finish();
    void barrier(VkImage image, uint32_t layers, VkImageLayout before, VkImageLayout after,
                 VkAccessFlags src, VkAccessFlags dst);
    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    VkCommandBuffer cmd_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory bufferMemory_ = VK_NULL_HANDLE;
    void* mapped_ = nullptr;
    struct ScaledImage {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        bool initialized = false;
    } scaled_[2];
};
}
#endif
