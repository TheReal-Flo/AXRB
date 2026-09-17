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
            const size_t target = used_ < 8 ? 8 : record_size(partial_.version);
            const size_t amount = std::min(size, target - used_);
            std::memcpy(reinterpret_cast<unsigned char*>(&partial_) + used_, cursor, amount);
            used_ += amount;
            cursor += amount;
            size -= amount;
            if (used_ >= 8) {
                if (partial_.magic != kPoseFrameMagic || (partial_.version < 1 || partial_.version > kPoseFrameVersion) ||
                    partial_.type != kPoseFrameType) { return false; }
            }
            if (used_ == record_size(partial_.version)) {
                latest = partial_;
                used_ = 0;
                partial_ = {};
            }
        }
        return true;
    }
    void reset() { used_ = 0; partial_ = {}; }
private:
    static size_t record_size(uint16_t version) { return version == 1 ? 112 : version == 2 ? 160 : version == 3 ? 2360 : version == 4 ? 2368 : sizeof(PoseFrame); }
    PoseFrame partial_{};
    size_t used_ = 0;
};
}
