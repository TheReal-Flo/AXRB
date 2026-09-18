#include "openxr_session.h"
#if defined(_WIN32)
namespace axrb::host::detail {
bool OpenXrSession::render_equirect(uint32_t slot, XrTime time, ID3D11Texture2D *source,
                                    const XrCompositionLayerEquirect2KHR &layer,
                                    const axrb::protocol::ImageEquirect &metadata) {
    if (slot >= equirectTargets_.size()) equirectTargets_.resize(slot + 1);
    auto &target = equirectTargets_[slot];
    if (!target.swapchain) {
        XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
        info.format = projectionFormat_;
        info.sampleCount = info.faceCount = info.mipCount = 1;
        info.arraySize = 2;
        info.width = projectionWidth_;
        info.height = projectionHeight_;
        if (createSwapchain_(session_, &info, &target.swapchain) != XR_SUCCESS)
            return false;
        uint32_t count = 0;
        if (enumerateSwapchainImages_(target.swapchain, 0, &count, nullptr) != XR_SUCCESS || !count)
            return false;
        target.images.resize(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
        if (enumerateSwapchainImages_(target.swapchain, count, &count,
                                      reinterpret_cast<XrSwapchainImageBaseHeader *>(target.images.data())) !=
            XR_SUCCESS)
            return false;
        std::fprintf(stderr, "AXRB compositor: GPU panoramic layer %u ready\n", slot);
    }
    XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};
    locate.displayTime = time;
    locate.space = localSpace_;
    locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    XrViewState state{XR_TYPE_VIEW_STATE};
    uint32_t count = 0;
    std::array<XrView, 2> views{{{XR_TYPE_VIEW}, {XR_TYPE_VIEW}}};
    if (locateViews_(session_, &locate, &state, 2, &count, views.data()) != XR_SUCCESS || count != 2)
        return false;
    uint32_t index = 0;
    XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if (acquireSwapchainImage_(target.swapchain, &acquire, &index) != XR_SUCCESS)
        return false;
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wait.timeout = XR_INFINITE_DURATION;
    if (waitSwapchainImage_(target.swapchain, &wait) != XR_SUCCESS)
        return false;
    bool rendered = index < target.images.size();
    for (uint32_t eye = 0; eye < 2 && rendered; ++eye) {
        rendered = equirectRenderer_.render(
            d3dDevice_.get(), d3dContext_.get(), source, layer.subImage.imageArrayIndex,
            layer.subImage.imageRect.extent, static_cast<DXGI_FORMAT>(projectionFormat_), metadata,
            views[eye], target.images[index].texture, eye, projectionWidth_, projectionHeight_);
        auto &output = target.views[eye];
        output = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
        output.pose = views[eye].pose;
        output.fov = views[eye].fov;
        output.subImage.swapchain = target.swapchain;
        output.subImage.imageArrayIndex = eye;
        output.subImage.imageRect.extent = {static_cast<int32_t>(projectionWidth_),
                                            static_cast<int32_t>(projectionHeight_)};
    }
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    if (releaseSwapchainImage_(target.swapchain, &release) != XR_SUCCESS || !rendered)
        return false;
    target.layer = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    target.layer.space = localSpace_;
    // The projected sphere leaves transparent pixels outside its angular bounds.
    target.layer.layerFlags = layer.layerFlags | XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    target.layer.viewCount = 2;
    target.layer.views = target.views.data();
    return true;
}
} // namespace axrb::host::detail
#endif
