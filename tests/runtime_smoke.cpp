#include <array>
#include "openxr_dispatch/openxr_minimal.h"
#include "openxr_dispatch/hand_tracking_types.h"

#include <cstdlib>
#include <cmath>
#include <ctime>
#include <limits>
#if defined(AXRB_INPUT_FIXTURE)
#include "pose_frame.h"
axrb::protocol::PoseFrame& axrb_input_fixture();
#endif
#include <cstring>
#include <string>
#include <vector>

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

#if defined(AXRB_INPUT_FIXTURE)
    using ToTime = XrResult(XRAPI_PTR*)(XrInstance, const timespec*, XrTime*);
    using ToTimespec = XrResult(XRAPI_PTR*)(XrInstance, XrTime, timespec*);
    auto toTime = get<ToTime>(runtimeRequest.getInstanceProcAddr, instance, "xrConvertTimespecTimeToTimeKHR");
    auto toTimespec = get<ToTimespec>(runtimeRequest.getInstanceProcAddr, instance, "xrConvertTimeToTimespecTimeKHR");
    timespec ts{123, 456789}, roundtrip{}; XrTime converted = 0;
    if (!toTime || !toTimespec || toTime(instance, &ts, &converted) != XR_SUCCESS || converted != 123000456789LL ||
        toTimespec(instance, converted, &roundtrip) != XR_SUCCESS || roundtrip.tv_sec != ts.tv_sec || roundtrip.tv_nsec != ts.tv_nsec ||
        toTime(nullptr, &ts, &converted) != XR_ERROR_HANDLE_INVALID ||
        toTime(instance, nullptr, &converted) != XR_ERROR_VALIDATION_FAILURE ||
        toTimespec(instance, 0, &roundtrip) != XR_ERROR_TIME_INVALID) return EXIT_FAILURE;
    ts.tv_sec = std::numeric_limits<decltype(ts.tv_sec)>::max();
    if (toTime(instance, &ts, &converted) != XR_ERROR_TIME_INVALID) return EXIT_FAILURE;
    ts = {123, 1000000000};
    if (toTime(instance, &ts, &converted) != XR_ERROR_TIME_INVALID) return EXIT_FAILURE;
#endif
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
#if defined(AXRB_INPUT_FIXTURE)
    axrb_input_fixture().render_width = 2880;
    axrb_input_fixture().render_height = 3200;
    axrb_input_fixture().local_origin_flags = axrb_input_fixture().hmd_flags = 15;
