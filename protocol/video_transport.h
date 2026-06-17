#pragma once

#include "video_packet.h"

#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

namespace axrb::protocol {

struct EncodedVideoFrame {
    uint32_t stream_id = 1;
    uint32_t codec = kEncodedVideoCodecOpaqueTest;
    uint64_t frame_id = 0;
    uint64_t capture_time_ns = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    bool key_frame = false;
    std::vector<uint8_t> payload;
};

class UdpVideoSender {
public:
    bool open(std::string_view host, uint16_t port);
    bool send_frame(const EncodedVideoFrame& frame);
    void close();

private:
    uintptr_t socket_ = 0;
    uint8_t address_[32]{};
    uint32_t address_size_ = 0;
};

class UdpVideoReceiver {
public:
    using FrameCallback = std::function<void(EncodedVideoFrame&&)>;

    int receive(uint16_t port, uint32_t max_frames, const FrameCallback& callback);
};

EncodedVideoFrame make_synthetic_encoded_frame(uint64_t frame_id, uint32_t width, uint32_t height, uint32_t payload_size);
EncodedVideoFrame make_synthetic_rgba_frame(uint64_t frame_id, uint32_t width, uint32_t height);

} // namespace axrb::protocol
