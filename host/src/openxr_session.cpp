#include "openxr_session.h"

namespace axrb::host::detail {

const char* xr_result_name(XrResult result)
{
    switch (result) {
    case XR_SUCCESS:
        return "XR_SUCCESS";
    case XR_TIMEOUT_EXPIRED:
        return "XR_TIMEOUT_EXPIRED";
    case XR_FRAME_DISCARDED:
        return "XR_FRAME_DISCARDED";
    case XR_SESSION_LOSS_PENDING:
        return "XR_SESSION_LOSS_PENDING";
    case XR_EVENT_UNAVAILABLE:
        return "XR_EVENT_UNAVAILABLE";
    case XR_SESSION_NOT_FOCUSED:
        return "XR_SESSION_NOT_FOCUSED";
    case XR_ERROR_RUNTIME_FAILURE:
        return "XR_ERROR_RUNTIME_FAILURE";
    case XR_ERROR_VALIDATION_FAILURE:
        return "XR_ERROR_VALIDATION_FAILURE";
    case XR_ERROR_RUNTIME_UNAVAILABLE:
        return "XR_ERROR_RUNTIME_UNAVAILABLE";
    case XR_ERROR_EXTENSION_NOT_PRESENT:
        return "XR_ERROR_EXTENSION_NOT_PRESENT";
    case XR_ERROR_FORM_FACTOR_UNAVAILABLE:
        return "XR_ERROR_FORM_FACTOR_UNAVAILABLE";
    case XR_ERROR_INITIALIZATION_FAILED:
        return "XR_ERROR_INITIALIZATION_FAILED";
    case XR_ERROR_GRAPHICS_DEVICE_INVALID:
        return "XR_ERROR_GRAPHICS_DEVICE_INVALID";
    case XR_ERROR_CALL_ORDER_INVALID:
        return "XR_ERROR_CALL_ORDER_INVALID";
    default:
        return "XR_RESULT_UNKNOWN";
    }
}

axrb::protocol::Pose to_protocol_pose(const XrPosef& pose)
{
    axrb::protocol::Pose out{};
    out.x = pose.position.x;
    out.y = pose.position.y;
    out.z = pose.position.z;
    out.qx = pose.orientation.x;
    out.qy = pose.orientation.y;
    out.qz = pose.orientation.z;
    out.qw = pose.orientation.w;
    return out;
}

uint64_t monotonic_time_ns()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}


OpenXrSession::~OpenXrSession()
{
    if (session_ != XR_NULL_HANDLE && endSession_ != nullptr && sessionRunning_) {
        endSession_(session_);
    }
    if (viewSpace_ != XR_NULL_HANDLE && destroySpace_ != nullptr) {
        destroySpace_(viewSpace_);
    }
    if (localSpace_ != XR_NULL_HANDLE && destroySpace_ != nullptr) {
        destroySpace_(localSpace_);
    }
    if (appLocalSpace_ != XR_NULL_HANDLE && destroySpace_) destroySpace_(appLocalSpace_);
#if defined(_WIN32)
    if (panelSwapchain_ && destroySwapchain_) destroySwapchain_(panelSwapchain_);
    for (auto& target : equirectTargets_) if (target.swapchain && destroySwapchain_) destroySwapchain_(target.swapchain);
    if (fpsHudEvent_) CloseHandle(fpsHudEvent_);
    if (fpsHudSwapchain_ != XR_NULL_HANDLE && destroySwapchain_) destroySwapchain_(fpsHudSwapchain_);
    if (projectionSwapchain_ != XR_NULL_HANDLE && destroySwapchain_ != nullptr) {
        destroySwapchain_(projectionSwapchain_);
        projectionSwapchain_ = XR_NULL_HANDLE;
    }
#endif
    for (XrSpace& handSpace : handSpaces_) {
        if (handSpace != XR_NULL_HANDLE && destroySpace_ != nullptr) {
            destroySpace_(handSpace);
            handSpace = XR_NULL_HANDLE;
        }
    }
    for (auto tracker : handTrackers_) {
        if (tracker && destroyHandTracker_) destroyHandTracker_(tracker);
    }
    for (XrSpace space : aimSpaces_) {
        if (space != XR_NULL_HANDLE && destroySpace_) destroySpace_(space);
    }
    if (aimPoseAction_ != XR_NULL_HANDLE && destroyAction_) destroyAction_(aimPoseAction_);
    if (handPoseAction_ != XR_NULL_HANDLE && destroyAction_ != nullptr) {
        destroyAction_(handPoseAction_);
        handPoseAction_ = XR_NULL_HANDLE;
    }
    if (actionSet_ != XR_NULL_HANDLE && destroyActionSet_ != nullptr) {
        destroyActionSet_(actionSet_);
        actionSet_ = XR_NULL_HANDLE;
    }
    if (session_ != XR_NULL_HANDLE && destroySession_ != nullptr) {
        destroySession_(session_);
    }
    if (instance_ != XR_NULL_HANDLE && destroyInstance_ != nullptr) {
        destroyInstance_(instance_);
    }
}

