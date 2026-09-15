#pragma once

#include <cstdint>
#include <cmath>
#include <initializer_list>
#include "pose_frame.h"

namespace axrb::protocol {

constexpr uint32_t kImageFrameMagic = 0x49585241; // AXRI, little-endian.
constexpr uint16_t kImageFrameVersion = 1;
constexpr uint16_t kProjectionImageFrameVersion = 2;
constexpr uint16_t kImageFrameTypeRgba8 = 2;
constexpr uint32_t kImageFrameFormatRgba8 = 1;

struct ImageFrameHeader {
    uint32_t magic = kImageFrameMagic;
    uint16_t version = kImageFrameVersion;
    uint16_t type = kImageFrameTypeRgba8;
    uint32_t header_size = sizeof(ImageFrameHeader);
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t layers = 0;
    uint32_t format = kImageFrameFormatRgba8;
    uint32_t bytes_per_pixel = 4;
    uint32_t reserved = 0;
    uint64_t sequence = 0;
    uint64_t monotonic_time_ns = 0;
    uint64_t payload_size = 0;
};

static_assert(sizeof(ImageFrameHeader) == 64);

struct ImageProjectionView {
    Pose pose; // The render camera in the host's LOCAL coordinate space, meters.
    float angle_left = 0, angle_right = 0, angle_up = 0, angle_down = 0;
};
struct ImageProjection {
    uint32_t view_count = 0; // Zero for legacy frames; v2 requires two eyes.
    uint32_t reserved = 0;
    ImageProjectionView views[2];
};
static_assert(sizeof(ImageProjectionView) == 44);
static_assert(sizeof(ImageProjection) == 96);

inline bool valid_projection(const ImageProjection& projection) {
    if (projection.view_count != 2 || projection.reserved != 0) { return false; }
    for (const auto& view : projection.views) {
        const auto& p = view.pose;
        for (float value : {p.x, p.y, p.z, p.qx, p.qy, p.qz, p.qw,
                           view.angle_left, view.angle_right, view.angle_up, view.angle_down}) {
            if (!std::isfinite(value)) { return false; }
        }
        const float norm = p.qx*p.qx + p.qy*p.qy + p.qz*p.qz + p.qw*p.qw;
        if (std::fabs(norm - 1.0f) > 0.01f ||
            view.angle_left >= view.angle_right || view.angle_down >= view.angle_up ||
            view.angle_left <= -1.5707963f || view.angle_right >= 1.5707963f ||
            view.angle_down <= -1.5707963f || view.angle_up >= 1.5707963f) { return false; }
    }
    return true;
}

} // namespace axrb::protocol
