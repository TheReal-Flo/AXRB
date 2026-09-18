#include "openxr_session.h"
#include "splash.h"

namespace axrb::host::detail {

#if defined(_WIN32)
bool OpenXrSession::create_d3d11_device()
{
    XrGraphicsRequirementsD3D11KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
    const XrResult result = getD3D11GraphicsRequirements_(instance_, systemId_, &requirements);
    if (result != XR_SUCCESS) {
        std::fprintf(
            stderr,
            "AXRB OpenXR: xrGetD3D11GraphicsRequirementsKHR failed: %s (%d)\n",
            xr_result_name(result),
            result);
        return false;
    }

    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(factory.put()));
    if (FAILED(hr)) {
        std::fprintf(stderr, "AXRB OpenXR: CreateDXGIFactory1 failed: 0x%08lx\n", static_cast<unsigned long>(hr));
        return false;
    }

    ComPtr<IDXGIAdapter1> selectedAdapter;
    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter1> adapter;
        hr = factory.get()->EnumAdapters1(i, adapter.put());
        if (hr == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        if (FAILED(hr)) {
            continue;
        }

        DXGI_ADAPTER_DESC1 desc{};
        if (FAILED(adapter.get()->GetDesc1(&desc))) {
            continue;
        }

        if (desc.AdapterLuid.HighPart == requirements.adapterLuid.HighPart &&
            desc.AdapterLuid.LowPart == requirements.adapterLuid.LowPart) {
            selectedAdapter = std::move(adapter);
            break;
        }
    }

    if (selectedAdapter.get() == nullptr) {
        std::fprintf(stderr, "AXRB OpenXR: failed to find D3D11 adapter requested by OpenXR runtime\n");
        return false;
    }

    const D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    D3D_FEATURE_LEVEL createdFeatureLevel{};
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    hr = D3D11CreateDevice(
        selectedAdapter.get(),
        D3D_DRIVER_TYPE_UNKNOWN,
        nullptr,
        flags,
        featureLevels,
        sizeof(featureLevels) / sizeof(featureLevels[0]),
        D3D11_SDK_VERSION,
        d3dDevice_.put(),
        &createdFeatureLevel,
        d3dContext_.put());
    if (FAILED(hr)) {
        std::fprintf(stderr, "AXRB OpenXR: D3D11CreateDevice failed: 0x%08lx\n", static_cast<unsigned long>(hr));
        return false;
    }

    // SteamVR also uses the binding device's immediate context during
    // xrEndFrame. Protect those accesses as well as our own copy commands.
    ComPtr<ID3D11Multithread> multithread;
    if (FAILED(d3dContext_.get()->QueryInterface(__uuidof(ID3D11Multithread),
            reinterpret_cast<void**>(multithread.put())))) return false;
    multithread.get()->SetMultithreadProtected(TRUE);

    if (createdFeatureLevel < requirements.minFeatureLevel) {
        std::fprintf(stderr, "AXRB OpenXR: D3D11 feature level is below runtime requirement\n");
        return false;
    }

    // Reception has its own immediate context: SteamVR can hold its
    // binding context while pacing xrEndFrame without delaying Android.
    hr = D3D11CreateDevice(selectedAdapter.get(), D3D_DRIVER_TYPE_UNKNOWN,
        nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, featureLevels,
        sizeof(featureLevels) / sizeof(featureLevels[0]), D3D11_SDK_VERSION,
        receiveDevice_.put(), nullptr, receiveContext_.put());
    if (FAILED(hr)) return false;

    std::fprintf(stderr, "AXRB OpenXR: D3D11 graphics binding ready\n");
    return true;
}
#endif