bool OpenXrSession::initialize(const std::string& gameName)
{
    if (!loader_.load()) {
        return false;
    }

    std::vector<const char*> extensions = {
#if defined(_WIN32)
        XR_KHR_D3D11_ENABLE_EXTENSION_NAME,
#else
        XR_MND_HEADLESS_EXTENSION_NAME,
        XR_KHR_CONVERT_TIMESPEC_TIME_EXTENSION_NAME,
#endif
    };
    PFN_xrVoidFunction enumerateRaw = nullptr;
    loader_.getInstanceProcAddr(XR_NULL_HANDLE, "xrEnumerateInstanceExtensionProperties", &enumerateRaw);
    if (enumerateRaw) {
        auto enumerate = reinterpret_cast<PFN_xrEnumerateInstanceExtensionProperties>(enumerateRaw);
        uint32_t count = 0; enumerate(nullptr, 0, &count, nullptr);
        std::vector<XrExtensionProperties> available(count, {XR_TYPE_EXTENSION_PROPERTIES});
        if (enumerate(nullptr, count, &count, available.data()) == XR_SUCCESS) {
            for (const auto& ext : available) {
                if (std::strcmp(ext.extensionName, XR_KHR_COMPOSITION_LAYER_EQUIRECT2_EXTENSION_NAME) == 0) equirectEnabled_ = true;
                if (std::strcmp(ext.extensionName, XR_EXT_HAND_TRACKING_EXTENSION_NAME) == 0) handTrackingEnabled_ = true;
                if (std::strcmp(ext.extensionName, XR_EXT_HAND_TRACKING_DATA_SOURCE_EXTENSION_NAME) == 0) handDataSourceEnabled_ = true;
            }
        }
    }
    if (equirectEnabled_) extensions.push_back(XR_KHR_COMPOSITION_LAYER_EQUIRECT2_EXTENSION_NAME);
    std::fprintf(stderr, "AXRB compositor: equirect2=%s\n", equirectEnabled_ ? "enabled" : "unavailable");
    if (handTrackingEnabled_) {
        extensions.push_back(XR_EXT_HAND_TRACKING_EXTENSION_NAME);
        if (handDataSourceEnabled_) extensions.push_back(XR_EXT_HAND_TRACKING_DATA_SOURCE_EXTENSION_NAME);
    }
    XrInstanceCreateInfo instanceInfo{XR_TYPE_INSTANCE_CREATE_INFO};
    std::string applicationName = gameName + " \xe2\x80\x93 AXRB";
    size_t nameLength = (std::min)(applicationName.size(), size_t(XR_MAX_APPLICATION_NAME_SIZE - 1));
    while (nameLength < applicationName.size() && nameLength &&
           (static_cast<unsigned char>(applicationName[nameLength]) & 0xc0) == 0x80) --nameLength;
    std::memcpy(instanceInfo.applicationInfo.applicationName, applicationName.data(), nameLength);
    instanceInfo.applicationInfo.applicationVersion = 1;
    std::strncpy(instanceInfo.applicationInfo.engineName, "AXRB", XR_MAX_ENGINE_NAME_SIZE - 1);
    instanceInfo.applicationInfo.engineVersion = 1;
#if defined(_WIN32)
    instanceInfo.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
#else
    instanceInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
#endif
    instanceInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    instanceInfo.enabledExtensionNames = extensions.data();

    XrResult result = loader_.createInstance(&instanceInfo, &instance_);
    if (result != XR_SUCCESS) {
        std::fprintf(
            stderr,
            "AXRB OpenXR: xrCreateInstance failed: %s (%d). Required extension: %s\n",
            xr_result_name(result),
            result,
#if defined(_WIN32)
            XR_KHR_D3D11_ENABLE_EXTENSION_NAME
#else
            XR_MND_HEADLESS_EXTENSION_NAME
#endif
        );
        return false;
    }

    if (!load_instance_functions()) {
        return false;
    }

    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    result = getSystem_(instance_, &systemInfo, &systemId_);
    if (result != XR_SUCCESS) {
        std::fprintf(stderr, "AXRB OpenXR: xrGetSystem failed: %s (%d)\n", xr_result_name(result), result);
        return false;
    }

#if defined(_WIN32)
    PFN_xrGetSystemProperties systemProperties = nullptr;
    XrSystemProperties properties{XR_TYPE_SYSTEM_PROPERTIES};
    if (!load_func("xrGetSystemProperties", &systemProperties) ||
        systemProperties(instance_, systemId_, &properties) != XR_SUCCESS || !properties.graphicsProperties.maxLayerCount) return false;
    nativeLayerLimit_ = properties.graphicsProperties.maxLayerCount;
    std::fprintf(stderr, "AXRB compositor: native layer limit=%u; excess layers composed on GPU\n", nativeLayerLimit_);
    if (!create_d3d11_device()) {
        return false;
    }
    XrGraphicsBindingD3D11KHR graphicsBinding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
    graphicsBinding.device = d3dDevice_.get();
#endif

    XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
    sessionInfo.systemId = systemId_;
#if defined(_WIN32)
    sessionInfo.next = &graphicsBinding;
#endif
    result = createSession_(instance_, &sessionInfo, &session_);
    if (result != XR_SUCCESS) {
        std::fprintf(stderr, "AXRB OpenXR: xrCreateSession failed: %s (%d)\n", xr_result_name(result), result);
        return false;
    }

    // Android games commonly request STAGE/floor tracking. Keep the shared
    // tracking world floor-relative when SteamVR provides a stage space.
    const bool hasFloor = create_reference_space(XR_REFERENCE_SPACE_TYPE_STAGE, &localSpace_);
    trackingSpaceType_ = hasFloor ? XR_REFERENCE_SPACE_TYPE_STAGE : XR_REFERENCE_SPACE_TYPE_LOCAL;
    std::fprintf(stderr, "AXRB OpenXR: tracking origin=%s\n", hasFloor ? "STAGE (floor)" : "LOCAL");
    if ((!hasFloor && !create_reference_space(XR_REFERENCE_SPACE_TYPE_LOCAL, &localSpace_)) ||
        !create_reference_space(XR_REFERENCE_SPACE_TYPE_VIEW, &viewSpace_) ||
        !create_reference_space(XR_REFERENCE_SPACE_TYPE_LOCAL, &appLocalSpace_)) {
        return false;
    }

#if defined(_WIN32)
    std::fprintf(stderr, "AXRB GPU: frame handoff=%s\n", concurrentGpuFrames_ ? "overlapping (experimental)" : "serialized");
    if (!create_projection_swapchain()) return false;
#endif
    initialize_controller_actions();
    initialize_hand_tracking();

    std::fprintf(
        stderr,
        "AXRB OpenXR: host tracking source initialized with %s\n",
#if defined(_WIN32)
        XR_KHR_D3D11_ENABLE_EXTENSION_NAME
#else
        XR_MND_HEADLESS_EXTENSION_NAME
#endif
    );
    return true;
}

