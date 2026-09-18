#include "runtime_internal.h"

namespace axrb::runtime::detail {

template <typename Function>
PFN_xrVoidFunction cast_function(Function function)
{
    return reinterpret_cast<PFN_xrVoidFunction>(function);
}



XrResult XRAPI_CALL xrGetInstanceProcAddr_impl(
    XrInstance instance,
    const char* name,
    PFN_xrVoidFunction* function)
{
    if (name == nullptr || function == nullptr) {
        return XR_ERROR_VALIDATION_FAILURE;
    }

    log_proc_request(name);

    *function = nullptr;
    const std::string_view requested{name};

    if (requested == "xrGetInstanceProcAddr") {
        *function = cast_function(xrGetInstanceProcAddr_impl);
        return XR_SUCCESS;
    }
    if (requested == "xrEnumerateDisplayRefreshRatesFB") {
        *function = cast_function(xrEnumerateDisplayRefreshRatesFB_impl);
        return XR_SUCCESS;
    }
    if (requested == "xrGetDisplayRefreshRateFB") {
        *function = cast_function(xrGetDisplayRefreshRateFB_impl);
        return XR_SUCCESS;
    }
    if (requested == "xrRequestDisplayRefreshRateFB") {
        *function = cast_function(xrRequestDisplayRefreshRateFB_impl);
        return XR_SUCCESS;
    }

    if (is_valid_instance(instance)) {
        if (requested == "xrCreateHandTrackerEXT") { *function = cast_function(xrCreateHandTrackerEXT_impl); return XR_SUCCESS; }
        if (requested == "xrDestroyHandTrackerEXT") { *function = cast_function(xrDestroyHandTrackerEXT_impl); return XR_SUCCESS; }
        if (requested == "xrLocateHandJointsEXT") { *function = cast_function(xrLocateHandJointsEXT_impl); return XR_SUCCESS; }
    }

    if (requested == "xrCreateInstance") {
        *function = cast_function(xrCreateInstance_impl);
        return XR_SUCCESS;
    }

    if (requested == "xrInitializeLoaderKHR") {
        *function = cast_function(xrInitializeLoaderKHR_impl);
        return XR_SUCCESS;
    }

    if (requested == "xrEnumerateInstanceExtensionProperties") {
        *function = cast_function(xrEnumerateInstanceExtensionProperties_impl);
        return XR_SUCCESS;
    }

    if (requested == "xrPollEvent") {
        *function = cast_function(xrPollEvent_impl);
        return XR_SUCCESS;
    }

    if (!is_valid_instance(instance)) {
        return XR_ERROR_HANDLE_INVALID;
    }

    if (requested == "xrDestroyInstance") {
        *function = cast_function(xrDestroyInstance_impl);
#if defined(__ANDROID__) || defined(AXRB_INPUT_FIXTURE)
    } else if (requested == "xrConvertTimespecTimeToTimeKHR") {
        *function = cast_function(xrConvertTimespecTimeToTimeKHR_impl);
    } else if (requested == "xrConvertTimeToTimespecTimeKHR") {
        *function = cast_function(xrConvertTimeToTimespecTimeKHR_impl);
#endif
    } else if (requested == "xrGetInstanceProperties") {
        *function = cast_function(xrGetInstanceProperties_impl);
    } else if (requested == "xrGetSystem") {
        *function = cast_function(xrGetSystem_impl);
    } else if (requested == "xrGetSystemProperties") {
        *function = cast_function(xrGetSystemProperties_impl);
    } else if (requested == "xrGetOpenGLESGraphicsRequirementsKHR") {
        *function = cast_function(xrGetOpenGLESGraphicsRequirementsKHR_impl);
    } else if (requested == "xrGetVulkanGraphicsRequirementsKHR") {
        *function = cast_function(xrGetVulkanGraphicsRequirementsKHR_impl);
#if defined(__ANDROID__)
    } else if (requested == "xrGetVulkanGraphicsRequirements2KHR") {
        *function = cast_function(xrGetVulkanGraphicsRequirementsKHR_impl);
    } else if (requested == "xrGetVulkanInstanceExtensionsKHR") {
        *function = cast_function(xrGetVulkanExtensionsKHR_impl);
    } else if (requested == "xrGetVulkanDeviceExtensionsKHR") {
        *function = cast_function(xrGetVulkanDeviceExtensionsKHR_impl);
    } else if (requested == "xrCreateSwapchainAndroidSurfaceKHR") {
        *function = cast_function(xrCreateSwapchainAndroidSurfaceKHR_impl);
    } else if (requested == "xrGetVulkanGraphicsDeviceKHR") {
        *function = cast_function(xrGetVulkanGraphicsDeviceKHR_impl);
    } else if (requested == "xrGetVulkanGraphicsDevice2KHR") {
        *function = cast_function(xrGetVulkanGraphicsDevice2KHR_impl);
    } else if (requested == "xrCreateVulkanInstanceKHR") {
        *function = cast_function(xrCreateVulkanInstanceKHR_impl);
    } else if (requested == "xrCreateVulkanDeviceKHR") {
        *function = cast_function(xrCreateVulkanDeviceKHR_impl);
#endif
    } else if (requested == "xrCreateSession") {
        *function = cast_function(xrCreateSession_impl);
    } else if (requested == "xrBeginSession") {
        *function = cast_function(xrBeginSession_impl);
    } else if (requested == "xrEndSession") {
        *function = cast_function(xrEndSession_impl);
    } else if (requested == "xrDestroySession") {
        *function = cast_function(xrDestroySession_impl);
    } else if (requested == "xrEnumerateViewConfigurations") {
        *function = cast_function(xrEnumerateViewConfigurations_impl);
    } else if (requested == "xrEnumerateEnvironmentBlendModes") {
        *function = cast_function(xrEnumerateEnvironmentBlendModes_impl);
    } else if (requested == "xrGetViewConfigurationProperties") {
        *function = cast_function(xrGetViewConfigurationProperties_impl);
    } else if (requested == "xrEnumerateViewConfigurationViews") {
        *function = cast_function(xrEnumerateViewConfigurationViews_impl);
    } else if (requested == "xrEnumerateSwapchainFormats") {
        *function = cast_function(xrEnumerateSwapchainFormats_impl);
    } else if (requested == "xrCreateSwapchain") {
        *function = cast_function(xrCreateSwapchain_impl);
    } else if (requested == "xrDestroySwapchain") {
        *function = cast_function(xrDestroySwapchain_impl);
    } else if (requested == "xrEnumerateSwapchainImages") {
        *function = cast_function(xrEnumerateSwapchainImages_impl);
    } else if (requested == "xrAcquireSwapchainImage") {
        *function = cast_function(xrAcquireSwapchainImage_impl);
    } else if (requested == "xrWaitSwapchainImage") {
        *function = cast_function(xrWaitSwapchainImage_impl);
    } else if (requested == "xrReleaseSwapchainImage") {
        *function = cast_function(xrReleaseSwapchainImage_impl);
    } else if (requested == "xrEnumerateReferenceSpaces") {
        *function = cast_function(xrEnumerateReferenceSpaces_impl);
    } else if (requested == "xrGetReferenceSpaceBoundsRect") {
        *function = cast_function(xrGetReferenceSpaceBoundsRect_impl);
    } else if (requested == "xrCreateReferenceSpace") {
        *function = cast_function(xrCreateReferenceSpace_impl);
    } else if (requested == "xrCreateActionSet") {
        *function = cast_function(xrCreateActionSet_impl);
    } else if (requested == "xrDestroyActionSet") {
        *function = cast_function(xrDestroyActionSet_impl);
    } else if (requested == "xrCreateAction") {
        *function = cast_function(xrCreateAction_impl);
    } else if (requested == "xrDestroyAction") {
        *function = cast_function(xrDestroyAction_impl);
    } else if (requested == "xrStringToPath") {
        *function = cast_function(xrStringToPath_impl);
    } else if (requested == "xrPathToString") {
        *function = cast_function(xrPathToString_impl);
    } else if (requested == "xrSuggestInteractionProfileBindings") {
        *function = cast_function(xrSuggestInteractionProfileBindings_impl);
    } else if (requested == "xrCreateActionSpace") {
        *function = cast_function(xrCreateActionSpace_impl);
    } else if (requested == "xrLocateSpace") {
        *function = cast_function(xrLocateSpace_impl);
    } else if (requested == "xrAttachSessionActionSets") {
        *function = cast_function(xrAttachSessionActionSets_impl);
    } else if (requested == "xrSyncActions") {
        *function = cast_function(xrSyncActions_impl);
    } else if (requested == "xrGetActionStateBoolean") {
        *function = cast_function(xrGetActionStateBoolean_impl);
    } else if (requested == "xrGetActionStateFloat") {
        *function = cast_function(xrGetActionStateFloat_impl);
    } else if (requested == "xrGetActionStateVector2f") {
        *function = cast_function(xrGetActionStateVector2f_impl);
    } else if (requested == "xrGetActionStatePose") {
        *function = cast_function(xrGetActionStatePose_impl);
    } else if (requested == "xrGetCurrentInteractionProfile") {
        *function = cast_function(xrGetCurrentInteractionProfile_impl);
    } else if (requested == "xrEnumerateBoundSourcesForAction") {
        *function = cast_function(xrEnumerateBoundSourcesForAction_impl);
    } else if (requested == "xrGetInputSourceLocalizedName") {
        *function = cast_function(xrGetInputSourceLocalizedName_impl);
    } else if (requested == "xrApplyHapticFeedback") {
        *function = cast_function(xrApplyHapticFeedback_impl);
    } else if (requested == "xrStopHapticFeedback") {
        *function = cast_function(xrStopHapticFeedback_impl);
    } else if (requested == "xrLocateViews") {
        *function = cast_function(xrLocateViews_impl);
    } else if (requested == "xrDestroySpace") {
        *function = cast_function(xrDestroySpace_impl);
    } else if (requested == "xrWaitFrame") {
        *function = cast_function(xrWaitFrame_impl);
    } else if (requested == "xrBeginFrame") {
        *function = cast_function(xrBeginFrame_impl);
    } else if (requested == "xrEndFrame") {
        *function = cast_function(xrEndFrame_impl);
    } else {
        return XR_ERROR_FUNCTION_UNSUPPORTED;
    }

    return XR_SUCCESS;
}


} // namespace axrb::runtime::detail

