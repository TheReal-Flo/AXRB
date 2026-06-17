#include "gpu_transport.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>

#include <fcntl.h>
#include <unistd.h>

int main()
{
    const std::string socketPath = "/tmp/axrb-gpu-transport-smoke-" + std::to_string(getpid()) + ".sock";
    std::atomic_bool received = false;

    axrb::protocol::UnixFdFrameServer server;
    std::thread serverThread([&] {
        server.serve(socketPath.c_str(), 1, [&](const axrb::protocol::GpuFrameDescriptor& descriptor,
                                                std::vector<axrb::protocol::UniqueFd>&& fds) {
            received = descriptor.sequence == 42 && descriptor.width == 16 && descriptor.height == 8 &&
                       descriptor.plane_count == 1 && fds.size() == 1 && fds[0].valid();
        });
    });

    for (int i = 0; i < 100 && !std::filesystem::exists(socketPath); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    const int fd = open("/dev/null", O_RDONLY);
    if (fd < 0) {
        serverThread.detach();
        return EXIT_FAILURE;
    }

    axrb::protocol::GpuFrameDescriptor descriptor{};
    descriptor.width = 16;
    descriptor.height = 8;
    descriptor.drm_format = 0x34325258; // XR24
    descriptor.plane_count = 1;
    descriptor.fd_count = 1;
    descriptor.sequence = 42;
    descriptor.planes[0].fd_index = 0;
    descriptor.planes[0].stride = 64;
    descriptor.planes[0].size = 512;

    const bool sent = axrb::protocol::send_gpu_frame_descriptor(socketPath.c_str(), descriptor, &fd, 1);
    close(fd);

    serverThread.join();
    std::filesystem::remove(socketPath);

    return sent && received ? EXIT_SUCCESS : EXIT_FAILURE;
}
