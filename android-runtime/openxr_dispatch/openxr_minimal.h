#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#define AXRB_XR_EXPORT extern "C" __declspec(dllexport)
#define XRAPI_ATTR
#define XRAPI_CALL __stdcall
#define XRAPI_PTR XRAPI_CALL
#else
#define AXRB_XR_EXPORT extern "C" __attribute__((visibility("default")))
#define XRAPI_ATTR
#define XRAPI_CALL
#define XRAPI_PTR
#endif

#define XR_MAKE_VERSION(major, minor, patch) \
    ((((major) & 0xffffULL) << 48) | (((minor) & 0xffffULL) << 32) | ((patch) & 0xffffffffULL))

#define XR_CURRENT_LOADER_RUNTIME_VERSION 1
#define XR_LOADER_INFO_STRUCT_VERSION 1
#define XR_RUNTIME_INFO_STRUCT_VERSION 1

#define XR_MAX_APPLICATION_NAME_SIZE 128
#define XR_MAX_ENGINE_NAME_SIZE 128
#define XR_MAX_RUNTIME_NAME_SIZE 128
#define XR_MAX_SYSTEM_NAME_SIZE 256
#define XR_MAX_EXTENSION_NAME_SIZE 128
#define XR_MAX_ACTION_SET_NAME_SIZE 64
#define XR_MAX_ACTION_NAME_SIZE 64
#define XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE 128
#define XR_MAX_LOCALIZED_ACTION_NAME_SIZE 128

using XrBool32 = uint32_t;
using XrFlags64 = uint64_t;
using XrVersion = uint64_t;
using XrDuration = int64_t;
using XrSystemId = uint64_t;
using XrTime = int64_t;
using XrPath = uint64_t;

struct XrInstance_T;
struct XrSession_T;
struct XrSpace_T;
struct XrSwapchain_T;
struct XrActionSet_T;
struct XrAction_T;

using XrInstance = XrInstance_T*;
using XrSession = XrSession_T*;
using XrSpace = XrSpace_T*;
using XrSwapchain = XrSwapchain_T*;
using XrActionSet = XrActionSet_T*;
using XrAction = XrAction_T*;

enum XrResult : int32_t {
    XR_SUCCESS = 0,
    XR_TIMEOUT_EXPIRED = 1,
    XR_SESSION_LOSS_PENDING = 3,
    XR_EVENT_UNAVAILABLE = 4,
    XR_SPACE_BOUNDS_UNAVAILABLE = 7,
    XR_SESSION_NOT_FOCUSED = 8,
    XR_FRAME_DISCARDED = 9,

