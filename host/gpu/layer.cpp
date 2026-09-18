#include "shared_texture.h"
#include "windows_gpu_frame.h"
#include <vulkan/vk_layer.h>
#include <array>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

using axrb::protocol::WindowsGpuMarker;
namespace {
void* key(const void* h) { return h ? *reinterpret_cast<void* const*>(h) : nullptr; }
struct Instance { VkInstance instance; PFN_vkGetInstanceProcAddr gipa; };
struct Export { axrb::SharedTexture eyes[2]; uint32_t width, height, formats[2]; };
struct Command { VkCommandPool pool{}; WindowsGpuMarker marker; VkBuffer buffer{}; VkDeviceSize offset{}; uint32_t eye = 2, family = 0; };
struct Device {
    VkDevice device; PFN_vkSetDeviceLoaderData setLoaderData = nullptr; Instance instance; PFN_vkGetDeviceProcAddr gdpa;
    axrb::SharedDevice shared;
    std::unordered_map<uint64_t, std::unique_ptr<Export>> exports;
    std::vector<uint64_t> batchSlots;
    std::unordered_map<VkCommandPool, uint32_t> pools;
    std::unordered_map<VkCommandBuffer, Command> commands;
};
std::recursive_mutex mutex;
std::unordered_map<void*, Instance> instances;
std::unordered_map<void*, std::unique_ptr<Device>> devices;
Device* state(const void* h) { auto it = devices.find(key(h)); return it == devices.end() ? nullptr : it->second.get(); }
template<class T> T fn(Device* d, const char* name) { return reinterpret_cast<T>(d->gdpa(d->device, name)); }
}

extern "C" {
__declspec(dllexport) VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance, const char*);
__declspec(dllexport) VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice, const char*);

