#pragma once
#include "image_frame.h"
#include "windows_gpu_frame.h"
#include <cstring>
#include <vector>
#include <unordered_set>
namespace axrb::protocol {
constexpr uint16_t kGpuBatchFrameVersion = 11;
struct GpuBatchPart {
    ImageFrameHeader header;
    ImageProjection projection;
    WindowsGpuFrame gpu;
};
static_assert(sizeof(GpuBatchPart) == 176);
inline bool valid_gpu_batch(const ImageFrameHeader& outer, const void* data, size_t bytes) {
    const uint32_t count = outer.reserved;
    if (!data || count < 2 || count > kMaxWireCompositionLayers || bytes != count * sizeof(GpuBatchPart) ||
        outer.payload_size != bytes || outer.sequence == UINT64_MAX || outer.sequence < count - 1) return false;
    std::unordered_set<uint64_t> sessions;
    sessions.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        GpuBatchPart part;
        std::memcpy(&part, static_cast<const uint8_t*>(data) + i * sizeof(part), sizeof(part));
        const auto& h = part.header;
        if (h.magic != kImageFrameMagic || !mixed_gpu_version(h.version) ||
            !valid_mixed_part(h.version, h.reserved) || h.reserved != ((count << 16) | i) ||
            h.sequence != outer.sequence - (count - 1) + i || h.monotonic_time_ns != outer.monotonic_time_ns ||
            h.header_size != sizeof(ImageFrameHeader) + sizeof(ImageProjection) ||
            h.type != kWindowsGpuFrameType || h.layers != 2 || h.format != kImageFrameFormatRgba8 ||
            h.bytes_per_pixel != 4 || h.payload_size != sizeof(WindowsGpuFrame) ||
            !valid_render_extent(h.width, h.height) || !part.gpu.session ||
            (part.gpu.formats[1] && part.gpu.formats[0] != part.gpu.formats[1]) ||
            (part.gpu.formats[0] != 37 && part.gpu.formats[0] != 43)) return false;
        if (i == 0 && (outer.width != h.width || outer.height != h.height)) return false;
        if (h.version == kMixedProjectionGpuFrameVersion ? !valid_projection(part.projection) :
            h.version == kMixedEquirectGpuFrameVersion ? !valid_equirect(part.projection) :
            (!valid_quads(part.projection) || part.projection.quad_count() != 1)) return false;
        if (!sessions.insert(part.gpu.session).second) return false;
    }
    return true;
}
}
