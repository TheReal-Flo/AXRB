#include "openxr_session.h"
#if defined(_WIN32)
namespace axrb::host::detail {
bool OpenXrSession::acquire_panel_swapchain(const HostImageSnapshot &frame) {
    const uint32_t first = frame.projection.view_count == 2 ? 1 : 0;
    if (!frame.gpu || frame.gpu->count <= first)
        return false;
    uint32_t width = 0, height = 0, count = frame.gpu->count - first;
    for (uint32_t i = first; i < frame.gpu->count; ++i) {
        width = (std::max)(width, frame.gpu->parts[i].header.width);
        height = (std::max)(height, frame.gpu->parts[i].header.height);
    }
    if (!panelSwapchain_ || width > panelWidth_ || height > panelHeight_ || count != panelLayers_) {
        if (panelSwapchain_) {
            if (destroySwapchain_(panelSwapchain_) != XR_SUCCESS)
                return false;
            panelSwapchain_ = XR_NULL_HANDLE;
            panelImages_.clear();
        }
        panelWidth_ = width;
        panelHeight_ = height;
        panelLayers_ = count;
        XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        info.format = projectionFormat_;
        info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT |
                          XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        info.width = panelWidth_;
        info.height = panelHeight_;
        info.arraySize = panelLayers_;
        info.sampleCount = info.faceCount = info.mipCount = 1;
        if (createSwapchain_(session_, &info, &panelSwapchain_) != XR_SUCCESS)
            return false;
        uint32_t images = 0;
        if (enumerateSwapchainImages_(panelSwapchain_, 0, &images, nullptr) != XR_SUCCESS || !images)
            return false;
        panelImages_.resize(images, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
        panelSequences_.assign(images, UINT64_MAX);
        if (enumerateSwapchainImages_(panelSwapchain_, images, &images,
                                      reinterpret_cast<XrSwapchainImageBaseHeader *>(panelImages_.data())) !=
            XR_SUCCESS)
            return false;
        std::fprintf(stderr, "AXRB compositor: panel storage %ux%u, %u layers (%llu MiB across %u images)\n",
                     panelWidth_, panelHeight_, panelLayers_,
                     static_cast<unsigned long long>(panelWidth_) * panelHeight_ * panelLayers_ * images * 4 /
                         (1024 * 1024),
                     images);
    }
    XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if (acquireSwapchainImage_(panelSwapchain_, &acquire, &panelImageIndex_) != XR_SUCCESS)
        return false;
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wait.timeout = XR_INFINITE_DURATION;
    if (waitSwapchainImage_(panelSwapchain_, &wait) != XR_SUCCESS)
        return false;
    panelAcquired_ = true;
    return panelImageIndex_ < panelImages_.size();
}
} // namespace axrb::host::detail
#endif