    XR_ERROR_VALIDATION_FAILURE = -1,
    XR_ERROR_RUNTIME_FAILURE = -2,
    XR_ERROR_OUT_OF_MEMORY = -3,
    XR_ERROR_API_VERSION_UNSUPPORTED = -4,
    XR_ERROR_INITIALIZATION_FAILED = -6,
    XR_ERROR_FUNCTION_UNSUPPORTED = -7,
    XR_ERROR_FEATURE_UNSUPPORTED = -8,
    XR_ERROR_EXTENSION_NOT_PRESENT = -9,
    XR_ERROR_HANDLE_INVALID = -11,
    XR_ERROR_INSTANCE_LOST = -13,
    XR_ERROR_SESSION_RUNNING = -14,
    XR_ERROR_SESSION_NOT_RUNNING = -16,
    XR_ERROR_SYSTEM_INVALID = -17,
    XR_ERROR_PATH_INVALID = -18,
    XR_ERROR_PATH_COUNT_EXCEEDED = -19,
    XR_ERROR_PATH_FORMAT_INVALID = -20,
    XR_ERROR_PATH_UNSUPPORTED = -21,
    XR_ERROR_LAYER_INVALID = -22,
    XR_ERROR_LAYER_LIMIT_EXCEEDED = -23,
    XR_ERROR_SWAPCHAIN_RECT_INVALID = -25,
    XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED = -26,
    XR_ERROR_ACTION_TYPE_MISMATCH = -27,
    XR_ERROR_SESSION_NOT_READY = -28,
    XR_ERROR_SESSION_NOT_STOPPING = -29,
    XR_ERROR_TIME_INVALID = -30,
    XR_ERROR_REFERENCE_SPACE_UNSUPPORTED = -31,
    XR_ERROR_FILE_ACCESS_ERROR = -32,
    XR_ERROR_FILE_CONTENTS_INVALID = -33,
    XR_ERROR_FORM_FACTOR_UNSUPPORTED = -34,
    XR_ERROR_FORM_FACTOR_UNAVAILABLE = -35,
    XR_ERROR_API_LAYER_NOT_PRESENT = -36,
    XR_ERROR_CALL_ORDER_INVALID = -37,
    XR_ERROR_GRAPHICS_DEVICE_INVALID = -38,
    XR_ERROR_POSE_INVALID = -39,
    XR_ERROR_INDEX_OUT_OF_RANGE = -40,
    XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED = -41,
    XR_ERROR_ENVIRONMENT_BLEND_MODE_UNSUPPORTED = -42,
    XR_ERROR_NAME_DUPLICATED = -44,
    XR_ERROR_NAME_INVALID = -45,
    XR_ERROR_ACTIONSET_NOT_ATTACHED = -46,
    XR_ERROR_ACTIONSETS_ALREADY_ATTACHED = -47,
    XR_ERROR_LOCALIZED_NAME_DUPLICATED = -48,
    XR_ERROR_LOCALIZED_NAME_INVALID = -49,
    XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING = -50,
    XR_ERROR_RUNTIME_UNAVAILABLE = -51,
};

enum XrStructureType : int32_t {
    XR_TYPE_UNKNOWN = 0,
    XR_TYPE_API_LAYER_PROPERTIES = 1,
    XR_TYPE_EXTENSION_PROPERTIES = 2,
    XR_TYPE_INSTANCE_CREATE_INFO = 3,
    XR_TYPE_SYSTEM_GET_INFO = 4,
    XR_TYPE_SYSTEM_PROPERTIES = 5,
    XR_TYPE_INSTANCE_PROPERTIES = 32,
    XR_TYPE_VIEW_LOCATE_INFO = 6,
    XR_TYPE_VIEW = 7,
    XR_TYPE_SESSION_CREATE_INFO = 8,
    XR_TYPE_SWAPCHAIN_CREATE_INFO = 9,
    XR_TYPE_SESSION_BEGIN_INFO = 10,
    XR_TYPE_VIEW_STATE = 11,
    XR_TYPE_FRAME_END_INFO = 12,
    XR_TYPE_HAPTIC_VIBRATION = 13,
    XR_TYPE_EVENT_DATA_BUFFER = 16,
    XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING = 17,
    XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED = 18,
    XR_TYPE_ACTION_STATE_BOOLEAN = 23,
    XR_TYPE_ACTION_STATE_FLOAT = 24,
    XR_TYPE_ACTION_STATE_VECTOR2F = 25,
    XR_TYPE_ACTION_STATE_POSE = 27,
    XR_TYPE_ACTION_SET_CREATE_INFO = 28,
    XR_TYPE_ACTION_CREATE_INFO = 29,
    XR_TYPE_FRAME_WAIT_INFO = 33,
    XR_TYPE_ACTIONS_SYNC_INFO = 61,
    XR_TYPE_COMPOSITION_LAYER_PROJECTION = 35,
    XR_TYPE_REFERENCE_SPACE_CREATE_INFO = 37,
    XR_TYPE_ACTION_SPACE_CREATE_INFO = 38,
    XR_TYPE_VIEW_CONFIGURATION_VIEW = 41,
    XR_TYPE_SPACE_LOCATION = 42,
    XR_TYPE_FRAME_STATE = 44,
    XR_TYPE_VIEW_CONFIGURATION_PROPERTIES = 45,
    XR_TYPE_FRAME_BEGIN_INFO = 46,
    XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW = 48,
    XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING = 51,
    XR_TYPE_INTERACTION_PROFILE_STATE = 53,
    XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO = 55,
    XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO = 56,
    XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO = 57,
    XR_TYPE_ACTION_STATE_GET_INFO = 58,
    XR_TYPE_HAPTIC_ACTION_INFO = 59,
    XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO = 60,
    XR_TYPE_BOUND_SOURCES_FOR_ACTION_ENUMERATE_INFO = 62,
    XR_TYPE_INPUT_SOURCE_LOCALIZED_NAME_GET_INFO = 63,
    XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR = 1000024001,
    XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR = 1000024002,
    XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR = 1000024003,
    XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR = 1000025000,
    XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR = 1000025001,
    XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR = 1000025002,
};

