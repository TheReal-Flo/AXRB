#pragma once

#include <chrono>
#include <cstdio>
#include <mutex>
#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace axrb::protocol {

// Five-second wall-clock samples. Host and guest clocks are never subtracted.
class PerfStats {
public:
    explicit PerfStats(const char* name) : name_(name) {}
    void record(double milliseconds) {
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(mutex_);
        ++count_;
        total_ += milliseconds;
        if (milliseconds > maximum_) { maximum_ = milliseconds; }
        const double seconds = std::chrono::duration<double>(now - start_).count();
        if (seconds < 5.0) { return; }
#if defined(__ANDROID__)
        __android_log_print(ANDROID_LOG_INFO, "AXRB.Perf",
#else
        std::fprintf(stderr,
#endif
            "AXRB.Perf %s: rate=%.1f/s avg=%.3fms max=%.3fms samples=%llu\n",
            name_, count_ / seconds, total_ / count_, maximum_,
            static_cast<unsigned long long>(count_));
        count_ = 0;
        total_ = maximum_ = 0;
        start_ = now;
    }
private:
    const char* name_;
    std::mutex mutex_;
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
    unsigned long long count_ = 0;
    double total_ = 0, maximum_ = 0;
};

class PerfScope {
public:
    explicit PerfScope(PerfStats& stats) : stats_(stats) {}
    ~PerfScope() {
        stats_.record(std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start_).count());
    }
private:
    PerfStats& stats_;
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

} // namespace axrb::protocol
