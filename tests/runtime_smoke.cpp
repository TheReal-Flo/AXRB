#include "openxr_dispatch/openxr_minimal.h"

#include <cstdlib>

extern "C" XrResult XRAPI_CALL xrNegotiateLoaderRuntimeInterface(
    const XrNegotiateLoaderInfo* loaderInfo,
    XrNegotiateRuntimeRequest* runtimeRequest);

namespace {

int require_success(XrResult result)
{
    return result == XR_SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE;
}

template <typename Function>
Function get(PFN_xrGetInstanceProcAddr getInstanceProcAddr, XrInstance instance, const char* name)
{
    PFN_xrVoidFunction raw = nullptr;
    if (getInstanceProcAddr(instance, name, &raw) != XR_SUCCESS) {
        return nullptr;
    }
    return reinterpret_cast<Function>(raw);
}

} // namespace

int main()
{
    XrNegotiateLoaderInfo loaderInfo{};
    loaderInfo.structType = XR_LOADER_INTERFACE_STRUCT_LOADER_INFO;
    loaderInfo.structVersion = XR_LOADER_INFO_STRUCT_VERSION;
    loaderInfo.structSize = sizeof(XrNegotiateLoaderInfo);
    loaderInfo.minInterfaceVersion = 1;
    loaderInfo.maxInterfaceVersion = XR_CURRENT_LOADER_RUNTIME_VERSION;
    loaderInfo.minApiVersion = XR_MAKE_VERSION(1, 0, 0);
    loaderInfo.maxApiVersion = XR_MAKE_VERSION(1, 0, 0);

    XrNegotiateRuntimeRequest runtimeRequest{};
    if (xrNegotiateLoaderRuntimeInterface(&loaderInfo, &runtimeRequest) != XR_SUCCESS ||
        runtimeRequest.getInstanceProcAddr == nullptr) {
        return EXIT_FAILURE;
    }

    using CreateInstance = XrResult(XRAPI_PTR*)(const XrInstanceCreateInfo*, XrInstance*);
    auto xrCreateInstance = get<CreateInstance>(runtimeRequest.getInstanceProcAddr, nullptr, "xrCreateInstance");
    if (xrCreateInstance == nullptr) {
        return EXIT_FAILURE;
    }

    XrInstanceCreateInfo instanceCreateInfo{};
    instanceCreateInfo.type = XR_TYPE_INSTANCE_CREATE_INFO;
    instanceCreateInfo.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);

    XrInstance instance = nullptr;
    if (xrCreateInstance(&instanceCreateInfo, &instance) != XR_SUCCESS || instance == nullptr) {
        return EXIT_FAILURE;
    }

    using GetSystem = XrResult(XRAPI_PTR*)(XrInstance, const XrSystemGetInfo*, XrSystemId*);
    using GetInstanceProperties = XrResult(XRAPI_PTR*)(XrInstance, XrInstanceProperties*);
    using GetSystemProperties = XrResult(XRAPI_PTR*)(XrInstance, XrSystemId, XrSystemProperties*);
    using PollEvent = XrResult(XRAPI_PTR*)(XrInstance, XrEventDataBuffer*);
    using EnumerateEnvironmentBlendModes =
        XrResult(XRAPI_PTR*)(XrInstance, XrSystemId, XrViewConfigurationType, uint32_t, uint32_t*, XrEnvironmentBlendMode*);
    using CreateSession = XrResult(XRAPI_PTR*)(XrInstance, const XrSessionCreateInfo*, XrSession*);
    using BeginSession = XrResult(XRAPI_PTR*)(XrSession, const XrSessionBeginInfo*);
    using EndSession = XrResult(XRAPI_PTR*)(XrSession);
    using EnumerateSwapchainFormats = XrResult(XRAPI_PTR*)(XrSession, uint32_t, uint32_t*, int64_t*);
    using CreateSwapchain = XrResult(XRAPI_PTR*)(XrSession, const XrSwapchainCreateInfo*, XrSwapchain*);
    using DestroySwapchain = XrResult(XRAPI_PTR*)(XrSwapchain);
    using EnumerateSwapchainImages = XrResult(XRAPI_PTR*)(XrSwapchain, uint32_t, uint32_t*, XrSwapchainImageBaseHeader*);
    using AcquireSwapchainImage = XrResult(XRAPI_PTR*)(XrSwapchain, const XrSwapchainImageAcquireInfo*, uint32_t*);
    using WaitSwapchainImage = XrResult(XRAPI_PTR*)(XrSwapchain, const XrSwapchainImageWaitInfo*);
    using ReleaseSwapchainImage = XrResult(XRAPI_PTR*)(XrSwapchain, const XrSwapchainImageReleaseInfo*);
    using EnumerateViewConfigurations =
        XrResult(XRAPI_PTR*)(XrInstance, XrSystemId, uint32_t, uint32_t*, XrViewConfigurationType*);
    using EnumerateReferenceSpaces = XrResult(XRAPI_PTR*)(XrSession, uint32_t, uint32_t*, XrReferenceSpaceType*);
    using CreateReferenceSpace = XrResult(XRAPI_PTR*)(XrSession, const XrReferenceSpaceCreateInfo*, XrSpace*);
    using LocateViews =
        XrResult(XRAPI_PTR*)(XrSession, const XrViewLocateInfo*, XrViewState*, uint32_t, uint32_t*, XrView*);
    using WaitFrame = XrResult(XRAPI_PTR*)(XrSession, const XrFrameWaitInfo*, XrFrameState*);
    using BeginFrame = XrResult(XRAPI_PTR*)(XrSession, const XrFrameBeginInfo*);
    using EndFrame = XrResult(XRAPI_PTR*)(XrSession, const XrFrameEndInfo*);

    auto xrGetSystem = get<GetSystem>(runtimeRequest.getInstanceProcAddr, instance, "xrGetSystem");
    auto xrGetInstanceProperties =
        get<GetInstanceProperties>(runtimeRequest.getInstanceProcAddr, instance, "xrGetInstanceProperties");
    auto xrGetSystemProperties =
        get<GetSystemProperties>(runtimeRequest.getInstanceProcAddr, instance, "xrGetSystemProperties");
    auto xrPollEvent = get<PollEvent>(runtimeRequest.getInstanceProcAddr, instance, "xrPollEvent");
    auto xrEnumerateEnvironmentBlendModes =
        get<EnumerateEnvironmentBlendModes>(runtimeRequest.getInstanceProcAddr, instance, "xrEnumerateEnvironmentBlendModes");
    auto xrCreateSession = get<CreateSession>(runtimeRequest.getInstanceProcAddr, instance, "xrCreateSession");
    auto xrBeginSession = get<BeginSession>(runtimeRequest.getInstanceProcAddr, instance, "xrBeginSession");
    auto xrEndSession = get<EndSession>(runtimeRequest.getInstanceProcAddr, instance, "xrEndSession");
    auto xrEnumerateSwapchainFormats =
        get<EnumerateSwapchainFormats>(runtimeRequest.getInstanceProcAddr, instance, "xrEnumerateSwapchainFormats");
    auto xrCreateSwapchain = get<CreateSwapchain>(runtimeRequest.getInstanceProcAddr, instance, "xrCreateSwapchain");
    auto xrDestroySwapchain = get<DestroySwapchain>(runtimeRequest.getInstanceProcAddr, instance, "xrDestroySwapchain");
    auto xrEnumerateSwapchainImages =
        get<EnumerateSwapchainImages>(runtimeRequest.getInstanceProcAddr, instance, "xrEnumerateSwapchainImages");
    auto xrAcquireSwapchainImage =
        get<AcquireSwapchainImage>(runtimeRequest.getInstanceProcAddr, instance, "xrAcquireSwapchainImage");
    auto xrWaitSwapchainImage =
        get<WaitSwapchainImage>(runtimeRequest.getInstanceProcAddr, instance, "xrWaitSwapchainImage");
    auto xrReleaseSwapchainImage =
        get<ReleaseSwapchainImage>(runtimeRequest.getInstanceProcAddr, instance, "xrReleaseSwapchainImage");
    auto xrEnumerateViewConfigurations =
        get<EnumerateViewConfigurations>(runtimeRequest.getInstanceProcAddr, instance, "xrEnumerateViewConfigurations");
    auto xrCreateReferenceSpace =
        get<CreateReferenceSpace>(runtimeRequest.getInstanceProcAddr, instance, "xrCreateReferenceSpace");
    auto xrLocateViews = get<LocateViews>(runtimeRequest.getInstanceProcAddr, instance, "xrLocateViews");
    auto xrEnumerateReferenceSpaces =
        get<EnumerateReferenceSpaces>(runtimeRequest.getInstanceProcAddr, instance, "xrEnumerateReferenceSpaces");
    auto xrWaitFrame = get<WaitFrame>(runtimeRequest.getInstanceProcAddr, instance, "xrWaitFrame");
    auto xrBeginFrame = get<BeginFrame>(runtimeRequest.getInstanceProcAddr, instance, "xrBeginFrame");
    auto xrEndFrame = get<EndFrame>(runtimeRequest.getInstanceProcAddr, instance, "xrEndFrame");

    if (xrGetSystem == nullptr || xrGetInstanceProperties == nullptr || xrGetSystemProperties == nullptr ||
        xrPollEvent == nullptr || xrEnumerateEnvironmentBlendModes == nullptr || xrCreateSession == nullptr ||
        xrBeginSession == nullptr || xrEndSession == nullptr ||
        xrEnumerateSwapchainFormats == nullptr || xrCreateSwapchain == nullptr || xrDestroySwapchain == nullptr ||
        xrEnumerateSwapchainImages == nullptr || xrAcquireSwapchainImage == nullptr ||
        xrWaitSwapchainImage == nullptr || xrReleaseSwapchainImage == nullptr ||
        xrEnumerateViewConfigurations == nullptr || xrCreateReferenceSpace == nullptr ||
        xrLocateViews == nullptr || xrEnumerateReferenceSpaces == nullptr || xrWaitFrame == nullptr || xrBeginFrame == nullptr ||
        xrEndFrame == nullptr) {
        return EXIT_FAILURE;
    }

    XrInstanceProperties instanceProperties{};
    instanceProperties.type = XR_TYPE_INSTANCE_PROPERTIES;
    if (xrGetInstanceProperties(instance, &instanceProperties) != XR_SUCCESS) {
        return EXIT_FAILURE;
    }

    XrSystemGetInfo systemGetInfo{};
    systemGetInfo.type = XR_TYPE_SYSTEM_GET_INFO;
    systemGetInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

    XrSystemId systemId = 0;
    if (xrGetSystem(instance, &systemGetInfo, &systemId) != XR_SUCCESS) {
        return EXIT_FAILURE;
    }

    XrSystemProperties systemProperties{};
    systemProperties.type = XR_TYPE_SYSTEM_PROPERTIES;
    if (xrGetSystemProperties(instance, systemId, &systemProperties) != XR_SUCCESS) {
        return EXIT_FAILURE;
    }

    uint32_t viewConfigurationCount = 0;
    if (xrEnumerateViewConfigurations(instance, systemId, 0, &viewConfigurationCount, nullptr) != XR_SUCCESS ||
        viewConfigurationCount != 1) {
        return EXIT_FAILURE;
    }

    uint32_t environmentBlendModeCount = 0;
    if (xrEnumerateEnvironmentBlendModes(
            instance,
            systemId,
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
            0,
            &environmentBlendModeCount,
            nullptr) != XR_SUCCESS || environmentBlendModeCount != 1) {
        return EXIT_FAILURE;
    }

    XrSessionCreateInfo sessionCreateInfo{};
    sessionCreateInfo.type = XR_TYPE_SESSION_CREATE_INFO;
    sessionCreateInfo.systemId = systemId;

    XrSession session = nullptr;
    if (xrCreateSession(instance, &sessionCreateInfo, &session) != XR_SUCCESS || session == nullptr) {
        return EXIT_FAILURE;
    }

    bool sawReady = false;
    for (int i = 0; i < 8; ++i) {
        XrEventDataBuffer event{};
        event.type = XR_TYPE_EVENT_DATA_BUFFER;
        const XrResult eventResult = xrPollEvent(instance, &event);
        if (eventResult == XR_EVENT_UNAVAILABLE) {
            break;
        }
        if (eventResult != XR_SUCCESS) {
            return EXIT_FAILURE;
        }
        if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            const auto* stateChanged = reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);
            sawReady = sawReady || stateChanged->state == XR_SESSION_STATE_READY;
        }
    }
    if (!sawReady) {
        return EXIT_FAILURE;
    }

    XrSessionBeginInfo beginSessionInfo{};
    beginSessionInfo.type = XR_TYPE_SESSION_BEGIN_INFO;
    beginSessionInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    if (xrBeginSession(session, &beginSessionInfo) != XR_SUCCESS) {
        return EXIT_FAILURE;
    }

    uint32_t swapchainFormatCount = 0;
    if (xrEnumerateSwapchainFormats(session, 0, &swapchainFormatCount, nullptr) != XR_SUCCESS ||
        swapchainFormatCount == 0) {
        return EXIT_FAILURE;
    }
    int64_t swapchainFormat = 0;
    if (xrEnumerateSwapchainFormats(session, 1, &swapchainFormatCount, &swapchainFormat) != XR_SUCCESS ||
        swapchainFormat == 0) {
        return EXIT_FAILURE;
    }

    XrSwapchainCreateInfo swapchainCreateInfo{};
    swapchainCreateInfo.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
    swapchainCreateInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    swapchainCreateInfo.format = swapchainFormat;
    swapchainCreateInfo.sampleCount = 1;
    swapchainCreateInfo.width = 1024;
    swapchainCreateInfo.height = 1024;
    swapchainCreateInfo.faceCount = 1;
    swapchainCreateInfo.arraySize = 2;
    swapchainCreateInfo.mipCount = 1;

    XrSwapchain swapchain = nullptr;
    if (xrCreateSwapchain(session, &swapchainCreateInfo, &swapchain) != XR_SUCCESS || swapchain == nullptr) {
        return EXIT_FAILURE;
    }

    uint32_t swapchainImageCount = 0;
    if (xrEnumerateSwapchainImages(swapchain, 0, &swapchainImageCount, nullptr) != XR_SUCCESS ||
        swapchainImageCount != 3) {
        return EXIT_FAILURE;
    }
    XrSwapchainImageOpenGLESKHR swapchainImages[3]{};
    for (auto& image : swapchainImages) {
        image.type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
    }
    if (xrEnumerateSwapchainImages(
            swapchain,
            3,
            &swapchainImageCount,
            reinterpret_cast<XrSwapchainImageBaseHeader*>(swapchainImages)) != XR_SUCCESS ||
        swapchainImageCount != 3) {
        return EXIT_FAILURE;
    }

    XrSwapchainImageAcquireInfo acquireInfo{};
    acquireInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;
    uint32_t swapchainImageIndex = 0;
    if (xrAcquireSwapchainImage(swapchain, &acquireInfo, &swapchainImageIndex) != XR_SUCCESS ||
        swapchainImageIndex >= 3) {
        return EXIT_FAILURE;
    }

    XrSwapchainImageWaitInfo waitSwapchainInfo{};
    waitSwapchainInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
    waitSwapchainInfo.timeout = 0;
    if (xrWaitSwapchainImage(swapchain, &waitSwapchainInfo) != XR_SUCCESS) {
        return EXIT_FAILURE;
    }

    XrSwapchainImageReleaseInfo releaseInfo{};
    releaseInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
    if (xrReleaseSwapchainImage(swapchain, &releaseInfo) != XR_SUCCESS) {
        return EXIT_FAILURE;
    }

    uint32_t referenceSpaceCount = 0;
    if (xrEnumerateReferenceSpaces(session, 0, &referenceSpaceCount, nullptr) != XR_SUCCESS ||
        referenceSpaceCount != 3) {
        return EXIT_FAILURE;
    }

    XrReferenceSpaceCreateInfo spaceCreateInfo{};
    spaceCreateInfo.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
    spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    spaceCreateInfo.poseInReferenceSpace.orientation.w = 1.0f;

    XrSpace space = nullptr;
    if (xrCreateReferenceSpace(session, &spaceCreateInfo, &space) != XR_SUCCESS || space == nullptr) {
        return EXIT_FAILURE;
    }

    XrFrameWaitInfo waitInfo{};
    waitInfo.type = XR_TYPE_FRAME_WAIT_INFO;
    XrFrameState frameState{};
    frameState.type = XR_TYPE_FRAME_STATE;
    if (xrWaitFrame(session, &waitInfo, &frameState) != XR_SUCCESS || frameState.shouldRender == 0) {
        return EXIT_FAILURE;
    }

    XrViewLocateInfo locateInfo{};
    locateInfo.type = XR_TYPE_VIEW_LOCATE_INFO;
    locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locateInfo.displayTime = frameState.predictedDisplayTime;
    locateInfo.space = space;

    XrViewState viewState{};
    viewState.type = XR_TYPE_VIEW_STATE;
    XrView views[2]{};
    views[0].type = XR_TYPE_VIEW;
    views[1].type = XR_TYPE_VIEW;
    uint32_t locatedViewCount = 0;
    if (xrLocateViews(session, &locateInfo, &viewState, 2, &locatedViewCount, views) != XR_SUCCESS ||
        locatedViewCount != 2 ||
        (viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) == 0) {
        return EXIT_FAILURE;
    }

    XrFrameBeginInfo beginInfo{};
    beginInfo.type = XR_TYPE_FRAME_BEGIN_INFO;
    if (xrBeginFrame(session, &beginInfo) != XR_SUCCESS) {
        return EXIT_FAILURE;
    }

    XrFrameEndInfo endInfo{};
    endInfo.type = XR_TYPE_FRAME_END_INFO;
    endInfo.displayTime = frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    if (xrEndFrame(session, &endInfo) != XR_SUCCESS) {
        return EXIT_FAILURE;
    }

    if (xrDestroySwapchain(swapchain) != XR_SUCCESS) {
        return EXIT_FAILURE;
    }

    return require_success(xrEndSession(session));
}
