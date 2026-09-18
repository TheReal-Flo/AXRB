#include "runtime_internal.h"
#include "gpu_frame_packet.h"

namespace axrb::runtime::detail {

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
        uint64_t payloadSize,
        const axrb::protocol::ImageProjection* projection = nullptr, bool gpu = false, uint32_t batchPart = 0)
    {
        static axrb::protocol::PerfStats stats("image-send");
        axrb::protocol::PerfScope scope(stats);
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
        if (projection && directWindows_) {
            header.version = gpu ? axrb::protocol::kWindowsGpuFrameVersion : axrb::protocol::kProjectionImageFrameVersion;
            if (projection->quad_count()) header.version = gpu ? axrb::protocol::kQuadGpuFrameVersion : axrb::protocol::kQuadImageFrameVersion;
            if (batchPart) {
                header.version = projection->quad_count() ? axrb::protocol::kMixedQuadGpuFrameVersion : axrb::protocol::kMixedProjectionGpuFrameVersion;
                header.reserved = batchPart;
            }
            if (projection->is_equirect()) header.version = batchPart ? axrb::protocol::kMixedEquirectGpuFrameVersion : gpu ? axrb::protocol::kEquirectGpuFrameVersion : axrb::protocol::kEquirectImageFrameVersion;
            if (gpu) header.type = axrb::protocol::kWindowsGpuFrameType;
            header.header_size += sizeof(*projection);
        }

        // GPU messages are small: one write avoids three emulator/ADB wakeups.
        bool sent = false;
        if (gpu && projection && directWindows_ && payloadSize == sizeof(axrb::protocol::WindowsGpuFrame)) {
            std::array<uint8_t, sizeof(header) + sizeof(*projection) + sizeof(axrb::protocol::WindowsGpuFrame)> message{};
            std::memcpy(message.data(), &header, sizeof(header));
            std::memcpy(message.data() + sizeof(header), projection, sizeof(*projection));
            std::memcpy(message.data() + sizeof(header) + sizeof(*projection), payload, static_cast<size_t>(payloadSize));
            sent = send_all(message.data(), message.size());
        } else {
            sent = send_all(&header, sizeof(header)) &&
                (!projection || !directWindows_ || send_all(projection, sizeof(*projection))) &&
                send_all(payload, static_cast<size_t>(payloadSize));
        }
        if (!sent) {
            if (!reportedSendFailure_) {
                __android_log_print(ANDROID_LOG_INFO, "AXRB.Image", "send failed");
                reportedSendFailure_ = true;
            }
            close_socket();
            return false;
        }
        if (gpu) {
            static axrb::protocol::PerfStats ackStats("image-ack-wait");
            axrb::protocol::PerfScope ackScope(ackStats);
            uint64_t acknowledgment = UINT64_MAX;
            auto* bytes = reinterpret_cast<uint8_t*>(&acknowledgment);
            size_t done = 0;
            while (done < sizeof(acknowledgment)) {
                ssize_t n = ::recv(socket_, bytes + done, sizeof(acknowledgment) - done, 0);
                if (n < 0 && errno == EINTR) continue;
                if (n <= 0) break;
                done += n;
            }
            if (done != sizeof(acknowledgment) || acknowledgment != sequence) { close_socket(); return false; }
        }
        return true;
    }

    bool send_empty(uint64_t sequence) {
        if (!ensure_connected() || !directWindows_) return false;
        axrb::protocol::ImageFrameHeader header{};
        header.version = axrb::protocol::kEmptyImageFrameVersion;
        header.type = axrb::protocol::kWindowsGpuFrameType; header.sequence = sequence;
        if (!send_all(reinterpret_cast<const uint8_t*>(&header), sizeof(header))) { close_socket(); return false; }
        uint64_t ack = UINT64_MAX; size_t done = 0;
        while (done < sizeof(ack)) {
            const auto n = ::recv(socket_, reinterpret_cast<uint8_t*>(&ack) + done, sizeof(ack) - done, 0);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) break;
            done += n;
        }
        if (done != sizeof(ack) || ack != sequence) { close_socket(); return false; }
        return true;
    }
    bool send_batch(const std::vector<axrb::protocol::GpuBatchPart>& parts) {
        static axrb::protocol::PerfStats stats("image-batch-send");
        axrb::protocol::PerfScope scope(stats);
        if (parts.size() < 2 || parts.size() > axrb::protocol::kMaxWireCompositionLayers || !ensure_connected() || !directWindows_) return false;
        auto header = parts.front().header;
        header.version = axrb::protocol::kGpuBatchFrameVersion;
        header.reserved = static_cast<uint32_t>(parts.size());
        header.sequence = parts.back().header.sequence;
        header.payload_size = parts.size() * sizeof(parts[0]);
        if (!axrb::protocol::valid_gpu_batch(header, parts.data(), header.payload_size)) return false;
        std::vector<uint8_t> packet(sizeof(header) + sizeof(parts[0].projection) + header.payload_size);
        std::memcpy(packet.data(), &header, sizeof(header));
        std::memcpy(packet.data() + sizeof(header), &parts[0].projection, sizeof(parts[0].projection));
        std::memcpy(packet.data() + header.header_size, parts.data(), header.payload_size);
        if (!send_all(packet.data(), packet.size())) { close_socket(); return false; }
        uint64_t acknowledgment = UINT64_MAX;
        size_t done = 0;
        {
            static axrb::protocol::PerfStats ackStats("image-batch-ack-wait");
            axrb::protocol::PerfScope ackScope(ackStats);
            while (done < sizeof(acknowledgment)) {
                const auto n = ::recv(socket_, reinterpret_cast<uint8_t*>(&acknowledgment) + done, sizeof(acknowledgment) - done, 0);
                if (n < 0 && errno == EINTR) continue;
                if (n <= 0) break;
                done += n;
            }
        }
        if (done != sizeof(acknowledgment) || acknowledgment != header.sequence) { close_socket(); return false; }
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

        char hardware[PROP_VALUE_MAX]{};
        __system_property_get("ro.hardware", hardware);
        if (std::strcmp(hardware, "ranchu") == 0 || std::strcmp(hardware, "goldfish") == 0) {
            // Stock Android SELinux separates the app and broker's Unix sockets.
            // Use adb reverse's native emulator pipe rather than its slow NAT.
            int candidate = ::socket(AF_INET, SOCK_STREAM, 0);
            if (candidate < 0) { return false; }
            int noDelay = 1;
            setsockopt(candidate, IPPROTO_TCP, TCP_NODELAY, &noDelay, sizeof(noDelay));
            int sendBuffer = 4 * 1024 * 1024;
            setsockopt(candidate, SOL_SOCKET, SO_SNDBUF, &sendBuffer, sizeof(sendBuffer));
            timeval timeout{3, 0};
            setsockopt(candidate, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
            setsockopt(candidate, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_port = htons(38491);
            inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
            if (::connect(candidate, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0) {
                socket_ = candidate;
                directWindows_ = true;
                __android_log_print(ANDROID_LOG_INFO, "AXRB.Image", "connected to Windows image stream via adb reverse :38491");
                return true;
            }
            ::close(candidate);
            return false;
        }

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
            const ssize_t sent = ::send(socket_, cursor, remaining, MSG_NOSIGNAL);
            if (sent <= 0) {
                if (sent < 0 && errno == EINTR) { continue; }
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
    bool directWindows_ = false;
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

void maybe_send_swapchain_image(const SwapchainRecord& sc,
                               const XrSwapchainSubImage* subImage = nullptr,
                               std::vector<uint8_t>* readback = nullptr)
{
    if (readback) { readback->clear(); }
    const auto readbackStart = std::chrono::steady_clock::now();
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
    if (!sc.created || sc.width == 0 || sc.height == 0 || sc.arraySize == 0) {
        if (!reportedMissingSwapchain) {
            __android_log_print(ANDROID_LOG_INFO, "AXRB.Image", "skip: no swapchain image available");
            reportedMissingSwapchain = true;
        }
        return;
    }
    if (sc.textures[sc.releasedImage] == 0) {
        if (!reportedMissingSwapchain) {
            __android_log_print(ANDROID_LOG_INFO, "AXRB.Image", "skip: swapchain texture is zero");
            reportedMissingSwapchain = true;
        }
        return;
    }

    const uint64_t sequence = readback ? 0 : g_imageFrameSequence++;
    if (sc.width > 16384 || sc.height > 16384 || sc.arraySize > 4) {
        if (!reportedOversize) {
            __android_log_print(
                ANDROID_LOG_INFO,
                "AXRB.Image",
                "skip: oversized image %ux%u layers=%u",
                sc.width,
                sc.height,
                sc.arraySize);
            reportedOversize = true;
        }
        return;
    }

    const uint32_t sourceWidth = subImage ? subImage->imageRect.extent.width : sc.width;
    const uint32_t sourceHeight = subImage ? subImage->imageRect.extent.height : sc.height;
    const GLint sourceX = subImage ? subImage->imageRect.offset.x : 0;
    const GLint sourceY = subImage ? subImage->imageRect.offset.y : 0;
    const uint32_t layerCount = subImage ? 1 : sc.arraySize;
    const uint32_t transportWidth =
        std::min(sourceWidth, g_renderWidth);
    const uint32_t transportHeight =
        std::min(sourceHeight, g_renderHeight);
    const uint64_t layerBytes = static_cast<uint64_t>(transportWidth) * transportHeight * 4;
    const uint64_t payloadBytes = layerBytes * layerCount;
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
    for (uint32_t layer = 0; layer < layerCount; ++layer) {
        while (glGetError() != GL_NO_ERROR) {
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER, sourceFramebuffer);
        if (sc.arraySize > 1) {
            glFramebufferTextureLayer(
                GL_READ_FRAMEBUFFER,
                GL_COLOR_ATTACHMENT0,
                sc.textures[sc.releasedImage],
                0,
                static_cast<GLint>(subImage ? subImage->imageArrayIndex : layer));
        } else {
            glFramebufferTexture2D(
                GL_READ_FRAMEBUFFER,
                GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_2D,
                sc.textures[sc.releasedImage],
                0);
        }

        if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            if (!reportedFramebufferFailure) {
                __android_log_print(
                    ANDROID_LOG_INFO,
                    "AXRB.Image",
                    "skip: framebuffer incomplete for tex=%u layer=%u status=0x%x",
                    sc.textures[sc.releasedImage],
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
            sourceX,
            sourceY,
            sourceX + static_cast<GLint>(sourceWidth),
            sourceY + static_cast<GLint>(sourceHeight),
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
                    sc.textures[sc.releasedImage],
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
    if (readback) {
        *readback = payload;
        static axrb::protocol::PerfStats stats("eye-readback");
        stats.record(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - readbackStart).count());
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
            sc.arraySize,
            static_cast<unsigned long long>(payloadBytes));
        reportedReadback = true;
    }

    static axrb::protocol::PerfStats readbackStats("image-readback");
    readbackStats.record(std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - readbackStart).count());
    if (image_transport_client().send_frame(
            sequence,
            transportWidth,
            transportHeight,
            sc.arraySize,
            payload.data(),
            payloadBytes) && sequence % 150 == 0) {
        __android_log_print(
            ANDROID_LOG_INFO,
            "AXRB.Image",
            "sent frame seq=%llu %ux%u layers=%u bytes=%llu",
            static_cast<unsigned long long>(sequence),
            transportWidth,
            transportHeight,
            sc.arraySize,
            static_cast<unsigned long long>(payloadBytes));
    }
}
#else
void maybe_send_swapchain_image(const SwapchainRecord&) {}
#endif

XrResult submit_projection_frame(const XrFrameEndInfo& info, uint32_t batchPart, bool validateOnly, PreparedGpuLayer* prepared)
{
    auto invalid = [&](const char* reason) {
#if defined(__ANDROID__)
        static uint32_t reports = 0;
        if (reports++ < 12) {
            __android_log_print(ANDROID_LOG_ERROR, "AXRB.Layer", "reject: %s layers=%u", reason, info.layerCount);
            if (info.layers) for (uint32_t i = 0; i < std::min<uint32_t>(info.layerCount, 16); ++i) {
                const auto* item = static_cast<const XrCompositionLayerBaseHeader*>(info.layers[i]);
                if (item) __android_log_print(ANDROID_LOG_ERROR, "AXRB.Layer", "layer[%u] type=%d flags=%llu", i,
                    item->type, static_cast<unsigned long long>(item->layerFlags));
            }
        }
#endif
        return XR_ERROR_LAYER_INVALID;
    };
    static bool reportedLayers = false;
    if (!reportedLayers && info.layerCount && info.layers && info.layers[0]) {
        const auto* first = static_cast<const XrCompositionLayerProjection*>(info.layers[0]);
#if defined(__ANDROID__)
        __android_log_print(ANDROID_LOG_INFO, "AXRB.Layer", "layers=%u type=%d flags=%llu views=%u", info.layerCount,
            first->type, static_cast<unsigned long long>(first->layerFlags), first->viewCount);
#else
        std::fprintf(stderr, "AXRB projection: layers=%u type=%d flags=%llu\n", info.layerCount,
            first->type, static_cast<unsigned long long>(first->layerFlags));
#endif
        reportedLayers = true;
    }
    if (info.layerCount == 0) {
#if defined(__ANDROID__)
        if (g_vulkan.active() && g_vulkan.gpu_export_enabled() &&
            (!g_vulkan.release_batch() || !image_transport_client().send_empty(g_imageFrameSequence++))) return XR_ERROR_RUNTIME_FAILURE;
#endif
        return XR_SUCCESS;
    }
    // Mixed scene/panel frames are transferred as an atomic GPU batch. Validate
    // every member before publishing any part; preserve application layer order.
    if (!batchPart && info.layerCount >= 2 && info.layerCount <= axrb::protocol::kMaxWireCompositionLayers && info.layers && info.layers[0] &&
        (info.layerCount > 2 || static_cast<const XrCompositionLayerBaseHeader*>(info.layers[0])->type == XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR || (info.layers[1] && static_cast<const XrCompositionLayerBaseHeader*>(info.layers[1])->type == XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR) || static_cast<const XrCompositionLayerBaseHeader*>(info.layers[0])->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION)) {
        std::vector<PreparedGpuLayer> preparedLayers(info.layerCount);
        for (uint32_t i = 0; i < info.layerCount; ++i) {
            if (!info.layers[i] || (static_cast<const XrCompositionLayerBaseHeader*>(info.layers[i])->type != XR_TYPE_COMPOSITION_LAYER_QUAD && static_cast<const XrCompositionLayerBaseHeader*>(info.layers[i])->type != XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR && (i || static_cast<const XrCompositionLayerBaseHeader*>(info.layers[i])->type != XR_TYPE_COMPOSITION_LAYER_PROJECTION)))
                return invalid("mixed layer type");
            auto part = info; part.layerCount = 1; part.layers = &info.layers[i];
            const auto result = submit_projection_frame(part, (info.layerCount << 16) | i, true, &preparedLayers[i]);
            if (result != XR_SUCCESS) return result;
        }
#if defined(__ANDROID__)
        if (g_vulkan.active() && g_vulkan.gpu_export_enabled()) {
            std::vector<VulkanExportRequest> requests(info.layerCount);
            std::vector<SwapchainRecord*> updatedSurfaces;
            for (uint32_t i = 0; i < info.layerCount; ++i) {
                const auto& input = preparedLayers[i]; auto& request = requests[i];
                request.width = input.width; request.height = input.height;
                for (uint32_t eye = 0; eye < 2; ++eye) {
                    auto* sc = input.swapchains[eye];
                    if (sc->surface && std::find(updatedSurfaces.begin(), updatedSurfaces.end(), sc) == updatedSurfaces.end()) {
                        if (!sc->surface->update()) return XR_ERROR_RUNTIME_FAILURE;
                        if (sc->surface->needs_copy()) {
                            if (!g_vulkan.copy_surface(sc->vulkan, sc->width, sc->height)) return XR_ERROR_RUNTIME_FAILURE;
                            sc->surface->copied();
                        }
                        updatedSurfaces.push_back(sc);
                    }
                    request.swapchains[eye] = &sc->vulkan; request.indices[eye] = sc->releasedImage;
                    request.subimages[eye] = input.subimages[eye]; request.verticalFlip[eye] = input.verticalFlip[eye];
                }
            }
            std::vector<axrb::protocol::WindowsGpuFrame> exports;
            if (!g_vulkan.export_batch(requests, exports)) return XR_ERROR_RUNTIME_FAILURE;
            std::vector<axrb::protocol::GpuBatchPart> parts(info.layerCount);
            const auto timestamp = static_cast<uint64_t>(monotonic_time_ns());
            for (uint32_t i = 0; i < info.layerCount; ++i) {
                auto& part = parts[i]; const auto& input = preparedLayers[i];
                part.projection = input.projection; part.gpu = exports[i];
                auto& header = part.header;
                header.version = input.projection.is_equirect() ? axrb::protocol::kMixedEquirectGpuFrameVersion :
                    input.projection.quad_count() ? axrb::protocol::kMixedQuadGpuFrameVersion : axrb::protocol::kMixedProjectionGpuFrameVersion;
                header.type = axrb::protocol::kWindowsGpuFrameType;
                header.header_size += sizeof(part.projection);
                header.width = input.width; header.height = input.height; header.layers = 2;
                header.reserved = (info.layerCount << 16) | i;
                header.sequence = g_imageFrameSequence++; header.monotonic_time_ns = timestamp;
                header.payload_size = sizeof(part.gpu);
            }
            const bool sent = image_transport_client().send_batch(parts);
            g_vulkan.acknowledge_batch(sent);
            static bool reported = false;
            if (sent && !reported) { __android_log_print(ANDROID_LOG_INFO, "AXRB.GPU", "Whole-frame export active: %u layers, one export submission and one acknowledgment", info.layerCount); reported = true; }
            return sent ? XR_SUCCESS : XR_ERROR_RUNTIME_FAILURE;
        }
#endif
        for (uint32_t i = 0; i < info.layerCount; ++i) {
            auto part = info; part.layerCount = 1; part.layers = &info.layers[i];
            const auto result = submit_projection_frame(part, (info.layerCount << 16) | i);
            if (result != XR_SUCCESS) return result;
        }
        return XR_SUCCESS;
    }
    if (info.layerCount > 2 || info.layers == nullptr || info.layers[0] == nullptr) { return invalid("layer count/pointer"); }
    const auto* layer = static_cast<const XrCompositionLayerProjection*>(info.layers[0]);
    const bool quads = layer->type == XR_TYPE_COMPOSITION_LAYER_QUAD;
    const bool equirect = layer->type == XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR;
    if (equirect && info.layerCount != 1) return invalid("equirect layer count");
    if (!quads && !equirect && (info.layerCount != 1 || layer->type != XR_TYPE_COMPOSITION_LAYER_PROJECTION || layer->viewCount != 2 || layer->views == nullptr ||
        (layer->layerFlags & ~uint64_t{7}) != 0)) { return invalid("projection type/views/flags"); }
    const auto* space = find_space(layer->space);
    if (!space) { return XR_ERROR_HANDLE_INVALID; }
    const XrPosef spaceWorld = world_pose_for_space(*space, g_lastViewPoseFrame);
    axrb::protocol::ImageProjection projection{};
    projection.view_count = 2;
    projection.layer_flags = static_cast<uint32_t>(layer->layerFlags);
    std::array<SwapchainRecord*, 2> swapchains{};
    const XrSwapchainSubImage* subimages[2]{};
    if (equirect) {
        const auto* sphere = static_cast<const XrCompositionLayerEquirect2KHR*>(info.layers[0]);
        const auto pose = multiply_pose(spaceWorld, sphere->pose);
        projection.view_count = axrb::protocol::kEquirectComposition;
        projection.layer_flags = 0;
        projection.equirect = {{pose.position.x, pose.position.y, pose.position.z,
            pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w},
            sphere->radius, sphere->centralHorizontalAngle, sphere->upperVerticalAngle, sphere->lowerVerticalAngle,
            static_cast<uint32_t>(sphere->eyeVisibility), static_cast<uint32_t>(sphere->layerFlags)};
        if ((sphere->layerFlags & ~uint64_t{7}) || !axrb::protocol::valid_equirect(projection)) return invalid("equirect geometry/flags");
        subimages[0] = subimages[1] = &sphere->subImage;
    } else if (quads) {
        projection.view_count = axrb::protocol::kQuadCompositionBit | info.layerCount;
        projection.layer_flags = 0;
        for (uint32_t i = 0; i < info.layerCount; ++i) {
            const auto* quad = static_cast<const XrCompositionLayerQuad*>(info.layers[i]);
            if (!quad || quad->type != XR_TYPE_COMPOSITION_LAYER_QUAD || (quad->layerFlags & ~uint64_t{7})) return invalid("quad type/flags");
            const auto* quadSpace = find_space(quad->space);
            if (!quadSpace) return XR_ERROR_HANDLE_INVALID;
            const auto pose = multiply_pose(world_pose_for_space(*quadSpace, g_lastViewPoseFrame), quad->pose);
            projection.quads[i] = {{pose.position.x, pose.position.y, pose.position.z,
                pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w},
                quad->size.width, quad->size.height, static_cast<uint32_t>(quad->eyeVisibility), static_cast<uint32_t>(quad->layerFlags)};
            subimages[i] = &quad->subImage;
        }
        if (!axrb::protocol::valid_quads(projection)) return invalid("quad pose/size/visibility");
        // The existing shared texture pair carries two ordered panel images;
        // a one-panel frame duplicates its image but submits only one layer.
        if (info.layerCount == 1) subimages[1] = subimages[0];
    } else {
        subimages[0] = &layer->views[0].subImage;
        subimages[1] = &layer->views[1].subImage;
    }
    bool verticalFlip[2]{};
    for (uint32_t eye = 0; eye < 2; ++eye) {
        const auto* header = static_cast<const XrCompositionLayerBaseHeader*>(info.layers[quads && info.layerCount == 2 ? eye : 0]);
        struct ChainHeader { XrStructureType type; const void* next; };
        for (auto* chain = static_cast<const ChainHeader*>(header->next); chain;
             chain = static_cast<const ChainHeader*>(chain->next)) {
            if (chain->type != XR_TYPE_COMPOSITION_LAYER_IMAGE_LAYOUT_FB) continue;
            const auto* layout = reinterpret_cast<const XrCompositionLayerImageLayoutFB*>(chain);
            if (layout->flags & ~XR_COMPOSITION_LAYER_IMAGE_LAYOUT_VERTICAL_FLIP_BIT_FB) return invalid("image layout flags");
            verticalFlip[eye] = (layout->flags & XR_COMPOSITION_LAYER_IMAGE_LAYOUT_VERTICAL_FLIP_BIT_FB) != 0;
#if defined(__ANDROID__)
            static bool reportedFlip = false;
            if (verticalFlip[eye] && !validateOnly && !reportedFlip) {
                __android_log_print(ANDROID_LOG_INFO, "AXRB.Layer", "Applying requested XR_FB_composition_layer_image_layout vertical flip (layer type %d)", int(header->type));
                reportedFlip = true;
            }
#endif
        }
    }
    uint32_t width = 0, height = 0;
    for (uint32_t eye = 0; eye < 2; ++eye) {
        const auto& sub = *subimages[eye];
        auto* sc = find_swapchain(sub.swapchain);
        if (!sc) { return XR_ERROR_HANDLE_INVALID; }
#if defined(__ANDROID__)
        if (sc->surface && !validateOnly && (eye == 0 || swapchains[0] != sc)) {
            if (!sc->surface->update()) return XR_ERROR_RUNTIME_FAILURE;
            if (sc->surface->needs_copy()) {
                if (!g_vulkan.copy_surface(sc->vulkan, sc->width, sc->height)) return XR_ERROR_RUNTIME_FAILURE;
                sc->surface->copied();
            }
        }
#endif
        if (!sc->hasReleasedImage) { return XR_ERROR_CALL_ORDER_INVALID; }
        const auto& rect = sub.imageRect;
        if (rect.offset.x < 0 || rect.offset.y < 0 || rect.extent.width <= 0 || rect.extent.height <= 0 ||
            static_cast<uint64_t>(rect.offset.x) + rect.extent.width > sc->width ||
            static_cast<uint64_t>(rect.offset.y) + rect.extent.height > sc->height ||
            sub.imageArrayIndex >= sc->arraySize) { return XR_ERROR_SWAPCHAIN_RECT_INVALID; }
        const uint32_t eyeWidth = std::min<uint32_t>(g_renderWidth, rect.extent.width);
        const uint32_t eyeHeight = std::min<uint32_t>(g_renderHeight, rect.extent.height);
        if (eye == 0) { width = eyeWidth; height = eyeHeight; }
        if (width != eyeWidth || height != eyeHeight) { return invalid("unequal eye dimensions"); }
        swapchains[eye] = sc;
        if (quads || equirect) continue;
        const auto& view = layer->views[eye];
        if (view.type != XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW) { return invalid("view type"); }
        const auto pose = multiply_pose(spaceWorld, view.pose);
        auto& out = projection.views[eye];
        out.pose = {pose.position.x, pose.position.y, pose.position.z,
                    pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w};
        out.angle_left = view.fov.angleLeft;
        out.angle_right = view.fov.angleRight;
        out.angle_up = view.fov.angleUp;
        out.angle_down = view.fov.angleDown;
    }
    if (!quads && !equirect && !axrb::protocol::valid_projection(projection)) {
#if defined(__ANDROID__)
        static bool reportedInvalidPose = false;
        if (!reportedInvalidPose) for (const auto& eye : projection.views)
            __android_log_print(ANDROID_LOG_ERROR, "AXRB.Layer", "pose q=(%f %f %f %f) fov=(%f %f %f %f)",
                eye.pose.qx, eye.pose.qy, eye.pose.qz, eye.pose.qw, eye.angle_left, eye.angle_right, eye.angle_up, eye.angle_down);
        reportedInvalidPose = true;
#endif
        return invalid("render pose/FOV");
    }
    if (prepared) {
        prepared->swapchains = swapchains; prepared->width = width; prepared->height = height;
        prepared->projection = projection;
        for (uint32_t eye = 0; eye < 2; ++eye) {
            prepared->subimages[eye] = *subimages[eye]; prepared->verticalFlip[eye] = verticalFlip[eye];
        }
    }
    if (validateOnly) return XR_SUCCESS;
#if defined(__ANDROID__)
    char hardware[PROP_VALUE_MAX]{};
    __system_property_get("ro.hardware", hardware);
    if (std::strcmp(hardware, "ranchu") != 0 && std::strcmp(hardware, "goldfish") != 0) {
        // The legacy Java proxy understands only v1. Preserve that development path.
        if (g_lastReleasedSwapchain) { maybe_send_swapchain_image(*g_lastReleasedSwapchain); }
        return XR_SUCCESS;
    }
    if (batchPart && (!g_vulkan.active())) return XR_ERROR_RUNTIME_FAILURE;
    static std::vector<uint8_t> eyes[2];
    static std::vector<uint8_t> stereo;
    const size_t eyeBytes = static_cast<size_t>(width) * height * 4;
    if (g_vulkan.active()) {
        const VulkanSwapchain* vkSwapchains[] = {&swapchains[0]->vulkan, &swapchains[1]->vulkan};
        const uint32_t indices[] = {swapchains[0]->releasedImage, swapchains[1]->releasedImage};
        if (!g_vulkan.readback(vkSwapchains, indices, subimages, width, height, eyes, verticalFlip)) return XR_ERROR_RUNTIME_FAILURE;
        if (g_vulkan.gpu_marker().status == 1) {
            const auto& marker = g_vulkan.gpu_marker();
            axrb::protocol::WindowsGpuFrame gpu{marker.session, {marker.formats[0], marker.formats[1]}};
            uint64_t sequence = g_imageFrameSequence++;
            if (image_transport_client().send_frame(sequence, width, height, 2,
                    reinterpret_cast<const uint8_t*>(&gpu), sizeof(gpu), &projection, true, batchPart)) {
                if (sequence % 90 == 0) __android_log_print(ANDROID_LOG_INFO, "AXRB.GPU", "shared GPU eyes seq=%llu %ux%u; no pixel readback", static_cast<unsigned long long>(sequence), width, height);
                return XR_SUCCESS;
            }
            if (batchPart) return XR_ERROR_RUNTIME_FAILURE;
            // Never reuse shared images after an uncertain consumer completion.
            g_vulkan.disable_gpu_export();
            __android_log_print(ANDROID_LOG_WARN, "AXRB.GPU", "GPU consumer unavailable; disabling shared export for this session");
            if (!g_vulkan.readback(vkSwapchains, indices, subimages, width, height, eyes, verticalFlip)) return XR_ERROR_RUNTIME_FAILURE;
        }
    }
    for (uint32_t eye = 0; eye < 2; ++eye) {
        if (!g_vulkan.active()) {
            maybe_send_swapchain_image(*swapchains[eye], subimages[eye], &eyes[eye]);
            if (verticalFlip[eye] && eyes[eye].size() == eyeBytes) {
                const size_t stride = static_cast<size_t>(width) * 4;
                for (uint32_t y = 0; y < height / 2; ++y)
                    std::swap_ranges(eyes[eye].begin() + y * stride, eyes[eye].begin() + (y + 1) * stride,
                                     eyes[eye].begin() + (height - 1 - y) * stride);
            }
        }
        if (eyes[eye].size() != eyeBytes) { return XR_ERROR_RUNTIME_FAILURE; }
    }
    if (batchPart) return XR_ERROR_RUNTIME_FAILURE;
    stereo.resize(eyeBytes * 2);
    std::memcpy(stereo.data(), eyes[0].data(), eyeBytes);
    std::memcpy(stereo.data() + eyeBytes, eyes[1].data(), eyeBytes);
    const uint64_t sequence = g_imageFrameSequence++;
    if (image_transport_client().send_frame(sequence, width, height, 2, stereo.data(), stereo.size(), &projection) && sequence % 450 == 0) {
        __android_log_print(ANDROID_LOG_INFO, "AXRB.Stereo", "sent two eyes seq=%llu %ux%u API=%s",
            static_cast<unsigned long long>(sequence), width, height,
            g_vulkan.active() ? "Vulkan" : "OpenGLES");
    }
#endif
    return XR_SUCCESS;
}


} // namespace axrb::runtime::detail
