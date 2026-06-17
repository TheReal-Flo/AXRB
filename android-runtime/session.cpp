#include "session.h"

#include "image_frame.h"
#include "openxr_dispatch/openxr_minimal.h"
#include "pose_client.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <deque>
#include <string_view>
#include <vector>

#if defined(__ANDROID__)
#include <android/log.h>
#include <GLES3/gl3.h>
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace axrb::runtime {

namespace {

constexpr XrSystemId kSystemId = 1;

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

RuntimeHandle g_instanceHandle{0xAABBCCDD00000001ULL};
RuntimeHandle g_sessionHandle{0xAABBCCDD00000002ULL};
RuntimeHandle g_spaceHandle{0xAABBCCDD00000003ULL};
RuntimeHandle g_swapchainHandle{0xAABBCCDD00000004ULL};
RuntimeHandle g_actionSetHandle{0xAABBCCDD00000005ULL};
RuntimeHandle g_actionHandles[8] = {
    {0xAABBCCDD00000100ULL},
    {0xAABBCCDD00000101ULL},
    {0xAABBCCDD00000102ULL},
    {0xAABBCCDD00000103ULL},
    {0xAABBCCDD00000104ULL},
    {0xAABBCCDD00000105ULL},
    {0xAABBCCDD00000106ULL},
    {0xAABBCCDD00000107ULL},
};
XrSessionState g_sessionState = XR_SESSION_STATE_UNKNOWN;
std::deque<XrSessionState> g_pendingSessionEvents;

enum class SpaceKind {
    Reference,
    View,
    LeftHand,
    RightHand,
};

struct SpaceRecord {
    RuntimeHandle handle{};
    SpaceKind kind = SpaceKind::Reference;
    XrPosef offsetInParent{};
};

struct PathRecord {
    XrPath path = XR_NULL_PATH;
    char text[128]{};
};

std::array<SpaceRecord, 16> g_spaces{};
uint32_t g_spaceCount = 0;
std::array<PathRecord, 64> g_paths{};
uint32_t g_pathCount = 0;
bool g_swapchainCreated = false;
bool g_swapchainImageAcquired = false;
bool g_swapchainImageWaited = false;
uint32_t g_nextSwapchainImage = 0;
uint32_t g_swapchainWidth = 0;
uint32_t g_swapchainHeight = 0;
uint32_t g_swapchainArraySize = 0;
int64_t g_swapchainFormat = 0;
uint32_t g_glSwapchainTextures[3] = {};
uint32_t g_currentSwapchainImage = 0;
uint32_t g_lastReleasedSwapchainImage = 0;
uint32_t g_actionCount = 0;
uint64_t g_nextPath = 1;
uint64_t g_imageFrameSequence = 0;

void log_call(const char* name)
{
#if defined(__ANDROID__)
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

XrSpace make_space(SpaceKind kind, const XrPosef& offsetInParent = identity_pose())
{
    if (g_spaceCount >= g_spaces.size()) {
        return nullptr;
    }
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
        if (space == reinterpret_cast<XrSpace>(&g_spaces[i].handle)) {
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

XrSwapchain fake_swapchain()
{
    return reinterpret_cast<XrSwapchain>(&g_swapchainHandle);
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

bool is_valid_swapchain(XrSwapchain swapchain)
{
    return g_swapchainCreated && swapchain == fake_swapchain();
}

bool is_valid_action_set(XrActionSet actionSet)
{
    return actionSet == fake_action_set();
}

bool is_valid_action(XrAction action)
{
    for (uint32_t i = 0; i < g_actionCount; ++i) {
        if (action == fake_action(i)) {
            return true;
        }
    }
    return false;
}

const char* path_text(XrPath path)
{
    for (uint32_t i = 0; i < g_pathCount; ++i) {
        if (g_paths[i].path == path) {
            return g_paths[i].text;
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
    case SpaceKind::View:
        base = protocol_pose_to_xr(poseFrame.hmd);
        break;
    case SpaceKind::LeftHand:
        base = protocol_pose_to_xr(poseFrame.left_controller);
        break;
    case SpaceKind::RightHand:
        base = protocol_pose_to_xr(poseFrame.right_controller);
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

void queue_session_state(XrSessionState state)
{
    g_sessionState = state;
    g_pendingSessionEvents.push_back(state);
}

void destroy_swapchain_images()
{
#if defined(__ANDROID__)
    if (g_glSwapchainTextures[0] != 0 || g_glSwapchainTextures[1] != 0 || g_glSwapchainTextures[2] != 0) {
        glDeleteTextures(3, g_glSwapchainTextures);
    }
#endif
    g_glSwapchainTextures[0] = 0;
    g_glSwapchainTextures[1] = 0;
    g_glSwapchainTextures[2] = 0;
}

void create_opengles_swapchain_images(const XrSwapchainCreateInfo& createInfo)
{
    destroy_swapchain_images();
#if defined(__ANDROID__)
    GLenum internalFormat = static_cast<GLenum>(createInfo.format);
    if (internalFormat != GL_RGBA8 && internalFormat != GL_SRGB8_ALPHA8) {
        internalFormat = GL_RGBA8;
    }

    const GLenum target = createInfo.arraySize > 1 ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;
    glGenTextures(3, g_glSwapchainTextures);
    for (uint32_t i = 0; i < 3; ++i) {
        glBindTexture(target, g_glSwapchainTextures[i]);
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (target == GL_TEXTURE_2D_ARRAY) {
            glTexStorage3D(
                target,
                static_cast<GLsizei>(createInfo.mipCount),
                internalFormat,
                static_cast<GLsizei>(createInfo.width),
                static_cast<GLsizei>(createInfo.height),
                static_cast<GLsizei>(createInfo.arraySize));
        } else {
            glTexStorage2D(
                target,
                static_cast<GLsizei>(createInfo.mipCount),
                internalFormat,
                static_cast<GLsizei>(createInfo.width),
                static_cast<GLsizei>(createInfo.height));
        }
    }
    glBindTexture(target, 0);
#else
    (void)createInfo;
#endif
}

#if defined(__ANDROID__)
class ImageTransportClient {
public:
    ~ImageTransportClient()
    {
        close_socket();
    }

    bool send_frame(
        uint64_t sequence,
        uint32_t width,
        uint32_t height,
        uint32_t layers,
        const uint8_t* payload,
        uint64_t payloadSize)
    {
        if (!ensure_connected()) {
            if (!reportedSendSkip_) {
                __android_log_print(ANDROID_LOG_INFO, "AXRB.Image", "send skipped: no TCP connection");
                reportedSendSkip_ = true;
            }
            return false;
        }

        axrb::protocol::ImageFrameHeader header{};
        header.width = width;
        header.height = height;
        header.layers = layers;
        header.sequence = sequence;
        header.monotonic_time_ns = static_cast<uint64_t>(monotonic_time_ns());
        header.payload_size = payloadSize;

        if (!send_all(&header, sizeof(header)) || !send_all(payload, static_cast<size_t>(payloadSize))) {
            if (!reportedSendFailure_) {
                __android_log_print(ANDROID_LOG_INFO, "AXRB.Image", "send failed");
                reportedSendFailure_ = true;
            }
            close_socket();
            return false;
        }
        return true;
    }

private:
    bool ensure_connected()
    {
        if (socket_ >= 0) {
            return true;
        }

        const XrTime now = monotonic_time_ns();
        if (now - lastConnectAttemptNs_ < 2'000'000'000LL) {
            return false;
        }
        lastConnectAttemptNs_ = now;

        int candidate = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (candidate < 0) {
            lastError_ = errno;
            return false;
        }

        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        constexpr const char* kSocketName = "axrb_image_proxy";
        address.sun_path[0] = '\0';
        std::strncpy(address.sun_path + 1, kSocketName, sizeof(address.sun_path) - 2);
        const socklen_t addressLength =
            static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + 1 + std::strlen(kSocketName));

        if (::connect(candidate, reinterpret_cast<sockaddr*>(&address), addressLength) == 0) {
            int bufferSize = 1024 * 1024;
            setsockopt(candidate, SOL_SOCKET, SO_SNDBUF, &bufferSize, sizeof(bufferSize));
            socket_ = candidate;
            __android_log_print(ANDROID_LOG_INFO, "AXRB.Image", "connected to local image proxy");
            return true;
        }
        lastError_ = errno;
        __android_log_print(ANDROID_LOG_INFO, "AXRB.Image", "local image proxy connect failed errno=%d", lastError_);
        ::close(candidate);

        if (!reportedConnectFailure_) {
            __android_log_print(ANDROID_LOG_INFO, "AXRB.Image", "connect failed: errno=%d", lastError_);
            reportedConnectFailure_ = true;
        }
        return false;
    }

    bool send_all(const void* data, size_t size)
    {
        const uint8_t* cursor = static_cast<const uint8_t*>(data);
        size_t remaining = size;
        while (remaining > 0) {
            const ssize_t sent = ::send(socket_, cursor, remaining, 0);
            if (sent <= 0) {
                return false;
            }
            cursor += sent;
            remaining -= static_cast<size_t>(sent);
        }
        return true;
    }

    void close_socket()
    {
        if (socket_ >= 0) {
            ::close(socket_);
            socket_ = -1;
        }
    }

    int socket_ = -1;
    int lastError_ = 0;
    XrTime lastConnectAttemptNs_ = 0;
    bool reportedConnectFailure_ = false;
    bool reportedSendSkip_ = false;
    bool reportedSendFailure_ = false;
};

ImageTransportClient& image_transport_client()
{
    static ImageTransportClient client;
    return client;
}

void maybe_send_swapchain_image()
{
    static bool reportedEntry = false;
    static bool reportedMissingSwapchain = false;
    static bool reportedOversize = false;
    static bool reportedFramebufferFailure = false;
    static bool reportedReadFailure = false;
    static bool reportedReadback = false;
    if (!reportedEntry) {
        __android_log_print(ANDROID_LOG_INFO, "AXRB.Image", "image transport hook entered");
        reportedEntry = true;
    }
    if (!g_swapchainCreated || g_swapchainWidth == 0 || g_swapchainHeight == 0 || g_swapchainArraySize == 0) {
        if (!reportedMissingSwapchain) {
            __android_log_print(ANDROID_LOG_INFO, "AXRB.Image", "skip: no swapchain image available");
            reportedMissingSwapchain = true;
        }
        return;
    }
    if (g_glSwapchainTextures[g_lastReleasedSwapchainImage] == 0) {
        if (!reportedMissingSwapchain) {
            __android_log_print(ANDROID_LOG_INFO, "AXRB.Image", "skip: swapchain texture is zero");
            reportedMissingSwapchain = true;
        }
        return;
    }

    const uint64_t sequence = g_imageFrameSequence++;
    if (g_swapchainWidth > 4096 || g_swapchainHeight > 4096 || g_swapchainArraySize > 4) {
        if (!reportedOversize) {
            __android_log_print(
                ANDROID_LOG_INFO,
                "AXRB.Image",
                "skip: oversized image %ux%u layers=%u",
                g_swapchainWidth,
                g_swapchainHeight,
                g_swapchainArraySize);
            reportedOversize = true;
        }
        return;
    }

    constexpr uint32_t kMaxTransportDimension = 512;
    const uint32_t transportWidth =
        g_swapchainWidth > kMaxTransportDimension ? kMaxTransportDimension : g_swapchainWidth;
    const uint32_t transportHeight =
        g_swapchainHeight > kMaxTransportDimension ? kMaxTransportDimension : g_swapchainHeight;
    const uint64_t layerBytes = static_cast<uint64_t>(transportWidth) * transportHeight * 4;
    const uint64_t payloadBytes = layerBytes * g_swapchainArraySize;
    if (payloadBytes > 128ull * 1024ull * 1024ull) {
        return;
    }

    GLint previousReadFramebuffer = 0;
    GLint previousDrawFramebuffer = 0;
    GLint previousPackAlignment = 4;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFramebuffer);
    glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);

    static GLuint sourceFramebuffer = 0;
    static GLuint transportFramebuffer = 0;
    static GLuint transportTexture = 0;
    static uint32_t transportTextureWidth = 0;
    static uint32_t transportTextureHeight = 0;
    static std::vector<uint8_t> payload;

    GLint previousTexture = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    if (sourceFramebuffer == 0) {
        glGenFramebuffers(1, &sourceFramebuffer);
    }
    if (transportFramebuffer == 0) {
        glGenFramebuffers(1, &transportFramebuffer);
    }
    if (transportTexture == 0 ||
        transportTextureWidth != transportWidth ||
        transportTextureHeight != transportHeight) {
        if (transportTexture != 0) {
            glDeleteTextures(1, &transportTexture);
            transportTexture = 0;
        }
        glGenTextures(1, &transportTexture);
        glBindTexture(GL_TEXTURE_2D, transportTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA8,
            static_cast<GLsizei>(transportWidth),
            static_cast<GLsizei>(transportHeight),
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            nullptr);
        transportTextureWidth = transportWidth;
        transportTextureHeight = transportHeight;
    }
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));

    if (sourceFramebuffer == 0 || transportFramebuffer == 0 || transportTexture == 0) {
        return;
    }

    payload.resize(static_cast<size_t>(payloadBytes));
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    bool ok = true;
    for (uint32_t layer = 0; layer < g_swapchainArraySize; ++layer) {
        while (glGetError() != GL_NO_ERROR) {
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER, sourceFramebuffer);
        if (g_swapchainArraySize > 1) {
            glFramebufferTextureLayer(
                GL_READ_FRAMEBUFFER,
                GL_COLOR_ATTACHMENT0,
                g_glSwapchainTextures[g_lastReleasedSwapchainImage],
                0,
                static_cast<GLint>(layer));
        } else {
            glFramebufferTexture2D(
                GL_READ_FRAMEBUFFER,
                GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_2D,
                g_glSwapchainTextures[g_lastReleasedSwapchainImage],
                0);
        }

        if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            if (!reportedFramebufferFailure) {
                __android_log_print(
                    ANDROID_LOG_INFO,
                    "AXRB.Image",
                    "skip: framebuffer incomplete for tex=%u layer=%u status=0x%x",
                    g_glSwapchainTextures[g_lastReleasedSwapchainImage],
                    layer,
                    glCheckFramebufferStatus(GL_READ_FRAMEBUFFER));
                reportedFramebufferFailure = true;
            }
            ok = false;
            break;
        }

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, transportFramebuffer);
        glFramebufferTexture2D(
            GL_DRAW_FRAMEBUFFER,
            GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_2D,
            transportTexture,
            0);
        if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            ok = false;
            break;
        }
        glBlitFramebuffer(
            0,
            0,
            static_cast<GLint>(g_swapchainWidth),
            static_cast<GLint>(g_swapchainHeight),
            0,
            0,
            static_cast<GLint>(transportWidth),
            static_cast<GLint>(transportHeight),
            GL_COLOR_BUFFER_BIT,
            GL_LINEAR);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, transportFramebuffer);
        glReadPixels(
            0,
            0,
            static_cast<GLsizei>(transportWidth),
            static_cast<GLsizei>(transportHeight),
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            payload.data() + static_cast<size_t>(layerBytes * layer));
        const GLenum readError = glGetError();
        if (readError != GL_NO_ERROR) {
            if (!reportedReadFailure) {
                __android_log_print(
                    ANDROID_LOG_INFO,
                    "AXRB.Image",
                    "skip: glReadPixels failed err=0x%x tex=%u layer=%u size=%ux%u",
                    readError,
                    g_glSwapchainTextures[g_lastReleasedSwapchainImage],
                    layer,
                    transportWidth,
                    transportHeight);
                reportedReadFailure = true;
            }
            ok = false;
            break;
        }
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousReadFramebuffer));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previousDrawFramebuffer));
    glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);

    if (!ok) {
        return;
    }

    if (!reportedReadback) {
        __android_log_print(
            ANDROID_LOG_INFO,
            "AXRB.Image",
            "readback ok seq=%llu %ux%u layers=%u bytes=%llu",
            static_cast<unsigned long long>(sequence),
            transportWidth,
            transportHeight,
            g_swapchainArraySize,
            static_cast<unsigned long long>(payloadBytes));
        reportedReadback = true;
    }

    if (image_transport_client().send_frame(
            sequence,
            transportWidth,
            transportHeight,
            g_swapchainArraySize,
            payload.data(),
            payloadBytes) && sequence % 150 == 0) {
        __android_log_print(
            ANDROID_LOG_INFO,
            "AXRB.Image",
            "sent frame seq=%llu %ux%u layers=%u bytes=%llu",
            static_cast<unsigned long long>(sequence),
            transportWidth,
            transportHeight,
            g_swapchainArraySize,
            static_cast<unsigned long long>(payloadBytes));
    }
}
#else
void maybe_send_swapchain_image() {}
#endif

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
    g_spaceCount = 0;
    g_pathCount = 0;
    g_nextPath = 1;
    g_swapchainCreated = false;
    g_swapchainImageAcquired = false;
    g_swapchainImageWaited = false;
    destroy_swapchain_images();
    g_nextSwapchainImage = 0;
    g_imageFrameSequence = 0;
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
    return is_valid_instance(instance) ? XR_SUCCESS : XR_ERROR_HANDLE_INVALID;
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
    properties->graphicsProperties.maxSwapchainImageHeight = 4096;
    properties->graphicsProperties.maxSwapchainImageWidth = 4096;
    properties->graphicsProperties.maxLayerCount = 16;
    properties->trackingProperties.orientationTracking = 1;
    properties->trackingProperties.positionTracking = 1;
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
    return XR_SUCCESS;
}

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

    *session = fake_session();
    g_pendingSessionEvents.clear();
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
    g_swapchainCreated = false;
    g_swapchainImageAcquired = false;
    g_swapchainImageWaited = false;
    destroy_swapchain_images();
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