#endif
    using EnumerateViews = XrResult(XRAPI_PTR*)(XrInstance, XrSystemId, XrViewConfigurationType, uint32_t, uint32_t*, XrViewConfigurationView*);
    auto enumerateViews = get<EnumerateViews>(runtimeRequest.getInstanceProcAddr, instance, "xrEnumerateViewConfigurationViews");
    XrViewConfigurationView configs[2]{{XR_TYPE_VIEW_CONFIGURATION_VIEW}, {XR_TYPE_VIEW_CONFIGURATION_VIEW}};
    uint32_t configCount = 0;
    if (!enumerateViews || enumerateViews(instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &configCount, configs) != XR_SUCCESS || configCount != 2) return EXIT_FAILURE;
    for (const auto& config : configs) {
#if defined(AXRB_INPUT_FIXTURE)
        if (config.recommendedImageRectWidth != 2880 || config.recommendedImageRectHeight != 3200) return EXIT_FAILURE;
#else
        if (config.recommendedImageRectWidth != 1024 || config.recommendedImageRectHeight != 1024) return EXIT_FAILURE;
#endif
    }
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
    // hello_xr allocates one swapchain per eye. Creating/acquiring/destroying
    // the second must not reset the first eye's outstanding acquisition.
    XrSwapchain secondEye = nullptr;
    uint32_t secondEyeIndex = 0;
    XrSwapchainImageReleaseInfo secondRelease{};
    secondRelease.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
    if (xrCreateSwapchain(session, &swapchainCreateInfo, &secondEye) != XR_SUCCESS ||
        secondEye == swapchain ||
        xrAcquireSwapchainImage(secondEye, nullptr, &secondEyeIndex) != XR_SUCCESS ||
        xrWaitSwapchainImage(secondEye, &waitSwapchainInfo) != XR_SUCCESS ||
        xrReleaseSwapchainImage(secondEye, nullptr) != XR_SUCCESS ||
        xrDestroySwapchain(secondEye) != XR_SUCCESS ||
        xrAcquireSwapchainImage(secondEye, &acquireInfo, &secondEyeIndex) != XR_ERROR_HANDLE_INVALID) {
        return EXIT_FAILURE;
    }
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

    using CreateHand = XrResult(XRAPI_PTR*)(XrSession, const XrHandTrackerCreateInfoEXT*, XrHandTrackerEXT*);
    using LocateHand = XrResult(XRAPI_PTR*)(XrHandTrackerEXT, const XrHandJointsLocateInfoEXT*, XrHandJointLocationsEXT*);
    using DestroyHand = XrResult(XRAPI_PTR*)(XrHandTrackerEXT);
    auto createHand = get<CreateHand>(runtimeRequest.getInstanceProcAddr, instance, "xrCreateHandTrackerEXT");
    auto locateHand = get<LocateHand>(runtimeRequest.getInstanceProcAddr, instance, "xrLocateHandJointsEXT");
    auto destroyHand = get<DestroyHand>(runtimeRequest.getInstanceProcAddr, instance, "xrDestroyHandTrackerEXT");
    if (!createHand || !locateHand || !destroyHand) return EXIT_FAILURE;
    XrHandTrackerCreateInfoEXT handInfo{XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT};
    handInfo.hand = XR_HAND_LEFT_EXT; handInfo.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT;
    XrHandTrackerEXT hand = nullptr;
    if (createHand(session, &handInfo, &hand) != XR_SUCCESS || !hand) return EXIT_FAILURE;
    XrHandJointLocationEXT joints[26]{};
    for (auto& joint : joints) joint.locationFlags = 15;
    XrHandTrackingDataSourceStateEXT source{XR_TYPE_HAND_TRACKING_DATA_SOURCE_STATE_EXT};
    source.isActive = 1;
    XrHandJointLocationsEXT locations{XR_TYPE_HAND_JOINT_LOCATIONS_EXT};
    locations.next = &source; locations.jointCount = 26; locations.jointLocations = joints;
    XrHandJointsLocateInfoEXT handLocate{XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT};
    handLocate.baseSpace = space; handLocate.time = 1;
    // No host hand data in the native smoke test: clear activity and all flags.
    if (locateHand(hand, &handLocate, &locations) != XR_SUCCESS || locations.isActive || source.isActive) return EXIT_FAILURE;
    for (const auto& joint : joints) if (joint.locationFlags) return EXIT_FAILURE;
#if defined(AXRB_INPUT_FIXTURE)
    auto& inputFrame = axrb_input_fixture();
    inputFrame.hands[0].active = 1; inputFrame.hands[0].source = 2;
    inputFrame.hands[0].joints[0] = {15, {1, 2, 3}, 0.02f};
    auto offsetInfo = spaceCreateInfo;
    offsetInfo.poseInReferenceSpace.position = {0.25f, 0.5f, 0.75f};
    XrSpace offsetSpace{};
    if (xrCreateReferenceSpace(session, &offsetInfo, &offsetSpace) != XR_SUCCESS) return EXIT_FAILURE;
    handLocate.baseSpace = offsetSpace;
    if (locateHand(hand, &handLocate, &locations) != XR_SUCCESS || !locations.isActive ||
        !source.isActive || source.dataSource != XR_HAND_TRACKING_DATA_SOURCE_CONTROLLER_EXT ||
        joints[0].locationFlags != 15 || joints[0].radius != 0.02f ||
        joints[0].pose.position.x != 0.75f || joints[0].pose.position.y != 1.5f || joints[0].pose.position.z != 2.25f) return EXIT_FAILURE;
    XrHandTrackingDataSourceEXT optical = XR_HAND_TRACKING_DATA_SOURCE_UNOBSTRUCTED_EXT;
    XrHandTrackingDataSourceInfoEXT sourceInfo{XR_TYPE_HAND_TRACKING_DATA_SOURCE_INFO_EXT};
    sourceInfo.requestedDataSourceCount = 1; sourceInfo.requestedDataSources = &optical;
    handInfo.next = &sourceInfo;
    XrHandTrackerEXT opticalHand{};
    if (createHand(session, &handInfo, &opticalHand) != XR_SUCCESS ||
        locateHand(opticalHand, &handLocate, &locations) != XR_SUCCESS || locations.isActive || joints[0].locationFlags) return EXIT_FAILURE;
    inputFrame.hands[0].source = 1;
    if (locateHand(opticalHand, &handLocate, &locations) != XR_SUCCESS || !locations.isActive ||
        source.dataSource != XR_HAND_TRACKING_DATA_SOURCE_UNOBSTRUCTED_EXT || destroyHand(opticalHand) != XR_SUCCESS) return EXIT_FAILURE;
    inputFrame.hands[0] = {};
    if (locateHand(hand, &handLocate, &locations) != XR_SUCCESS || locations.isActive || joints[0].locationFlags) return EXIT_FAILURE;