#if defined(_WIN32)
bool OpenXrSession::create_projection_swapchain()
{
    XrResult result;
    // Extent/format are negotiated before receive/pose threads start. Growing
    // the layer array must not rewrite these concurrently observed values.
    if (projectionFormat_ == 0) {
        uint32_t viewCount = 0;
        if (enumerateViewConfigurationViews_(instance_, systemId_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                0, &viewCount, nullptr) != XR_SUCCESS || viewCount != 2) return false;
        std::array<XrViewConfigurationView, 2> configViews{{{XR_TYPE_VIEW_CONFIGURATION_VIEW}, {XR_TYPE_VIEW_CONFIGURATION_VIEW}}};
        if (enumerateViewConfigurationViews_(instance_, systemId_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                2, &viewCount, configViews.data()) != XR_SUCCESS) return false;
        projectionWidth_ = projectionHeight_ = 0;
        // The stereo transport uses equal-size array slices. Accommodate both
        // recommendations if the runtime recommends asymmetric view sizes.
        for (const auto& view : configViews) {
            projectionWidth_ = (std::max)(projectionWidth_, view.recommendedImageRectWidth);
            projectionHeight_ = (std::max)(projectionHeight_, view.recommendedImageRectHeight);
        }
        if (!axrb::protocol::valid_render_extent(projectionWidth_, projectionHeight_)) {
            std::fprintf(stderr, "AXRB OpenXR: unsupported recommended eye extent %ux%u\n", projectionWidth_, projectionHeight_);
            return false;
        }
        std::fprintf(stderr, "AXRB OpenXR: runtime recommended stereo extent %ux%u\n", projectionWidth_, projectionHeight_);
        uint32_t formatCount = 0;
        result = enumerateSwapchainFormats_(session_, 0, &formatCount, nullptr);
        if (result != XR_SUCCESS || formatCount == 0) {
            std::fprintf(stderr, "AXRB OpenXR: xrEnumerateSwapchainFormats failed: %s (%d)\n", xr_result_name(result), result);
            return false;
        }

        std::vector<int64_t> formats(formatCount);
        result = enumerateSwapchainFormats_(session_, formatCount, &formatCount, formats.data());
        if (result != XR_SUCCESS) {
            std::fprintf(stderr, "AXRB OpenXR: xrEnumerateSwapchainFormats(list) failed: %s (%d)\n", xr_result_name(result), result);
            return false;
        }

        std::fprintf(stderr, "AXRB OpenXR: supported swapchain formats:");
        for (int64_t format : formats) {
            std::fprintf(stderr, " %lld", static_cast<long long>(format));
        }
        std::fprintf(stderr, "\n");

        int64_t selectedFormat = formats[0];
        constexpr int64_t preferredFormats[] = {
            DXGI_FORMAT_R8G8B8A8_UNORM,
            DXGI_FORMAT_B8G8R8A8_UNORM,
            DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
            DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
        };
        for (int64_t preferred : preferredFormats) {
            for (int64_t format : formats) {
                if (format == preferred) {
                    selectedFormat = format;
                    break;
                }
            }
            if (selectedFormat == preferred) {
                break;
            }
        }
        projectionFormat_ = selectedFormat;
    }

    XrSwapchainCreateInfo swapchainInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    swapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    swapchainInfo.format = projectionFormat_;
    swapchainInfo.sampleCount = 1;
    swapchainInfo.width = projectionWidth_;
    swapchainInfo.height = projectionHeight_;
    swapchainInfo.faceCount = 1;
    swapchainInfo.arraySize = projectionArraySize_;
    swapchainInfo.mipCount = 1;

    result = createSwapchain_(session_, &swapchainInfo, &projectionSwapchain_);
    if (result != XR_SUCCESS) {
        std::fprintf(stderr, "AXRB OpenXR: xrCreateSwapchain failed: %s (%d)\n", xr_result_name(result), result);
        return false;
    }

    uint32_t imageCount = 0;
    result = enumerateSwapchainImages_(projectionSwapchain_, 0, &imageCount, nullptr);
    if (result != XR_SUCCESS || imageCount == 0) {
        std::fprintf(stderr, "AXRB OpenXR: xrEnumerateSwapchainImages failed: %s (%d)\n", xr_result_name(result), result);
        return false;
    }

    projectionImages_.resize(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
    splashUploaded_.assign(imageCount, false);
    uploadedAndroidSequenceByImage_.assign(imageCount, UINT64_MAX);
    uploadedGpuSessionByImage_.assign(imageCount, 0);
    uploadedProjectionByImage_.resize(imageCount);
    uploadedMixedQuads_.resize(imageCount);
    uploadedMixedExtents_.resize(imageCount);
    uploadedMixedCounts_.assign(imageCount, 0);
    uploadedMixedTimes_.assign(imageCount, 0);
    uploadedExtentByImage_.resize(imageCount, {static_cast<int32_t>(projectionWidth_), static_cast<int32_t>(projectionHeight_)});
    result = enumerateSwapchainImages_(
        projectionSwapchain_,
        imageCount,
        &imageCount,
        reinterpret_cast<XrSwapchainImageBaseHeader*>(projectionImages_.data()));
    if (result != XR_SUCCESS) {
        std::fprintf(stderr, "AXRB OpenXR: xrEnumerateSwapchainImages(list) failed: %s (%d)\n", xr_result_name(result), result);
        return false;
    }

    std::fprintf(
        stderr,
        "AXRB OpenXR: projection swapchain ready %ux%u images=%u\n",
        projectionWidth_,
        projectionHeight_,
        imageCount);
    return true;
}
#endif

#if defined(_WIN32)
bool OpenXrSession::update_projection_layer(
    XrTime displayTime,
    std::array<XrCompositionLayerProjectionView, 2>& projectionViews,
    XrCompositionLayerProjection& projectionLayer,
    std::vector<XrCompositionLayerQuad>& quadLayers,
    std::vector<XrCompositionLayerEquirect2KHR>& sphereLayers,
    uint32_t& quadCount, bool& mixedProjection)
{
    static axrb::protocol::PerfStats stats("host-projection");
    axrb::protocol::PerfScope scope(stats);
    std::unique_lock handoffLock(frameHandoffMutex_, std::defer_lock);
    if (!concurrentGpuFrames_) handoffLock.lock();
    // Pin one coherent frame through swapchain growth, acquisition and copy.
    HostImageSnapshot frame;
    {
        static axrb::protocol::PerfStats snapshotStats("host-frame-snapshot");
        axrb::protocol::PerfScope snapshotScope(snapshotStats);
        if (imageFrame_) frame = imageFrame_->snapshot();
    }
    if (handoffLock.owns_lock()) handoffLock.unlock();
    if (frame.header.version == axrb::protocol::kEmptyImageFrameVersion) {
        trim_composition_resources(0, false); return false;
    }
    uint32_t imageIndex = 0;
    XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    XrResult result = acquireSwapchainImage_(projectionSwapchain_, &acquireInfo, &imageIndex);
    if (result != XR_SUCCESS || imageIndex >= projectionImages_.size()) {
        return false;
    }

    XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitInfo.timeout = XR_INFINITE_DURATION;
    static axrb::protocol::PerfStats swapchainWaitStats("host-swapchain-wait");
    { axrb::protocol::PerfScope waitScope(swapchainWaitStats); result = waitSwapchainImage_(projectionSwapchain_, &waitInfo); }
    if (result != XR_SUCCESS) {
        return false;
    }

    if (!concurrentGpuFrames_) handoffLock.lock();
    // A newer complete frame may have arrived while SteamVR held the swapchain.
    // Replace texture and pose metadata together.
    if (imageFrame_) {
        auto newest = imageFrame_->snapshot();
        if (newest.pixels) frame = std::move(newest);
    }
    if (frame.pixels) {
        static axrb::protocol::PerfStats ageStats("host-selected-frame-age");
        ageStats.record(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frame.receivedAt).count());
    }
    // Panels retain their own pixel extents instead of allocating every slice
    // at the headset resolution. Keep them acquired through sphere rendering.
    struct ReleasePanels {
        OpenXrSession* owner;
        ~ReleasePanels() {
            if (owner->panelAcquired_) {
                XrSwapchainImageReleaseInfo info{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                owner->releaseSwapchainImage_(owner->panelSwapchain_, &info);
                owner->panelAcquired_ = false;
            }
        }
    } releasePanels{this};
    if (frame.gpu && axrb::protocol::mixed_gpu_version(frame.header.version) && frame.gpu->count <= nativeLayerLimit_ && !acquire_panel_swapchain(frame)) {
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        releaseSwapchainImage_(projectionSwapchain_, &release);
        return false;
    }
    if (frame.gpu && frame.gpu->count > nativeLayerLimit_) {
        if (frame.projection.view_count == 2) {
            for (uint32_t eye = 0; eye < 2; ++eye) {
                const auto& source = frame.projection.views[eye]; auto& view = overflowViews_[eye];
                view = {XR_TYPE_VIEW}; view.pose.position = {source.pose.x,source.pose.y,source.pose.z};
                view.pose.orientation = {source.pose.qx,source.pose.qy,source.pose.qz,source.pose.qw};
                view.fov = {source.angle_left,source.angle_right,source.angle_up,source.angle_down};
            }
        } else {
            XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO}; locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            locate.displayTime = displayTime; locate.space = localSpace_;
            XrViewState state{XR_TYPE_VIEW_STATE}; uint32_t count = 0;
            for (auto& view : overflowViews_) view = {XR_TYPE_VIEW};
            if (locateViews_(session_, &locate, &state, 2, &count, overflowViews_.data()) != XR_SUCCESS || count != 2) {
                XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                releaseSwapchainImage_(projectionSwapchain_, &release); return false;
            }
        }
    }
    bool uploaded;
    {
        static axrb::protocol::PerfStats copyStats("host-texture-upload");
        axrb::protocol::PerfScope copyScope(copyStats);
        uploaded = fill_projection_texture(projectionImages_[imageIndex].texture, imageIndex, frame);
    }
    if (handoffLock.owns_lock()) handoffLock.unlock();

    // Both eyes/layers have finished copying into our acquired OpenXR image.
    // Preview/capture touch only that destination, so the shared-cache lock is
    // no longer needed. The runtime cannot reuse the image until we release it.
    if (uploaded) {
        static axrb::protocol::PerfStats mirrorStats("host-preview");
        axrb::protocol::PerfScope mirrorScope(mirrorStats);
        const auto extent = uploadedExtentByImage_[imageIndex];
        const auto sequence = uploadedAndroidSequenceByImage_[imageIndex];
        mirror_.present(d3dContext_.get(), projectionImages_[imageIndex].texture,
            extent.width, extent.height, static_cast<DXGI_FORMAT>(projectionFormat_), sequence);
        if (frame.gpu && (axrb::protocol::mixed_gpu_version(frame.header.version) || frame.projection.view_count == 2))
            debug_capture_frame(d3dContext_.get(), projectionImages_[imageIndex].texture,
                extent.width, extent.height, sequence);
    }

    const uint32_t splashSize = (std::min)(512u, (std::min)(projectionWidth_, projectionHeight_));
    if (!uploaded && !debugGraphicsTest_) {
        mirror_.present(d3dContext_.get(), projectionImages_[imageIndex].texture,
            splashSize, splashSize, static_cast<DXGI_FORMAT>(projectionFormat_), UINT64_MAX);
    }

    // Keep the image acquired while fallback layers sample its array slices.
    struct ReleaseImage {
        PFN_xrReleaseSwapchainImage release; XrSwapchain swapchain;
        ~ReleaseImage() { XrSwapchainImageReleaseInfo info{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO}; release(swapchain, &info); }
    } releaseImage{releaseSwapchainImage_, projectionSwapchain_};

    if (!uploaded && !debugGraphicsTest_) {
        quadLayers.resize(1); sphereLayers.resize(1);
        trim_composition_resources(0, false);
        auto& splash = quadLayers[0];
        splash = {XR_TYPE_COMPOSITION_LAYER_QUAD};
        splash.space = viewSpace_;
        splash.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        splash.pose.orientation.w = 1.0f;
        splash.pose.position.z = -2.0f;
        splash.size = {0.8f, 0.8f};
        splash.subImage.swapchain = projectionSwapchain_;
        splash.subImage.imageRect.extent = {static_cast<int32_t>(splashSize), static_cast<int32_t>(splashSize)};
        quadCount = 1;
        mixedProjection = false;
        return true;
    }

    const auto& composition = uploadedProjectionByImage_[imageIndex];
    const bool mixedBatch = uploadedMixedCounts_[imageIndex] != 0;
    mixedProjection = mixedBatch && composition.view_count == 2;
    quadCount = mixedBatch ? uploadedMixedCounts_[imageIndex] : composition.is_equirect() ? 1 : composition.quad_count();
    quadLayers.resize(quadCount); sphereLayers.resize(quadCount);
    sphereFallbackIndices_.resize(quadCount);
    uint32_t fallbackSphereCount = 0;
    if (quadCount) {
        for (uint32_t i = 0; i < quadCount; ++i) {
            const auto& metadata = mixedBatch ? uploadedMixedQuads_[imageIndex][i] : composition;
            if (metadata.is_equirect()) {
                const auto& source = metadata.equirect;
                auto& sphere = sphereLayers[i];
                sphere = {XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR};
                sphere.space = localSpace_;
                sphere.layerFlags = source.layer_flags;
                sphere.eyeVisibility = static_cast<XrEyeVisibility>(source.eye_visibility);
                sphere.pose.position = {source.pose.x, source.pose.y, source.pose.z};
                sphere.pose.orientation = {source.pose.qx, source.pose.qy, source.pose.qz, source.pose.qw};
                sphere.radius = source.radius;
                sphere.centralHorizontalAngle = source.horizontal_angle;
                sphere.upperVerticalAngle = source.upper_angle;
                sphere.lowerVerticalAngle = source.lower_angle;
                sphere.subImage.swapchain = mixedBatch ? panelSwapchain_ : projectionSwapchain_;
                sphere.subImage.imageArrayIndex = mixedBatch ? i : 0;
                sphere.subImage.imageRect.extent = mixedBatch ? uploadedMixedExtents_[imageIndex][i] : uploadedExtentByImage_[imageIndex];
                if (!equirectEnabled_) {
                    sphereFallbackIndices_[i] = fallbackSphereCount++;
                    auto* image = mixedBatch ? panelImages_[panelImageIndex_].texture : projectionImages_[imageIndex].texture;
                    if (!render_equirect(sphereFallbackIndices_[i], displayTime, image, sphere, source)) return false;
                }
                continue;
            }
            const auto& source = metadata.quads[mixedBatch ? 0 : i];
            auto& quad = quadLayers[i];
            quad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
            quad.space = localSpace_;
            quad.layerFlags = source.layer_flags;
            quad.eyeVisibility = static_cast<XrEyeVisibility>(source.eye_visibility);
            quad.pose.position = {source.pose.x, source.pose.y, source.pose.z};
            quad.pose.orientation = {source.pose.qx, source.pose.qy, source.pose.qz, source.pose.qw};
            quad.size = {source.width, source.height};
            quad.subImage.swapchain = mixedBatch ? panelSwapchain_ : projectionSwapchain_;
            quad.subImage.imageArrayIndex = i;
            quad.subImage.imageRect.extent = mixedBatch ? uploadedMixedExtents_[imageIndex][i] : uploadedExtentByImage_[imageIndex];
        }
        trim_composition_resources(fallbackSphereCount, mixedBatch);
        if (!mixedProjection) return true;
    }

    if (!quadCount) trim_composition_resources(0, false);
    XrViewLocateInfo locateInfo{XR_TYPE_VIEW_LOCATE_INFO};
    locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locateInfo.displayTime = displayTime;
    locateInfo.space = localSpace_;

    XrViewState viewState{XR_TYPE_VIEW_STATE};
    std::array<XrView, 2> views{XrView{XR_TYPE_VIEW}, XrView{XR_TYPE_VIEW}};
    uint32_t viewCount = 0;
    result = locateViews_(session_, &locateInfo, &viewState, static_cast<uint32_t>(views.size()), &viewCount, views.data());
    if (result != XR_SUCCESS || viewCount < 2) {
        return false;
    }

    for (uint32_t i = 0; i < 2; ++i) {
        projectionViews[i] = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
        projectionViews[i].pose = views[i].pose;
        projectionViews[i].fov.angleLeft = -kAppProjectionHalfFovRadians;
        projectionViews[i].fov.angleRight = kAppProjectionHalfFovRadians;
        projectionViews[i].fov.angleUp = kAppProjectionHalfFovRadians;
        projectionViews[i].fov.angleDown = -kAppProjectionHalfFovRadians;
        // Metadata must describe the camera that rendered this exact
        // texture, not the newest tracking pose sampled after rendering.
        const auto& projection = uploadedProjectionByImage_[imageIndex];
        if (projection.view_count == 2) {
            const auto& eye = projection.views[i];
            projectionViews[i].pose.position = {eye.pose.x, eye.pose.y, eye.pose.z};
            projectionViews[i].pose.orientation = {eye.pose.qx, eye.pose.qy, eye.pose.qz, eye.pose.qw};
            projectionViews[i].fov = {eye.angle_left, eye.angle_right, eye.angle_up, eye.angle_down};
        }
        projectionViews[i].subImage.swapchain = projectionSwapchain_;
        projectionViews[i].subImage.imageRect.offset = {0, 0};
        projectionViews[i].subImage.imageRect.extent = {
            static_cast<int32_t>(projectionWidth_),
            static_cast<int32_t>(projectionHeight_),
        };
        projectionViews[i].subImage.imageArrayIndex = i;
        if (projection.view_count == 2) {
            projectionViews[i].subImage.imageRect.extent = uploadedExtentByImage_[imageIndex];
        }
    }

    projectionLayer.space = localSpace_;
    projectionLayer.layerFlags = uploadedProjectionByImage_[imageIndex].layer_flags;
    projectionLayer.viewCount = 2;
    projectionLayer.views = projectionViews.data();
    if (!reportedProjectionSubmit_) {
        std::fprintf(stderr, "AXRB OpenXR: submitting projection layer to runtime\n");
        reportedProjectionSubmit_ = true;
    }
    return true;
}
#endif