VKAPI_ATTR VkResult VKAPI_CALL createInstance(const VkInstanceCreateInfo* info, const VkAllocationCallbacks* alloc, VkInstance* out) {
    auto* chain = reinterpret_cast<VkLayerInstanceCreateInfo*>(const_cast<void*>(info->pNext));
    while (chain && (chain->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO || chain->function != VK_LAYER_LINK_INFO)) chain = reinterpret_cast<VkLayerInstanceCreateInfo*>(const_cast<void*>(chain->pNext));
    if (!chain) return VK_ERROR_INITIALIZATION_FAILED;
    auto gipa = chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    auto create = reinterpret_cast<PFN_vkCreateInstance>(gipa(nullptr, "vkCreateInstance"));
    chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;
    VkResult result = create(info, alloc, out);
    if (result == VK_SUCCESS) { std::lock_guard lock(mutex); instances[key(*out)] = {*out, gipa}; std::fprintf(stderr, "AXRB GPU layer: instance active\n"); }
    return result;
}
VKAPI_ATTR void VKAPI_CALL destroyInstance(VkInstance instance, const VkAllocationCallbacks* alloc) {
    std::lock_guard lock(mutex); auto it = instances.find(key(instance)); if (it == instances.end()) return;
    auto next = reinterpret_cast<PFN_vkDestroyInstance>(it->second.gipa(instance, "vkDestroyInstance")); instances.erase(it); next(instance, alloc);
}
VKAPI_ATTR VkResult VKAPI_CALL createDevice(VkPhysicalDevice physical, const VkDeviceCreateInfo* info, const VkAllocationCallbacks* alloc, VkDevice* out) {
    std::lock_guard lock(mutex);
    auto instanceIt = instances.find(key(physical)); if (instanceIt == instances.end()) return VK_ERROR_INITIALIZATION_FAILED;
    Instance inst = instanceIt->second;
    auto* chain = reinterpret_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(info->pNext));
    while (chain && (chain->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO || chain->function != VK_LAYER_LINK_INFO)) chain = reinterpret_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(chain->pNext));
    if (!chain) return VK_ERROR_INITIALIZATION_FAILED;
    PFN_vkSetDeviceLoaderData setLoaderData = nullptr;
    auto* callback = reinterpret_cast<const VkLayerDeviceCreateInfo*>(info->pNext);
    while (callback) {
        if (callback->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO && callback->function == VK_LOADER_DATA_CALLBACK) setLoaderData = callback->u.pfnSetDeviceLoaderData;
        callback = reinterpret_cast<const VkLayerDeviceCreateInfo*>(callback->pNext);
    }
    auto gdpa = chain->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    auto create = reinterpret_cast<PFN_vkCreateDevice>(chain->u.pLayerInfo->pfnNextGetInstanceProcAddr(inst.instance, "vkCreateDevice"));
    chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;
    std::vector<const char*> extensions;
    for (uint32_t i = 0; i < info->enabledExtensionCount; ++i) extensions.push_back(info->ppEnabledExtensionNames[i]);
    bool found = false; for (auto e : extensions) if (!std::strcmp(e, VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME)) found = true;
    if (!found) extensions.push_back(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);
    VkDeviceCreateInfo modified = *info; modified.enabledExtensionCount = static_cast<uint32_t>(extensions.size()); modified.ppEnabledExtensionNames = extensions.data();
    VkResult result = create(physical, &modified, alloc, out);
    if (result != VK_SUCCESS) return result;
    auto d = std::make_unique<Device>(); d->device = *out; d->setLoaderData = setLoaderData; d->instance = inst; d->gdpa = gdpa;
    bool ready = d->shared.initialize(inst.instance, physical, *out, inst.gipa, gdpa);
    std::fprintf(stderr, "AXRB GPU layer: device active, sharing=%d\n", ready);
    devices[key(*out)] = std::move(d); return result;
}
VKAPI_ATTR void VKAPI_CALL destroyDevice(VkDevice device, const VkAllocationCallbacks* alloc) {
    std::lock_guard lock(mutex); auto* d = state(device); if (!d) return;
    auto destroy = fn<PFN_vkDestroyDevice>(d, "vkDestroyDevice");
    fn<PFN_vkDeviceWaitIdle>(d, "vkDeviceWaitIdle")(device);
    for (auto& [id, images] : d->exports) for (auto& eye : images->eyes) d->shared.destroy(eye);
    devices.erase(key(device)); destroy(device, alloc);
}
VKAPI_ATTR VkResult VKAPI_CALL createCommandPool(VkDevice device, const VkCommandPoolCreateInfo* info, const VkAllocationCallbacks* alloc, VkCommandPool* out) {
    std::lock_guard lock(mutex); auto* d = state(device); if (!d) return VK_ERROR_INITIALIZATION_FAILED;
    auto result = fn<PFN_vkCreateCommandPool>(d, "vkCreateCommandPool")(device, info, alloc, out);
    if (result == VK_SUCCESS) d->pools[*out] = info->queueFamilyIndex; return result;
}
VKAPI_ATTR VkResult VKAPI_CALL allocateCommands(VkDevice device, const VkCommandBufferAllocateInfo* info, VkCommandBuffer* out) {
    std::lock_guard lock(mutex); auto* d = state(device); if (!d) return VK_ERROR_INITIALIZATION_FAILED;
    auto result = fn<PFN_vkAllocateCommandBuffers>(d, "vkAllocateCommandBuffers")(device, info, out);
    if (result == VK_SUCCESS) for (uint32_t i = 0; i < info->commandBufferCount; ++i) {
        if (d->setLoaderData) d->setLoaderData(device, out[i]);
        d->commands[out[i]].family = d->pools[info->commandPool];
        d->commands[out[i]].pool = info->commandPool;
    }
    return result;
}
VKAPI_ATTR VkResult VKAPI_CALL beginCommand(VkCommandBuffer cmd, const VkCommandBufferBeginInfo* info) {
    std::lock_guard lock(mutex); auto* d = state(cmd); if (!d) return VK_ERROR_INITIALIZATION_FAILED;
    auto found = d->commands.find(cmd); if (found != d->commands.end()) found->second.eye = 2;
    return fn<PFN_vkBeginCommandBuffer>(d, "vkBeginCommandBuffer")(cmd, info);
}
VKAPI_ATTR void VKAPI_CALL freeCommands(VkDevice device, VkCommandPool pool, uint32_t count, const VkCommandBuffer* commands) {
    std::lock_guard lock(mutex); auto* d = state(device); if (!d) return;
    for (uint32_t i = 0; i < count; ++i) d->commands.erase(commands[i]);
    fn<PFN_vkFreeCommandBuffers>(d, "vkFreeCommandBuffers")(device, pool, count, commands);
}
VKAPI_ATTR void VKAPI_CALL destroyPool(VkDevice device, VkCommandPool pool, const VkAllocationCallbacks* alloc) {
    std::lock_guard lock(mutex); auto* d = state(device); if (!d) return;
    for (auto it = d->commands.begin(); it != d->commands.end();) {
        if (it->second.pool == pool) it = d->commands.erase(it); else ++it;
    }
    d->pools.erase(pool);
    fn<PFN_vkDestroyCommandPool>(d, "vkDestroyCommandPool")(device, pool, alloc);
}
VKAPI_ATTR void VKAPI_CALL updateBuffer(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size, const void* data) {
    std::lock_guard lock(mutex); auto* d = state(cmd); if (!d) return;
    fn<PFN_vkCmdUpdateBuffer>(d, "vkCmdUpdateBuffer")(cmd, buffer, offset, size, data);
    if (size != sizeof(WindowsGpuMarker) || !data) return;
    WindowsGpuMarker marker; std::memcpy(&marker, data, sizeof(marker));
    if (marker.magic != axrb::protocol::kGpuMarkerMagic || marker.status) return;
    auto discard = [&](uint64_t session) {
        auto old = d->exports.find(session);
        if (old != d->exports.end()) {
            if (old->second) for (auto& eye : old->second->eyes) d->shared.destroy(eye);
            d->exports.erase(old);
        }
    };
    if (marker.reserved[4] == axrb::protocol::kGpuBatchSlotTag) {
        const uint32_t count = marker.reserved[1];
        if (count > 0xffff || (count && marker.reserved[0] >= count)) return;
        if (!marker.reserved[0]) {
            for (size_t i = count; i < d->batchSlots.size(); ++i) discard(d->batchSlots[i]);
            d->batchSlots.resize(count);
        }
        if (!count) { d->commands[cmd].eye = 2; return; }
        if (marker.reserved[0] >= d->batchSlots.size()) return;
        auto& previous = d->batchSlots[marker.reserved[0]];
        if (previous && previous != marker.session) discard(previous);
        previous = marker.session;
    }
    if (!marker.session || !marker.width || !marker.height || marker.width > 8192 || marker.height > 8192) return;
    auto& c = d->commands[cmd]; c.marker = marker; c.buffer = buffer; c.offset = offset; c.eye = 0;
}
VKAPI_ATTR void VKAPI_CALL blitImage(VkCommandBuffer cmd, VkImage source, VkImageLayout sourceLayout, VkImage dest, VkImageLayout destLayout, uint32_t count, const VkImageBlit* regions, VkFilter filter) {
    std::lock_guard lock(mutex); auto* d = state(cmd); if (!d) return;
    auto blit = fn<PFN_vkCmdBlitImage>(d, "vkCmdBlitImage");
    const auto original = [&] { blit(cmd, source, sourceLayout, dest, destLayout, count, regions, filter); };
    auto found = d->commands.find(cmd); if (found == d->commands.end()) { original(); return; }
    auto& c = found->second; const uint32_t eyeCount = c.marker.formats[1] ? 2u : 1u;
    if (c.eye >= eyeCount || count != 1) { original(); return; }
    auto& exported = d->exports[c.marker.session];
    if (!exported) {
        exported = std::make_unique<Export>(); exported->width = c.marker.width; exported->height = c.marker.height;
        for (uint32_t eye = 0; eye < eyeCount; ++eye) {
            exported->formats[eye] = c.marker.formats[eye];
            wchar_t name[96]; swprintf_s(name, L"Local\\AXRB_GPU_%016llx_%u", c.marker.session, eye);
            if (!d->shared.create(exported->eyes[eye], c.marker.width, c.marker.height, static_cast<VkFormat>(c.marker.formats[eye]), name)) {
                for (auto& image : exported->eyes) d->shared.destroy(image);
                exported.reset(); c.eye = 2; original(); return;
            }
        }
    }
    if (exported->width != c.marker.width || exported->height != c.marker.height || exported->formats[c.eye] != c.marker.formats[c.eye]) { c.eye = 2; original(); return; }
    // A validated AXRB export consumes the source directly. The guest's scaled
    // destination is only needed for pixel fallback, which records a new pass.
    VkImage image = exported->eyes[c.eye].image;
    auto pipelineBarrier = fn<PFN_vkCmdPipelineBarrier>(d, "vkCmdPipelineBarrier");
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; barrier.image = image;
    // Previous frame was fully consumed and acknowledged before recording this one.
    // Discard old contents, acquire ownership from the D3D11 consumer.
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL; barrier.dstQueueFamilyIndex = c.family;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    pipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkImageBlit region = regions[0]; region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstOffsets[0] = {0,0,0}; region.dstOffsets[1] = {static_cast<int32_t>(c.marker.width),static_cast<int32_t>(c.marker.height),1};
    blit(cmd, source, sourceLayout, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, filter);
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = c.family; barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = 0;
    pipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    if (++c.eye == eyeCount) {
        c.marker.status = 1;
        VkMemoryBarrier markerOrder{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        markerOrder.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; markerOrder.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        pipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &markerOrder, 0, nullptr, 0, nullptr);
        fn<PFN_vkCmdUpdateBuffer>(d, "vkCmdUpdateBuffer")(cmd, c.buffer, c.offset, sizeof(c.marker), &c.marker);
    }
}

