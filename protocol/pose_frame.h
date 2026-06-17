#pragma once

#include <cstdint>

namespace axrb::protocol {

constexpr uint32_t kPoseFrameMagic = 0x42525841; // AXRB, little-endian.
constexpr uint16_t kPoseFrameVersion = 1;
constexpr uint16_t kPoseFrameType = 1;

struct Pose {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float qx = 0.0f;
    float qy = 0.0f;
    float qz = 0.0f;
    float qw = 1.0f;
};

struct PoseFrame {
    uint32_t magic = kPoseFrameMagic;
    uint16_t version = kPoseFrameVersion;
    uint16_t type = kPoseFrameType;
    uint64_t sequence = 0;
    uint64_t monotonic_time_ns = 0;
    Pose hmd;
    Pose left_controller;
    Pose right_controller;
};

static_assert(sizeof(Pose) == 28);
static_assert(sizeof(PoseFrame) == 112);

} // namespace axrb::protocol