#endif
    locations.jointCount = 25;
    if (locateHand(hand, &handLocate, &locations) != XR_ERROR_VALIDATION_FAILURE) return EXIT_FAILURE;
    locations.jointCount = 26;
    if (destroyHand(hand) != XR_SUCCESS || destroyHand(hand) != XR_ERROR_HANDLE_INVALID ||
        locateHand(hand, &handLocate, &locations) != XR_ERROR_HANDLE_INVALID) return EXIT_FAILURE;

    XrFrameWaitInfo waitInfo{};
    // Full games create hundreds of paths/actions and retain early handles.
    using StringToPath = XrResult(XRAPI_PTR*)(XrInstance, const char*, XrPath*);
    using PathToString = XrResult(XRAPI_PTR*)(XrInstance, XrPath, uint32_t, uint32_t*, char*);
    using CreateActionSet = XrResult(XRAPI_PTR*)(XrInstance, const XrActionSetCreateInfo*, XrActionSet*);
    using CreateAction = XrResult(XRAPI_PTR*)(XrActionSet, const XrActionCreateInfo*, XrAction*);
    using DestroyAction = XrResult(XRAPI_PTR*)(XrAction);
    auto stringToPath = get<StringToPath>(runtimeRequest.getInstanceProcAddr, instance, "xrStringToPath");
    auto pathToString = get<PathToString>(runtimeRequest.getInstanceProcAddr, instance, "xrPathToString");
    auto createActionSet = get<CreateActionSet>(runtimeRequest.getInstanceProcAddr, instance, "xrCreateActionSet");
    auto createAction = get<CreateAction>(runtimeRequest.getInstanceProcAddr, instance, "xrCreateAction");
    auto destroyAction = get<DestroyAction>(runtimeRequest.getInstanceProcAddr, instance, "xrDestroyAction");
    if (!stringToPath || !pathToString || !createActionSet || !createAction || !destroyAction) { return EXIT_FAILURE; }
    XrActionSetCreateInfo setInfo{};
    setInfo.type = XR_TYPE_ACTION_SET_CREATE_INFO;
    std::strcpy(setInfo.actionSetName, "game");
    std::strcpy(setInfo.localizedActionSetName, "Game");
    XrActionSet actionSet{};
    if (createActionSet(instance, &setInfo, &actionSet) != XR_SUCCESS) { return EXIT_FAILURE; }
#if defined(AXRB_INPUT_FIXTURE)
    using Suggest = XrResult(XRAPI_PTR*)(XrInstance, const XrInteractionProfileSuggestedBinding*);
    using CreateActionSpace = XrResult(XRAPI_PTR*)(XrSession, const XrActionSpaceCreateInfo*, XrSpace*);
    using LocateSpace = XrResult(XRAPI_PTR*)(XrSpace, XrSpace, XrTime, XrSpaceLocation*);
    auto suggest = get<Suggest>(runtimeRequest.getInstanceProcAddr, instance, "xrSuggestInteractionProfileBindings");
    auto createActionSpace = get<CreateActionSpace>(runtimeRequest.getInstanceProcAddr, instance, "xrCreateActionSpace");
    auto locateSpace = get<LocateSpace>(runtimeRequest.getInstanceProcAddr, instance, "xrLocateSpace");
    if (!suggest || !createActionSpace || !locateSpace) return EXIT_FAILURE;