bool OpenXrSession::open_mirror(const std::string& gameName)
{
#if defined(_WIN32)
    return mirror_.open(d3dDevice_.get(), gameName);
#else
    return true;
#endif
}

bool OpenXrSession::pump_mirror()
{
#if defined(_WIN32)
    return mirror_.pump();
#else
    return true;
#endif
}

bool OpenXrSession::drives_frame_loop() const
{ return sessionRunning_ && useFrameLoop_; }

bool OpenXrSession::load_instance_functions()
{
    return load_func("xrDestroyInstance", &destroyInstance_) &&
        load_func("xrGetSystem", &getSystem_) &&
        load_func("xrEnumerateViewConfigurationViews", &enumerateViewConfigurationViews_) &&
        load_func("xrCreateSession", &createSession_) &&
        load_func("xrDestroySession", &destroySession_) &&
        load_func("xrCreateReferenceSpace", &createReferenceSpace_) &&
        load_func("xrDestroySpace", &destroySpace_) &&
        load_func("xrPollEvent", &pollEvent_) &&
        load_func("xrBeginSession", &beginSession_) &&
        load_func("xrEndSession", &endSession_) &&
        load_func("xrWaitFrame", &waitFrame_) &&
        load_func("xrBeginFrame", &beginFrame_) &&
        load_func("xrEndFrame", &endFrame_) &&
        load_func("xrLocateSpace", &locateSpace_) &&
        load_func("xrLocateViews", &locateViews_) &&
        load_func("xrStringToPath", &stringToPath_) &&
        load_func("xrCreateActionSet", &createActionSet_) &&
        load_func("xrDestroyActionSet", &destroyActionSet_) &&
        load_func("xrCreateAction", &createAction_) &&
        load_func("xrDestroyAction", &destroyAction_) &&
        load_func("xrSuggestInteractionProfileBindings", &suggestInteractionProfileBindings_) &&
        load_func("xrAttachSessionActionSets", &attachSessionActionSets_) &&
        load_func("xrCreateActionSpace", &createActionSpace_) &&
        load_func("xrSyncActions", &syncActions_) &&
        load_func("xrGetActionStatePose", &getActionStatePose_) &&
        load_func("xrGetActionStateBoolean", &getActionStateBoolean_) &&
        load_func("xrGetActionStateFloat", &getActionStateFloat_) &&
        load_func("xrGetActionStateVector2f", &getActionStateVector2f_)
#if defined(_WIN32)
        && load_func("xrEnumerateSwapchainFormats", &enumerateSwapchainFormats_)
        && load_func("xrCreateSwapchain", &createSwapchain_)
        && load_func("xrDestroySwapchain", &destroySwapchain_)
        && load_func("xrEnumerateSwapchainImages", &enumerateSwapchainImages_)
        && load_func("xrAcquireSwapchainImage", &acquireSwapchainImage_)
        && load_func("xrWaitSwapchainImage", &waitSwapchainImage_)
        && load_func("xrReleaseSwapchainImage", &releaseSwapchainImage_)
        && load_func("xrGetD3D11GraphicsRequirementsKHR", &getD3D11GraphicsRequirements_)
#endif
#if !defined(_WIN32)
        && load_func("xrConvertTimespecTimeToTimeKHR", &convertTimespecTimeToTime_)
#endif
        ;
}

