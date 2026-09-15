#pragma once
#include "pose_frame.h"
#include <algorithm>
#include <cstddef>
#include <cstring>

namespace axrb::protocol {
// TCP can split a pose anywhere or coalesce several poses. Retain partial
// records and expose the newest complete record, without waiting for another.
class PoseStreamDecoder {
public:
    bool append(const void* bytes, size_t size, PoseFrame& latest) {
        const auto* cursor = static_cast<const unsigned char*>(bytes);
        while (size) {
            const size_t amount = std::min(size, sizeof(partial_) - used_);
            std::memcpy(reinterpret_cast<unsigned char*>(&partial_) + used_, cursor, amount);
            used_ += amount;
            cursor += amount;
            size -= amount;
            if (used_ == sizeof(partial_)) {
                used_ = 0;
                if (partial_.magic != kPoseFrameMagic || partial_.version != kPoseFrameVersion ||
                    partial_.type != kPoseFrameType) { return false; }
                latest = partial_;
            }
        }
        return true;
    }
    void reset() { used_ = 0; }
private:
    PoseFrame partial_{};
    size_t used_ = 0;
};
}
