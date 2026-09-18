#include "runtime_internal.h"

namespace axrb::runtime::detail {

void destroy_swapchain_images(SwapchainRecord& sc)
{
#if defined(__ANDROID__)
    if (sc.vulkan.images[0]) { g_vulkan.destroy(sc.vulkan); }
    sc.surface.reset();
    if (sc.textures[0] != 0 || sc.textures[1] != 0 || sc.textures[2] != 0) {
        glDeleteTextures(3, sc.textures);
    }
#endif
    sc.textures[0] = 0;
    sc.textures[1] = 0;
    sc.textures[2] = 0;
}

void create_opengles_swapchain_images(SwapchainRecord& sc, const XrSwapchainCreateInfo& createInfo)
{
    destroy_swapchain_images(sc);
#if defined(__ANDROID__)
    const auto* renderer = glGetString(GL_RENDERER);
    const auto* vendor = glGetString(GL_VENDOR);
    __android_log_print(ANDROID_LOG_INFO, "AXRB.GPU", "swapchain GLES vendor=%s renderer=%s",
                        vendor ? reinterpret_cast<const char*>(vendor) : "unknown",
                        renderer ? reinterpret_cast<const char*>(renderer) : "unknown");
    GLenum internalFormat = static_cast<GLenum>(createInfo.format);
    if (internalFormat != GL_RGBA8 && internalFormat != GL_SRGB8_ALPHA8) {
        internalFormat = GL_RGBA8;
    }

    const GLenum target = createInfo.arraySize > 1 ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;
    glGenTextures(sc.imageCount, sc.textures);
    for (uint32_t i = 0; i < sc.imageCount; ++i) {
        glBindTexture(target, sc.textures[i]);
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (target == GL_TEXTURE_2D_ARRAY) {
            glTexStorage3D(
                target,
                static_cast<GLsizei>(createInfo.mipCount),
                internalFormat,
                static_cast<GLsizei>(createInfo.width),
                static_cast<GLsizei>(createInfo.height),
                static_cast<GLsizei>(createInfo.arraySize));
        } else {
            glTexStorage2D(
                target,
                static_cast<GLsizei>(createInfo.mipCount),
                internalFormat,
                static_cast<GLsizei>(createInfo.width),
                static_cast<GLsizei>(createInfo.height));
        }
    }
    glBindTexture(target, 0);
#else
    (void)createInfo;
#endif
}
XrResult XRAPI_CALL xrEnumerateSwapchainFormats_impl(
    XrSession session,
    uint32_t formatCapacityInput,
    uint32_t* formatCountOutput,
    int64_t* formats)
{
    log_call("xrEnumerateSwapchainFormats");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (formatCountOutput == nullptr) {
        return XR_ERROR_VALIDATION_FAILURE;
    }

    std::vector<int64_t> supported{0x8058, 0x8C43};
#if defined(__ANDROID__)
    if (g_vulkan.active()) supported = g_vulkan.formats();
#endif
    *formatCountOutput = static_cast<uint32_t>(supported.size());
    if (!formatCapacityInput) return XR_SUCCESS;
    if (!formats) return XR_ERROR_VALIDATION_FAILURE;
    if (formatCapacityInput < supported.size()) return XR_ERROR_SIZE_INSUFFICIENT;
    std::copy(supported.begin(), supported.end(), formats);
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrCreateSwapchain_impl(
    XrSession session,
    const XrSwapchainCreateInfo* createInfo,
    XrSwapchain* swapchain)
{
    log_call("xrCreateSwapchain");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (createInfo == nullptr || swapchain == nullptr || createInfo->type != XR_TYPE_SWAPCHAIN_CREATE_INFO) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    if (createInfo->width == 0 || createInfo->height == 0 ||
        createInfo->sampleCount == 0 || createInfo->faceCount == 0 ||
        createInfo->arraySize == 0 || createInfo->mipCount == 0) {
        return XR_ERROR_VALIDATION_FAILURE;
    }

    if (createInfo->createFlags & ~XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT) return XR_ERROR_FEATURE_UNSUPPORTED;
    SwapchainRecord* available = nullptr;
    for (auto& record : g_swapchains) {
        if (!record.created) { available = &record; break; }
    }
    // A deque preserves every live handle when another layer allocates images.
    if (!available) { g_swapchains.emplace_back(); available = &g_swapchains.back(); }
    auto& sc = *available;
    sc = {};
    sc.created = true;
    sc.acquired = false;
    sc.waited = false;
    sc.imageCount = (createInfo->createFlags & XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT) ? 1 : 3;
    sc.nextImage = 0;
    sc.currentImage = 0;
    sc.releasedImage = 0;
    sc.width = createInfo->width;
    sc.height = createInfo->height;
    sc.arraySize = createInfo->arraySize;
    sc.format = createInfo->format;
#if defined(__ANDROID__)
    if (g_vulkan.active()) {
        const XrResult result = g_vulkan.create(sc.vulkan, *createInfo);
        if (result != XR_SUCCESS) { sc = {}; return result; }
    } else
#endif
    { create_opengles_swapchain_images(sc, *createInfo); }
    *swapchain = reinterpret_cast<XrSwapchain>(&sc);
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrDestroySwapchain_impl(XrSwapchain swapchain)
{
    log_call("xrDestroySwapchain");
    auto* record = find_swapchain(swapchain);
    if (!record) { return XR_ERROR_HANDLE_INVALID; }
    auto& sc = *record;
    if (g_lastReleasedSwapchain == &sc) { g_lastReleasedSwapchain = nullptr; }
    sc.created = false;
    sc.acquired = false;
    sc.waited = false;
    destroy_swapchain_images(sc);
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrEnumerateSwapchainImages_impl(
    XrSwapchain swapchain,
    uint32_t imageCapacityInput,
    uint32_t* imageCountOutput,
    XrSwapchainImageBaseHeader* images)
{
    log_call("xrEnumerateSwapchainImages");
    auto* record = find_swapchain(swapchain);
    if (!record) { return XR_ERROR_HANDLE_INVALID; }
    auto& sc = *record;
#if defined(__ANDROID__)
    if (sc.surface) return XR_ERROR_FUNCTION_UNSUPPORTED;
#endif
    if (imageCountOutput == nullptr) {
        return XR_ERROR_VALIDATION_FAILURE;
    }

    *imageCountOutput = sc.imageCount;
    if (imageCapacityInput > 0 && images != nullptr) {
        const uint32_t count = imageCapacityInput < *imageCountOutput ? imageCapacityInput : *imageCountOutput;
        if (images[0].type == XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR) {
#if defined(__ANDROID__)
            if (g_vulkan.active()) return XR_ERROR_VALIDATION_FAILURE;
#endif
            auto* glImages = reinterpret_cast<XrSwapchainImageOpenGLESKHR*>(images);
            for (uint32_t i = 0; i < count; ++i) {
                if (glImages[i].type != XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR) {
                    return XR_ERROR_VALIDATION_FAILURE;
                }
                glImages[i].image = sc.textures[i];
            }
        } else if (images[0].type == XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR) {
            auto* vkImages = reinterpret_cast<XrSwapchainImageVulkanKHR*>(images);
            for (uint32_t i = 0; i < count; ++i) {
                if (vkImages[i].type != XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR) {
                    return XR_ERROR_VALIDATION_FAILURE;
                }
#if defined(__ANDROID__)
                if (!g_vulkan.active()) return XR_ERROR_VALIDATION_FAILURE;
                vkImages[i].image = reinterpret_cast<uint64_t>(sc.vulkan.images[i]);
#else
                return XR_ERROR_FEATURE_UNSUPPORTED;
#endif
            }
        } else {
            return XR_ERROR_VALIDATION_FAILURE;
        }
    }
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrAcquireSwapchainImage_impl(
    XrSwapchain swapchain,
    const XrSwapchainImageAcquireInfo* acquireInfo,
    uint32_t* index)
{
    log_call("xrAcquireSwapchainImage");
    auto* record = find_swapchain(swapchain);
    if (!record) { return XR_ERROR_HANDLE_INVALID; }
    auto& sc = *record;
#if defined(__ANDROID__)
    if (sc.surface) return XR_ERROR_FUNCTION_UNSUPPORTED;
#endif
    if (index == nullptr || (acquireInfo != nullptr && acquireInfo->type != XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO)) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    if (sc.acquired || (sc.imageCount == 1 && sc.hasReleasedImage)) {
        return XR_ERROR_CALL_ORDER_INVALID;
    }

    *index = sc.nextImage;
    sc.currentImage = *index;
    sc.nextImage = (sc.nextImage + 1) % sc.imageCount;
    sc.acquired = true;
    sc.waited = false;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrWaitSwapchainImage_impl(
    XrSwapchain swapchain,
    const XrSwapchainImageWaitInfo* waitInfo)
{
    log_call("xrWaitSwapchainImage");
    auto* record = find_swapchain(swapchain);
    if (!record) { return XR_ERROR_HANDLE_INVALID; }
    auto& sc = *record;
#if defined(__ANDROID__)
    if (sc.surface) return XR_ERROR_FUNCTION_UNSUPPORTED;
#endif
    if (waitInfo == nullptr || waitInfo->type != XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    if (!sc.acquired) {
        return XR_ERROR_CALL_ORDER_INVALID;
    }

    sc.waited = true;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrReleaseSwapchainImage_impl(
    XrSwapchain swapchain,
    const XrSwapchainImageReleaseInfo* releaseInfo)
{
    log_call("xrReleaseSwapchainImage");
    auto* record = find_swapchain(swapchain);
    if (!record) { return XR_ERROR_HANDLE_INVALID; }
    auto& sc = *record;
#if defined(__ANDROID__)
    if (sc.surface) return XR_ERROR_FUNCTION_UNSUPPORTED;
#endif
    if (releaseInfo != nullptr && releaseInfo->type != XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    if (!sc.acquired || !sc.waited) {
        return XR_ERROR_CALL_ORDER_INVALID;
    }

    sc.acquired = false;
    sc.waited = false;
    sc.releasedImage = sc.currentImage;
    sc.hasReleasedImage = true;
    g_lastReleasedSwapchain = &sc;
    return XR_SUCCESS;
}


} // namespace axrb::runtime::detail