PFN_vkVoidFunction intercept(const char* name) {
#define ENTRY(n, f) if (!std::strcmp(name, n)) return reinterpret_cast<PFN_vkVoidFunction>(f)
    ENTRY("vkGetInstanceProcAddr", vkGetInstanceProcAddr); ENTRY("vkGetDeviceProcAddr", vkGetDeviceProcAddr);
    ENTRY("vkCreateInstance", createInstance); ENTRY("vkDestroyInstance", destroyInstance);
    ENTRY("vkCreateDevice", createDevice); ENTRY("vkDestroyDevice", destroyDevice);
    ENTRY("vkCreateCommandPool", createCommandPool); ENTRY("vkAllocateCommandBuffers", allocateCommands);
    ENTRY("vkBeginCommandBuffer", beginCommand); ENTRY("vkFreeCommandBuffers", freeCommands); ENTRY("vkDestroyCommandPool", destroyPool);
    ENTRY("vkCmdUpdateBuffer", updateBuffer); ENTRY("vkCmdBlitImage", blitImage);
    return nullptr;
}
__declspec(dllexport) VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* name) {
    if (auto result = intercept(name)) return result;
    std::lock_guard lock(mutex); auto it = instances.find(key(instance)); return it == instances.end() ? nullptr : it->second.gipa(instance, name);
}
__declspec(dllexport) VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char* name) {
    if (auto result = intercept(name)) return result;
    std::lock_guard lock(mutex); auto* d = state(device); return d ? d->gdpa(device, name) : nullptr;
}
VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* info) {
    if (info->loaderLayerInterfaceVersion > 2) info->loaderLayerInterfaceVersion = 2;
    info->pfnGetInstanceProcAddr = vkGetInstanceProcAddr; info->pfnGetDeviceProcAddr = vkGetDeviceProcAddr;
    info->pfnGetPhysicalDeviceProcAddr = nullptr;
    return VK_SUCCESS;
}
}
