#include <android/log.h>
#include <cstddef>
#include <dlfcn.h>
#include <atomic>
#include "pcm_recovery.h"

// Forward declarations preserve the HIDL ABI without copying or modifying its
// objects. The original HAL still validates and constructs every AudioConfig.
// Upstream: device/generic/goldfish/hals/audio/{primary_device,util}.cpp in AOSP.
namespace android::hardware::audio::common::V7_0 { struct AudioConfig; }

namespace android::hardware::audio::V7_1::implementation::util {
bool checkAudioConfig(bool output, size_t durationMs, size_t alignment,
                      const common::V7_0::AudioConfig& requested,
                      common::V7_0::AudioConfig& suggested)
{
    using Original = bool (*)(bool, size_t, size_t,
                             const common::V7_0::AudioConfig&,
                             common::V7_0::AudioConfig&);
    static const auto original = reinterpret_cast<Original>(dlsym(RTLD_NEXT,
        "_ZN7android8hardware5audio4V7_114implementation4util16checkAudioConfigEbmmRKNS1_6common4V7_011AudioConfigERS7_"));
    if (!original) {
        __android_log_print(ANDROID_LOG_ERROR, "AXRB.Audio", "Missing original HAL configuration entry point");
        return false;
    }
    // Goldfish defaults to 22 ms (1088 frames at 48 kHz). Meta's EPS buffer
    // adapter cannot service a device burst larger than its 960-frame engine
    // block. 20 ms yields exactly 960 aligned frames, avoiding that adapter.
    // Retain a substantial hardware buffer: 5 ms bursts caused repeated PCM
    // I/O errors on the Windows emulator even though EPS rendered correctly.
    // Explicit frame counts and input streams remain under the original HAL's
    // control; only its default output duration changes.
    if (output && durationMs > 20) {
        __android_log_print(ANDROID_LOG_INFO, "AXRB.Audio",
                            "Default output burst: %zu -> 20 ms", durationMs);
        durationMs = 20;
    }
    return original(output, durationMs, alignment, requested, suggested);
}
}

extern "C" void* HIDL_FETCH_IDevicesFactory(const char* name)
{
    using Original = void* (*)(const char*);
    static const auto original = reinterpret_cast<Original>(
        dlsym(RTLD_NEXT, "HIDL_FETCH_IDevicesFactory"));
    return original ? original(name) : nullptr;
}

extern "C" int pcm_writei(void* pcm, const void* data, unsigned frames)
{
    using Write = int (*)(void*, const void*, unsigned);
    using Control = int (*)(void*);
    static const auto write = reinterpret_cast<Write>(dlsym(RTLD_NEXT, "pcm_writei"));
    static const auto stop = reinterpret_cast<Control>(dlsym(RTLD_NEXT, "pcm_stop"));
    static const auto prepare = reinterpret_cast<Control>(dlsym(RTLD_NEXT, "pcm_prepare"));
    if (!write) { errno = ENOSYS; return -1; }
    if (!stop || !prepare) return write(pcm, data, frames);
    bool recovered;
    const int result = axrb::audio::write_with_recovery(
        [&] { return write(pcm, data, frames); },
        [&] { return stop(pcm); }, [&] { return prepare(pcm); }, recovered);
    if (recovered) {
        static std::atomic<unsigned> count{0};
        const unsigned n = count.fetch_add(1, std::memory_order_relaxed) + 1;
        if ((n & (n - 1)) == 0) {
            __android_log_print(ANDROID_LOG_WARN, "AXRB.Audio", "Recovered stalled PCM output (%u)", n);
        }
    }
    return result;
}
