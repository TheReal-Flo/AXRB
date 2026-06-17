#pragma once

#include <cstdint>

namespace axrb::protocol {

constexpr uint32_t kEncodedVideoPacketMagic = 0x56525841; // AXRV
constexpr uint16_t kEncodedVideoPacketVersion = 1;
constexpr uint16_t kEncodedVideoHeaderSize = 64;
constexpr uint32_t kEncodedVideoPayloadMax = 1200;

enum EncodedVideoCodec : uint32_t {
    kEncodedVideoCodecOpaqueTest = 0,
    kEncodedVideoCodecH264AnnexB = 1,
    kEncodedVideoCodecHevcAnnexB = 2,
    kEncodedVideoCodecAv1Obu = 3,
};

enum EncodedVideoPacketFlags : uint16_t {
    kEncodedVideoPacketKeyFrame = 1u << 0u,
    kEncodedVideoPacketEndOfFrame = 1u << 1u,
};

struct EncodedVideoPacketHeader {
    uint32_t magic = kEncodedVideoPacketMagic;
    uint16_t version = kEncodedVideoPacketVersion;
    uint16_t header_size = kEncodedVideoHeaderSize;
    uint32_t stream_id = 1;
    uint32_t codec = kEncodedVideoCodecOpaqueTest;
    uint64_t frame_id = 0;
    uint64_t capture_time_ns = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t frame_size = 0;
    uint32_t chunk_offset = 0;
    uint16_t chunk_size = 0;
    uint16_t flags = 0;
    uint32_t reserved = 0;
    uint64_t reserved2 = 0;
};

static_assert(sizeof(EncodedVideoPacketHeader) == kEncodedVideoHeaderSize);

} // namespace axrb::protocol