#if defined(_WIN32)
bool OpenXrSession::fill_projection_texture(ID3D11Texture2D* texture, uint32_t imageIndex, const HostImageSnapshot& frame)
{
    if (texture == nullptr || d3dContext_.get() == nullptr) {
        return false;
    }
    if (upload_android_frame(texture, imageIndex, frame)) {
        splashUploaded_[imageIndex] = false;
        return true;
    }
    // Never associate fallback pixels with stale game poses or layer metadata.
    uploadedProjectionByImage_[imageIndex] = {};
    uploadedMixedCounts_[imageIndex] = 0;
    uploadedAndroidSequenceByImage_[imageIndex] = UINT64_MAX;
    uploadedGpuSessionByImage_[imageIndex] = 0;
    if (!debugGraphicsTest_) {
        if (!splashUploaded_[imageIndex]) {
            const uint32_t size = (std::min)(512u, (std::min)(projectionWidth_, projectionHeight_));
            if (splashPixels_.empty()) {
                const bool bgra = projectionFormat_ == DXGI_FORMAT_B8G8R8A8_UNORM || projectionFormat_ == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
                const bool linear = projectionFormat_ == DXGI_FORMAT_B8G8R8A8_UNORM || projectionFormat_ == DXGI_FORMAT_R8G8B8A8_UNORM;
                splashPixels_ = load_splash_pixels(size, bgra, linear);
            }
            D3D11_BOX box{0, 0, 0, size, size, 1};
            d3dContext_.get()->UpdateSubresource(texture, 0, &box, splashPixels_.data(), size * 4, size * size * 4);
            splashUploaded_[imageIndex] = true;
        }
        return false;
    }

    const uint32_t stride = projectionWidth_ * 4;
    std::vector<uint8_t> pixels(static_cast<size_t>(stride) * projectionHeight_);
    const uint32_t tick = projectionFrameCounter_++;
    for (uint32_t y = 0; y < projectionHeight_; ++y) {
        for (uint32_t x = 0; x < projectionWidth_; ++x) {
            const size_t offset = static_cast<size_t>(y) * stride + x * 4;
            pixels[offset + 0] = static_cast<uint8_t>((x + tick * 3) & 0xff);
            pixels[offset + 1] = static_cast<uint8_t>((y + tick * 2) & 0xff);
            pixels[offset + 2] = static_cast<uint8_t>((x / 8 + y / 8 + tick) & 0xff);
            pixels[offset + 3] = 255;
        }
    }

    for (uint32_t layer = 0; layer < 2; ++layer) {
        D3D11_BOX box{};
        box.left = 0;
        box.top = 0;
        box.front = 0;
        box.right = projectionWidth_;
        box.bottom = projectionHeight_;
        box.back = 1;
        d3dContext_.get()->UpdateSubresource(texture, layer, &box, pixels.data(), stride, stride * projectionHeight_);
    }
    return false;
}
#endif

