#include "runtime_internal.h"

namespace axrb::runtime::detail {

XrResult XRAPI_CALL xrCreateSession_impl(
    XrInstance instance,
    const XrSessionCreateInfo* createInfo,
    XrSession* session)
{
    log_call("xrCreateSession");
    if (!is_valid_instance(instance)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (createInfo == nullptr || session == nullptr || createInfo->type != XR_TYPE_SESSION_CREATE_INFO) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    if (createInfo->systemId != kSystemId) {
        return XR_ERROR_SYSTEM_INVALID;
    }

#if defined(__ANDROID__)
    struct Base { XrStructureType type; const Base* next; };
    for (auto* next = static_cast<const Base*>(createInfo->next); next; next = next->next) {
        if (next->type == XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR) {
            if (!g_vulkanRequirementsQueried) return XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING;
            if (!g_vulkan.initialize(*reinterpret_cast<const XrGraphicsBindingVulkanKHR*>(next))) return XR_ERROR_GRAPHICS_DEVICE_INVALID;
            break;
        }
    }
#endif
    *session = fake_session();
    g_pendingSessionEvents.clear();
    g_pendingInteractionProfileEvent = false;
    queue_session_state(XR_SESSION_STATE_IDLE);
    queue_session_state(XR_SESSION_STATE_READY);
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrDestroySession_impl(XrSession session)
{
    log_call("xrDestroySession");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    g_sessionState = XR_SESSION_STATE_UNKNOWN;
    g_pendingSessionEvents.clear();
    g_pendingInteractionProfileEvent = false;
    for (auto& sc : g_swapchains) {
        destroy_swapchain_images(sc);
        sc = {};
    }
    g_lastReleasedSwapchain = nullptr;
    for (auto& tracker : g_handTrackers) tracker.alive = false;
#if defined(__ANDROID__)
    g_vulkan.shutdown();
#endif
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrBeginSession_impl(XrSession session, const XrSessionBeginInfo* beginInfo)
{
    log_call("xrBeginSession");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (beginInfo == nullptr || beginInfo->type != XR_TYPE_SESSION_BEGIN_INFO) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    if (beginInfo->primaryViewConfigurationType != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) {
        return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
    }
    if (g_sessionState != XR_SESSION_STATE_READY) {
        return XR_ERROR_SESSION_NOT_READY;
    }

    queue_session_state(XR_SESSION_STATE_SYNCHRONIZED);
    queue_session_state(XR_SESSION_STATE_VISIBLE);
    queue_session_state(XR_SESSION_STATE_FOCUSED);
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrEndSession_impl(XrSession session)
{
    log_call("xrEndSession");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (g_sessionState != XR_SESSION_STATE_FOCUSED &&
        g_sessionState != XR_SESSION_STATE_VISIBLE &&
        g_sessionState != XR_SESSION_STATE_SYNCHRONIZED) {
        return XR_ERROR_SESSION_NOT_RUNNING;
    }

    queue_session_state(XR_SESSION_STATE_STOPPING);
    queue_session_state(XR_SESSION_STATE_IDLE);
    queue_session_state(XR_SESSION_STATE_READY);
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrPollEvent_impl(XrInstance instance, XrEventDataBuffer* eventData)
{
    log_call("xrPollEvent");
    if (!is_valid_instance(instance)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (eventData == nullptr || eventData->type != XR_TYPE_EVENT_DATA_BUFFER) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    if (g_pendingSessionEvents.empty()) {
        if (g_pendingInteractionProfileEvent) {
            g_pendingInteractionProfileEvent = false;
            auto* changed = reinterpret_cast<XrEventDataInteractionProfileChanged*>(eventData);
            changed->type = XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED;
            changed->next = nullptr;
            changed->session = fake_session();
            return XR_SUCCESS;
        }
        return XR_EVENT_UNAVAILABLE;
    }

    const XrSessionState state = g_pendingSessionEvents.front();
    g_pendingSessionEvents.pop_front();

    auto* stateChanged = reinterpret_cast<XrEventDataSessionStateChanged*>(eventData);
    stateChanged->type = XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED;
    stateChanged->next = nullptr;
    stateChanged->session = fake_session();
    stateChanged->state = state;
    stateChanged->time = monotonic_time_ns();
    return XR_SUCCESS;
}


} // namespace axrb::runtime::detail