XrResult XRAPI_CALL xrEnumerateSwapchainFormats_impl(
    XrSession session,
    uint32_t formatCapacityInput,
    uint32_t* formatCountOutput,
    int64_t* formats)
{
    log_call("xrEnumerateSwapchainFormats");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (formatCountOutput == nullptr) {
        return XR_ERROR_VALIDATION_FAILURE;
    }

    constexpr int64_t kFormats[] = {
        0x8058, // GL_RGBA8
        0x8C43, // GL_SRGB8_ALPHA8
        37, // VK_FORMAT_R8G8B8A8_UNORM
        43, // VK_FORMAT_B8G8R8A8_UNORM
    };

    *formatCountOutput = static_cast<uint32_t>(sizeof(kFormats) / sizeof(kFormats[0]));
    if (formatCapacityInput > 0 && formats != nullptr) {
        const uint32_t count = formatCapacityInput < *formatCountOutput ? formatCapacityInput : *formatCountOutput;
        for (uint32_t i = 0; i < count; ++i) {
            formats[i] = kFormats[i];
        }
    }
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrCreateSwapchain_impl(
    XrSession session,
    const XrSwapchainCreateInfo* createInfo,
    XrSwapchain* swapchain)
{
    log_call("xrCreateSwapchain");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (createInfo == nullptr || swapchain == nullptr || createInfo->type != XR_TYPE_SWAPCHAIN_CREATE_INFO) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    if (createInfo->width == 0 || createInfo->height == 0 ||
        createInfo->sampleCount == 0 || createInfo->faceCount == 0 ||
        createInfo->arraySize == 0 || createInfo->mipCount == 0) {
        return XR_ERROR_VALIDATION_FAILURE;
    }

    g_swapchainCreated = true;
    g_swapchainImageAcquired = false;
    g_swapchainImageWaited = false;
    g_nextSwapchainImage = 0;
    g_currentSwapchainImage = 0;
    g_lastReleasedSwapchainImage = 0;
    g_swapchainWidth = createInfo->width;
    g_swapchainHeight = createInfo->height;
    g_swapchainArraySize = createInfo->arraySize;
    g_swapchainFormat = createInfo->format;
    create_opengles_swapchain_images(*createInfo);
    *swapchain = fake_swapchain();
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrDestroySwapchain_impl(XrSwapchain swapchain)
{
    log_call("xrDestroySwapchain");
    if (!is_valid_swapchain(swapchain)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    g_swapchainCreated = false;
    g_swapchainImageAcquired = false;
    g_swapchainImageWaited = false;
    destroy_swapchain_images();
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrEnumerateSwapchainImages_impl(
    XrSwapchain swapchain,
    uint32_t imageCapacityInput,
    uint32_t* imageCountOutput,
    XrSwapchainImageBaseHeader* images)
{
    log_call("xrEnumerateSwapchainImages");
    if (!is_valid_swapchain(swapchain)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (imageCountOutput == nullptr) {
        return XR_ERROR_VALIDATION_FAILURE;
    }

    *imageCountOutput = 3;
    if (imageCapacityInput > 0 && images != nullptr) {
        const uint32_t count = imageCapacityInput < *imageCountOutput ? imageCapacityInput : *imageCountOutput;
        if (images[0].type == XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR) {
            auto* glImages = reinterpret_cast<XrSwapchainImageOpenGLESKHR*>(images);
            for (uint32_t i = 0; i < count; ++i) {
                if (glImages[i].type != XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR) {
                    return XR_ERROR_VALIDATION_FAILURE;
                }
                glImages[i].image = g_glSwapchainTextures[i];
            }
        } else if (images[0].type == XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR) {
            auto* vkImages = reinterpret_cast<XrSwapchainImageVulkanKHR*>(images);
            for (uint32_t i = 0; i < count; ++i) {
                if (vkImages[i].type != XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR) {
                    return XR_ERROR_VALIDATION_FAILURE;
                }
                vkImages[i].image = 0;
            }
        } else {
            return XR_ERROR_VALIDATION_FAILURE;
        }
    }
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrAcquireSwapchainImage_impl(
    XrSwapchain swapchain,
    const XrSwapchainImageAcquireInfo* acquireInfo,
    uint32_t* index)
{
    log_call("xrAcquireSwapchainImage");
    if (!is_valid_swapchain(swapchain)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (acquireInfo == nullptr || index == nullptr || acquireInfo->type != XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    if (g_swapchainImageAcquired) {
        return XR_ERROR_CALL_ORDER_INVALID;
    }

    *index = g_nextSwapchainImage;
    g_currentSwapchainImage = *index;
    g_nextSwapchainImage = (g_nextSwapchainImage + 1) % 3;
    g_swapchainImageAcquired = true;
    g_swapchainImageWaited = false;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrWaitSwapchainImage_impl(
    XrSwapchain swapchain,
    const XrSwapchainImageWaitInfo* waitInfo)
{
    log_call("xrWaitSwapchainImage");
    if (!is_valid_swapchain(swapchain)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (waitInfo == nullptr || waitInfo->type != XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    if (!g_swapchainImageAcquired) {
        return XR_ERROR_CALL_ORDER_INVALID;
    }

    g_swapchainImageWaited = true;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrReleaseSwapchainImage_impl(
    XrSwapchain swapchain,
    const XrSwapchainImageReleaseInfo* releaseInfo)
{
    log_call("xrReleaseSwapchainImage");
    if (!is_valid_swapchain(swapchain)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (releaseInfo == nullptr || releaseInfo->type != XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    if (!g_swapchainImageAcquired || !g_swapchainImageWaited) {
        return XR_ERROR_CALL_ORDER_INVALID;
    }

    g_swapchainImageAcquired = false;
    g_swapchainImageWaited = false;
    g_lastReleasedSwapchainImage = g_currentSwapchainImage;
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
    if (viewCapacityInput > 0 && views != nullptr) {
        const uint32_t count = viewCapacityInput < 2 ? viewCapacityInput : 2;
        for (uint32_t i = 0; i < count; ++i) {
            if (views[i].type != XR_TYPE_VIEW_CONFIGURATION_VIEW) {
                return XR_ERROR_VALIDATION_FAILURE;
            }
            views[i].recommendedImageRectWidth = 1024;
            views[i].maxImageRectWidth = 1024;
            views[i].recommendedImageRectHeight = 1024;
            views[i].maxImageRectHeight = 1024;
            views[i].recommendedSwapchainSampleCount = 1;
            views[i].maxSwapchainSampleCount = 1;
        }
    }
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrEnumerateReferenceSpaces_impl(
    XrSession session,
    uint32_t spaceCapacityInput,
    uint32_t* spaceCountOutput,
    XrReferenceSpaceType* spaces)
{
    log_call("xrEnumerateReferenceSpaces");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (spaceCountOutput == nullptr) {
        return XR_ERROR_VALIDATION_FAILURE;
    }

    constexpr XrReferenceSpaceType kSpaces[] = {
        XR_REFERENCE_SPACE_TYPE_VIEW,
        XR_REFERENCE_SPACE_TYPE_LOCAL,
        XR_REFERENCE_SPACE_TYPE_STAGE,
    };

    *spaceCountOutput = static_cast<uint32_t>(sizeof(kSpaces) / sizeof(kSpaces[0]));
    if (spaceCapacityInput > 0 && spaces != nullptr) {
        const uint32_t count = spaceCapacityInput < *spaceCountOutput ? spaceCapacityInput : *spaceCountOutput;
        for (uint32_t i = 0; i < count; ++i) {
            spaces[i] = kSpaces[i];
        }
    }
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrCreateReferenceSpace_impl(
    XrSession session,
    const XrReferenceSpaceCreateInfo* createInfo,
    XrSpace* space)
{
    log_call("xrCreateReferenceSpace");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (createInfo == nullptr || space == nullptr || createInfo->type != XR_TYPE_REFERENCE_SPACE_CREATE_INFO) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    if (createInfo->referenceSpaceType != XR_REFERENCE_SPACE_TYPE_VIEW &&
        createInfo->referenceSpaceType != XR_REFERENCE_SPACE_TYPE_LOCAL &&
        createInfo->referenceSpaceType != XR_REFERENCE_SPACE_TYPE_STAGE) {
        return XR_ERROR_REFERENCE_SPACE_UNSUPPORTED;
    }

    *space = make_space(
        createInfo->referenceSpaceType == XR_REFERENCE_SPACE_TYPE_VIEW ? SpaceKind::View : SpaceKind::Reference,
        createInfo->poseInReferenceSpace);
    if (*space == nullptr) {
        return XR_ERROR_OUT_OF_MEMORY;
    }
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrCreateActionSet_impl(
    XrInstance instance,
    const XrActionSetCreateInfo* createInfo,
    XrActionSet* actionSet)
{
    log_call("xrCreateActionSet");
    if (!is_valid_instance(instance)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (createInfo == nullptr || createInfo->type != XR_TYPE_ACTION_SET_CREATE_INFO || actionSet == nullptr) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    *actionSet = fake_action_set();
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrDestroyActionSet_impl(XrActionSet actionSet)
{
    log_call("xrDestroyActionSet");
    return is_valid_action_set(actionSet) ? XR_SUCCESS : XR_ERROR_HANDLE_INVALID;
}

XrResult XRAPI_CALL xrCreateAction_impl(
    XrActionSet actionSet,
    const XrActionCreateInfo* createInfo,
    XrAction* action)
{
    log_call("xrCreateAction");
    if (!is_valid_action_set(actionSet)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (createInfo == nullptr || createInfo->type != XR_TYPE_ACTION_CREATE_INFO || action == nullptr) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    if (g_actionCount >= 8) {
        return XR_ERROR_OUT_OF_MEMORY;
    }
    *action = fake_action(g_actionCount++);
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrDestroyAction_impl(XrAction action)
{
    log_call("xrDestroyAction");
    return is_valid_action(action) ? XR_SUCCESS : XR_ERROR_HANDLE_INVALID;
}

XrResult XRAPI_CALL xrStringToPath_impl(XrInstance instance, const char* pathString, XrPath* path)
{
    log_call("xrStringToPath");
    if (!is_valid_instance(instance)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (pathString == nullptr || path == nullptr || pathString[0] != '/') {
        return XR_ERROR_PATH_FORMAT_INVALID;
    }
    const std::string_view requested{pathString};
    for (uint32_t i = 0; i < g_pathCount; ++i) {
        if (requested == g_paths[i].text) {
            *path = g_paths[i].path;
            return XR_SUCCESS;
        }
    }
    if (g_pathCount >= g_paths.size()) {
        return XR_ERROR_OUT_OF_MEMORY;
    }
    PathRecord& record = g_paths[g_pathCount++];
    record.path = g_nextPath++;
    std::strncpy(record.text, pathString, sizeof(record.text) - 1);
    *path = record.path;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrPathToString_impl(
    XrInstance instance,
    XrPath path,
    uint32_t bufferCapacityInput,
    uint32_t* bufferCountOutput,
    char* buffer)
{
    log_call("xrPathToString");
    if (!is_valid_instance(instance)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (path == XR_NULL_PATH) {
        return XR_ERROR_PATH_INVALID;
    }
    constexpr const char kUnknownPath[] = "/axrb/unknown";
    constexpr uint32_t kUnknownPathSize = sizeof(kUnknownPath);
    if (bufferCountOutput != nullptr) {
        *bufferCountOutput = kUnknownPathSize;
    }
    if (buffer != nullptr) {
        if (bufferCapacityInput < kUnknownPathSize) {
            return XR_ERROR_VALIDATION_FAILURE;
        }
        std::memcpy(buffer, kUnknownPath, kUnknownPathSize);
    }
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrSuggestInteractionProfileBindings_impl(
    XrInstance instance,
    const XrInteractionProfileSuggestedBinding* suggestedBindings)
{
    log_call("xrSuggestInteractionProfileBindings");
    if (!is_valid_instance(instance)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (suggestedBindings == nullptr ||
        suggestedBindings->type != XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrCreateActionSpace_impl(
    XrSession session,
    const XrActionSpaceCreateInfo* createInfo,
    XrSpace* space)
{
    log_call("xrCreateActionSpace");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (createInfo == nullptr || createInfo->type != XR_TYPE_ACTION_SPACE_CREATE_INFO || space == nullptr ||
        !is_valid_action(createInfo->action)) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    *space = make_space(action_space_kind(createInfo->subactionPath), createInfo->poseInActionSpace);
    if (*space == nullptr) {
        return XR_ERROR_OUT_OF_MEMORY;
    }
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrLocateSpace_impl(
    XrSpace space,
    XrSpace baseSpace,
    XrTime time,
    XrSpaceLocation* location)
{
    log_call("xrLocateSpace");
    SpaceRecord* spaceRecord = find_space(space);
    SpaceRecord* baseRecord = find_space(baseSpace);
    if (spaceRecord == nullptr || baseRecord == nullptr) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (location == nullptr || location->type != XR_TYPE_SPACE_LOCATION) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    (void)time;
    location->locationFlags =
        XR_SPACE_LOCATION_ORIENTATION_VALID_BIT |
        XR_SPACE_LOCATION_POSITION_VALID_BIT |
        XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT |
        XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
    const axrb::protocol::PoseFrame& poseFrame = pose_client().latest_pose_frame();
    const XrPosef spaceWorld = world_pose_for_space(*spaceRecord, poseFrame);
    const XrPosef baseWorld = world_pose_for_space(*baseRecord, poseFrame);
    location->pose = multiply_pose(inverse_pose(baseWorld), spaceWorld);
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrAttachSessionActionSets_impl(
    XrSession session,
    const XrSessionActionSetsAttachInfo* attachInfo)
{
    log_call("xrAttachSessionActionSets");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (attachInfo == nullptr || attachInfo->type != XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrSyncActions_impl(XrSession session, const XrActionsSyncInfo* syncInfo)
{
    log_call("xrSyncActions");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (syncInfo == nullptr || syncInfo->type != XR_TYPE_ACTIONS_SYNC_INFO) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrGetActionStateBoolean_impl(
    XrSession session,
    const XrActionStateGetInfo* getInfo,
    XrActionStateBoolean* state)
{
    log_call("xrGetActionStateBoolean");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (getInfo == nullptr || getInfo->type != XR_TYPE_ACTION_STATE_GET_INFO || state == nullptr ||
        state->type != XR_TYPE_ACTION_STATE_BOOLEAN || !is_valid_action(getInfo->action)) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    state->currentState = 0;
    state->changedSinceLastSync = 0;
    state->lastChangeTime = monotonic_time_ns();
    state->isActive = 1;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrGetActionStateFloat_impl(
    XrSession session,
    const XrActionStateGetInfo* getInfo,
    XrActionStateFloat* state)
{
    log_call("xrGetActionStateFloat");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (getInfo == nullptr || getInfo->type != XR_TYPE_ACTION_STATE_GET_INFO || state == nullptr ||
        state->type != XR_TYPE_ACTION_STATE_FLOAT || !is_valid_action(getInfo->action)) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    state->currentState = 0.0f;
    state->changedSinceLastSync = 0;
    state->lastChangeTime = monotonic_time_ns();
    state->isActive = 1;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrGetActionStateVector2f_impl(
    XrSession session,
    const XrActionStateGetInfo* getInfo,
    XrActionStateVector2f* state)
{
    log_call("xrGetActionStateVector2f");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (getInfo == nullptr || getInfo->type != XR_TYPE_ACTION_STATE_GET_INFO || state == nullptr ||
        state->type != XR_TYPE_ACTION_STATE_VECTOR2F || !is_valid_action(getInfo->action)) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    state->currentState = {0.0f, 0.0f};
    state->changedSinceLastSync = 0;
    state->lastChangeTime = monotonic_time_ns();
    state->isActive = 1;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrGetActionStatePose_impl(
    XrSession session,
    const XrActionStateGetInfo* getInfo,
    XrActionStatePose* state)
{
    log_call("xrGetActionStatePose");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (getInfo == nullptr || getInfo->type != XR_TYPE_ACTION_STATE_GET_INFO || state == nullptr ||
        state->type != XR_TYPE_ACTION_STATE_POSE || !is_valid_action(getInfo->action)) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    state->isActive = 1;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrGetCurrentInteractionProfile_impl(
    XrSession session,
    XrPath topLevelUserPath,
    XrInteractionProfileState* interactionProfile)
{
    log_call("xrGetCurrentInteractionProfile");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (topLevelUserPath == XR_NULL_PATH || interactionProfile == nullptr ||
        interactionProfile->type != XR_TYPE_INTERACTION_PROFILE_STATE) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    interactionProfile->interactionProfile = XR_NULL_PATH;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrEnumerateBoundSourcesForAction_impl(
    XrSession session,
    const XrBoundSourcesForActionEnumerateInfo* enumerateInfo,
    uint32_t sourceCapacityInput,
    uint32_t* sourceCountOutput,
    XrPath* sources)
{
    log_call("xrEnumerateBoundSourcesForAction");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (enumerateInfo == nullptr || sourceCountOutput == nullptr ||
        enumerateInfo->type != XR_TYPE_BOUND_SOURCES_FOR_ACTION_ENUMERATE_INFO || !is_valid_action(enumerateInfo->action)) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    *sourceCountOutput = 0;
    (void)sourceCapacityInput;
    (void)sources;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrGetInputSourceLocalizedName_impl(
    XrSession session,
    const XrInputSourceLocalizedNameGetInfo* getInfo,
    uint32_t bufferCapacityInput,
    uint32_t* bufferCountOutput,
    char* buffer)
{
    log_call("xrGetInputSourceLocalizedName");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (getInfo == nullptr || getInfo->type != XR_TYPE_INPUT_SOURCE_LOCALIZED_NAME_GET_INFO || bufferCountOutput == nullptr) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    constexpr const char kName[] = "AXRB Input";
    constexpr uint32_t kNameSize = sizeof(kName);
    *bufferCountOutput = kNameSize;
    if (buffer != nullptr) {
        if (bufferCapacityInput < kNameSize) {
            return XR_ERROR_VALIDATION_FAILURE;
        }
        std::memcpy(buffer, kName, kNameSize);
    }
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrApplyHapticFeedback_impl(
    XrSession session,
    const XrHapticActionInfo* hapticActionInfo,
    const XrHapticBaseHeader* hapticFeedback)
{
    log_call("xrApplyHapticFeedback");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (hapticActionInfo == nullptr || hapticActionInfo->type != XR_TYPE_HAPTIC_ACTION_INFO ||
        hapticFeedback == nullptr) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrStopHapticFeedback_impl(
    XrSession session,
    const XrHapticActionInfo* hapticActionInfo)
{
    log_call("xrStopHapticFeedback");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (hapticActionInfo == nullptr || hapticActionInfo->type != XR_TYPE_HAPTIC_ACTION_INFO) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrLocateViews_impl(
    XrSession session,
    const XrViewLocateInfo* viewLocateInfo,
    XrViewState* viewState,
    uint32_t viewCapacityInput,
    uint32_t* viewCountOutput,
    XrView* views)
{
    log_call("xrLocateViews");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (viewLocateInfo == nullptr || viewState == nullptr || viewCountOutput == nullptr ||
        viewLocateInfo->type != XR_TYPE_VIEW_LOCATE_INFO || viewState->type != XR_TYPE_VIEW_STATE) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    if (viewLocateInfo->viewConfigurationType != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) {
        return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
    }
    if (find_space(viewLocateInfo->space) == nullptr) {
        return XR_ERROR_HANDLE_INVALID;
    }

    *viewCountOutput = 2;
    viewState->viewStateFlags =
        XR_VIEW_STATE_ORIENTATION_VALID_BIT |
        XR_VIEW_STATE_POSITION_VALID_BIT |
        XR_VIEW_STATE_ORIENTATION_TRACKED_BIT |
        XR_VIEW_STATE_POSITION_TRACKED_BIT;

    if (viewCapacityInput == 0 || views == nullptr) {
        return XR_SUCCESS;
    }

    const uint32_t count = viewCapacityInput < 2 ? viewCapacityInput : 2;
    for (uint32_t i = 0; i < count; ++i) {
        if (views[i].type != XR_TYPE_VIEW) {
            return XR_ERROR_VALIDATION_FAILURE;
        }

        const axrb::protocol::PoseFrame& poseFrame = pose_client().latest_pose_frame();
        views[i].pose.orientation.x = poseFrame.hmd.qx;
        views[i].pose.orientation.y = poseFrame.hmd.qy;
        views[i].pose.orientation.z = poseFrame.hmd.qz;
        views[i].pose.orientation.w = poseFrame.hmd.qw;
        views[i].pose.position.x = poseFrame.hmd.x + (i == 0 ? -0.032f : 0.032f);
        views[i].pose.position.y = poseFrame.hmd.y;
        views[i].pose.position.z = poseFrame.hmd.z;
        views[i].fov.angleLeft = -0.75f;
        views[i].fov.angleRight = 0.75f;
        views[i].fov.angleUp = 0.75f;
        views[i].fov.angleDown = -0.75f;
    }
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrDestroySpace_impl(XrSpace space)
{
    log_call("xrDestroySpace");
    return find_space(space) != nullptr ? XR_SUCCESS : XR_ERROR_HANDLE_INVALID;
}

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
    if (frameWaitInfo == nullptr || frameState == nullptr ||
        frameWaitInfo->type != XR_TYPE_FRAME_WAIT_INFO || frameState->type != XR_TYPE_FRAME_STATE) {
        return XR_ERROR_VALIDATION_FAILURE;
    }

    frameState->predictedDisplayTime = monotonic_time_ns() + 11'111'111;
    frameState->predictedDisplayPeriod = 11'111'111;
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
    if (frameBeginInfo == nullptr || frameBeginInfo->type != XR_TYPE_FRAME_BEGIN_INFO) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrEndFrame_impl(XrSession session, const XrFrameEndInfo* frameEndInfo)
{
    log_call("xrEndFrame");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (frameEndInfo == nullptr || frameEndInfo->type != XR_TYPE_FRAME_END_INFO) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    maybe_send_swapchain_image();
    if (g_sessionState != XR_SESSION_STATE_FOCUSED) {
        return XR_ERROR_SESSION_NOT_RUNNING;
    }
    return XR_SUCCESS;
}

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

} // namespace

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
