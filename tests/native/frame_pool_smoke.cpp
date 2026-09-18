#include "../../host/src/frame_pool.h"
#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "Frame pool check failed: %s\n", #x); return 1; } } while (0)
struct Frame { unsigned sequence = 0; unsigned layers[5]{}; };
int main() {
    axrb::host::FramePool<Frame> pool;
    auto displayed = pool.acquire();
    auto latest = pool.acquire();
    auto writing = pool.acquire();
    CHECK(displayed && latest && writing && !pool.acquire());
    displayed->sequence = 17;
    for (auto& layer : displayed->layers) layer = 17;
    auto pinned = displayed;
    displayed.reset();
    CHECK(!pool.acquire());
    writing.reset();
    std::atomic<bool> correct{true};
    std::thread producer([&] {
        for (unsigned sequence = 18; sequence < 100018; ++sequence) {
            auto next = pool.acquire();
            if (!next) { correct = false; return; }
            next->sequence = sequence;
            for (auto& layer : next->layers) layer = sequence;
            latest = std::move(next);
        }
    });
    producer.join();
    CHECK(correct && pinned->sequence == 17);
    for (auto layer : pinned->layers) CHECK(layer == 17);
    CHECK(latest->sequence == 100017);
    for (auto layer : latest->layers) CHECK(layer == latest->sequence);
    pool.retire(pinned);
    pinned.reset(); latest.reset();
    auto clean = pool.acquire();
    CHECK(clean && clean->sequence == 0);
    // A presentation lease may outlive the owning session without a dangling
    // pool pointer in its destructor.
    std::shared_ptr<Frame> survivor;
    { axrb::host::FramePool<Frame> temporary; survivor = temporary.acquire(); survivor->sequence = 9; }
    CHECK(survivor->sequence == 9);
    struct DynamicFrame { std::vector<unsigned> layers; };
    axrb::host::FramePool<DynamicFrame> dynamic;
    auto old = dynamic.acquire(); old->layers.resize(40, 42);
    auto newer = dynamic.acquire(); newer->layers.resize(17, 7);
    dynamic.trim([](auto& frame) { if (frame.layers.size() > 3) frame.layers.resize(3); });
    CHECK(old->layers.size() == 40 && old->layers.back() == 42);
    CHECK(newer->layers.size() == 17);
    old.reset();
    auto recycled = dynamic.acquire(); CHECK(recycled->layers.size() == 3);
    newer.reset(); recycled.reset();
    dynamic.trim([](auto& frame) { frame.layers.clear(); });
    CHECK(dynamic.acquire()->layers.empty());
    std::puts("Frame pool: bounded reuse, pinned five-layer frame, retirement and lifetime passed.");
}
