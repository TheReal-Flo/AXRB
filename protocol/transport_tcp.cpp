#include "transport_tcp.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

namespace axrb::protocol {

namespace {

class SocketRuntime {
public:
    SocketRuntime()
    {
#if defined(_WIN32)
        WSADATA data{};
        ok_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
        ok_ = true;
#endif
    }

    ~SocketRuntime()
    {
#if defined(_WIN32)
        if (ok_) {
            WSACleanup();
        }
#endif
    }

    bool ok() const { return ok_; }

private:
    bool ok_ = false;
};

void close_socket(SocketHandle socket)
{
#if defined(_WIN32)
    closesocket(socket);
#else
    close(socket);
#endif
}

bool send_all(SocketHandle socket, const void* data, size_t size)
{
    const char* cursor = static_cast<const char*>(data);
    size_t remaining = size;
    while (remaining > 0) {
#if defined(_WIN32)
        const int sent = send(socket, cursor, static_cast<int>(remaining), 0);
#else
        const ssize_t sent = send(socket, cursor, remaining, 0);
#endif
        if (sent <= 0) {
            return false;
        }
        cursor += sent;
        remaining -= static_cast<size_t>(sent);
    }
    return true;
}

uint64_t monotonic_time_ns()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

PoseFrame make_fake_pose_frame(uint64_t sequence)
{
    const float t = static_cast<float>(sequence) / 90.0f;
    PoseFrame frame{};
    frame.sequence = sequence;
    frame.monotonic_time_ns = monotonic_time_ns();

    frame.hmd.x = std::sin(t) * 0.02f;
    frame.hmd.y = 1.65f;
    frame.hmd.z = -0.05f;

    frame.left_controller.x = -0.25f;
    frame.left_controller.y = 1.25f + std::sin(t * 2.0f) * 0.02f;
    frame.left_controller.z = -0.45f;

    frame.right_controller.x = 0.25f;
    frame.right_controller.y = 1.25f + std::cos(t * 2.0f) * 0.02f;
    frame.right_controller.z = -0.45f;
    return frame;
}

} // namespace

int TcpPoseServer::serve(uint16_t port, uint32_t maxFrames)
{
    return serve_with_producer(port, maxFrames, make_fake_pose_frame);
}

int TcpPoseServer::serve_with_producer(
    uint16_t port,
    uint32_t maxFrames,
    const std::function<PoseFrame(uint64_t)>& producer)
{
    SocketRuntime runtime;
    if (!runtime.ok()) {
        std::fprintf(stderr, "AXRB TCP: socket runtime init failed\n");
        return 1;
    }

    SocketHandle server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == kInvalidSocket) {
        std::fprintf(stderr, "AXRB TCP: socket creation failed\n");
        return 1;
    }

    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        std::fprintf(stderr, "AXRB TCP: bind failed on port %u\n", static_cast<unsigned>(port));
        close_socket(server);
        return 1;
    }

    if (listen(server, 1) != 0) {
        std::fprintf(stderr, "AXRB TCP: listen failed\n");
        close_socket(server);
        return 1;
    }

    std::fprintf(stderr, "AXRB TCP: listening on 0.0.0.0:%u\n", static_cast<unsigned>(port));
    uint64_t sequence = 0;
    while (maxFrames == 0 || sequence < maxFrames) {
        SocketHandle client = accept(server, nullptr, nullptr);
        if (client == kInvalidSocket) {
            std::fprintf(stderr, "AXRB TCP: accept failed\n");
            close_socket(server);
            return 1;
        }

        std::fprintf(stderr, "AXRB TCP: client connected\n");
        while (maxFrames == 0 || sequence < maxFrames) {
            const PoseFrame frame = producer(sequence);
            if (!send_all(client, &frame, sizeof(frame))) {
                std::fprintf(stderr, "AXRB TCP: client disconnected\n");
                close_socket(client);
                break;
            }
            ++sequence;
            std::this_thread::sleep_for(std::chrono::microseconds(11111));
        }

        if (maxFrames != 0 && sequence >= maxFrames) {
            close_socket(client);
            break;
        }
    }

    close_socket(server);
    return 0;
}

} // namespace axrb::protocol
