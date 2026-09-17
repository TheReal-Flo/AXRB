#pragma once

#include <cstdint>

namespace axrb::protocol {

constexpr uint32_t kPoseFrameMagic = 0x42525841; // AXRB, little-endian.
constexpr uint16_t kPoseFrameVersion = 5;
constexpr uint32_t kMaxEyeDimension = 8192;
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

enum ControllerButton : uint32_t {
    PrimaryClick = 1u << 0, SecondaryClick = 1u << 1, MenuClick = 1u << 2,
    StickClick = 1u << 3, PrimaryTouch = 1u << 4, SecondaryTouch = 1u << 5,
    TriggerTouch = 1u << 6, StickTouch = 1u << 7, ThumbrestTouch = 1u << 8,
};

struct ControllerInput {
    uint32_t active = 0;
    uint32_t buttons = 0;
    float trigger = 0, squeeze = 0, stick_x = 0, stick_y = 0;
};

struct HandJoint {
    uint64_t flags = 0;
    Pose pose;
    float radius = 0;
};
struct HandSkeleton {
    uint32_t active = 0;
    uint32_t source = 0; // XrHandTrackingDataSourceEXT: 1 optical, 2 controller.
    HandJoint joints[26];
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
    uint32_t reserved = 0; // Preserve the complete 112-byte v1 prefix.
    ControllerInput controllers[2]; // Preserve the complete 160-byte v2 prefix.
    Pose aim[2];
    uint64_t grip_flags[2]{};
    uint64_t aim_flags[2]{};
    uint32_t aim_active[2]{};
    HandSkeleton hands[2];
    uint32_t hand_tracking_supported = 0;
    // Optional v3 reserved-word extension. Zero means unknown (older hosts).
    // A duration, not a timestamp: no host/guest clock subtraction is needed.
    uint32_t display_period_ns = 0;
    uint32_t render_width = 0, render_height = 0; // v4: host stereo recommendation.
    Pose local_origin; // v5: host LOCAL origin in the transmitted tracking world.
    uint32_t local_origin_flags = 0;
    uint32_t hmd_flags = 0;
    uint32_t reserved_v5 = 0;
};

inline bool valid_render_extent(uint32_t width, uint32_t height) {
    return width && height && width <= kMaxEyeDimension && height <= kMaxEyeDimension;
}

inline bool valid_display_period(uint64_t period) {
    return period >= 4'000'000 && period <= 25'000'000; // 40–250 Hz; exclude standby.
}
inline uint32_t display_period_or_default(const PoseFrame& frame) {
    return valid_display_period(frame.display_period_ns) ? frame.display_period_ns : 11'111'111;
}

static_assert(sizeof(Pose) == 28);
static_assert(sizeof(ControllerInput) == 24);
static_assert(sizeof(PoseFrame) == 2408);

} // namespace axrb::protocol
