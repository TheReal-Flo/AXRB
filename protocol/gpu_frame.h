#pragma once

#include <cstdint>

namespace axrb::protocol {

constexpr uint32_t kGpuFrameMagic = 0x47525841; // AXRG
constexpr uint16_t kGpuFrameVersion = 1;
constexpr uint16_t kGpuFrameTypeDmaBuf = 1;
constexpr uint32_t kGpuFrameFormatDrmFourcc = 1;
constexpr uint32_t kMaxGpuFramePlanes = 4;

struct GpuFramePlane {
    uint32_t fd_index = 0;
    uint32_t plane_index = 0;
    uint64_t offset = 0;
    uint64_t size = 0;
    uint32_t stride = 0;
    uint32_t reserved = 0;
};

struct GpuFrameDescriptor {
    uint32_t magic = kGpuFrameMagic;
    uint16_t version = kGpuFrameVersion;
    uint16_t type = kGpuFrameTypeDmaBuf;
    uint32_t header_size = sizeof(GpuFrameDescriptor);
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t layers = 1;
    uint32_t format_type = kGpuFrameFormatDrmFourcc;
    uint64_t drm_format = 0;
    uint64_t drm_modifier = 0;
    uint32_t plane_count = 0;
    uint32_t fd_count = 0;
    uint64_t sequence = 0;
    uint64_t monotonic_time_ns = 0;
    GpuFramePlane planes[kMaxGpuFramePlanes]{};
};

static_assert(sizeof(GpuFramePlane) == 32);
static_assert(sizeof(GpuFrameDescriptor) == 200);

} // namespace axrb::protocol
