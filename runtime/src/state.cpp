#include "runtime_internal.h"

namespace axrb::runtime::detail {

RuntimeHandle g_instanceHandle{0xAABBCCDD00000001ULL};
RuntimeHandle g_sessionHandle{0xAABBCCDD00000002ULL};
RuntimeHandle g_spaceHandle{0xAABBCCDD00000003ULL};
RuntimeHandle g_actionSetHandle{0xAABBCCDD00000005ULL};
std::deque<ActionRecord> g_actionHandles;
std::deque<HandTrackerRecord> g_handTrackers;
XrSessionState g_sessionState = XR_SESSION_STATE_UNKNOWN;
std::deque<XrSessionState> g_pendingSessionEvents;
bool g_pendingInteractionProfileEvent = false;
std::deque<SpaceRecord> g_spaces;
uint32_t g_spaceCount = 0;
std::deque<PathRecord> g_paths;
uint32_t g_pathCount = 0;
std::deque<SwapchainRecord> g_swapchains{};
SwapchainRecord* g_lastReleasedSwapchain = nullptr;
uint32_t g_actionCount = 0;
uint64_t g_nextPath = 1;
uint64_t g_imageFrameSequence = 0;
XrTime g_nextFrameStart = 0;
uint32_t g_renderWidth = 1024, g_renderHeight = 1024;
bool g_renderExtentQueried = false;
axrb::protocol::PoseFrame g_lastViewPoseFrame{};
#if defined(__ANDROID__)
VulkanBackend g_vulkan;
bool g_vulkanRequirementsQueried = false;
VkInstance g_vulkanInstance = VK_NULL_HANDLE;
#endif

void query_render_extent() {
    if (g_renderExtentQueried) return;
    g_renderExtentQueried = true;
    // View enumeration precedes the first frame. Allow the initial nonblocking
    // pose connection to deliver the host configuration before the app allocates.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    do {
        const auto frame = pose_client().latest_pose_frame();
        if (axrb::protocol::valid_render_extent(frame.render_width, frame.render_height)) {
            g_renderWidth = frame.render_width; g_renderHeight = frame.render_height;
            break;
        }
#if defined(__ANDROID__)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
#else
        break;
#endif
    } while (std::chrono::steady_clock::now() < deadline);
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO, "AXRB.GPU", "recommended eye extent %ux%u", g_renderWidth, g_renderHeight);
#endif
}
void log_call(const char* name)
{
#if defined(__ANDROID__) && defined(NDEBUG)
    (void)name; // Keep release frame loops free of per-entry-point log traffic.
#elif defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO, "AXRB.Runtime", "%s", name);
#else
    std::fprintf(stderr, "AXRB.Runtime: %s\n", name);
#endif
}

void log_proc_request(const char* name)
{
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO, "AXRB.Runtime", "xrGetInstanceProcAddr(%s)", name);
#else
    std::fprintf(stderr, "AXRB.Runtime: xrGetInstanceProcAddr(%s)\n", name);
#endif
}

XrInstance fake_instance()
{
    return reinterpret_cast<XrInstance>(&g_instanceHandle);
}

XrSession fake_session()
{
    return reinterpret_cast<XrSession>(&g_sessionHandle);
}

XrSpace fake_space()
{
    return reinterpret_cast<XrSpace>(&g_spaceHandle);
}

XrPosef identity_pose()
{
    XrPosef pose{};
    pose.orientation.w = 1.0f;
    return pose;
}

XrSpace make_space(SpaceKind kind, const XrPosef& offsetInParent)
{
    g_spaces.emplace_back();
    SpaceRecord& record = g_spaces[g_spaceCount];
    record.handle.magic = 0xAABBCCDD00000200ULL + g_spaceCount;
    record.kind = kind;
    record.offsetInParent = offsetInParent;
    ++g_spaceCount;
    return reinterpret_cast<XrSpace>(&record.handle);
}

SpaceRecord* find_space(XrSpace space)
{
    for (uint32_t i = 0; i < g_spaceCount; ++i) {
        if (space == reinterpret_cast<XrSpace>(&g_spaces[i].handle) && g_spaces[i].handle.magic != 0) {
            return &g_spaces[i];
        }
    }
    if (space == fake_space()) {
        static SpaceRecord legacy{};
        legacy.handle = g_spaceHandle;
        legacy.kind = SpaceKind::Reference;
        legacy.offsetInParent = identity_pose();
        return &legacy;
    }
    return nullptr;
}

