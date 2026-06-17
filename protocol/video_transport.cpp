#include "video_transport.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

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
#include <netdb.h>
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

uint64_t monotonic_time_ns()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

bool socket_is_open(uintptr_t socket)
{
    return socket != 0 && static_cast<SocketHandle>(socket) != kInvalidSocket;
}

bool resolve_ipv4(std::string_view host, uint16_t port, sockaddr_in* out)
{
    std::memset(out, 0, sizeof(*out));
    out->sin_family = AF_INET;
    out->sin_port = htons(port);

    std::string host_string(host);
    if (inet_pton(AF_INET, host_string.c_str(), &out->sin_addr) == 1) {
        return true;
    }

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    addrinfo* result = nullptr;
    if (getaddrinfo(host_string.c_str(), nullptr, &hints, &result) != 0 || result == nullptr) {
        return false;
    }

    *out = *reinterpret_cast<sockaddr_in*>(result->ai_addr);
    out->sin_port = htons(port);
    freeaddrinfo(result);
    return true;
}

struct FrameAssembly {
    EncodedVideoFrame frame;
    std::vector<uint8_t> received;
    uint32_t received_bytes = 0;
};

bool valid_packet(const EncodedVideoPacketHeader& header, size_t packet_size)
{
    if (header.magic != kEncodedVideoPacketMagic ||
        header.version != kEncodedVideoPacketVersion ||
        header.header_size != sizeof(EncodedVideoPacketHeader)) {
        return false;
    }
    if (header.chunk_size == 0 || header.chunk_size > kEncodedVideoPayloadMax ||
        packet_size != sizeof(EncodedVideoPacketHeader) + header.chunk_size) {
        return false;
    }
    if (header.frame_size == 0 || header.frame_size > 64u * 1024u * 1024u ||
        header.chunk_offset > header.frame_size ||
        header.chunk_size > header.frame_size - header.chunk_offset) {
        return false;
    }
    return true;
}

} // namespace

bool UdpVideoSender::open(std::string_view host, uint16_t port)
{
    close();
    static SocketRuntime runtime;
    if (!runtime.ok()) {
        std::fprintf(stderr, "AXRB Video UDP: socket runtime init failed\n");
        return false;
    }

    sockaddr_in address{};
    if (!resolve_ipv4(host, port, &address)) {
        std::fprintf(stderr, "AXRB Video UDP: failed to resolve host\n");
        return false;
    }

    SocketHandle socket = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socket == kInvalidSocket) {
        std::fprintf(stderr, "AXRB Video UDP: socket creation failed\n");
        return false;
    }

    int send_buffer_size = 4 * 1024 * 1024;
    setsockopt(socket, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&send_buffer_size), sizeof(send_buffer_size));
    socket_ = static_cast<uintptr_t>(socket);
    std::memcpy(address_, &address, sizeof(address));
    address_size_ = sizeof(address);
    std::fprintf(stderr, "AXRB Video UDP: sender connected to %.*s:%u\n",
                 static_cast<int>(host.size()),
                 host.data(),
                 static_cast<unsigned>(port));
    return true;
}

