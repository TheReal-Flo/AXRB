#include "video_transport.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <thread>

int main()
{
    constexpr uint16_t kPort = 39591;
    constexpr uint32_t kFrames = 16;
    constexpr uint32_t kPayloadSize = 8192;

    std::atomic_uint32_t received_frames = 0;
    std::atomic_bool payload_ok = true;

    std::thread receiver_thread([&] {
        axrb::protocol::UdpVideoReceiver receiver;
        receiver.receive(kPort, kFrames, [&](axrb::protocol::EncodedVideoFrame&& frame) {
            const auto expected = axrb::protocol::make_synthetic_encoded_frame(
                frame.frame_id,
                frame.width,
                frame.height,
                static_cast<uint32_t>(frame.payload.size()));
            payload_ok = payload_ok && frame.payload == expected.payload;
            ++received_frames;
        });
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    axrb::protocol::UdpVideoSender sender;
    if (!sender.open("127.0.0.1", kPort)) {
        receiver_thread.detach();
        return EXIT_FAILURE;
    }

    for (uint32_t i = 0; i < kFrames; ++i) {
        auto frame = axrb::protocol::make_synthetic_encoded_frame(i, 1920, 1080, kPayloadSize);
        if (!sender.send_frame(frame)) {
            receiver_thread.detach();
            return EXIT_FAILURE;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    receiver_thread.join();
    return received_frames == kFrames && payload_ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
