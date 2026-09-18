#pragma once

#include "session.h"

#include "image_frame.h"
#include "openxr_dispatch/openxr_minimal.h"
#include "pose_client.h"
#include "perf_stats.h"
#include "vulkan_backend.h"
#include "android_surface.h"
#include <memory>
#include "controller_input.h"
#include "openxr_dispatch/hand_tracking_types.h"
#include "windows_gpu_frame.h"

#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <deque>
#include <string>
#include <string_view>
#include <vector>
#include <thread>
#include <ctime>
#include <limits>

#if defined(__ANDROID__)
#include <android/log.h>
#include <GLES3/gl3.h>
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/system_properties.h>
#include <unistd.h>
#endif

namespace axrb::runtime::detail {

constexpr XrSystemId kSystemId = 1;
constexpr float kEyeHalfIpdMeters = 0.0315f;
constexpr float kProjectionHalfFovRadians = 0.95f;

#if defined(__ANDROID__)
struct AndroidLoaderInitInfo {
    XrStructureType type;
    const void* next;
    void* applicationVM;
    void* applicationContext;
};
#endif

struct RuntimeHandle {
    uint64_t magic;
};

extern RuntimeHandle g_instanceHandle;
extern RuntimeHandle g_sessionHandle;
extern RuntimeHandle g_spaceHandle;
extern RuntimeHandle g_actionSetHandle;
struct ActionSample {
    axrb::protocol::InputValue value;
    bool changed = false;
    XrTime changedAt = 0;
};
struct ActionRecord {
    uint64_t magic;
    XrActionType type = XR_ACTION_TYPE_BOOLEAN_INPUT;
    std::vector<XrPath> bindings;
    std::array<ActionSample, 3> samples{}; // Left, right, aggregate.
};
extern std::deque<ActionRecord> g_actionHandles;
struct HandTrackerRecord { bool alive = true; uint32_t hand = 0; uint32_t sources = 3; };
extern std::deque<HandTrackerRecord> g_handTrackers;
extern XrSessionState g_sessionState;
extern std::deque<XrSessionState> g_pendingSessionEvents;
extern bool g_pendingInteractionProfileEvent;

enum class SpaceKind {
    Reference,
    Local,
    View,
    LeftHand,
    RightHand,
    LeftAim,
    RightAim,
};

struct SpaceRecord {
    RuntimeHandle handle{};
    SpaceKind kind = SpaceKind::Reference;
    XrPosef offsetInParent{};
};

struct PathRecord {
    XrPath path = XR_NULL_PATH;
    std::string text;
};

extern std::deque<SpaceRecord> g_spaces;
extern uint32_t g_spaceCount;
extern std::deque<PathRecord> g_paths;
extern uint32_t g_pathCount;
struct SwapchainRecord {
#if defined(__ANDROID__)
    VulkanSwapchain vulkan{};
    std::shared_ptr<AndroidSurface> surface;
#endif
    bool created = false;
    bool acquired = false;
    bool waited = false;
    bool hasReleasedImage = false;
    uint32_t imageCount = 3;
    uint32_t nextImage = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t arraySize = 0;
    int64_t format = 0;
    uint32_t textures[3] = {};
    uint32_t currentImage = 0;
    uint32_t releasedImage = 0;
};
extern std::deque<SwapchainRecord> g_swapchains;
extern SwapchainRecord* g_lastReleasedSwapchain;
extern uint32_t g_actionCount;
extern uint64_t g_nextPath;
extern uint64_t g_imageFrameSequence;
extern XrTime g_nextFrameStart;
extern uint32_t g_renderWidth, g_renderHeight;
extern bool g_renderExtentQueried;
extern axrb::protocol::PoseFrame g_lastViewPoseFrame;
#if defined(__ANDROID__)
XrResult XRAPI_CALL xrCreateSwapchainAndroidSurfaceKHR_impl(XrSession session, const XrSwapchainCreateInfo* info, XrSwapchain* swapchain, jobject* surface);
XrResult XRAPI_CALL xrGetVulkanDeviceExtensionsKHR_impl(XrInstance instance, XrSystemId systemId, uint32_t capacity, uint32_t* count, char* buffer);
extern VulkanBackend g_vulkan;
extern bool g_vulkanRequirementsQueried;
extern VkInstance g_vulkanInstance;
#endif

void query_render_extent();
void log_call(const char* name);
void log_proc_request(const char* name);
XrInstance fake_instance();
XrSession fake_session();
XrSpace fake_space();
XrPosef identity_pose();
XrSpace make_space(SpaceKind kind, const XrPosef& offsetInParent = identity_pose());
SpaceRecord* find_space(XrSpace space);
XrActionSet fake_action_set();
XrAction fake_action(uint32_t index);
bool is_valid_instance(XrInstance instance);
bool is_valid_session(XrSession session);
SwapchainRecord* find_swapchain(XrSwapchain handle);
bool is_valid_action_set(XrActionSet actionSet);
bool is_valid_action(XrAction action);
const char* path_text(XrPath path);
SpaceKind action_space_kind(XrPath subactionPath);
XrPosef protocol_pose_to_xr(const axrb::protocol::Pose& pose);
XrPosef normalize_pose(XrPosef pose);
XrPosef multiply_pose(const XrPosef& a, const XrPosef& b);
XrPosef inverse_pose(const XrPosef& pose);
XrPosef world_pose_for_space(const SpaceRecord& record, const axrb::protocol::PoseFrame& poseFrame);
XrTime monotonic_time_ns();
#if defined(__ANDROID__) || defined(AXRB_INPUT_FIXTURE)
XrResult XRAPI_CALL xrConvertTimespecTimeToTimeKHR_impl(XrInstance instance, const timespec* source, XrTime* time);
#endif
#if defined(__ANDROID__) || defined(AXRB_INPUT_FIXTURE)
XrResult XRAPI_CALL xrConvertTimeToTimespecTimeKHR_impl(XrInstance instance, XrTime time, timespec* target);
#endif
void queue_session_state(XrSessionState state);
void destroy_swapchain_images(SwapchainRecord& sc);
void create_opengles_swapchain_images(SwapchainRecord& sc, const XrSwapchainCreateInfo& createInfo);
struct PreparedGpuLayer {
    std::array<SwapchainRecord*, 2> swapchains{};
    XrSwapchainSubImage subimages[2]{};
    bool verticalFlip[2]{};
    uint32_t width = 0, height = 0;
    axrb::protocol::ImageProjection projection{};
};
XrResult submit_projection_frame(const XrFrameEndInfo& info, uint32_t batchPart = 0, bool validateOnly = false, PreparedGpuLayer* prepared = nullptr);
XrResult XRAPI_CALL xrCreateInstance_impl(const XrInstanceCreateInfo* createInfo, XrInstance* instance);
XrResult XRAPI_CALL xrInitializeLoaderKHR_impl(const void* loaderInitInfo);
XrResult XRAPI_CALL xrDestroyInstance_impl(XrInstance instance);
XrResult XRAPI_CALL xrEnumerateInstanceExtensionProperties_impl(
    const char* layerName,
    uint32_t propertyCapacityInput,
    uint32_t* propertyCountOutput,
    XrExtensionProperties* properties);
XrResult XRAPI_CALL xrGetInstanceProperties_impl(
    XrInstance instance,
    XrInstanceProperties* instanceProperties);
XrResult XRAPI_CALL xrGetSystem_impl(
    XrInstance instance,
    const XrSystemGetInfo* getInfo,
    XrSystemId* systemId);
XrResult XRAPI_CALL xrGetSystemProperties_impl(
    XrInstance instance,
    XrSystemId systemId,
    XrSystemProperties* properties);
XrResult XRAPI_CALL xrGetOpenGLESGraphicsRequirementsKHR_impl(
    XrInstance instance,
    XrSystemId systemId,
    XrGraphicsRequirementsOpenGLESKHR* graphicsRequirements);
XrResult XRAPI_CALL xrGetVulkanGraphicsRequirementsKHR_impl(
    XrInstance instance,
    XrSystemId systemId,
    XrGraphicsRequirementsVulkanKHR* graphicsRequirements);
#if defined(__ANDROID__)
XrResult XRAPI_CALL xrGetVulkanExtensionsKHR_impl(XrInstance instance, XrSystemId systemId,
                                                uint32_t capacity, uint32_t* count, char* buffer);
#endif
#if defined(__ANDROID__)
XrResult XRAPI_CALL xrGetVulkanGraphicsDeviceKHR_impl(XrInstance instance, XrSystemId systemId,
                                                    VkInstance vkInstance, VkPhysicalDevice* device);
#endif
#if defined(__ANDROID__)
XrResult XRAPI_CALL xrGetVulkanGraphicsDevice2KHR_impl(XrInstance instance,
        const XrVulkanGraphicsDeviceGetInfoKHR* info, VkPhysicalDevice* device);
#endif
#if defined(__ANDROID__)
XrResult XRAPI_CALL xrCreateVulkanInstanceKHR_impl(XrInstance instance, const XrVulkanInstanceCreateInfoKHR* info,
                                                VkInstance* vkInstance, VkResult* result);
#endif
#if defined(__ANDROID__)
XrResult XRAPI_CALL xrCreateVulkanDeviceKHR_impl(XrInstance instance, const XrVulkanDeviceCreateInfoKHR* info,
                                              VkDevice* device, VkResult* result);
#endif
XrResult XRAPI_CALL xrCreateSession_impl(
    XrInstance instance,
    const XrSessionCreateInfo* createInfo,
    XrSession* session);
XrResult XRAPI_CALL xrDestroySession_impl(XrSession session);
XrResult XRAPI_CALL xrBeginSession_impl(XrSession session, const XrSessionBeginInfo* beginInfo);
XrResult XRAPI_CALL xrEndSession_impl(XrSession session);
XrResult XRAPI_CALL xrPollEvent_impl(XrInstance instance, XrEventDataBuffer* eventData);
XrResult XRAPI_CALL xrEnumerateViewConfigurations_impl(
    XrInstance instance,
    XrSystemId systemId,
    uint32_t viewConfigurationTypeCapacityInput,
    uint32_t* viewConfigurationTypeCountOutput,
    XrViewConfigurationType* viewConfigurationTypes);
XrResult XRAPI_CALL xrEnumerateEnvironmentBlendModes_impl(
    XrInstance instance,
    XrSystemId systemId,
    XrViewConfigurationType viewConfigurationType,
    uint32_t environmentBlendModeCapacityInput,
    uint32_t* environmentBlendModeCountOutput,
    XrEnvironmentBlendMode* environmentBlendModes);
XrResult XRAPI_CALL xrGetViewConfigurationProperties_impl(
    XrInstance instance,
    XrSystemId systemId,
    XrViewConfigurationType viewConfigurationType,
    XrViewConfigurationProperties* configurationProperties);
XrResult XRAPI_CALL xrEnumerateSwapchainFormats_impl(
    XrSession session,
    uint32_t formatCapacityInput,
    uint32_t* formatCountOutput,
    int64_t* formats);
XrResult XRAPI_CALL xrCreateSwapchain_impl(
    XrSession session,
    const XrSwapchainCreateInfo* createInfo,
    XrSwapchain* swapchain);
XrResult XRAPI_CALL xrDestroySwapchain_impl(XrSwapchain swapchain);
XrResult XRAPI_CALL xrEnumerateSwapchainImages_impl(
    XrSwapchain swapchain,
    uint32_t imageCapacityInput,
    uint32_t* imageCountOutput,
    XrSwapchainImageBaseHeader* images);
XrResult XRAPI_CALL xrAcquireSwapchainImage_impl(
    XrSwapchain swapchain,
    const XrSwapchainImageAcquireInfo* acquireInfo,
    uint32_t* index);
XrResult XRAPI_CALL xrWaitSwapchainImage_impl(
    XrSwapchain swapchain,
    const XrSwapchainImageWaitInfo* waitInfo);
XrResult XRAPI_CALL xrReleaseSwapchainImage_impl(
    XrSwapchain swapchain,
    const XrSwapchainImageReleaseInfo* releaseInfo);
XrResult XRAPI_CALL xrEnumerateViewConfigurationViews_impl(
    XrInstance instance,
    XrSystemId systemId,
    XrViewConfigurationType viewConfigurationType,
    uint32_t viewCapacityInput,
    uint32_t* viewCountOutput,
    XrViewConfigurationView* views);
XrResult XRAPI_CALL xrEnumerateReferenceSpaces_impl(
    XrSession session,
    uint32_t spaceCapacityInput,
    uint32_t* spaceCountOutput,
    XrReferenceSpaceType* spaces);
XrResult XRAPI_CALL xrGetReferenceSpaceBoundsRect_impl(
    XrSession session, XrReferenceSpaceType type, XrExtent2Df* bounds);
XrResult XRAPI_CALL xrCreateReferenceSpace_impl(
    XrSession session,
    const XrReferenceSpaceCreateInfo* createInfo,
    XrSpace* space);
XrResult XRAPI_CALL xrCreateActionSet_impl(
    XrInstance instance,
    const XrActionSetCreateInfo* createInfo,
    XrActionSet* actionSet);
XrResult XRAPI_CALL xrDestroyActionSet_impl(XrActionSet actionSet);
XrResult XRAPI_CALL xrCreateAction_impl(
    XrActionSet actionSet,
    const XrActionCreateInfo* createInfo,
    XrAction* action);
XrResult XRAPI_CALL xrDestroyAction_impl(XrAction action);
XrResult XRAPI_CALL xrStringToPath_impl(XrInstance instance, const char* pathString, XrPath* path);
XrResult XRAPI_CALL xrPathToString_impl(
    XrInstance instance,
    XrPath path,
    uint32_t bufferCapacityInput,
    uint32_t* bufferCountOutput,
    char* buffer);
XrResult XRAPI_CALL xrSuggestInteractionProfileBindings_impl(
    XrInstance instance,
    const XrInteractionProfileSuggestedBinding* suggestedBindings);
XrResult XRAPI_CALL xrCreateActionSpace_impl(
    XrSession session,
    const XrActionSpaceCreateInfo* createInfo,
    XrSpace* space);
XrResult XRAPI_CALL xrLocateSpace_impl(
    XrSpace space,
    XrSpace baseSpace,
    XrTime time,
    XrSpaceLocation* location);
XrResult XRAPI_CALL xrAttachSessionActionSets_impl(
    XrSession session,
    const XrSessionActionSetsAttachInfo* attachInfo);
XrResult XRAPI_CALL xrSyncActions_impl(XrSession session, const XrActionsSyncInfo* syncInfo);
const ActionSample& action_sample(const XrActionStateGetInfo& info);
XrResult XRAPI_CALL xrGetActionStateBoolean_impl(
    XrSession session,
    const XrActionStateGetInfo* getInfo,
    XrActionStateBoolean* state);
XrResult XRAPI_CALL xrGetActionStateFloat_impl(
    XrSession session,
    const XrActionStateGetInfo* getInfo,
    XrActionStateFloat* state);
XrResult XRAPI_CALL xrGetActionStateVector2f_impl(
    XrSession session,
    const XrActionStateGetInfo* getInfo,
    XrActionStateVector2f* state);
XrResult XRAPI_CALL xrGetActionStatePose_impl(
    XrSession session,
    const XrActionStateGetInfo* getInfo,
    XrActionStatePose* state);
XrResult XRAPI_CALL xrGetCurrentInteractionProfile_impl(
    XrSession session,
    XrPath topLevelUserPath,
    XrInteractionProfileState* interactionProfile);
XrResult XRAPI_CALL xrEnumerateBoundSourcesForAction_impl(
    XrSession session,
    const XrBoundSourcesForActionEnumerateInfo* enumerateInfo,
    uint32_t sourceCapacityInput,
    uint32_t* sourceCountOutput,
    XrPath* sources);
XrResult XRAPI_CALL xrGetInputSourceLocalizedName_impl(
    XrSession session,
    const XrInputSourceLocalizedNameGetInfo* getInfo,
    uint32_t bufferCapacityInput,
    uint32_t* bufferCountOutput,
    char* buffer);
XrResult XRAPI_CALL xrApplyHapticFeedback_impl(
    XrSession session,
    const XrHapticActionInfo* hapticActionInfo,
    const XrHapticBaseHeader* hapticFeedback);
HandTrackerRecord* find_hand_tracker(XrHandTrackerEXT tracker);
XrResult XRAPI_CALL xrCreateHandTrackerEXT_impl(XrSession session, const XrHandTrackerCreateInfoEXT* info, XrHandTrackerEXT* tracker);
XrResult XRAPI_CALL xrDestroyHandTrackerEXT_impl(XrHandTrackerEXT tracker);
XrResult XRAPI_CALL xrLocateHandJointsEXT_impl(XrHandTrackerEXT tracker, const XrHandJointsLocateInfoEXT* info, XrHandJointLocationsEXT* locations);
XrResult XRAPI_CALL xrStopHapticFeedback_impl(
    XrSession session,
    const XrHapticActionInfo* hapticActionInfo);
XrResult XRAPI_CALL xrLocateViews_impl(
    XrSession session,
    const XrViewLocateInfo* viewLocateInfo,
    XrViewState* viewState,
    uint32_t viewCapacityInput,
    uint32_t* viewCountOutput,
    XrView* views);
XrResult XRAPI_CALL xrDestroySpace_impl(XrSpace space);
XrResult XRAPI_CALL xrWaitFrame_impl(
    XrSession session,
    const XrFrameWaitInfo* frameWaitInfo,
    XrFrameState* frameState);
XrResult XRAPI_CALL xrBeginFrame_impl(XrSession session, const XrFrameBeginInfo* frameBeginInfo);
XrResult XRAPI_CALL xrEndFrame_impl(XrSession session, const XrFrameEndInfo* frameEndInfo);
XrResult XRAPI_CALL xrEnumerateDisplayRefreshRatesFB_impl(XrSession session, uint32_t capacity, uint32_t* count, float* rates);
XrResult XRAPI_CALL xrGetDisplayRefreshRateFB_impl(XrSession session, float* rate);
XrResult XRAPI_CALL xrRequestDisplayRefreshRateFB_impl(XrSession session, float rate);
XrResult XRAPI_CALL xrGetInstanceProcAddr_impl(
    XrInstance instance,
    const char* name,
    PFN_xrVoidFunction* function);

} // namespace axrb::runtime::detail