bool UdpVideoSender::send_frame(const EncodedVideoFrame& frame)
{
    if (!socket_is_open(socket_) || frame.payload.empty() || frame.payload.size() > 64u * 1024u * 1024u) {
        return false;
    }

    uint32_t offset = 0;
    while (offset < frame.payload.size()) {
        const uint32_t chunk_size = std::min<uint32_t>(kEncodedVideoPayloadMax, static_cast<uint32_t>(frame.payload.size()) - offset);
        std::vector<uint8_t> packet(sizeof(EncodedVideoPacketHeader) + chunk_size);

        EncodedVideoPacketHeader header{};
        header.stream_id = frame.stream_id;
        header.codec = frame.codec;
        header.frame_id = frame.frame_id;
        header.capture_time_ns = frame.capture_time_ns;
        header.width = frame.width;
        header.height = frame.height;
        header.frame_size = static_cast<uint32_t>(frame.payload.size());
        header.chunk_offset = offset;
        header.chunk_size = static_cast<uint16_t>(chunk_size);
        header.flags = 0;
        if (frame.key_frame) {
            header.flags |= kEncodedVideoPacketKeyFrame;
        }
        if (offset + chunk_size == frame.payload.size()) {
            header.flags |= kEncodedVideoPacketEndOfFrame;
        }

        std::memcpy(packet.data(), &header, sizeof(header));
        std::memcpy(packet.data() + sizeof(EncodedVideoPacketHeader), frame.payload.data() + offset, chunk_size);
#if defined(_WIN32)
        const int sent = sendto(static_cast<SocketHandle>(socket_),
                                reinterpret_cast<const char*>(packet.data()),
                                static_cast<int>(packet.size()),
                                0,
                                reinterpret_cast<const sockaddr*>(address_),
                                static_cast<int>(address_size_));
#else
        const ssize_t sent = sendto(static_cast<SocketHandle>(socket_),
                                    packet.data(),
                                    packet.size(),
                                    0,
                                    reinterpret_cast<const sockaddr*>(address_),
                                    address_size_);
#endif
        if (sent != static_cast<decltype(sent)>(packet.size())) {
            std::fprintf(stderr, "AXRB Video UDP: short send at frame=%llu offset=%u sent=%lld expected=%zu\n",
                         static_cast<unsigned long long>(frame.frame_id),
                         offset,
                         static_cast<long long>(sent),
                         packet.size());
            return false;
        }

        offset += chunk_size;
    }

    return true;
}

void UdpVideoSender::close()
{
    if (socket_is_open(socket_)) {
        close_socket(static_cast<SocketHandle>(socket_));
        socket_ = 0;
    }
}

int UdpVideoReceiver::receive(uint16_t port, uint32_t max_frames, const FrameCallback& callback)
{
    SocketRuntime runtime;
    if (!runtime.ok()) {
        std::fprintf(stderr, "AXRB Video UDP: socket runtime init failed\n");
        return 1;
    }

    SocketHandle socket = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socket == kInvalidSocket) {
        std::fprintf(stderr, "AXRB Video UDP: socket creation failed\n");
        return 1;
    }

    int reuse = 1;
    setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
    int receive_buffer_size = 8 * 1024 * 1024;
    setsockopt(socket, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&receive_buffer_size), sizeof(receive_buffer_size));
#if defined(_WIN32)
    DWORD receive_timeout_ms = 3000;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&receive_timeout_ms), sizeof(receive_timeout_ms));
#else
    timeval receive_timeout{};
    receive_timeout.tv_sec = 3;
    receive_timeout.tv_usec = 0;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &receive_timeout, sizeof(receive_timeout));
