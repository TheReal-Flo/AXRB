// Live Android integration test. tests/integration/test_android_pose_concurrency.py feeds
// fragmented records while four consumers share the real PoseClient.
#include "pose_client.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

int main() {
    std::atomic<unsigned> errors{0}, completed{0};
    std::vector<std::thread> readers;
    for (int i=0;i<4;++i) readers.emplace_back([&] {
        uint64_t previous=0; unsigned changes=0;
        const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while (std::chrono::steady_clock::now()<end) {
            const auto frame=axrb::runtime::pose_client().latest_pose_frame();
            if (frame.sequence) {
                if (frame.sequence<previous || frame.hmd.x!=float(frame.sequence%1024) ||
                    frame.hmd.y!=-frame.hmd.x || frame.hmd.qw!=1) ++errors;
                if (frame.sequence!=previous) ++changes;
                previous=frame.sequence;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        if (changes<100) ++errors;
        std::printf("reader: updates=%u final_sequence=%llu\n",changes,(unsigned long long)previous);
        ++completed;
    });
    for (auto& thread:readers) thread.join();
    std::printf("readers=%u errors=%u\n",completed.load(),errors.load());
    return errors ? 1 : 0;
}