enum XrLoaderInterfaceStructs : int32_t {
    XR_LOADER_INTERFACE_STRUCT_UNINTIALIZED = 0,
    XR_LOADER_INTERFACE_STRUCT_LOADER_INFO = 1,
    XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST = 2,
    XR_LOADER_INTERFACE_STRUCT_RUNTIME_REQUEST = 3,
    XR_LOADER_INTERFACE_STRUCT_API_LAYER_CREATE_INFO = 4,
    XR_LOADER_INTERFACE_STRUCT_API_LAYER_NEXT_INFO = 5,
};

enum XrFormFactor : int32_t {
    XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY = 1,
    XR_FORM_FACTOR_HANDHELD_DISPLAY = 2,
};

enum XrViewConfigurationType : int32_t {
    XR_VIEW_CONFIGURATION_TYPE_PRIMARY_MONO = 1,
    XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO = 2,
};

enum XrReferenceSpaceType : int32_t {
    XR_REFERENCE_SPACE_TYPE_VIEW = 1,
    XR_REFERENCE_SPACE_TYPE_LOCAL = 2,
    XR_REFERENCE_SPACE_TYPE_STAGE = 3,
};

enum XrEnvironmentBlendMode : int32_t {
    XR_ENVIRONMENT_BLEND_MODE_OPAQUE = 1,
    XR_ENVIRONMENT_BLEND_MODE_ADDITIVE = 2,
    XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND = 3,
};

enum XrSessionState : int32_t {
    XR_SESSION_STATE_UNKNOWN = 0,
    XR_SESSION_STATE_IDLE = 1,
    XR_SESSION_STATE_READY = 2,
    XR_SESSION_STATE_SYNCHRONIZED = 3,
    XR_SESSION_STATE_VISIBLE = 4,
    XR_SESSION_STATE_FOCUSED = 5,
    XR_SESSION_STATE_STOPPING = 6,
    XR_SESSION_STATE_LOSS_PENDING = 7,
    XR_SESSION_STATE_EXITING = 8,
};

using XrInstanceCreateFlags = XrFlags64;
using XrSessionCreateFlags = XrFlags64;
using XrSwapchainCreateFlags = XrFlags64;
using XrSwapchainUsageFlags = XrFlags64;
using XrViewConfigurationViewCreateFlags = XrFlags64;
using XrViewConfigurationViewStateFlags = XrFlags64;
using XrCompositionLayerFlags = XrFlags64;
using XrSystemGraphicsPropertiesFlags = XrFlags64;
using XrSystemTrackingPropertiesFlags = XrFlags64;
using XrViewStateFlags = XrFlags64;
using XrSpaceLocationFlags = XrFlags64;
using XrInputSourceLocalizedNameFlags = XrFlags64;

