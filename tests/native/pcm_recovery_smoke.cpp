#include "../../runtime/audio/pcm_recovery.h"
#include <cstdio>
#include <initializer_list>

int main()
{
    auto check = [](bool ok) { if (!ok) std::fputs("PCM recovery check failed\n", stderr); return ok; };
    for (int failure : {-1, -EIO, -EPIPE}) {
        int writes = 0, stops = 0, prepares = 0;
        bool recovered = false;
        const int result = axrb::audio::write_with_recovery(
            [&] { errno = EIO; return ++writes == 1 ? failure : 960; },
            [&] { ++stops; return 0; }, [&] { ++prepares; return 0; }, recovered);
        if (!check(result == 960 && recovered && writes == 2 && stops == 1 && prepares == 1)) return 1;
    }
    for (int error : {EAGAIN, EINVAL, ENODEV}) {
        int controls = 0;
        bool recovered;
        const int result = axrb::audio::write_with_recovery(
            [&] { errno = error; return -1; }, [&] { ++controls; return 0; },
            [&] { ++controls; return 0; }, recovered);
        if (!check(result == -1 && errno == error && !recovered && controls == 0)) return 2;
    }
    int writes = 0;
    bool recovered;
    const int failedRetry = axrb::audio::write_with_recovery(
        [&] { ++writes; errno = EIO; return -1; }, [] { return 0; }, [] { return 0; }, recovered);
    if (!check(failedRetry == -1 && !recovered && writes == 2)) return 3;
    const int partial = axrb::audio::write_with_recovery(
        [] { return 17; }, [] { return -1; }, [] { return -1; }, recovered);
    if (!check(partial == 17 && !recovered)) return 4;
    int prepares = 0;
    const int stopFailure = axrb::audio::write_with_recovery(
        [] { errno = EIO; return -1; }, [] { errno = EINVAL; return -1; },
        [&] { ++prepares; return 0; }, recovered);
    if (!check(stopFailure == -1 && errno == EIO && prepares == 0 && !recovered)) return 5;
    std::puts("PCM recovery checks passed");
}
