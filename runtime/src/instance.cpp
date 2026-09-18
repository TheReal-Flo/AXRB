#include "runtime_internal.h"

namespace axrb::runtime::detail {

XrResult XRAPI_CALL xrCreateInstance_impl(const XrInstanceCreateInfo* createInfo, XrInstance* instance)
{
    log_call("xrCreateInstance");
    if (createInfo == nullptr || instance == nullptr) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    if (createInfo->type != XR_TYPE_INSTANCE_CREATE_INFO) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    if (createInfo->applicationInfo.apiVersion > XR_MAKE_VERSION(1, 1, 0)) {
        return XR_ERROR_API_VERSION_UNSUPPORTED;
    }

    *instance = fake_instance();
    g_sessionState = XR_SESSION_STATE_UNKNOWN;
    g_pendingSessionEvents.clear();
    g_pendingInteractionProfileEvent = false;
    g_spaceCount = 0;
    g_pathCount = 0;
    g_spaces.clear();
    g_paths.clear();
    g_actionHandles.clear();
    g_actionCount = 0;
    g_nextPath = 1;
    for (auto& sc : g_swapchains) {
        destroy_swapchain_images(sc);
        sc = {};
    }
    g_lastReleasedSwapchain = nullptr;
    g_imageFrameSequence = 0;
    g_nextFrameStart = 0;
    g_renderWidth = g_renderHeight = 1024;
    g_renderExtentQueried = false;
#if defined(__ANDROID__)
    g_vulkan.shutdown();
    g_vulkanRequirementsQueried = false;
    g_vulkanInstance = VK_NULL_HANDLE;
#endif
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrInitializeLoaderKHR_impl(const void* loaderInitInfo)
{
    log_call("xrInitializeLoaderKHR");
    if (loaderInitInfo == nullptr) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
#if defined(__ANDROID__)
    const auto* initInfo = static_cast<const AndroidLoaderInitInfo*>(loaderInitInfo);
    if (initInfo->applicationVM != nullptr && initInfo->applicationContext != nullptr) {
        pose_client().set_android_context(
            static_cast<JavaVM*>(initInfo->applicationVM),
            static_cast<jobject>(initInfo->applicationContext));
    }
#endif
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrDestroyInstance_impl(XrInstance instance)
{
    log_call("xrDestroyInstance");
    if (!is_valid_instance(instance)) return XR_ERROR_HANDLE_INVALID;
    for (auto& sc : g_swapchains) { destroy_swapchain_images(sc); sc = {}; }
    g_lastReleasedSwapchain = nullptr;
    for (auto& tracker : g_handTrackers) tracker.alive = false;
#if defined(__ANDROID__)
    g_vulkan.shutdown();
#endif
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrEnumerateInstanceExtensionProperties_impl(
    const char* layerName,
    uint32_t propertyCapacityInput,
    uint32_t* propertyCountOutput,
    XrExtensionProperties* properties)
{
    log_call("xrEnumerateInstanceExtensionProperties");
    if (layerName != nullptr && layerName[0] != '\0') {
        return XR_ERROR_API_LAYER_NOT_PRESENT;
    }
    if (propertyCountOutput == nullptr) {
        return XR_ERROR_VALIDATION_FAILURE;
    }

    constexpr const char* kExtensions[] = {
        "XR_KHR_android_create_instance",
        "XR_KHR_opengl_es_enable",
        "XR_KHR_vulkan_enable",
        "XR_FB_display_refresh_rate",
        "XR_EXT_hand_tracking",
        "XR_EXT_hand_tracking_data_source",
#if defined(__ANDROID__) || defined(AXRB_INPUT_FIXTURE)
        "XR_KHR_convert_timespec_time",
#endif
#if defined(__ANDROID__)
        "XR_KHR_vulkan_enable2",
        "XR_KHR_android_surface_swapchain",
        "XR_KHR_composition_layer_equirect2",
        "XR_FB_composition_layer_image_layout",
#endif
    };

    *propertyCountOutput = static_cast<uint32_t>(sizeof(kExtensions) / sizeof(kExtensions[0]));
    if (propertyCapacityInput == 0 || properties == nullptr) {
        return XR_SUCCESS;
    }

    const uint32_t count = propertyCapacityInput < *propertyCountOutput
        ? propertyCapacityInput
        : *propertyCountOutput;
    for (uint32_t i = 0; i < count; ++i) {
        if (properties[i].type != XR_TYPE_EXTENSION_PROPERTIES) {
            return XR_ERROR_VALIDATION_FAILURE;
        }
        std::strncpy(properties[i].extensionName, kExtensions[i], XR_MAX_EXTENSION_NAME_SIZE - 1);
        properties[i].extensionName[XR_MAX_EXTENSION_NAME_SIZE - 1] = '\0';
        properties[i].extensionVersion = 1;
    }
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrGetInstanceProperties_impl(
    XrInstance instance,
    XrInstanceProperties* instanceProperties)
{
    log_call("xrGetInstanceProperties");
    if (!is_valid_instance(instance)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (instanceProperties == nullptr || instanceProperties->type != XR_TYPE_INSTANCE_PROPERTIES) {
        return XR_ERROR_VALIDATION_FAILURE;
    }

    instanceProperties->runtimeVersion = XR_MAKE_VERSION(0, 1, 0);
    std::strncpy(instanceProperties->runtimeName, "Android XR Bridge Runtime", XR_MAX_RUNTIME_NAME_SIZE - 1);
    instanceProperties->runtimeName[XR_MAX_RUNTIME_NAME_SIZE - 1] = '\0';
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrGetSystem_impl(
    XrInstance instance,
    const XrSystemGetInfo* getInfo,
    XrSystemId* systemId)
{
    log_call("xrGetSystem");
    if (!is_valid_instance(instance)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (getInfo == nullptr || systemId == nullptr || getInfo->type != XR_TYPE_SYSTEM_GET_INFO) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    if (getInfo->formFactor != XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY) {
        return XR_ERROR_FORM_FACTOR_UNSUPPORTED;
    }

    *systemId = kSystemId;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrGetSystemProperties_impl(
    XrInstance instance,
    XrSystemId systemId,
    XrSystemProperties* properties)
{
    log_call("xrGetSystemProperties");
    if (!is_valid_instance(instance)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (systemId != kSystemId) {
        return XR_ERROR_SYSTEM_INVALID;
    }
    if (properties == nullptr || properties->type != XR_TYPE_SYSTEM_PROPERTIES) {
        return XR_ERROR_VALIDATION_FAILURE;
    }

    properties->systemId = kSystemId;
    properties->vendorId = 0;
    std::strncpy(properties->systemName, "AXRB Fake HMD", XR_MAX_SYSTEM_NAME_SIZE - 1);
    properties->systemName[XR_MAX_SYSTEM_NAME_SIZE - 1] = '\0';
    properties->graphicsProperties.maxSwapchainImageHeight = 16384;
    properties->graphicsProperties.maxSwapchainImageWidth = 16384;
    properties->graphicsProperties.maxLayerCount = axrb::protocol::kMaxWireCompositionLayers;
    properties->trackingProperties.orientationTracking = 1;
    properties->trackingProperties.positionTracking = 1;
    struct OutputHeader { XrStructureType type; void* next; };
    for (auto* next = static_cast<OutputHeader*>(properties->next); next; next = static_cast<OutputHeader*>(next->next)) {
        if (next->type == XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT)
            // Capability is independent of whether the first asynchronous host
            // packet has arrived or controllers are currently awake.
            reinterpret_cast<XrSystemHandTrackingPropertiesEXT*>(next)->supportsHandTracking = 1;
    }
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrGetOpenGLESGraphicsRequirementsKHR_impl(
    XrInstance instance,
    XrSystemId systemId,
    XrGraphicsRequirementsOpenGLESKHR* graphicsRequirements)
{
    log_call("xrGetOpenGLESGraphicsRequirementsKHR");
    if (!is_valid_instance(instance)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (systemId != kSystemId) {
        return XR_ERROR_SYSTEM_INVALID;
    }
    if (graphicsRequirements == nullptr ||
        graphicsRequirements->type != XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR) {
        return XR_ERROR_VALIDATION_FAILURE;
    }

    graphicsRequirements->minApiVersionSupported = XR_MAKE_VERSION(3, 0, 0);
    graphicsRequirements->maxApiVersionSupported = XR_MAKE_VERSION(3, 2, 0);
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrGetVulkanGraphicsRequirementsKHR_impl(
    XrInstance instance,
    XrSystemId systemId,
    XrGraphicsRequirementsVulkanKHR* graphicsRequirements)
{
    log_call("xrGetVulkanGraphicsRequirementsKHR");
    if (!is_valid_instance(instance)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (systemId != kSystemId) {
        return XR_ERROR_SYSTEM_INVALID;
    }
    if (graphicsRequirements == nullptr ||
        graphicsRequirements->type != XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR) {
        return XR_ERROR_VALIDATION_FAILURE;
    }

    graphicsRequirements->minApiVersionSupported = XR_MAKE_VERSION(1, 0, 0);
    graphicsRequirements->maxApiVersionSupported = XR_MAKE_VERSION(1, 1, 0);
#if defined(__ANDROID__)
    g_vulkanRequirementsQueried = true;
#endif
    return XR_SUCCESS;
}

#if defined(__ANDROID__)
XrResult XRAPI_CALL xrGetVulkanExtensionsKHR_impl(XrInstance instance, XrSystemId systemId,
                                                uint32_t capacity, uint32_t* count, char* buffer) {
    if (!is_valid_instance(instance)) return XR_ERROR_HANDLE_INVALID;
    if (systemId != kSystemId) return XR_ERROR_SYSTEM_INVALID;
    if (!count || (capacity && !buffer)) return XR_ERROR_VALIDATION_FAILURE;
    *count = 1; // No runtime-specific Vulkan extensions are needed for CPU readback.
    if (capacity) buffer[0] = '\0';
    return XR_SUCCESS;
}
XrResult XRAPI_CALL xrGetVulkanDeviceExtensionsKHR_impl(XrInstance instance, XrSystemId systemId,
        uint32_t capacity, uint32_t* count, char* buffer) {
    if (!is_valid_instance(instance)) return XR_ERROR_HANDLE_INVALID;
    if (systemId != kSystemId) return XR_ERROR_SYSTEM_INVALID;
    if (!count || (capacity && !buffer)) return XR_ERROR_VALIDATION_FAILURE;
    constexpr char extensions[] = "VK_ANDROID_external_memory_android_hardware_buffer VK_EXT_queue_family_foreign";
    *count = sizeof(extensions);
    if (!capacity) return XR_SUCCESS;
    if (capacity < sizeof(extensions)) return XR_ERROR_SIZE_INSUFFICIENT;
    std::memcpy(buffer, extensions, sizeof(extensions)); return XR_SUCCESS;
}
XrResult XRAPI_CALL xrGetVulkanGraphicsDeviceKHR_impl(XrInstance instance, XrSystemId systemId,
                                                    VkInstance vkInstance, VkPhysicalDevice* device) {
    if (!is_valid_instance(instance)) return XR_ERROR_HANDLE_INVALID;
    if (systemId != kSystemId) return XR_ERROR_SYSTEM_INVALID;
    if (!vkInstance || !device) return XR_ERROR_VALIDATION_FAILURE;
    *device = VulkanBackend::choose_device(vkInstance);
    g_vulkanInstance = vkInstance;
    return *device ? XR_SUCCESS : XR_ERROR_GRAPHICS_DEVICE_INVALID;
}
XrResult XRAPI_CALL xrGetVulkanGraphicsDevice2KHR_impl(XrInstance instance,
        const XrVulkanGraphicsDeviceGetInfoKHR* info, VkPhysicalDevice* device) {
    if (!info || info->type != XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR) return XR_ERROR_VALIDATION_FAILURE;
    return xrGetVulkanGraphicsDeviceKHR_impl(instance, info->systemId, info->vulkanInstance, device);
}
XrResult XRAPI_CALL xrCreateVulkanInstanceKHR_impl(XrInstance instance, const XrVulkanInstanceCreateInfoKHR* info,
                                                VkInstance* vkInstance, VkResult* result) {
    if (!is_valid_instance(instance)) return XR_ERROR_HANDLE_INVALID;
    if (!info || info->type != XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR || !vkInstance || !result ||
        !info->vulkanCreateInfo || !info->pfnGetInstanceProcAddr || info->createFlags) return XR_ERROR_VALIDATION_FAILURE;
    if (info->systemId != kSystemId) return XR_ERROR_SYSTEM_INVALID;
    auto create = reinterpret_cast<PFN_vkCreateInstance>(info->pfnGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance"));
    if (!create) return XR_ERROR_RUNTIME_FAILURE;
    *result = create(info->vulkanCreateInfo, info->vulkanAllocator, vkInstance);
    if (*result == VK_SUCCESS) g_vulkanInstance = *vkInstance;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL xrCreateVulkanDeviceKHR_impl(XrInstance instance, const XrVulkanDeviceCreateInfoKHR* info,
                                              VkDevice* device, VkResult* result) {
    if (!is_valid_instance(instance)) return XR_ERROR_HANDLE_INVALID;
    if (!info || info->type != XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR || !device || !result ||
        !info->vulkanCreateInfo || !info->pfnGetInstanceProcAddr || info->createFlags) return XR_ERROR_VALIDATION_FAILURE;
    if (info->systemId != kSystemId) return XR_ERROR_SYSTEM_INVALID;
    if (!g_vulkanInstance || info->vulkanPhysicalDevice != VulkanBackend::choose_device(g_vulkanInstance)) return XR_ERROR_GRAPHICS_DEVICE_INVALID;
    auto create = reinterpret_cast<PFN_vkCreateDevice>(info->pfnGetInstanceProcAddr(g_vulkanInstance, "vkCreateDevice"));
    if (!create) return XR_ERROR_RUNTIME_FAILURE;
    auto deviceInfo = *info->vulkanCreateInfo;
    std::vector<const char*> extensions;
    for (uint32_t i=0; i<deviceInfo.enabledExtensionCount; ++i) extensions.push_back(deviceInfo.ppEnabledExtensionNames[i]);
    for (const char* name : {"VK_ANDROID_external_memory_android_hardware_buffer", "VK_EXT_queue_family_foreign"})
        if (std::none_of(extensions.begin(), extensions.end(), [name](const char* value){ return std::strcmp(value,name)==0; })) extensions.push_back(name);
    deviceInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size()); deviceInfo.ppEnabledExtensionNames = extensions.data();
    *result = create(info->vulkanPhysicalDevice, &deviceInfo, info->vulkanAllocator, device);
    return XR_SUCCESS;
}
#endif

XrResult XRAPI_CALL xrEnumerateViewConfigurations_impl(
    XrInstance instance,
    XrSystemId systemId,
    uint32_t viewConfigurationTypeCapacityInput,
    uint32_t* viewConfigurationTypeCountOutput,
    XrViewConfigurationType* viewConfigurationTypes)
{
    log_call("xrEnumerateViewConfigurations");
    if (!is_valid_instance(instance)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (systemId != kSystemId) {
        return XR_ERROR_SYSTEM_INVALID;
    }
    if (viewConfigurationTypeCountOutput == nullptr) {
        return XR_ERROR_VALIDATION_FAILURE;
    }

    *viewConfigurationTypeCountOutput = 1;
    if (viewConfigurationTypeCapacityInput > 0 && viewConfigurationTypes != nullptr) {
        viewConfigurationTypes[0] = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    }
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrEnumerateEnvironmentBlendModes_impl(
    XrInstance instance,
    XrSystemId systemId,
    XrViewConfigurationType viewConfigurationType,
    uint32_t environmentBlendModeCapacityInput,
    uint32_t* environmentBlendModeCountOutput,
    XrEnvironmentBlendMode* environmentBlendModes)
{
    log_call("xrEnumerateEnvironmentBlendModes");
    if (!is_valid_instance(instance)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (systemId != kSystemId) {
        return XR_ERROR_SYSTEM_INVALID;
    }
    if (viewConfigurationType != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) {
        return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
    }
    if (environmentBlendModeCountOutput == nullptr) {
        return XR_ERROR_VALIDATION_FAILURE;
    }

    *environmentBlendModeCountOutput = 1;
    if (environmentBlendModeCapacityInput > 0 && environmentBlendModes != nullptr) {
        environmentBlendModes[0] = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    }
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrGetViewConfigurationProperties_impl(
    XrInstance instance,
    XrSystemId systemId,
    XrViewConfigurationType viewConfigurationType,
    XrViewConfigurationProperties* configurationProperties)
{
    log_call("xrGetViewConfigurationProperties");
    if (!is_valid_instance(instance)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (systemId != kSystemId) {
        return XR_ERROR_SYSTEM_INVALID;
    }
    if (viewConfigurationType != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) {
        return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
    }
    if (configurationProperties == nullptr || configurationProperties->type != XR_TYPE_VIEW_CONFIGURATION_PROPERTIES) {
        return XR_ERROR_VALIDATION_FAILURE;
    }

    configurationProperties->viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    configurationProperties->fovMutable = 0;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrEnumerateViewConfigurationViews_impl(
    XrInstance instance,
    XrSystemId systemId,
    XrViewConfigurationType viewConfigurationType,
    uint32_t viewCapacityInput,
    uint32_t* viewCountOutput,
    XrViewConfigurationView* views)
{
    log_call("xrEnumerateViewConfigurationViews");
    if (!is_valid_instance(instance)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (systemId != kSystemId) {
        return XR_ERROR_SYSTEM_INVALID;
    }
    if (viewConfigurationType != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) {
        return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
    }
    if (viewCountOutput == nullptr) {
        return XR_ERROR_VALIDATION_FAILURE;
    }

    *viewCountOutput = 2;
    query_render_extent();
    if (viewCapacityInput > 0 && views != nullptr) {
        const uint32_t count = viewCapacityInput < 2 ? viewCapacityInput : 2;
        for (uint32_t i = 0; i < count; ++i) {
            if (views[i].type != XR_TYPE_VIEW_CONFIGURATION_VIEW) {
                return XR_ERROR_VALIDATION_FAILURE;
            }
            views[i].recommendedImageRectWidth = g_renderWidth;
            views[i].maxImageRectWidth = g_renderWidth;
            views[i].recommendedImageRectHeight = g_renderHeight;
            views[i].maxImageRectHeight = g_renderHeight;
            views[i].recommendedSwapchainSampleCount = 1;
            views[i].maxSwapchainSampleCount = 1;
        }
    }
    return XR_SUCCESS;
}


} // namespace axrb::runtime::detail
