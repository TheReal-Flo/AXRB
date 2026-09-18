#pragma once
#include <array>
#include <memory>
#include <mutex>
#include <functional>

namespace axrb::host {
// Bounded storage, not a FIFO. The caller publishes only its newest completed
// lease. A lease pins its slot until all readers (including GPU reads) finish.
template<class T, size_t Count = 3>
class FramePool {
    struct Slot { std::unique_ptr<T> value; bool busy = false, retired = false; };
    struct State { std::mutex mutex; std::array<Slot, Count> slots; std::function<void(T&)> trim; };
    std::shared_ptr<State> state_ = std::make_shared<State>();
public:
    std::shared_ptr<T> acquire() {
        std::lock_guard lock(state_->mutex);
        for (size_t i = 0; i < Count; ++i) {
            auto& slot = state_->slots[i];
            if (slot.busy) continue;
            if (!slot.value) slot.value = std::make_unique<T>();
            slot.busy = true;
            return {slot.value.get(), [state = state_, i](T*) {
                std::lock_guard releaseLock(state->mutex);
                auto& released = state->slots[i];
                if (released.retired) { released.value.reset(); released.retired = false; }
                if (released.value && state->trim) state->trim(*released.value);
                released.busy = false;
            }};
        }
        return {}; // Never overwrite a reader or grow an unbounded queue.
    }
    // Busy leases are trimmed only after their final reader releases them.
    template<class F> void trim(F operation) {
        std::lock_guard lock(state_->mutex);
        state_->trim = operation;
        for (auto& slot : state_->slots) if (!slot.busy && slot.value) operation(*slot.value);
    }
    void retire(const std::shared_ptr<T>& lease) {
        std::lock_guard lock(state_->mutex);
        for (auto& slot : state_->slots)
            if (slot.value.get() == lease.get()) slot.retired = true;
    }
};
}
