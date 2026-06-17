#include "gpu_transport.h"

#include <cstdio>
#include <cstring>

#if !defined(_WIN32)
#include <cerrno>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace axrb::protocol {

UniqueFd::UniqueFd(int fd)
    : fd_(fd)
{
}

UniqueFd::~UniqueFd()
{
    reset();
}

UniqueFd::UniqueFd(UniqueFd&& other) noexcept
    : fd_(other.release())
{
}

UniqueFd& UniqueFd::operator=(UniqueFd&& other) noexcept
{
    if (this != &other) {
        reset(other.release());
    }
    return *this;
}

int UniqueFd::release()
{
    const int fd = fd_;
    fd_ = -1;
    return fd;
}

void UniqueFd::reset(int fd)
{
    if (fd_ >= 0) {
#if !defined(_WIN32)
        close(fd_);
#endif
    }
    fd_ = fd;
}

#if defined(_WIN32)

int UnixFdFrameServer::serve(const char*, uint32_t, const FrameCallback&)
{
    std::fprintf(stderr, "AXRB GPU: Unix dma-buf FD transport is only available on Linux\n");
    return 1;
}

bool send_gpu_frame_descriptor(const char*, const GpuFrameDescriptor&, const int*, uint32_t)
{
    return false;
}

#else

namespace {

bool validate_descriptor(const GpuFrameDescriptor& descriptor, uint32_t received_fd_count)
{
    if (descriptor.magic != kGpuFrameMagic || descriptor.version != kGpuFrameVersion ||
        descriptor.type != kGpuFrameTypeDmaBuf || descriptor.header_size != sizeof(GpuFrameDescriptor)) {
        std::fprintf(stderr, "AXRB GPU: invalid frame descriptor header\n");
        return false;
    }
    if (descriptor.width == 0 || descriptor.height == 0 || descriptor.layers == 0) {
        std::fprintf(stderr, "AXRB GPU: invalid frame dimensions\n");
        return false;
    }
    if (descriptor.plane_count == 0 || descriptor.plane_count > kMaxGpuFramePlanes ||
        descriptor.fd_count == 0 || descriptor.fd_count > kMaxGpuFramePlanes ||
        descriptor.fd_count != received_fd_count) {
        std::fprintf(stderr,
                     "AXRB GPU: invalid plane/fd counts planes=%u descriptor_fds=%u received_fds=%u\n",
                     descriptor.plane_count,
                     descriptor.fd_count,
                     received_fd_count);
        return false;
    }
    return true;
}

bool recv_descriptor_with_fds(int socket_fd, GpuFrameDescriptor* descriptor, std::vector<UniqueFd>* fds)
{
    char control[CMSG_SPACE(sizeof(int) * kMaxGpuFramePlanes)]{};
    iovec iov{};
    iov.iov_base = descriptor;
    iov.iov_len = sizeof(*descriptor);

    msghdr msg{};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);

    const ssize_t received = recvmsg(socket_fd, &msg, 0);
    if (received == 0) {
        return false;
    }
    if (received < 0) {
        std::fprintf(stderr, "AXRB GPU: recvmsg failed: %s\n", std::strerror(errno));
        return false;
    }
    if (static_cast<size_t>(received) != sizeof(*descriptor)) {
        std::fprintf(stderr, "AXRB GPU: short descriptor read: %zd bytes\n", received);
        return false;
    }

    for (cmsghdr* cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) {
            continue;
        }
        const size_t fd_bytes = cmsg->cmsg_len - CMSG_LEN(0);
        const size_t fd_count = fd_bytes / sizeof(int);
        const int* raw_fds = reinterpret_cast<const int*>(CMSG_DATA(cmsg));
        for (size_t i = 0; i < fd_count; ++i) {
            fds->emplace_back(raw_fds[i]);
        }
    }

    return validate_descriptor(*descriptor, static_cast<uint32_t>(fds->size()));
}

} // namespace

int UnixFdFrameServer::serve(const char* socket_path, uint32_t max_frames, const FrameCallback& callback)
{
    if (socket_path == nullptr || socket_path[0] == '\0') {
        std::fprintf(stderr, "AXRB GPU: missing Unix socket path\n");
        return 2;
    }

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (std::strlen(socket_path) >= sizeof(address.sun_path)) {
        std::fprintf(stderr, "AXRB GPU: socket path is too long: %s\n", socket_path);
        return 2;
    }
    std::strncpy(address.sun_path, socket_path, sizeof(address.sun_path) - 1);

    UniqueFd server_fd(socket(AF_UNIX, SOCK_SEQPACKET, 0));
    if (!server_fd.valid()) {
        std::fprintf(stderr, "AXRB GPU: socket failed: %s\n", std::strerror(errno));
        return 1;
    }

    unlink(socket_path);
    if (bind(server_fd.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        std::fprintf(stderr, "AXRB GPU: bind failed for %s: %s\n", socket_path, std::strerror(errno));
        return 1;
    }
    if (listen(server_fd.get(), 1) != 0) {
        std::fprintf(stderr, "AXRB GPU: listen failed: %s\n", std::strerror(errno));
        return 1;
    }

    std::fprintf(stderr, "AXRB GPU: listening for dma-buf descriptors on %s\n", socket_path);
    uint32_t frames = 0;
    while (max_frames == 0 || frames < max_frames) {
        UniqueFd client_fd(accept(server_fd.get(), nullptr, nullptr));
        if (!client_fd.valid()) {
            std::fprintf(stderr, "AXRB GPU: accept failed: %s\n", std::strerror(errno));
            continue;
        }

        while (max_frames == 0 || frames < max_frames) {
            GpuFrameDescriptor descriptor{};
            std::vector<UniqueFd> fds;
            if (!recv_descriptor_with_fds(client_fd.get(), &descriptor, &fds)) {
                break;
            }

            callback(descriptor, std::move(fds));
            ++frames;
        }
    }

    unlink(socket_path);
    return 0;
}

bool send_gpu_frame_descriptor(const char* socket_path,
                               const GpuFrameDescriptor& descriptor,
                               const int* fds,
                               uint32_t fd_count)
{
    if (socket_path == nullptr || fds == nullptr || fd_count == 0 || fd_count > kMaxGpuFramePlanes) {
        return false;
    }

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (std::strlen(socket_path) >= sizeof(address.sun_path)) {
        return false;
    }
    std::strncpy(address.sun_path, socket_path, sizeof(address.sun_path) - 1);

    UniqueFd socket_fd(socket(AF_UNIX, SOCK_SEQPACKET, 0));
    if (!socket_fd.valid()) {
        return false;
    }
    if (connect(socket_fd.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        return false;
    }

    char control[CMSG_SPACE(sizeof(int) * kMaxGpuFramePlanes)]{};
    iovec iov{};
    iov.iov_base = const_cast<GpuFrameDescriptor*>(&descriptor);
    iov.iov_len = sizeof(descriptor);

    msghdr msg{};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = CMSG_SPACE(sizeof(int) * fd_count);

    cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int) * fd_count);
    std::memcpy(CMSG_DATA(cmsg), fds, sizeof(int) * fd_count);

    msg.msg_controllen = cmsg->cmsg_len;
    return sendmsg(socket_fd.get(), &msg, 0) == static_cast<ssize_t>(sizeof(descriptor));
}

#endif

} // namespace axrb::protocol
