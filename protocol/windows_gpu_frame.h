#pragma once
#include <cstdint>
namespace axrb::protocol {
constexpr uint64_t kGpuMarkerMagic = 0x4158524247505531ULL;
// A standard vkCmdUpdateBuffer packet marks AXRB's following two eye blits.
// The host layer writes status=1 only after recording both shared-image copies.
struct WindowsGpuMarker {
    uint64_t magic = kGpuMarkerMagic;
    uint64_t session = 0;
    uint64_t sequence = 0;
    uint32_t width = 0, height = 0;
    uint32_t formats[2]{};
    uint32_t status = 0;
    uint32_t reserved[5]{};
};
static_assert(sizeof(WindowsGpuMarker) == 64);
struct WindowsGpuFrame { uint64_t session = 0; uint32_t formats[2]{}; };
static_assert(sizeof(WindowsGpuFrame) == 16);
constexpr uint16_t kWindowsGpuFrameVersion = 3;
constexpr uint16_t kWindowsGpuFrameType = 3;
}