#endif

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    if (bind(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        std::fprintf(stderr, "AXRB Video UDP: bind failed on port %u\n", static_cast<unsigned>(port));
        close_socket(socket);
        return 1;
    }

    std::fprintf(stderr, "AXRB Video UDP: listening on 0.0.0.0:%u\n", static_cast<unsigned>(port));
    std::unordered_map<uint64_t, FrameAssembly> frames;
    uint32_t completed = 0;
    uint32_t invalid_packets = 0;
    while (max_frames == 0 || completed < max_frames) {
        uint8_t packet[sizeof(EncodedVideoPacketHeader) + kEncodedVideoPayloadMax]{};
#if defined(_WIN32)
        const int received = recv(socket, reinterpret_cast<char*>(packet), sizeof(packet), 0);
#else
        const ssize_t received = recv(socket, packet, sizeof(packet), 0);
#endif
        if (received <= 0) {
            std::fprintf(stderr,
                         "AXRB Video UDP: receive timeout after %u complete frames, invalid_packets=%u\n",
                         static_cast<unsigned>(completed),
                         static_cast<unsigned>(invalid_packets));
            if (max_frames == 0) {
                continue;
            }
            close_socket(socket);
            return completed == max_frames ? 0 : 1;
        }

        const auto* header = reinterpret_cast<const EncodedVideoPacketHeader*>(packet);
        if (!valid_packet(*header, static_cast<size_t>(received))) {
            ++invalid_packets;
            continue;
        }

        auto& assembly = frames[header->frame_id];
        if (assembly.frame.payload.empty()) {
            assembly.frame.stream_id = header->stream_id;
            assembly.frame.codec = header->codec;
            assembly.frame.frame_id = header->frame_id;
            assembly.frame.capture_time_ns = header->capture_time_ns;
            assembly.frame.width = header->width;
            assembly.frame.height = header->height;
            assembly.frame.key_frame = (header->flags & kEncodedVideoPacketKeyFrame) != 0;
            assembly.frame.payload.resize(header->frame_size);
            assembly.received.resize(header->frame_size);
        }

        if (assembly.frame.payload.size() != header->frame_size) {
            frames.erase(header->frame_id);
            continue;
        }

        std::memcpy(assembly.frame.payload.data() + header->chunk_offset,
                    packet + sizeof(EncodedVideoPacketHeader),
                    header->chunk_size);
        for (uint32_t i = 0; i < header->chunk_size; ++i) {
            if (assembly.received[header->chunk_offset + i] == 0) {
                assembly.received[header->chunk_offset + i] = 1;
                ++assembly.received_bytes;
            }
        }

        if (assembly.received_bytes == assembly.frame.payload.size()) {
            if (callback) {
                callback(std::move(assembly.frame));
            }
            frames.erase(header->frame_id);
            ++completed;
            if (completed % 90 == 0) {
                std::fprintf(stderr, "AXRB Video UDP: received %u complete frames\n", completed);
            }
        }

        while (frames.size() > 8) {
            const auto oldest = std::min_element(frames.begin(), frames.end(), [](const auto& left, const auto& right) {
                return left.first < right.first;
            });
            if (oldest == frames.end()) {
                break;
            }
            frames.erase(oldest);
        }
    }

    close_socket(socket);
    return 0;
}

EncodedVideoFrame make_synthetic_encoded_frame(uint64_t frame_id, uint32_t width, uint32_t height, uint32_t payload_size)
{
    EncodedVideoFrame frame{};
    frame.frame_id = frame_id;
    frame.capture_time_ns = monotonic_time_ns();
    frame.width = width;
    frame.height = height;
    frame.key_frame = frame_id % 90 == 0;
    frame.payload.resize(payload_size);
    for (uint32_t i = 0; i < payload_size; ++i) {
        frame.payload[i] = static_cast<uint8_t>((frame_id * 31 + i * 17) & 0xff);
    }
    return frame;
}

EncodedVideoFrame make_synthetic_rgba_frame(uint64_t frame_id, uint32_t width, uint32_t height)
{
    EncodedVideoFrame frame{};
    frame.codec = kEncodedVideoCodecAxrbRgba8;
    frame.frame_id = frame_id;
    frame.capture_time_ns = monotonic_time_ns();
    frame.width = width;
    frame.height = height;
    frame.key_frame = true;
    frame.payload.resize(static_cast<size_t>(width) * height * 4);

    const uint32_t tick = static_cast<uint32_t>(frame_id);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const size_t offset = (static_cast<size_t>(y) * width + x) * 4;
            const bool marker = ((x / 16 + y / 16 + tick) % 2) == 0;
            frame.payload[offset + 0] = static_cast<uint8_t>((x * 255u) / (width > 1 ? width - 1 : 1));
            frame.payload[offset + 1] = static_cast<uint8_t>((y * 255u) / (height > 1 ? height - 1 : 1));
            frame.payload[offset + 2] = marker ? 255 : static_cast<uint8_t>((tick * 7u) & 0xff);
            frame.payload[offset + 3] = 255;
        }
    }
    return frame;
}

} // namespace axrb::protocol