XrTime OpenXrSession::current_xr_time()
{
#if !defined(_WIN32)
    if (convertTimespecTimeToTime_ != nullptr) {
        timespec now{};
        if (clock_gettime(CLOCK_MONOTONIC, &now) == 0) {
            XrTime xrTime = 0;
            const XrResult result = convertTimespecTimeToTime_(instance_, &now, &xrTime);
            if (result == XR_SUCCESS) {
                return xrTime;
            }
        }
    }
#endif
    return 0;
}

void OpenXrSession::pump_events()
{
    XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
    while (pollEvent_(instance_, &event) == XR_SUCCESS) {
        if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            const auto* stateEvent = reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);
            handle_session_state(stateEvent->state);
        }
        event = {XR_TYPE_EVENT_DATA_BUFFER};
    }
}

void OpenXrSession::handle_session_state(XrSessionState state)
{
    std::fprintf(stderr, "AXRB OpenXR: session state=%d\n", static_cast<int>(state));
    if (state == XR_SESSION_STATE_READY && !sessionRunning_) {
        XrSessionBeginInfo beginInfo{XR_TYPE_SESSION_BEGIN_INFO};
        beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        const XrResult result = beginSession_(session_, &beginInfo);
        if (result == XR_SUCCESS) {
            sessionRunning_ = true;
            std::fprintf(stderr, "AXRB OpenXR: host session running\n");
        } else {
            std::fprintf(stderr, "AXRB OpenXR: xrBeginSession failed: %s (%d)\n", xr_result_name(result), result);
        }
    } else if (state == XR_SESSION_STATE_STOPPING && sessionRunning_) {
        endSession_(session_);
        sessionRunning_ = false;
        std::fprintf(stderr, "AXRB OpenXR: host session stopped\n");
    } else if (state == XR_SESSION_STATE_EXITING || state == XR_SESSION_STATE_LOSS_PENDING) {
        sessionRunning_ = false;
    }
}

} // namespace axrb::host::detail
