#include "pose_stream_decoder.h"
#include <array>
#include <cstdlib>
#include <cstring>

int main()
{
    using namespace axrb::protocol;
    std::array<PoseFrame, 3> frames{};
    for (size_t i = 0; i < frames.size(); ++i) {
        frames[i].sequence = i + 1;
        frames[i].hmd.x = static_cast<float>(i + 10);
    }
    // Every possible split of a TCP record must preserve the incomplete tail,
    // then drain coalesced records through to the newest pose.
    for (size_t split = 1; split < sizeof(PoseFrame); ++split) {
        PoseStreamDecoder decoder;
        PoseFrame latest{};
        const auto* data = reinterpret_cast<const unsigned char*>(frames.data());
        if (!decoder.append(data, split, latest) || latest.sequence != 0 ||
            !decoder.append(data + split, sizeof(frames) - split, latest) ||
            latest.sequence != 3 || latest.hmd.x != 12.0f) { return EXIT_FAILURE; }
    }
    PoseStreamDecoder decoder;
    PoseFrame latest{};
    if (!decoder.append(&frames[0], 31, latest)) { return EXIT_FAILURE; }
    decoder.reset(); // Reconnect must discard the previous connection's tail.
    if (!decoder.append(&frames[1], sizeof(PoseFrame), latest) || latest.sequence != 2) { return EXIT_FAILURE; }
    auto bad = frames[0];
    bad.magic = 0;
    if (decoder.append(&bad, sizeof(bad), latest) || latest.sequence != 2) { return EXIT_FAILURE; }
    return EXIT_SUCCESS;
}