#if defined(AXRB_INPUT_FIXTURE)
    {
        auto& pose = axrb_input_fixture();
        const auto saved = pose;
        pose.hmd = {0.4f, 1.72f, -0.2f, 0, 0, 0, 1};
        pose.local_origin = {0.4f, 1.6f, -0.2f, 0, 0, 0, 1};
        XrReferenceSpaceCreateInfo originInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        originInfo.poseInReferenceSpace.orientation.w = 1;
        XrSpace head{}, floor{}, offsetLocal{};
        originInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
        if (xrCreateReferenceSpace(session, &originInfo, &head) != XR_SUCCESS) return EXIT_FAILURE;
        originInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
        if (xrCreateReferenceSpace(session, &originInfo, &floor) != XR_SUCCESS) return EXIT_FAILURE;
        originInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        originInfo.poseInReferenceSpace.position.y = 0.25f;
        if (xrCreateReferenceSpace(session, &originInfo, &offsetLocal) != XR_SUCCESS) return EXIT_FAILURE;
        XrSpaceLocation height{XR_TYPE_SPACE_LOCATION};
        if (locateSpace(head, space, 1, &height) != XR_SUCCESS || height.locationFlags != 15 ||
            std::abs(height.pose.position.y - 0.12f) > 0.0001f || std::abs(height.pose.position.x) > 0.0001f) return EXIT_FAILURE;
        if (locateSpace(head, floor, 1, &height) != XR_SUCCESS || std::abs(height.pose.position.y - 1.72f) > 0.0001f) return EXIT_FAILURE;
        if (locateSpace(head, offsetLocal, 1, &height) != XR_SUCCESS || std::abs(height.pose.position.y + 0.13f) > 0.0001f) return EXIT_FAILURE;
        // Engines emulate floor tracking by offsetting eye-level LOCAL down by
        // the measured height. It must agree with STAGE, not add height twice.
        originInfo.poseInReferenceSpace.position.y = -1.6f;
        XrSpace emulatedFloor{};
        if (xrCreateReferenceSpace(session, &originInfo, &emulatedFloor) != XR_SUCCESS ||
            locateSpace(head, emulatedFloor, 1, &height) != XR_SUCCESS ||
            std::abs(height.pose.position.y - 1.72f) > 0.0001f) return EXIT_FAILURE;
        // A vertical origin adjustment must preserve the headset's roll and
        // pitch, including when it was resting at an angle during startup.
        pose.hmd.qx = 0.3f;
        pose.hmd.qz = 0.4f;
        pose.hmd.qw = std::sqrt(0.75f);
        if (locateSpace(head, emulatedFloor, 1, &height) != XR_SUCCESS ||
            std::abs(height.pose.orientation.x - pose.hmd.qx) > 0.0001f ||
            std::abs(height.pose.orientation.z - pose.hmd.qz) > 0.0001f ||
            std::abs(height.pose.orientation.w - pose.hmd.qw) > 0.0001f) return EXIT_FAILURE;
        pose.local_origin_flags = 0;
        if (locateSpace(head, space, 1, &height) != XR_SUCCESS || height.locationFlags) return EXIT_FAILURE;
        pose = saved;
    }
