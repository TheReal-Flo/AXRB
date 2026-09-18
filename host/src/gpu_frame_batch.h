#pragma once
namespace axrb::host { struct GpuFrameBatch; }
#if defined(_WIN32)
#include "windows_gpu_receiver.h"
#include "image_transport.h"
#include <deque>

namespace axrb::host {
struct GpuFrameBatch {
    struct Part {
        WindowsGpuReceiver receiver;
        protocol::ImageFrameHeader header{};
        protocol::ImageProjection projection{};
    };
    std::deque<Part> parts;
    void resize(uint32_t size) { parts.resize(size); count = size; }
    void trim(uint32_t size) { if (parts.size() > size) { parts.resize(size); count = size; } }
    uint32_t count = 0;
};
}
#endif
