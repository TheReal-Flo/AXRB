#pragma once
#include "pose_frame.h"
#include <cstdint>

namespace axrb::host {
// Use a monotonic host clock; guest prediction times can jump during recentering.
class MenuShortcut {
public:
    void reset() { holding_ = false; }

    void apply(axrb::protocol::ControllerInput (&hands)[2], uint64_t nowNs) {
        using namespace axrb::protocol;
        const bool chord = hands[0].active && hands[1].active &&
            (hands[0].buttons & StickClick) && (hands[1].buttons & StickClick);
        if (!chord) { reset(); return; }
        if (!holding_ || nowNs < startedNs_) {
            holding_ = true;
            startedNs_ = nowNs;
        }
        if (nowNs - startedNs_ < 500000000ULL) return;
        hands[0].buttons |= MenuClick;
        // Once recognized, hold menu until release rather than repeatedly pulsing it.
        hands[0].buttons &= ~StickClick;
        hands[1].buttons &= ~StickClick;
    }
private:
    bool holding_ = false;
    uint64_t startedNs_ = 0;
};
}