constexpr XrViewStateFlags XR_VIEW_STATE_ORIENTATION_VALID_BIT = 0x00000001;
constexpr XrViewStateFlags XR_VIEW_STATE_POSITION_VALID_BIT = 0x00000002;
constexpr XrViewStateFlags XR_VIEW_STATE_ORIENTATION_TRACKED_BIT = 0x00000004;
constexpr XrViewStateFlags XR_VIEW_STATE_POSITION_TRACKED_BIT = 0x00000008;
constexpr XrSpaceLocationFlags XR_SPACE_LOCATION_ORIENTATION_VALID_BIT = 0x00000001;
constexpr XrSpaceLocationFlags XR_SPACE_LOCATION_POSITION_VALID_BIT = 0x00000002;
constexpr XrSpaceLocationFlags XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT = 0x00000004;
constexpr XrSpaceLocationFlags XR_SPACE_LOCATION_POSITION_TRACKED_BIT = 0x00000008;
constexpr XrSwapchainUsageFlags XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT = 0x00000001;
constexpr XrSwapchainUsageFlags XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT = 0x00000002;
constexpr XrSwapchainUsageFlags XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT = 0x00000008;
constexpr XrSwapchainUsageFlags XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT = 0x00000010;
constexpr XrSwapchainUsageFlags XR_SWAPCHAIN_USAGE_SAMPLED_BIT = 0x00000020;
constexpr XrPath XR_NULL_PATH = 0;

using PFN_xrVoidFunction = void(XRAPI_PTR*)();
using PFN_xrGetInstanceProcAddr = XrResult(XRAPI_PTR*)(XrInstance instance, const char* name, PFN_xrVoidFunction* function);

enum XrActionType : int32_t {
    XR_ACTION_TYPE_BOOLEAN_INPUT = 1,
    XR_ACTION_TYPE_FLOAT_INPUT = 2,
    XR_ACTION_TYPE_VECTOR2F_INPUT = 3,
    XR_ACTION_TYPE_POSE_INPUT = 4,
    XR_ACTION_TYPE_VIBRATION_OUTPUT = 100,
};

struct XrApplicationInfo {
    char applicationName[XR_MAX_APPLICATION_NAME_SIZE];
    uint32_t applicationVersion;
    char engineName[XR_MAX_ENGINE_NAME_SIZE];
    uint32_t engineVersion;
    XrVersion apiVersion;
};

struct XrInstanceCreateInfo {
    XrStructureType type;
    const void* next;
    XrInstanceCreateFlags createFlags;
    XrApplicationInfo applicationInfo;
    uint32_t enabledApiLayerCount;
    const char* const* enabledApiLayerNames;
    uint32_t enabledExtensionCount;
    const char* const* enabledExtensionNames;
};

struct XrExtensionProperties {
    XrStructureType type;
    void* next;
    char extensionName[XR_MAX_EXTENSION_NAME_SIZE];
    uint32_t extensionVersion;
};

struct XrInstanceProperties {
    XrStructureType type;
    void* next;
    XrVersion runtimeVersion;
    char runtimeName[XR_MAX_RUNTIME_NAME_SIZE];
};

struct XrSystemGetInfo {
    XrStructureType type;
    const void* next;
    XrFormFactor formFactor;
};

struct XrSystemGraphicsProperties {
    uint32_t maxSwapchainImageHeight;
    uint32_t maxSwapchainImageWidth;
    uint32_t maxLayerCount;
};

struct XrSystemTrackingProperties {
    XrBool32 orientationTracking;
    XrBool32 positionTracking;
};

struct XrSystemProperties {
    XrStructureType type;
    void* next;
    XrSystemId systemId;
    uint32_t vendorId;
    char systemName[XR_MAX_SYSTEM_NAME_SIZE];
    XrSystemGraphicsProperties graphicsProperties;
    XrSystemTrackingProperties trackingProperties;
};

struct XrSessionCreateInfo {
    XrStructureType type;
    const void* next;
    XrSessionCreateFlags createFlags;
    XrSystemId systemId;
};

struct XrSessionBeginInfo {
    XrStructureType type;
    const void* next;
    XrViewConfigurationType primaryViewConfigurationType;
};

