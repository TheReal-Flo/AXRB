#pragma once

#include "pose_frame.h"

#include <cstdint>
#include <functional>

namespace axrb::protocol {

class TcpPoseServer {
public:
    int serve(uint16_t port, uint32_t maxFrames);
    int serve_with_producer(
        uint16_t port,
        uint32_t maxFrames,
        const std::function<PoseFrame(uint64_t)>& producer);
};

} // namespace axrb::protocol
