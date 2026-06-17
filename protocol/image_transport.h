#pragma once

#include "image_frame.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace axrb::protocol {

class TcpImageServer {
public:
    using FrameCallback = std::function<void(const ImageFrameHeader&, std::vector<uint8_t>&&)>;

    int serve(uint16_t port, uint32_t maxFrames);
    int serve_with_callback(uint16_t port, uint32_t maxFrames, const FrameCallback& callback);
};

} // namespace axrb::protocol
