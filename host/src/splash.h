#pragma once
#if defined(_WIN32)
#include <cstdint>
#include <vector>
namespace axrb::host::detail {
// Decode once; callers cache the upload in each swapchain image.
std::vector<uint8_t> load_splash_pixels(uint32_t size, bool bgra, bool linear);
}
#endif
