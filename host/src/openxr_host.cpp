#include "openxr_host.h"
#include "openxr_session.h"

namespace axrb::host {
using namespace detail;
namespace {
bool parse_u16(const char* text, uint16_t* value)
{
    uint32_t parsed = 0;
    const std::string_view input{text};
    const auto result = std::from_chars(input.data(), input.data() + input.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != input.data() + input.size() || parsed > 65535) {
        return false;
    }
    *value = static_cast<uint16_t>(parsed);
    return true;
}

bool parse_u32(const char* text, uint32_t* value)
{
    const std::string_view input{text};
    const auto result = std::from_chars(input.data(), input.data() + input.size(), *value);
    return result.ec == std::errc{} && result.ptr == input.data() + input.size();
}

void print_usage()
{
    std::fprintf(stderr, "Usage:\n");
    std::fprintf(stderr, "  axrb-host-bridge --serve [port] [frames]\n");
    std::fprintf(stderr, "  axrb-host-bridge --serve-openxr [port] [frames] [game-name]\n");
    std::fprintf(stderr, "  axrb-host-bridge --serve-images [port] [frames]\n");
    std::fprintf(stderr, "  axrb-host-bridge --serve-gpu-fds [socket-path] [frames]\n");
    std::fprintf(stderr, "  axrb-host-bridge --video-recv-udp [port] [frames]\n");
    std::fprintf(stderr, "  axrb-host-bridge --video-send-synthetic [host] [port] [frames] [fps]\n");
    std::fprintf(stderr, "  axrb-host-bridge --video-send-rgba [host] [port] [frames] [fps] [width] [height]\n");
    std::fprintf(stderr, "  axrb-host-bridge --smoke\n");
}

bool decode_video_frame_to_image(const axrb::protocol::EncodedVideoFrame& frame,
                                 axrb::protocol::ImageFrameHeader* header,
                                 std::vector<uint8_t>* pixels)
{
    if (frame.codec != axrb::protocol::kEncodedVideoCodecAxrbRgba8) {
        return false;
    }
    const uint64_t expected = static_cast<uint64_t>(frame.width) * frame.height * 4;
    if (frame.width == 0 || frame.height == 0 || frame.payload.size() != expected) {
        return false;
    }

    axrb::protocol::ImageFrameHeader outHeader{};
    outHeader.width = frame.width;
    outHeader.height = frame.height;
    outHeader.layers = 1;
    outHeader.sequence = frame.frame_id;
    outHeader.monotonic_time_ns = frame.capture_time_ns;
    outHeader.payload_size = expected;
    *header = outHeader;
    *pixels = frame.payload;
    return true;
}


} // namespace
int OpenXrHost::run(int argc, char** argv)
{
    if (argc <= 1 || std::string_view(argv[1]) == "--smoke") {
        std::fprintf(stderr, "AXRB host bridge smoke OK\n");
        return 0;
    }

    const std::string_view mode(argv[1]);
    if (mode == "--serve-gpu-fds") {
        const char* socket_path = argc >= 3 ? argv[2] : "/tmp/axrb-gpu-frame.sock";
        uint32_t frames = 0;
        if (argc >= 4 && !parse_u32(argv[3], &frames)) {
            std::fprintf(stderr, "Invalid frame count: %s\n", argv[3]);
            return 2;
        }

        axrb::protocol::UnixFdFrameServer server;
        return server.serve(socket_path, frames, [](const axrb::protocol::GpuFrameDescriptor& descriptor,
                                                    std::vector<axrb::protocol::UniqueFd>&& fds) {
            std::fprintf(stderr,
                         "AXRB GPU: frame seq=%llu %ux%u layers=%u drm_format=0x%llx modifier=0x%llx planes=%u fds=%zu\n",
                         static_cast<unsigned long long>(descriptor.sequence),
                         descriptor.width,
                         descriptor.height,
                         descriptor.layers,
                         static_cast<unsigned long long>(descriptor.drm_format),
                         static_cast<unsigned long long>(descriptor.drm_modifier),
                         descriptor.plane_count,
                         fds.size());
        });
    }

    if (mode == "--serve-images") {
        uint16_t port = 38491;
        uint32_t frames = 0;
        if (argc >= 3 && !parse_u16(argv[2], &port)) { return 2; }
        if (argc >= 4 && !parse_u32(argv[3], &frames)) { return 2; }
        axrb::protocol::TcpImageServer receiver;
        return receiver.serve(port, frames);
    }

    if (mode == "--video-recv-udp") {
        uint16_t port = 38492;
        uint32_t frames = 0;
        if (argc >= 3 && !parse_u16(argv[2], &port)) {
            std::fprintf(stderr, "Invalid port: %s\n", argv[2]);
            return 2;
        }
        if (argc >= 4 && !parse_u32(argv[3], &frames)) {
            std::fprintf(stderr, "Invalid frame count: %s\n", argv[3]);
            return 2;
        }

        uint64_t first_time = 0;
        uint64_t last_time = 0;
        uint32_t received = 0;
        axrb::protocol::UdpVideoReceiver receiver;
        return receiver.receive(port, frames, [&](axrb::protocol::EncodedVideoFrame&& frame) {
            if (first_time == 0) {
                first_time = monotonic_time_ns();
            }
            last_time = monotonic_time_ns();
            ++received;
            if (received == 1 || received % 90 == 0) {
                const double seconds = last_time > first_time ? static_cast<double>(last_time - first_time) / 1000000000.0 : 0.0;
                const double fps = seconds > 0.0 ? static_cast<double>(received - 1) / seconds : 0.0;
                std::fprintf(stderr,
                             "AXRB Video UDP: frame=%llu %ux%u codec=%u bytes=%zu avg_fps=%.2f\n",
                             static_cast<unsigned long long>(frame.frame_id),
                             frame.width,
                             frame.height,
                             frame.codec,
                             frame.payload.size(),
                             fps);
            }
        });
    }

    if (mode == "--video-send-synthetic") {
        const char* host = argc >= 3 ? argv[2] : "127.0.0.1";
        uint16_t port = 38492;
        uint32_t frames = 900;
        uint32_t fps = 90;
        if (argc >= 4 && !parse_u16(argv[3], &port)) {
            std::fprintf(stderr, "Invalid port: %s\n", argv[3]);
            return 2;
        }
        if (argc >= 5 && !parse_u32(argv[4], &frames)) {
            std::fprintf(stderr, "Invalid frame count: %s\n", argv[4]);
            return 2;
        }
        if (argc >= 6 && (!parse_u32(argv[5], &fps) || fps == 0)) {
            std::fprintf(stderr, "Invalid fps: %s\n", argv[5]);
            return 2;
        }

        axrb::protocol::UdpVideoSender sender;
        if (!sender.open(host, port)) {
            return 1;
        }

        const auto frame_interval = std::chrono::nanoseconds(1000000000ull / fps);
        auto next_frame = std::chrono::steady_clock::now();
        for (uint32_t i = 0; i < frames; ++i) {
            auto frame = axrb::protocol::make_synthetic_encoded_frame(i, 1920, 1080, 64 * 1024);
            if (!sender.send_frame(frame)) {
                std::fprintf(stderr, "AXRB Video UDP: send failed at frame %u\n", i);
                return 1;
            }
            next_frame += frame_interval;
            std::this_thread::sleep_until(next_frame);
        }
        return 0;
    }

    if (mode == "--video-send-rgba") {
        const char* host = argc >= 3 ? argv[2] : "127.0.0.1";
        uint16_t port = 38492;
        uint32_t frames = 900;
        uint32_t fps = 90;
        uint32_t width = 320;
        uint32_t height = 180;
        if (argc >= 4 && !parse_u16(argv[3], &port)) {
            std::fprintf(stderr, "Invalid port: %s\n", argv[3]);
            return 2;
        }
        if (argc >= 5 && !parse_u32(argv[4], &frames)) {
            std::fprintf(stderr, "Invalid frame count: %s\n", argv[4]);
            return 2;
        }
        if (argc >= 6 && (!parse_u32(argv[5], &fps) || fps == 0)) {
            std::fprintf(stderr, "Invalid fps: %s\n", argv[5]);
            return 2;
        }
        if (argc >= 7 && !parse_u32(argv[6], &width)) {
            std::fprintf(stderr, "Invalid width: %s\n", argv[6]);
            return 2;
        }
        if (argc >= 8 && !parse_u32(argv[7], &height)) {
            std::fprintf(stderr, "Invalid height: %s\n", argv[7]);
            return 2;
        }

        axrb::protocol::UdpVideoSender sender;
        if (!sender.open(host, port)) {
            return 1;
        }

        const auto frame_interval = std::chrono::nanoseconds(1000000000ull / fps);
        auto next_frame = std::chrono::steady_clock::now();
        for (uint32_t i = 0; i < frames; ++i) {
            auto frame = axrb::protocol::make_synthetic_rgba_frame(i, width, height);
            if (!sender.send_frame(frame)) {
                std::fprintf(stderr, "AXRB Video UDP: RGBA send failed at frame %u\n", i);
                return 1;
            }
            next_frame += frame_interval;
            std::this_thread::sleep_until(next_frame);
        }
        return 0;
    }

    if (mode != "--serve" && mode != "--serve-openxr") {
        print_usage();
        return 2;
    }

    uint16_t port = 38490;
    uint32_t frames = 0;
    if (argc >= 3 && !parse_u16(argv[2], &port)) {
        std::fprintf(stderr, "Invalid port: %s\n", argv[2]);
        return 2;
    }
    if (argc >= 4 && !parse_u32(argv[3], &frames)) {
        std::fprintf(stderr, "Invalid frame count: %s\n", argv[3]);
        return 2;
    }

    axrb::protocol::TcpPoseServer server;
    if (mode == "--serve-openxr") {
        auto imageFrame = std::make_shared<HostImageFrame>();
        auto poseSource = std::make_shared<OpenXrSession>(imageFrame.get());
        const std::string gameName = argc >= 5 ? argv[4] : "Android game";
        if (!poseSource->initialize(gameName) || !poseSource->open_mirror(gameName)) {
            return 1;
        }
        std::thread imageThread([imageFrame, poseSource] {
            axrb::protocol::TcpImageServer imageServer;
            imageServer.serve_with_callback(
                38491,
                0,
                [&](const axrb::protocol::ImageFrameHeader& header, const axrb::protocol::ImageProjection& projection, std::vector<uint8_t>&& pixels) {
                    return poseSource->receive_image(header, projection, std::move(pixels));
                });
        });
        imageThread.detach();

        std::thread videoThread([imageFrame] {
            axrb::protocol::UdpVideoReceiver videoReceiver;
            videoReceiver.receive(38492, 0, [&](axrb::protocol::EncodedVideoFrame&& frame) {
                axrb::protocol::ImageFrameHeader header{};
                std::vector<uint8_t> pixels;
                if (!decode_video_frame_to_image(frame, &header, &pixels)) {
                    static bool reportedUnsupported = false;
                    if (!reportedUnsupported) {
                        std::fprintf(stderr, "AXRB Video UDP: received unsupported codec %u for OpenXR image feed\n", frame.codec);
                        reportedUnsupported = true;
                    }
                    return;
                }
                imageFrame->store(header, std::move(pixels));
            });
        });
        videoThread.detach();

        std::thread poseThread([poseSource, imageFrame, port, frames] {
            axrb::protocol::TcpPoseServer poseServer;
            poseServer.serve_with_producer(port, frames, [&](uint64_t sequence) {
                return poseSource->latest_frame(sequence);
            });
        });
        poseThread.detach();

        uint64_t sequence = 0;
        while ((frames == 0 || sequence < frames) && poseSource->pump_mirror()) {
            poseSource->make_frame(sequence++);
            // OpenXR paces an active frame loop. An extra Windows sleep can
            // consume a timer tick and lower the compositor submission rate.
            if (!poseSource->drives_frame_loop()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        return 0;
   }

    return server.serve(port, frames);
}

} // namespace axrb::host
