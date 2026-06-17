#include "image_transport.h"

#include <cstdint>
#include <cstdio>
#include <vector>

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

bool recv_all(SocketHandle socket, void* data, size_t size)
{
    char* cursor = static_cast<char*>(data);
    size_t remaining = size;
    while (remaining > 0) {
#if defined(_WIN32)
        const int received = recv(socket, cursor, static_cast<int>(remaining), 0);
#else
        const ssize_t received = recv(socket, cursor, remaining, 0);
#endif
        if (received <= 0) {
            return false;
        }
        cursor += received;
        remaining -= static_cast<size_t>(received);
    }
    return true;
}

bool recv_payload(SocketHandle socket, std::vector<uint8_t>* payload, uint64_t size)
{
    payload->resize(static_cast<size_t>(size));
    if (payload->empty()) {
        return true;
    }
    return recv_all(socket, payload->data(), payload->size());
}

} // namespace

int TcpImageServer::serve(uint16_t port, uint32_t maxFrames)
{
    return serve_with_callback(port, maxFrames, {});
}

int TcpImageServer::serve_with_callback(uint16_t port, uint32_t maxFrames, const FrameCallback& callback)
{
    SocketRuntime runtime;
    if (!runtime.ok()) {
        std::fprintf(stderr, "AXRB Image TCP: socket runtime init failed\n");
        return 1;
    }

    SocketHandle server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == kInvalidSocket) {
        std::fprintf(stderr, "AXRB Image TCP: socket creation failed\n");
        return 1;
    }

    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        std::fprintf(stderr, "AXRB Image TCP: bind failed on port %u\n", static_cast<unsigned>(port));
        close_socket(server);
        return 1;
    }

    if (listen(server, 1) != 0) {
        std::fprintf(stderr, "AXRB Image TCP: listen failed\n");
        close_socket(server);
        return 1;
    }

    std::fprintf(stderr, "AXRB Image TCP: listening on 0.0.0.0:%u\n", static_cast<unsigned>(port));
    uint32_t receivedFrames = 0;
    while (maxFrames == 0 || receivedFrames < maxFrames) {
        SocketHandle client = accept(server, nullptr, nullptr);
        if (client == kInvalidSocket) {
            std::fprintf(stderr, "AXRB Image TCP: accept failed\n");
            close_socket(server);
            return 1;
        }

        std::fprintf(stderr, "AXRB Image TCP: client connected\n");
        int receiveBufferSize = 4 * 1024 * 1024;
        setsockopt(client, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&receiveBufferSize), sizeof(receiveBufferSize));
        while (maxFrames == 0 || receivedFrames < maxFrames) {
            ImageFrameHeader header{};
            if (!recv_all(client, &header, sizeof(header))) {
                std::fprintf(stderr, "AXRB Image TCP: client disconnected\n");
                close_socket(client);
                break;
            }

            if (header.magic != kImageFrameMagic ||
                header.version != kImageFrameVersion ||
                header.header_size != sizeof(ImageFrameHeader) ||
                header.format != kImageFrameFormatRgba8 ||
                header.bytes_per_pixel != 4) {
                std::fprintf(stderr, "AXRB Image TCP: invalid image header\n");
                close_socket(client);
                break;
            }

            const uint64_t expected =
                static_cast<uint64_t>(header.width) * header.height * header.layers * header.bytes_per_pixel;
            if (header.payload_size != expected || header.payload_size > 128ull * 1024ull * 1024ull) {
                std::fprintf(stderr, "AXRB Image TCP: invalid payload size %llu\n",
                    static_cast<unsigned long long>(header.payload_size));
                close_socket(client);
                break;
            }

            std::vector<uint8_t> payload;
            if (!recv_payload(client, &payload, header.payload_size)) {
                std::fprintf(stderr, "AXRB Image TCP: client disconnected during payload\n");
                close_socket(client);
                break;
            }

            if (callback) {
                callback(header, std::move(payload));
            }

            if (receivedFrames % 10 == 0) {
                std::fprintf(
                    stderr,
                    "AXRB Image TCP: frame seq=%llu %ux%u layers=%u bytes=%llu\n",
                    static_cast<unsigned long long>(header.sequence),
                    header.width,
                    header.height,
                    header.layers,
                    static_cast<unsigned long long>(header.payload_size));
            }
            ++receivedFrames;
        }
    }

    close_socket(server);
    return 0;
}

} // namespace axrb::protocol
