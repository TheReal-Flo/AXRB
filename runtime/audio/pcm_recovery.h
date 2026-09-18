#pragma once
#include <cerrno>

namespace axrb::audio {
// Recover only output transport failures. A single retry bounds the work on
// the audio thread; partial writes and unrelated errors retain their semantics.
template<class Write, class Stop, class Prepare>
int write_with_recovery(Write write, Stop stop, Prepare prepare, bool& recovered)
{
    recovered = false;
    const int result = write();
    const int savedErrno = errno;
    const int error = result == -1 ? savedErrno : -result;
    if (result >= 0 || (error != EIO && error != EPIPE)) return result;
    if (stop() != 0 || prepare() != 0) {
        errno = savedErrno;
        return result;
    }
    const int retry = write();
    recovered = retry >= 0;
    return retry;
}
}
