#include <winsock2.h>
#include <ws2tcpip.h>
#include "image_transport.h"
#include "windows_gpu_frame.h"
#include "gpu_frame_packet.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

int run_case(unsigned short port, bool consumed, bool quads = false, uint32_t mixedPart = 0, bool sphere = false) {
    using namespace axrb::protocol;
    bool checked = false;
    std::thread server([&] {
        TcpImageServer listener;
        listener.serve_with_callback(port, 1, [&](const ImageFrameHeader& header, const ImageProjection& projection, std::vector<uint8_t>&& payload) {
            WindowsGpuFrame frame{};
            if (payload.size() == sizeof(frame)) std::memcpy(&frame, payload.data(), sizeof(frame));
            checked = header.version == (sphere ? (mixedPart ? kMixedEquirectGpuFrameVersion : kEquirectGpuFrameVersion) : mixedPart ? (quads ? kMixedQuadGpuFrameVersion : kMixedProjectionGpuFrameVersion) : (quads ? kQuadGpuFrameVersion : kWindowsGpuFrameVersion)) && header.reserved == mixedPart && header.sequence == 42 &&
                header.width == 5120 && header.height == 2880 && header.layers == 2 &&
                (sphere ? valid_equirect(projection) : quads ? (projection.quad_count() == (mixedPart ? 1u : 2u) && projection.quads[0].width == 2.5f && projection.quads[0].eye_visibility == 1)
                       : projection.view_count == 2) && frame.session == 12345 && frame.formats[1] == 43;
            return consumed;
        });
    });
    SOCKET client = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(port); address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bool connected = false;
    for (int i = 0; i < 100; ++i) {
        if (connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0) { connected = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!connected) { std::fprintf(stderr, "Cannot connect to test server\n"); std::terminate(); }
    ImageFrameHeader header{}; header.version = kWindowsGpuFrameVersion; header.type = kWindowsGpuFrameType;
    header.header_size += sizeof(ImageProjection); header.width = 5120; header.height = 2880; header.layers = 2;
    header.sequence = 42; header.payload_size = sizeof(WindowsGpuFrame);
    ImageProjection projection{}; projection.view_count = 2;
    for (auto& view : projection.views) { view.pose.qw = 1; view.angle_left = view.angle_down = -0.9f; view.angle_right = view.angle_up = 0.9f; }
    if (quads) {
        header.version = kQuadGpuFrameVersion;
        projection.view_count = kQuadCompositionBit | 2;
        for (unsigned i = 0; i < 2; ++i) projection.quads[i] = {{0,1,-2,0,0,0,1},2.5f,1.5f,i+1,7};
    }
    if (mixedPart) {
        header.version = quads ? kMixedQuadGpuFrameVersion : kMixedProjectionGpuFrameVersion;
        header.reserved = mixedPart;
        if (quads) projection.view_count = kQuadCompositionBit | 1;
    }
    if (sphere) {
        header.version = mixedPart ? kMixedEquirectGpuFrameVersion : kEquirectGpuFrameVersion;
        projection = {}; projection.view_count = kEquirectComposition;
        projection.equirect = {{0,0,0,0,0,0,1},0,6.2831853f,1.5707963f,-1.5707963f,0,7};
    }
    WindowsGpuFrame frame{12345, {43, 43}};
    auto fragmented = [&](const void* pointer, size_t count) {
        auto* bytes = static_cast<const char*>(pointer);
        while (count) {
            int sent = send(client, bytes, static_cast<int>(count > 7 ? 7 : count), 0);
            if (sent <= 0) return false;
            bytes += sent; count -= sent;
        }
        return true;
    };
    bool sent = fragmented(&header, sizeof(header)) && fragmented(&projection, sizeof(projection)) && fragmented(&frame, sizeof(frame));
    uint64_t acknowledgment = 0; size_t received = 0;
    while (received < sizeof(acknowledgment)) {
        int n = recv(client, reinterpret_cast<char*>(&acknowledgment) + received, static_cast<int>(sizeof(acknowledgment) - received), 0);
        if (n <= 0) break;
        received += n;
    }
    closesocket(client); server.join();
    return sent && checked && received == sizeof(acknowledgment) && acknowledgment == (consumed ? 42 : UINT64_MAX) ? 0 : 1;
}
int run_batch_case(unsigned short port, bool consumed, bool duplicateSession = false, bool wrongSequence = false) {
    using namespace axrb::protocol;
    std::vector<GpuBatchPart> parts(5);
    for (uint32_t i = 0; i < parts.size(); ++i) {
        auto& part = parts[i]; auto& h = part.header;
        h.version = kMixedQuadGpuFrameVersion; h.type = kWindowsGpuFrameType;
        h.header_size += sizeof(ImageProjection); h.width = h.height = 512; h.layers = 2;
        h.reserved = (5u << 16) | i; h.sequence = 100 + i; h.monotonic_time_ns = 999;
        h.payload_size = sizeof(WindowsGpuFrame);
        part.projection.view_count = kQuadCompositionBit | 1;
        part.projection.quads[0] = {{0,1,-2,0,0,0,1},2,1,0,7};
        part.gpu = {500 + i, {43, i % 2 ? 0u : 43u}};
    }
    if (duplicateSession) parts[4].gpu.session = parts[0].gpu.session;
    if (wrongSequence) parts[3].header.sequence++;
    auto header = parts[0].header;
    header.version = kGpuBatchFrameVersion; header.reserved = 5; header.sequence = 104;
    header.payload_size = parts.size() * sizeof(GpuBatchPart);
    bool called = false, correct = false;
    std::thread server([&] {
        TcpImageServer listener;
        listener.serve_with_callback(port, 1, [&](const ImageFrameHeader& h, const ImageProjection&, std::vector<uint8_t>&& bytes) {
            called = true; correct = valid_gpu_batch(h, bytes.data(), bytes.size()) && h.sequence == 104;
            return consumed;
        });
    });
    SOCKET client = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(port); address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bool connected = false;
    for (int i = 0; i < 100; ++i) {
        if (connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0) { connected = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!connected) std::terminate();
    DWORD timeout = 2000;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    auto sendFragmented = [&](const void* ptr, size_t bytes) {
        auto* p = static_cast<const char*>(ptr);
        while (bytes) { const int n = send(client, p, static_cast<int>(bytes > 7 ? 7 : bytes), 0); if (n <= 0) return false; p += n; bytes -= n; }
        return true;
    };
    bool sent = sendFragmented(&header, sizeof(header)) && sendFragmented(&parts[0].projection, sizeof(ImageProjection)) &&
                sendFragmented(parts.data(), header.payload_size);
    uint64_t ack = 0; size_t received = 0;
    while (received < sizeof(ack)) { int n = recv(client, reinterpret_cast<char*>(&ack) + received, static_cast<int>(sizeof(ack)-received), 0); if (n <= 0) break; received += n; }
    const bool invalid = duplicateSession || wrongSequence;
    // A complete batch produces exactly one acknowledgment, never one per part.
    char extra; const int more = recv(client, &extra, 1, 0);
    closesocket(client); server.join();
    return sent && (invalid ? !called : called && correct) && received == sizeof(ack) && more == 0 &&
           ack == (!invalid && consumed ? 104 : UINT64_MAX) ? 0 : 1;
}
int main() {
    WSADATA data{}; if (WSAStartup(MAKEWORD(2,2), &data)) return 1;
    int result = run_case(38495, true) | run_case(38496, false) | run_case(38497, true, true) | run_case(38498, true, false, 3u << 16) | run_case(38499, true, true, (16u << 16) | 15);
    result |= run_case(38500, true, false, 0, true) | run_case(38501, true, false, (5u << 16) | 2, true);
    using namespace axrb::protocol;
    if (valid_mixed_part(6, (3u << 16) | 1) || valid_mixed_part(7, (3u << 16) | 3) || !valid_mixed_part(6, 17u << 16)) result = 1;
    axrb::protocol::ImageProjection invalid{};
    invalid.view_count = axrb::protocol::kQuadCompositionBit | 3;
    if (axrb::protocol::valid_quads(invalid)) result = 1;
    invalid = {}; invalid.view_count = kEquirectComposition;
    invalid.equirect = {{0,0,0,0,0,0,1},0,6.2831853f,1.5707963f,-1.5707963f,0,7};
    if (!valid_equirect(invalid)) result = 1;
    invalid.equirect.radius = -1; if (valid_equirect(invalid)) result = 1;
    invalid.equirect.radius = 0; invalid.equirect.horizontal_angle = 7; if (valid_equirect(invalid)) result = 1;
    result |= run_batch_case(38502, true) | run_batch_case(38503, false) |
              run_batch_case(38504, true, true) | run_batch_case(38505, true, false, true);
    WSACleanup();
    if (!result) std::puts("GPU metadata fragmentation and completion/rejection acknowledgments passed");
    return result;
}