XrActionSet fake_action_set()
{
    return reinterpret_cast<XrActionSet>(&g_actionSetHandle);
}

XrAction fake_action(uint32_t index)
{
    return reinterpret_cast<XrAction>(&g_actionHandles[index]);
}

bool is_valid_instance(XrInstance instance)
{
    return instance == fake_instance();
}

bool is_valid_session(XrSession session)
{
    return session == fake_session();
}

SwapchainRecord* find_swapchain(XrSwapchain handle)
{
    for (auto& record : g_swapchains) {
        if (record.created && handle == reinterpret_cast<XrSwapchain>(&record)) { return &record; }
    }
    return nullptr;
}

bool is_valid_action_set(XrActionSet actionSet)
{
    return actionSet == fake_action_set();
}

bool is_valid_action(XrAction action)
{
    for (uint32_t i = 0; i < g_actionCount; ++i) {
        if (action == fake_action(i) && g_actionHandles[i].magic != 0) {
            return true;
        }
    }
    return false;
}

const char* path_text(XrPath path)
{
    for (uint32_t i = 0; i < g_pathCount; ++i) {
        if (g_paths[i].path == path) {
            return g_paths[i].text.c_str();
        }
    }
    return "";
}

SpaceKind action_space_kind(XrPath subactionPath)
{
    const std::string_view text{path_text(subactionPath)};
    if (text == "/user/hand/left") {
        return SpaceKind::LeftHand;
    }
    if (text == "/user/hand/right") {
        return SpaceKind::RightHand;
    }
    return SpaceKind::Reference;
}

XrPosef protocol_pose_to_xr(const axrb::protocol::Pose& pose)
{
    XrPosef out{};
    out.orientation.x = pose.qx;
    out.orientation.y = pose.qy;
    out.orientation.z = pose.qz;
    out.orientation.w = pose.qw;
    out.position.x = pose.x;
    out.position.y = pose.y;
    out.position.z = pose.z;
    return out;
}

XrPosef normalize_pose(XrPosef pose)
{
    const float length = std::sqrt(
        pose.orientation.x * pose.orientation.x +
        pose.orientation.y * pose.orientation.y +
        pose.orientation.z * pose.orientation.z +
        pose.orientation.w * pose.orientation.w);
    if (length > 0.00001f) {
        pose.orientation.x /= length;
        pose.orientation.y /= length;
        pose.orientation.z /= length;
        pose.orientation.w /= length;
    } else {
        pose.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    }
    return pose;
}

XrPosef multiply_pose(const XrPosef& a, const XrPosef& b)
{
    XrPosef out{};
    const float ax = a.orientation.x;
    const float ay = a.orientation.y;
    const float az = a.orientation.z;
    const float aw = a.orientation.w;
    const float bx = b.orientation.x;
    const float by = b.orientation.y;
    const float bz = b.orientation.z;
    const float bw = b.orientation.w;

    out.orientation.x = aw * bx + ax * bw + ay * bz - az * by;
    out.orientation.y = aw * by - ax * bz + ay * bw + az * bx;
    out.orientation.z = aw * bz + ax * by - ay * bx + az * bw;
    out.orientation.w = aw * bw - ax * bx - ay * by - az * bz;

    const float vx = b.position.x;
    const float vy = b.position.y;
    const float vz = b.position.z;
    const float tx = 2.0f * (ay * vz - az * vy);
    const float ty = 2.0f * (az * vx - ax * vz);
    const float tz = 2.0f * (ax * vy - ay * vx);
    const float rx = vx + aw * tx + (ay * tz - az * ty);
    const float ry = vy + aw * ty + (az * tx - ax * tz);
    const float rz = vz + aw * tz + (ax * ty - ay * tx);

    out.position.x = a.position.x + rx;
    out.position.y = a.position.y + ry;
    out.position.z = a.position.z + rz;
    return normalize_pose(out);
}