#if defined(_WIN32)
bool OpenXrSession::upload_android_frame(ID3D11Texture2D* texture, uint32_t imageIndex, const HostImageSnapshot& frame)
{
    if (imageFrame_ == nullptr) {
        return false;
    }
    if (imageIndex >= uploadedAndroidSequenceByImage_.size()) {
        return false;
    }

    const auto& header = frame.header;
    const auto& projection = frame.projection;
    const auto& pixels = frame.pixels;
    if (header.width == 0 || header.height == 0 || header.layers == 0 || pixels == nullptr || pixels->empty()) {
        return false;
    }
    if (axrb::protocol::mixed_gpu_version(header.version)) {
        if (!frame.gpu) return false;
        const auto activeMixedCount = frame.gpu->count;
        const bool flattened = activeMixedCount > nativeLayerLimit_;
        if (activeMixedCount < 2 || activeMixedCount > axrb::protocol::kMaxWireCompositionLayers || (!flattened && !panelAcquired_)) return false;
        if (uploadedAndroidSequenceByImage_[imageIndex] != header.sequence || uploadedMixedTimes_[imageIndex] != header.monotonic_time_ns || (!flattened && panelSequences_[panelImageIndex_] != header.sequence)) {
            bool queued = true;
            const bool scene = projection.view_count == 2;
            for (uint32_t i = 0; i < activeMixedCount; ++i) {
                if (flattened && (!scene || i != 0)) continue;
                auto& part = frame.gpu->parts[i];
                const uint32_t slice = scene && i ? i - 1 : i;
                auto* destination = scene && i == 0 ? texture : panelImages_[panelImageIndex_].texture;
                if (!part.receiver.enqueue_copy_to(d3dContext_.get(), destination, scene && i == 0 ? 0 : slice, scene && i == 0 ? 2 : 1) ||
                    (!scene && i == 0 && !part.receiver.enqueue_copy_to(d3dContext_.get(), texture, 0, 1))) {
                    queued = false;
                    break;
                }
            }
            if (queued && flattened) queued = compose_overflow(texture, frame);
            // One completion covers every eye and layer. Keep the frame lease
            // and handoff lock until it finishes, even after a partial enqueue.
            const bool completed = frameCopyCompletion_.wait(d3dDevice_.get(), d3dContext_.get());
            if (!queued || !completed) { gpuFrames_.retire(frame.gpu); return false; }
            static bool reportedBatch = false;
            if (!reportedBatch) {
                std::fprintf(stderr, "AXRB GPU: mixed frame copies use one completion fence\n");
                reportedBatch = true;
            }
            uploadedMixedQuads_[imageIndex].resize(activeMixedCount);
            uploadedMixedExtents_[imageIndex].resize(activeMixedCount);
            // Publish cache metadata only after the whole GPU batch completes.
            for (uint32_t i = 0; i < activeMixedCount; ++i) {
                auto& part = frame.gpu->parts[i];
                const uint32_t firstQuad = projection.view_count == 2 ? 1 : 0;
                if (i >= firstQuad) {
                    uploadedMixedQuads_[imageIndex][i - firstQuad] = part.projection;
                    uploadedMixedExtents_[imageIndex][i - firstQuad] = {static_cast<int32_t>(part.header.width), static_cast<int32_t>(part.header.height)};
                }
            }
            uploadedProjectionByImage_[imageIndex] = projection;
            if (flattened) {
                auto& out = uploadedProjectionByImage_[imageIndex]; out = {}; out.view_count = 2;
                out.layer_flags = scene ? (projection.layer_flags & ~4u) : 2u;
                for (uint32_t eye = 0; eye < 2; ++eye) {
                    const auto& view = overflowViews_[eye]; auto& v = out.views[eye];
                    v.pose = to_protocol_pose(view.pose);
                    v.angle_left = view.fov.angleLeft; v.angle_right = view.fov.angleRight;
                    v.angle_up = view.fov.angleUp; v.angle_down = view.fov.angleDown;
                }
            }
            uploadedExtentByImage_[imageIndex] = flattened && !scene ? XrExtent2Di{static_cast<int32_t>(projectionWidth_), static_cast<int32_t>(projectionHeight_)} : XrExtent2Di{static_cast<int32_t>(header.width), static_cast<int32_t>(header.height)};
            uploadedAndroidSequenceByImage_[imageIndex] = header.sequence;
            uploadedGpuSessionByImage_[imageIndex] = 0;
            uploadedMixedCounts_[imageIndex] = flattened ? 0 : activeMixedCount - (projection.view_count == 2 ? 1 : 0);
            uploadedMixedTimes_[imageIndex] = header.monotonic_time_ns;
            if (!flattened) panelSequences_[panelImageIndex_] = header.sequence;
        }

        return true;
    }
    uploadedMixedCounts_[imageIndex] = 0;
    if ((header.version == axrb::protocol::kWindowsGpuFrameVersion || header.version == axrb::protocol::kQuadGpuFrameVersion || axrb::protocol::equirect_gpu_version(header.version))) {
        if (pixels->size() != sizeof(axrb::protocol::WindowsGpuFrame)) return false;
        axrb::protocol::WindowsGpuFrame gpu{};
        std::memcpy(&gpu, pixels->data(), sizeof(gpu));
        if (uploadedGpuSessionByImage_[imageIndex] == gpu.session &&
            uploadedAndroidSequenceByImage_[imageIndex] == header.sequence &&
            uploadedExtentByImage_[imageIndex].width == static_cast<int32_t>(header.width) &&
            uploadedExtentByImage_[imageIndex].height == static_cast<int32_t>(header.height)) {

            return true;
        }
        if (!frame.gpu || frame.gpu->count != 1) return false;
        if (!frame.gpu->parts[0].receiver.copy_to(d3dContext_.get(), texture)) { gpuFrames_.retire(frame.gpu); return false; }
        uploadedGpuSessionByImage_[imageIndex] = gpu.session;
        uploadedAndroidSequenceByImage_[imageIndex] = header.sequence;
        uploadedProjectionByImage_[imageIndex] = projection;
        uploadedExtentByImage_[imageIndex] = {static_cast<int32_t>(header.width), static_cast<int32_t>(header.height)};

        if (!reportedGpuImage_) { std::fprintf(stderr, "AXRB GPU: shared Windows textures active; no pixel TCP transfer\n"); reportedGpuImage_ = true; }
        return true;
    }
    if (header.sequence == uploadedAndroidSequenceByImage_[imageIndex]) {

        return true;
    }
    uploadedGpuSessionByImage_[imageIndex] = 0;
    if (projection.view_count == 2 && (header.width > projectionWidth_ || header.height > projectionHeight_)) {
        return false; // Cropping here would change the angular scale.
    }

    const uint32_t copyWidth = header.width < projectionWidth_ ? header.width : projectionWidth_;
    const uint32_t copyHeight = header.height < projectionHeight_ ? header.height : projectionHeight_;
    const uint32_t sourceStride = header.width * header.bytes_per_pixel;
    const bool needsBgra =
        projectionFormat_ == DXGI_FORMAT_B8G8R8A8_UNORM ||
        projectionFormat_ == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;

    std::vector<uint8_t> uploadBuffer(static_cast<size_t>(header.width) * header.height * header.layers * 4);
    for (uint32_t layer = 0; layer < header.layers; ++layer) {
        const uint64_t layerOffset = static_cast<uint64_t>(layer) * header.width * header.height * header.bytes_per_pixel;
        for (uint32_t y = 0; y < header.height; ++y) {
            const uint32_t sourceY = header.height - 1 - y;
            const uint64_t sourceRow = layerOffset + static_cast<uint64_t>(sourceY) * sourceStride;
            const uint64_t destRow =
                (static_cast<uint64_t>(layer) * header.width * header.height + static_cast<uint64_t>(y) * header.width) * 4;
            for (uint32_t x = 0; x < header.width; ++x) {
                const uint64_t source = sourceRow + static_cast<uint64_t>(x) * header.bytes_per_pixel;
                const uint64_t dest = destRow + static_cast<uint64_t>(x) * 4;
                if (needsBgra) {
                    uploadBuffer[dest + 0] = (*pixels)[source + 2];
                    uploadBuffer[dest + 1] = (*pixels)[source + 1];
                    uploadBuffer[dest + 2] = (*pixels)[source + 0];
                    uploadBuffer[dest + 3] = (*pixels)[source + 3];
                } else {
                    uploadBuffer[dest + 0] = (*pixels)[source + 0];
                    uploadBuffer[dest + 1] = (*pixels)[source + 1];
                    uploadBuffer[dest + 2] = (*pixels)[source + 2];
                    uploadBuffer[dest + 3] = (*pixels)[source + 3];
                }
            }
        }
    }

    const uint8_t* uploadPixels = uploadBuffer.data();
    const uint32_t uploadStride = header.width * 4;

    const uint32_t sourceLayers = header.layers;
    for (uint32_t targetLayer = 0; targetLayer < 2; ++targetLayer) {
        const uint32_t sourceLayer = sourceLayers > 1 ? targetLayer % sourceLayers : 0;
        const uint64_t sourceOffset =
            static_cast<uint64_t>(sourceLayer) * header.width * header.height * 4;
        if (sourceOffset >= uploadBuffer.size()) {
            continue;
        }

        D3D11_BOX box{};
        box.left = 0;
        box.top = 0;
        box.front = 0;
        box.right = copyWidth;
        box.bottom = copyHeight;
        box.back = 1;
        d3dContext_.get()->UpdateSubresource(
            texture,
            targetLayer,
            &box,
            uploadPixels + sourceOffset,
            uploadStride,
            uploadStride * header.height);
    }

    if (!reportedAndroidImageSubmit_) {
        std::fprintf(
            stderr,
            "AXRB OpenXR: submitting Android image frames to SteamVR (%ux%u layers=%u)\n",
            header.width,
            header.height,
            header.layers);
        reportedAndroidImageSubmit_ = true;
    }
    uploadedAndroidSequenceByImage_[imageIndex] = header.sequence;
    uploadedProjectionByImage_[imageIndex] = projection;
    uploadedExtentByImage_[imageIndex] = {static_cast<int32_t>(copyWidth), static_cast<int32_t>(copyHeight)};
    if (projection.view_count == 2 && !reportedStereoProjection_) {
        const auto& l = projection.views[0];
        const auto& r = projection.views[1];
        const float dx = r.pose.x-l.pose.x, dy = r.pose.y-l.pose.y, dz = r.pose.z-l.pose.z;
        std::fprintf(stderr, "AXRB OpenXR: stereo render metadata active; camera separation=%.1fmm left FOV=(%.3f %.3f %.3f %.3f)\n",
                     std::sqrt(dx*dx+dy*dy+dz*dz)*1000, l.angle_left, l.angle_right, l.angle_up, l.angle_down);
        reportedStereoProjection_ = true;
    }
    return true;
}
#endif

} // namespace axrb::host::detail