struct XrGraphicsRequirementsOpenGLESKHR {
    XrStructureType type;
    void* next;
    XrVersion minApiVersionSupported;
    XrVersion maxApiVersionSupported;
};

struct XrGraphicsRequirementsVulkanKHR {
    XrStructureType type;
    void* next;
    XrVersion minApiVersionSupported;
    XrVersion maxApiVersionSupported;
};

struct XrSwapchainCreateInfo {
    XrStructureType type;
    const void* next;
    XrSwapchainCreateFlags createFlags;
    XrSwapchainUsageFlags usageFlags;
    int64_t format;
    uint32_t sampleCount;
    uint32_t width;
    uint32_t height;
    uint32_t faceCount;
    uint32_t arraySize;
    uint32_t mipCount;
};

struct XrSwapchainImageBaseHeader {
    XrStructureType type;
    void* next;
};

struct XrSwapchainImageOpenGLESKHR {
    XrStructureType type;
    void* next;
    uint32_t image;
};

struct XrSwapchainImageVulkanKHR {
    XrStructureType type;
    void* next;
    uint64_t image;
};

struct XrSwapchainImageAcquireInfo {
    XrStructureType type;
    const void* next;
};

struct XrSwapchainImageWaitInfo {
    XrStructureType type;
    const void* next;
    XrDuration timeout;
};

struct XrSwapchainImageReleaseInfo {
    XrStructureType type;
    const void* next;
};

struct XrPosef {
    struct {
        float x;
        float y;
        float z;
        float w;
    } orientation;
    struct {
        float x;
        float y;
        float z;
    } position;
};

struct XrFovf {
    float angleLeft;
    float angleRight;
    float angleUp;
    float angleDown;
};

struct XrViewLocateInfo {
    XrStructureType type;
    const void* next;
    XrViewConfigurationType viewConfigurationType;
    XrTime displayTime;
    XrSpace space;
};

struct XrViewState {
    XrStructureType type;
    void* next;
    XrViewStateFlags viewStateFlags;
};

struct XrView {
    XrStructureType type;
    void* next;
    XrPosef pose;
    XrFovf fov;
};

struct XrReferenceSpaceCreateInfo {
    XrStructureType type;
    const void* next;
    XrReferenceSpaceType referenceSpaceType;
    XrPosef poseInReferenceSpace;
};

struct XrActionSetCreateInfo {
    XrStructureType type;
    const void* next;
    char actionSetName[XR_MAX_ACTION_SET_NAME_SIZE];
    char localizedActionSetName[XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE];
    uint32_t priority;
};

struct XrActionCreateInfo {
    XrStructureType type;
    const void* next;
    char actionName[XR_MAX_ACTION_NAME_SIZE];
    XrActionType actionType;
    uint32_t countSubactionPaths;
    const XrPath* subactionPaths;
    char localizedActionName[XR_MAX_LOCALIZED_ACTION_NAME_SIZE];
};

struct XrActionSuggestedBinding {
    XrAction action;
    XrPath binding;
};

struct XrInteractionProfileSuggestedBinding {
    XrStructureType type;
    const void* next;
    XrPath interactionProfile;
    uint32_t countSuggestedBindings;
    const XrActionSuggestedBinding* suggestedBindings;
};

struct XrSessionActionSetsAttachInfo {
    XrStructureType type;
    const void* next;
    uint32_t countActionSets;
    const XrActionSet* actionSets;
};

struct XrInteractionProfileState {
    XrStructureType type;
    void* next;
    XrPath interactionProfile;
};

struct XrActionStateGetInfo {
    XrStructureType type;
    const void* next;
    XrAction action;
    XrPath subactionPath;
};

struct XrActionStateBoolean {
    XrStructureType type;
    void* next;
    XrBool32 currentState;
    XrBool32 changedSinceLastSync;
    XrTime lastChangeTime;
    XrBool32 isActive;
};

