#pragma once

#include <cstdint>

namespace axrb::protocol {

constexpr uint32_t kImageFrameMagic = 0x49585241; // AXRI, little-endian.
constexpr uint16_t kImageFrameVersion = 1;
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

} // namespace axrb::protocol