XrPosef inverse_pose(const XrPosef& pose)
{
    XrPosef inverse{};
    inverse.orientation.x = -pose.orientation.x;
    inverse.orientation.y = -pose.orientation.y;
    inverse.orientation.z = -pose.orientation.z;
    inverse.orientation.w = pose.orientation.w;

    const float px = -pose.position.x;
    const float py = -pose.position.y;
    const float pz = -pose.position.z;
    const float qx = inverse.orientation.x;
    const float qy = inverse.orientation.y;
    const float qz = inverse.orientation.z;
    const float qw = inverse.orientation.w;
    const float tx = 2.0f * (qy * pz - qz * py);
    const float ty = 2.0f * (qz * px - qx * pz);
    const float tz = 2.0f * (qx * py - qy * px);
    inverse.position.x = px + qw * tx + (qy * tz - qz * ty);
    inverse.position.y = py + qw * ty + (qz * tx - qx * tz);
    inverse.position.z = pz + qw * tz + (qx * ty - qy * tx);
    return normalize_pose(inverse);
}

XrPosef world_pose_for_space(const SpaceRecord& record, const axrb::protocol::PoseFrame& poseFrame)
{
    XrPosef base = identity_pose();
    switch (record.kind) {
    case SpaceKind::Local:
        if (poseFrame.version >= 5 && (poseFrame.local_origin_flags & 3) == 3)
            base = protocol_pose_to_xr(poseFrame.local_origin);
        break;
    case SpaceKind::View:
        base = protocol_pose_to_xr(poseFrame.hmd);
        break;
    case SpaceKind::LeftHand:
        base = protocol_pose_to_xr(poseFrame.left_controller);
        break;
    case SpaceKind::RightHand:
        base = protocol_pose_to_xr(poseFrame.right_controller);
        break;
    case SpaceKind::LeftAim:
        base = protocol_pose_to_xr(poseFrame.version >= 3 ? poseFrame.aim[0] : poseFrame.left_controller);
        break;
    case SpaceKind::RightAim:
        base = protocol_pose_to_xr(poseFrame.version >= 3 ? poseFrame.aim[1] : poseFrame.right_controller);
        break;
    case SpaceKind::Reference:
    default:
        base = identity_pose();
        break;
    }
    return multiply_pose(base, record.offsetInParent);
}

XrTime monotonic_time_ns()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
}

#if defined(__ANDROID__) || defined(AXRB_INPUT_FIXTURE)
// Android's steady_clock and this runtime's XrTime both use CLOCK_MONOTONIC.
XrResult XRAPI_CALL xrConvertTimespecTimeToTimeKHR_impl(XrInstance instance, const timespec* source, XrTime* time)
{
    if (!is_valid_instance(instance)) return XR_ERROR_HANDLE_INVALID;
    if (!source || !time) return XR_ERROR_VALIDATION_FAILURE;
    constexpr int64_t billion = 1000000000;
    if (source->tv_sec < 0 || source->tv_nsec < 0 || source->tv_nsec >= billion ||
        source->tv_sec > (std::numeric_limits<XrTime>::max() - source->tv_nsec) / billion)
        return XR_ERROR_TIME_INVALID;
    const XrTime value = static_cast<XrTime>(source->tv_sec) * billion + source->tv_nsec;
    if (value <= 0) return XR_ERROR_TIME_INVALID;
    *time = value;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrConvertTimeToTimespecTimeKHR_impl(XrInstance instance, XrTime time, timespec* target)
{
    if (!is_valid_instance(instance)) return XR_ERROR_HANDLE_INVALID;
    if (!target) return XR_ERROR_VALIDATION_FAILURE;
    if (time <= 0 || time / 1000000000 > std::numeric_limits<decltype(target->tv_sec)>::max())
        return XR_ERROR_TIME_INVALID;
    target->tv_sec = static_cast<decltype(target->tv_sec)>(time / 1000000000);
    target->tv_nsec = static_cast<decltype(target->tv_nsec)>(time % 1000000000);
    return XR_SUCCESS;
}
#endif

void queue_session_state(XrSessionState state)
{
    g_sessionState = state;
    g_pendingSessionEvents.push_back(state);
}


} // namespace axrb::runtime::detail