namespace axrb::runtime {
using namespace detail;
int session_probe()
{
    return 1;
}

XrResult negotiate_loader_runtime_interface(
    const XrNegotiateLoaderInfo* loaderInfo,
    XrNegotiateRuntimeRequest* runtimeRequest)
{
    log_call("xrNegotiateLoaderRuntimeInterface");
    if (loaderInfo == nullptr || runtimeRequest == nullptr) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    if (loaderInfo->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO ||
        loaderInfo->structVersion != XR_LOADER_INFO_STRUCT_VERSION ||
        loaderInfo->structSize != sizeof(XrNegotiateLoaderInfo)) {
        return XR_ERROR_INITIALIZATION_FAILED;
    }
    if (loaderInfo->minInterfaceVersion > XR_CURRENT_LOADER_RUNTIME_VERSION ||
        loaderInfo->maxInterfaceVersion < XR_CURRENT_LOADER_RUNTIME_VERSION) {
        return XR_ERROR_INITIALIZATION_FAILED;
    }
    if (loaderInfo->minApiVersion > XR_MAKE_VERSION(1, 0, 0) ||
        loaderInfo->maxApiVersion < XR_MAKE_VERSION(1, 0, 0)) {
        return XR_ERROR_API_VERSION_UNSUPPORTED;
    }

    std::memset(runtimeRequest, 0, sizeof(*runtimeRequest));
    runtimeRequest->structType = XR_LOADER_INTERFACE_STRUCT_RUNTIME_REQUEST;
    runtimeRequest->structVersion = XR_RUNTIME_INFO_STRUCT_VERSION;
    runtimeRequest->structSize = sizeof(XrNegotiateRuntimeRequest);
    runtimeRequest->runtimeInterfaceVersion = XR_CURRENT_LOADER_RUNTIME_VERSION;
    runtimeRequest->runtimeApiVersion = XR_MAKE_VERSION(1, 0, 0);
    runtimeRequest->getInstanceProcAddr = xrGetInstanceProcAddr_impl;
    return XR_SUCCESS;
}

} // namespace axrb::runtime
