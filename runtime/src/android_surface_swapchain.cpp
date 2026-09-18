#include "runtime_internal.h"
#if defined(__ANDROID__)
namespace axrb::runtime::detail {
XrResult XRAPI_CALL xrCreateSwapchainAndroidSurfaceKHR_impl(XrSession session,
                                                            const XrSwapchainCreateInfo *info,
                                                            XrSwapchain *swapchain, jobject *surface) {
    if (!is_valid_session(session))
        return XR_ERROR_HANDLE_INVALID;
    if (!info || !swapchain || !surface || info->type != XR_TYPE_SWAPCHAIN_CREATE_INFO)
        return XR_ERROR_VALIDATION_FAILURE;
    if (info->format || info->sampleCount || info->faceCount || info->arraySize || info->mipCount ||
        !info->width || !info->height)
        return XR_ERROR_VALIDATION_FAILURE;
    *swapchain = nullptr;
    *surface = nullptr;
    if (!g_vulkan.active() || info->createFlags)
        return XR_ERROR_FEATURE_UNSUPPORTED;
    auto producer = std::make_shared<AndroidSurface>();
    if (!producer->initialize(pose_client(), info->width, info->height))
        return XR_ERROR_RUNTIME_FAILURE;
    auto create = *info;
    create.format = VK_FORMAT_R8G8B8A8_SRGB;
    create.sampleCount = create.faceCount = create.arraySize = create.mipCount = 1;
    create.createFlags = XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT;
    create.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    auto result = xrCreateSwapchain_impl(session, &create, swapchain);
    if (result != XR_SUCCESS)
        return result;
    auto *sc = find_swapchain(*swapchain);
    if (!g_vulkan.import_surface(sc->vulkan, producer->buffer(), info->width, info->height)) {
        xrDestroySwapchain_impl(*swapchain);
        *swapchain = nullptr;
        return XR_ERROR_RUNTIME_FAILURE;
    }
    sc->surface = std::move(producer);
    sc->hasReleasedImage = true;
    sc->releasedImage = 0;
    *surface = sc->surface->java_surface();
    __android_log_print(ANDROID_LOG_INFO, "AXRB.Surface", "Android video swapchain ready %ux%u", info->width,
                        info->height);
    return XR_SUCCESS;
}
} // namespace axrb::runtime::detail
#endif