struct XrActionStateFloat {
    XrStructureType type;
    void* next;
    float currentState;
    XrBool32 changedSinceLastSync;
    XrTime lastChangeTime;
    XrBool32 isActive;
};

struct XrVector2f {
    float x;
    float y;
};

struct XrActionStateVector2f {
    XrStructureType type;
    void* next;
    XrVector2f currentState;
    XrBool32 changedSinceLastSync;
    XrTime lastChangeTime;
    XrBool32 isActive;
};

struct XrActionStatePose {
    XrStructureType type;
    void* next;
    XrBool32 isActive;
};

struct XrActionSpaceCreateInfo {
    XrStructureType type;
    const void* next;
    XrAction action;
    XrPath subactionPath;
    XrPosef poseInActionSpace;
};

struct XrSpaceLocation {
    XrStructureType type;
    void* next;
    XrSpaceLocationFlags locationFlags;
    XrPosef pose;
};

struct XrActiveActionSet {
    XrActionSet actionSet;
    XrPath subactionPath;
};

struct XrActionsSyncInfo {
    XrStructureType type;
    const void* next;
    uint32_t countActiveActionSets;
    const XrActiveActionSet* activeActionSets;
};

struct XrBoundSourcesForActionEnumerateInfo {
    XrStructureType type;
    const void* next;
    XrAction action;
};

struct XrInputSourceLocalizedNameGetInfo {
    XrStructureType type;
    const void* next;
    XrPath sourcePath;
    XrInputSourceLocalizedNameFlags whichComponents;
};

struct XrHapticActionInfo {
    XrStructureType type;
    const void* next;
    XrAction action;
    XrPath subactionPath;
};

struct XrHapticBaseHeader {
    XrStructureType type;
    const void* next;
};

struct XrFrameWaitInfo {
    XrStructureType type;
    const void* next;
};

struct XrFrameState {
    XrStructureType type;
    void* next;
    XrTime predictedDisplayTime;
    XrDuration predictedDisplayPeriod;
    XrBool32 shouldRender;
};

struct XrFrameBeginInfo {
    XrStructureType type;
    const void* next;
};

struct XrFrameEndInfo {
    XrStructureType type;
    const void* next;
    XrTime displayTime;
    XrEnvironmentBlendMode environmentBlendMode;
    uint32_t layerCount;
    const void* const* layers;
};

struct XrEventDataBuffer {
    XrStructureType type;
    const void* next;
    uint8_t varying[4000];
};

struct XrEventDataSessionStateChanged {
    XrStructureType type;
    const void* next;
    XrSession session;
    XrSessionState state;
    XrTime time;
};

struct XrViewConfigurationProperties {
    XrStructureType type;
    void* next;
    XrViewConfigurationType viewConfigurationType;
    XrBool32 fovMutable;
};

struct XrViewConfigurationView {
    XrStructureType type;
    void* next;
    uint32_t recommendedImageRectWidth;
    uint32_t maxImageRectWidth;
    uint32_t recommendedImageRectHeight;
    uint32_t maxImageRectHeight;
    uint32_t recommendedSwapchainSampleCount;
    uint32_t maxSwapchainSampleCount;
};

struct XrNegotiateLoaderInfo {
    XrLoaderInterfaceStructs structType;
    uint32_t structVersion;
    size_t structSize;
    uint32_t minInterfaceVersion;
    uint32_t maxInterfaceVersion;
    XrVersion minApiVersion;
    XrVersion maxApiVersion;
};

struct XrNegotiateRuntimeRequest {
    XrLoaderInterfaceStructs structType;
    uint32_t structVersion;
    size_t structSize;
    uint32_t runtimeInterfaceVersion;
    XrVersion runtimeApiVersion;
    PFN_xrGetInstanceProcAddr getInstanceProcAddr;
};

using PFN_xrNegotiateLoaderRuntimeInterface =
    XrResult(XRAPI_PTR*)(const XrNegotiateLoaderInfo* loaderInfo, XrNegotiateRuntimeRequest* runtimeRequest);
