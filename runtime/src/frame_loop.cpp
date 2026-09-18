#include "runtime_internal.h"

namespace axrb::runtime::detail {

XrResult XRAPI_CALL xrWaitFrame_impl(
    XrSession session,
    const XrFrameWaitInfo* frameWaitInfo,
    XrFrameState* frameState)
{
    log_call("xrWaitFrame");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (g_sessionState != XR_SESSION_STATE_FOCUSED) {
        return XR_ERROR_SESSION_NOT_RUNNING;
    }
    if (frameState == nullptr ||
        (frameWaitInfo != nullptr && frameWaitInfo->type != XR_TYPE_FRAME_WAIT_INFO) || frameState->type != XR_TYPE_FRAME_STATE) {
        return XR_ERROR_VALIDATION_FAILURE;
    }

    // Pace at the active host display period. Never build a queue of
    // catch-up frames after a slow render or a disconnected transport.
    const XrTime period = axrb::protocol::display_period_or_default(pose_client().latest_pose_frame());
    XrTime now = monotonic_time_ns();
    if (g_nextFrameStart == 0 || now - g_nextFrameStart >= period) {
        g_nextFrameStart = now;
    }
    if (g_nextFrameStart > now) {
        std::this_thread::sleep_for(std::chrono::nanoseconds(g_nextFrameStart - now));
        now = monotonic_time_ns();
    }
    if (now - g_nextFrameStart >= period) { g_nextFrameStart = now; }
    g_nextFrameStart += period;
    frameState->predictedDisplayTime = g_nextFrameStart;
    frameState->predictedDisplayPeriod = period;
    frameState->shouldRender = 1;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrBeginFrame_impl(XrSession session, const XrFrameBeginInfo* frameBeginInfo)
{
    log_call("xrBeginFrame");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (g_sessionState != XR_SESSION_STATE_FOCUSED) {
        return XR_ERROR_SESSION_NOT_RUNNING;
    }
    if (frameBeginInfo != nullptr && frameBeginInfo->type != XR_TYPE_FRAME_BEGIN_INFO) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrEndFrame_impl(XrSession session, const XrFrameEndInfo* frameEndInfo)
{
    static axrb::protocol::PerfStats stats("end-frame");
    axrb::protocol::PerfScope scope(stats);
    log_call("xrEndFrame");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (frameEndInfo == nullptr || frameEndInfo->type != XR_TYPE_FRAME_END_INFO) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    if (g_sessionState != XR_SESSION_STATE_FOCUSED) {
        return XR_ERROR_SESSION_NOT_RUNNING;
    }
    return submit_projection_frame(*frameEndInfo);
}

XrResult XRAPI_CALL xrEnumerateDisplayRefreshRatesFB_impl(XrSession session, uint32_t capacity, uint32_t* count, float* rates) {
    if (!is_valid_session(session)) return XR_ERROR_HANDLE_INVALID;
    if (!count || (capacity && !rates)) return XR_ERROR_VALIDATION_FAILURE;
    *count = 1;
    if (capacity) rates[0] = 1'000'000'000.0f / axrb::protocol::display_period_or_default(pose_client().latest_pose_frame());
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrGetDisplayRefreshRateFB_impl(XrSession session, float* rate) {
    if (!is_valid_session(session)) return XR_ERROR_HANDLE_INVALID;
    if (!rate) return XR_ERROR_VALIDATION_FAILURE;
    *rate = 1'000'000'000.0f / axrb::protocol::display_period_or_default(pose_client().latest_pose_frame());
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrRequestDisplayRefreshRateFB_impl(XrSession session, float rate) {
    if (!is_valid_session(session)) return XR_ERROR_HANDLE_INVALID;
    const float activeRate = 1'000'000'000.0f / axrb::protocol::display_period_or_default(pose_client().latest_pose_frame());
    return rate == 0.0f || std::abs(rate - activeRate) < 0.01f ? XR_SUCCESS : static_cast<XrResult>(-1000101000);
}


} // namespace axrb::runtime::detail
