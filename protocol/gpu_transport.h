#pragma once

#include "gpu_frame.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace axrb::protocol {

class UniqueFd {
public:
    UniqueFd() = default;
    explicit UniqueFd(int fd);
    ~UniqueFd();

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;
    UniqueFd(UniqueFd&& other) noexcept;
    UniqueFd& operator=(UniqueFd&& other) noexcept;

    int get() const { return fd_; }
    bool valid() const { return fd_ >= 0; }
    int release();
    void reset(int fd = -1);

private:
    int fd_ = -1;
};

class UnixFdFrameServer {
public:
    using FrameCallback = std::function<void(const GpuFrameDescriptor&, std::vector<UniqueFd>&&)>;

    int serve(const char* socket_path, uint32_t max_frames, const FrameCallback& callback);
};

bool send_gpu_frame_descriptor(const char* socket_path,
                               const GpuFrameDescriptor& descriptor,
                               const int* fds,
                               uint32_t fd_count);

} // namespace axrb::protocol