#endif
    XrPath profile{}, leftPath{}, gripPath{}, aimPath{};
    if (stringToPath(instance, "/interaction_profiles/oculus/touch_controller", &profile) != XR_SUCCESS ||
        stringToPath(instance, "/user/hand/left", &leftPath) != XR_SUCCESS ||
        stringToPath(instance, "/user/hand/left/input/grip/pose", &gripPath) != XR_SUCCESS ||
        stringToPath(instance, "/user/hand/left/input/aim/pose", &aimPath) != XR_SUCCESS) return EXIT_FAILURE;
    inputFrame.left_controller = {1, 2, 3}; inputFrame.aim[0] = {4, 5, 6};
    inputFrame.controllers[0].active = inputFrame.aim_active[0] = 1;
    inputFrame.grip_flags[0] = 15; inputFrame.aim_flags[0] = 3;
    for (bool aim : {false, true}) {
        XrActionCreateInfo poseActionInfo{XR_TYPE_ACTION_CREATE_INFO};
        poseActionInfo.actionType = XR_ACTION_TYPE_POSE_INPUT;
        std::strcpy(poseActionInfo.actionName, aim ? "test_aim" : "test_grip");
        std::strcpy(poseActionInfo.localizedActionName, poseActionInfo.actionName);
        poseActionInfo.countSubactionPaths = 1; poseActionInfo.subactionPaths = &leftPath;
        XrAction poseAction{};
        if (createAction(actionSet, &poseActionInfo, &poseAction) != XR_SUCCESS) return EXIT_FAILURE;
        XrActionSuggestedBinding binding{poseAction, aim ? aimPath : gripPath};
        XrInteractionProfileSuggestedBinding suggestion{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        suggestion.interactionProfile = profile; suggestion.countSuggestedBindings = 1; suggestion.suggestedBindings = &binding;
        XrActionSpaceCreateInfo actionSpaceInfo{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        actionSpaceInfo.action = poseAction; actionSpaceInfo.subactionPath = leftPath;
        actionSpaceInfo.poseInActionSpace.orientation.w = 1;
        actionSpaceInfo.poseInActionSpace.position.x = 0.25f;
        XrSpace actionSpace{};
        if (suggest(instance, &suggestion) != XR_SUCCESS || createActionSpace(session, &actionSpaceInfo, &actionSpace) != XR_SUCCESS) return EXIT_FAILURE;
        XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
        if (locateSpace(actionSpace, space, 1, &location) != XR_SUCCESS ||
            location.pose.position.x != (aim ? 4.25f : 1.25f) || location.locationFlags != (aim ? 3 : 15)) return EXIT_FAILURE;
        (aim ? inputFrame.aim_active[0] : inputFrame.controllers[0].active) = 0;
        if (locateSpace(actionSpace, space, 1, &location) != XR_SUCCESS || location.locationFlags) return EXIT_FAILURE;
    }
#endif
    std::vector<XrAction> actions;
    for (int i = 0; i < 256; ++i) {
        const std::string pathText = "/test/" + std::string(140, 'a') + std::to_string(i);
        XrPath path{}, repeated{};
        char buffer[256]{};
        uint32_t length = 0;
        if (stringToPath(instance, pathText.c_str(), &path) != XR_SUCCESS ||
            stringToPath(instance, pathText.c_str(), &repeated) != XR_SUCCESS || path != repeated ||
            pathToString(instance, path, 0, &length, nullptr) != XR_SUCCESS || length != pathText.size() + 1 ||
            pathToString(instance, path, 1, &length, buffer) != XR_ERROR_SIZE_INSUFFICIENT ||
            pathToString(instance, path, sizeof(buffer), &length, buffer) != XR_SUCCESS || buffer != pathText) { return EXIT_FAILURE; }
        XrActionCreateInfo actionInfo{};
        actionInfo.type = XR_TYPE_ACTION_CREATE_INFO;
        actionInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
        const std::string name = "action" + std::to_string(i);
        std::strcpy(actionInfo.actionName, name.c_str());
        std::strcpy(actionInfo.localizedActionName, name.c_str());
        XrAction action{};
        if (createAction(actionSet, &actionInfo, &action) != XR_SUCCESS) { return EXIT_FAILURE; }
        actions.push_back(action);
        XrSpace extraSpace{};
        if (xrCreateReferenceSpace(session, &spaceCreateInfo, &extraSpace) != XR_SUCCESS) { return EXIT_FAILURE; }
    }
    for (auto action : actions) {
        if (destroyAction(action) != XR_SUCCESS || destroyAction(action) != XR_ERROR_HANDLE_INVALID) { return EXIT_FAILURE; }
    }
    using GetBounds = XrResult(XRAPI_PTR*)(XrSession, XrReferenceSpaceType, XrExtent2Df*);
    auto getBounds = get<GetBounds>(runtimeRequest.getInstanceProcAddr, instance, "xrGetReferenceSpaceBoundsRect");
    XrExtent2Df bounds{1, 1};
    if (!getBounds || getBounds(session, XR_REFERENCE_SPACE_TYPE_STAGE, &bounds) != XR_SPACE_BOUNDS_UNAVAILABLE ||
        bounds.width != 0 || bounds.height != 0) { return EXIT_FAILURE; }
    waitInfo.type = XR_TYPE_FRAME_WAIT_INFO;
    XrFrameState frameState{};
    frameState.type = XR_TYPE_FRAME_STATE;
    if (xrWaitFrame(session, nullptr, &frameState) != XR_SUCCESS || frameState.shouldRender == 0) {
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
        locatedViewCount != 2) {
        return EXIT_FAILURE;
    }
#if defined(AXRB_INPUT_FIXTURE)
    if ((viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) == 0) return EXIT_FAILURE;
#else
    if (viewState.viewStateFlags != 0) return EXIT_FAILURE; // No real host pose yet.
#endif

    XrFrameBeginInfo beginInfo{};
    beginInfo.type = XR_TYPE_FRAME_BEGIN_INFO;
    if (xrBeginFrame(session, nullptr) != XR_SUCCESS) {
        return EXIT_FAILURE;
    }

    XrFrameEndInfo endInfo{};
    endInfo.type = XR_TYPE_FRAME_END_INFO;
    endInfo.displayTime = frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    if (xrEndFrame(session, &endInfo) != XR_SUCCESS) {
        return EXIT_FAILURE;
    }

    // Submit actual per-eye subimages, including a shared array swapchain.
    XrCompositionLayerProjectionView projectionViews[2]{};
    for (uint32_t eye = 0; eye < 2; ++eye) {
        auto& view = projectionViews[eye];
        view.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
        view.pose = views[eye].pose;
        view.fov = views[eye].fov;
        view.subImage.swapchain = swapchain;
        view.subImage.imageRect = {{0, 0}, {1024, 1024}};
        view.subImage.imageArrayIndex = eye;
    }
    XrCompositionLayerProjection projection{};
    projection.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
    projection.space = space;
    projection.viewCount = 2;
    projection.views = projectionViews;
    const void* layers[] = {&projection};
    endInfo.layerCount = 1;
    endInfo.layers = layers;
    if (xrEndFrame(session, &endInfo) != XR_SUCCESS) { return EXIT_FAILURE; }
    // Forward core composition flags to the host compositor, including alpha
    // blending. Reject unknown bits rather than silently dropping semantics.
    projection.layerFlags = 7;
    if (xrEndFrame(session, &endInfo) != XR_SUCCESS) { return EXIT_FAILURE; }
    projection.layerFlags = 8;
    if (xrEndFrame(session, &endInfo) != XR_ERROR_LAYER_INVALID) { return EXIT_FAILURE; }
    projection.layerFlags = 0;
    projectionViews[1].subImage.imageArrayIndex = 2;
    if (xrEndFrame(session, &endInfo) != XR_ERROR_SWAPCHAIN_RECT_INVALID) { return EXIT_FAILURE; }
    projectionViews[1].subImage.imageArrayIndex = 1;
    projectionViews[1].subImage.imageRect.offset.x = 1;
    if (xrEndFrame(session, &endInfo) != XR_ERROR_SWAPCHAIN_RECT_INVALID) { return EXIT_FAILURE; }
    projectionViews[1].subImage.imageRect.offset.x = 0;
    projectionViews[1].fov.angleLeft = projectionViews[1].fov.angleRight;
    if (xrEndFrame(session, &endInfo) != XR_ERROR_LAYER_INVALID) { return EXIT_FAILURE; }
    projectionViews[1].fov = views[1].fov;

    // Separate eye swapchains must both have a released image before submission.
    if (xrCreateSwapchain(session, &swapchainCreateInfo, &secondEye) != XR_SUCCESS) { return EXIT_FAILURE; }
    projectionViews[1].subImage.swapchain = secondEye;
    if (xrEndFrame(session, &endInfo) != XR_ERROR_CALL_ORDER_INVALID ||
        xrAcquireSwapchainImage(secondEye, &acquireInfo, &secondEyeIndex) != XR_SUCCESS ||
        xrWaitSwapchainImage(secondEye, &waitSwapchainInfo) != XR_SUCCESS ||
        xrReleaseSwapchainImage(secondEye, &releaseInfo) != XR_SUCCESS ||
        xrEndFrame(session, &endInfo) != XR_SUCCESS ||
        xrDestroySwapchain(secondEye) != XR_SUCCESS) { return EXIT_FAILURE; }

    // Static swapchains allocate one image and may be acquired only once.
    auto staticInfo = swapchainCreateInfo;
    staticInfo.createFlags = XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT;
    XrSwapchain staticSwapchain{};
    uint32_t staticCount = 0, staticIndex = 99;
    if (xrCreateSwapchain(session, &staticInfo, &staticSwapchain) != XR_SUCCESS ||
        xrEnumerateSwapchainImages(staticSwapchain, 0, &staticCount, nullptr) != XR_SUCCESS || staticCount != 1 ||
        xrAcquireSwapchainImage(staticSwapchain, &acquireInfo, &staticIndex) != XR_SUCCESS || staticIndex != 0 ||
        xrWaitSwapchainImage(staticSwapchain, &waitSwapchainInfo) != XR_SUCCESS ||
        xrReleaseSwapchainImage(staticSwapchain, &releaseInfo) != XR_SUCCESS ||
        xrAcquireSwapchainImage(staticSwapchain, &acquireInfo, &staticIndex) != XR_ERROR_CALL_ORDER_INVALID ||
        xrDestroySwapchain(staticSwapchain) != XR_SUCCESS) return EXIT_FAILURE;

    XrCompositionLayerQuad quads[2]{};
    for (unsigned i = 0; i < 2; ++i) {
        quads[i].type = XR_TYPE_COMPOSITION_LAYER_QUAD;
        quads[i].space = space;
        quads[i].pose.orientation.w = 1;
        quads[i].pose.position.z = -2;
        quads[i].size = {2, 1};
        quads[i].eyeVisibility = i + 1;
        quads[i].layerFlags = 7;
        quads[i].subImage = projectionViews[0].subImage;
    }
    projectionViews[1].subImage = projectionViews[0].subImage;
    const void* mixed[] = {&projection, &quads[0], &quads[1]};
    endInfo.layerCount = 3; endInfo.layers = mixed;
    if (xrEndFrame(session, &endInfo) != XR_SUCCESS) return EXIT_FAILURE;
    quads[1].subImage.imageArrayIndex = 99;
    if (xrEndFrame(session, &endInfo) != XR_ERROR_SWAPCHAIN_RECT_INVALID) return EXIT_FAILURE;
    quads[1].subImage = projectionViews[0].subImage;
    quads[1].size.width = -1;
    if (xrEndFrame(session, &endInfo) != XR_ERROR_LAYER_INVALID) return EXIT_FAILURE;
    quads[1].size.width = 2;
    const void* sceneWithPanels[] = {&projection, &quads[0], &quads[1], &quads[0], &quads[1]};
    endInfo.layerCount = 5; endInfo.layers = sceneWithPanels;
    if (xrEndFrame(session, &endInfo) != XR_SUCCESS) return EXIT_FAILURE;
    std::array<const void*, 16> maximumPanels{};
    maximumPanels.fill(&quads[0]);
    endInfo.layerCount = 16; endInfo.layers = maximumPanels.data();
    if (xrEndFrame(session, &endInfo) != XR_SUCCESS) return EXIT_FAILURE;
    const void* fourPanels[] = {&quads[0], &quads[1], &quads[0], &quads[1]};
    endInfo.layerCount = 4; endInfo.layers = fourPanels;
    if (xrEndFrame(session, &endInfo) != XR_SUCCESS) return EXIT_FAILURE;
    const void* panels[] = {&quads[0], &quads[1]};
    endInfo.layerCount = 2;
    endInfo.layers = panels;
    if (xrEndFrame(session, &endInfo) != XR_SUCCESS) return EXIT_FAILURE;
    quads[1].size.width = -1;
    if (xrEndFrame(session, &endInfo) != XR_ERROR_LAYER_INVALID) return EXIT_FAILURE;
    quads[1].size.width = 2;
    quads[1].eyeVisibility = 3;
    if (xrEndFrame(session, &endInfo) != XR_ERROR_LAYER_INVALID) return EXIT_FAILURE;
    quads[1].eyeVisibility = 2;
    panels[1] = nullptr;
    if (xrEndFrame(session, &endInfo) != XR_ERROR_LAYER_INVALID) return EXIT_FAILURE;
    endInfo.layerCount = 1;
    if (xrEndFrame(session, &endInfo) != XR_SUCCESS) return EXIT_FAILURE;

    if (xrDestroySwapchain(swapchain) != XR_SUCCESS) {
        return EXIT_FAILURE;
    }

    return require_success(xrEndSession(session));
}
