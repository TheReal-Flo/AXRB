#pragma once
#include <chrono>
#include <cstdint>
namespace axrb::host {
class FpsCounter {
public:
    using Clock = std::chrono::steady_clock;
    bool sample(uint64_t total, Clock::time_point now) {
        if (!started_) { started_ = true; previous_ = total; time_ = now; return false; }
        const double seconds = std::chrono::duration<double>(now - time_).count();
        if (seconds < 0.5) return false;
        fps_ = total >= previous_ ? (total - previous_) / seconds : 0;
        previous_ = total; time_ = now; return true;
    }
    double fps() const { return fps_; }
private:
    bool started_ = false;
    uint64_t previous_ = 0;
    Clock::time_point time_{};
    double fps_ = 0;
};
}
