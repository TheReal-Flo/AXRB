// Read-only asset I/O probe. Run the same binary/offsets for each comparison.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string_view>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 5) {
        std::fprintf(stderr, "Usage: storage-read-probe FILE BYTES BLOCK_BYTES sequential|random\n");
        return 2;
    }
    const size_t bytes = std::strtoull(argv[2], nullptr, 10);
    const size_t block = std::strtoull(argv[3], nullptr, 10);
    const bool random = std::string_view(argv[4]) == "random";
    if (!bytes || block < 4096 || block > 1048576 || bytes % block ||
        (!random && std::string_view(argv[4]) != "sequential")) return 2;
    const int fd = open(argv[1], O_RDONLY | O_CLOEXEC);
    struct stat st{};
    if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < static_cast<off_t>(bytes)) {
        std::perror("open/stat/size");
        return 1;
    }
    std::vector<char> buffer(block);
    std::vector<double> latencies;
    latencies.reserve(bytes / block);
    uint64_t seed = 1729;
    rusage before{}, after{};
    getrusage(RUSAGE_SELF, &before);
    const auto start = std::chrono::steady_clock::now();
    for (size_t done = 0; done < bytes; done += block) {
        seed = seed * 6364136223846793005ULL + 1;
        const off_t offset = random ? ((seed >> 16) % (st.st_size / block)) * block : done;
        const auto readStart = std::chrono::steady_clock::now();
        const auto n = pread(fd, buffer.data(), block, offset);
        if (n != static_cast<ssize_t>(block)) { std::perror("pread"); close(fd); return 1; }
        latencies.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - readStart).count());
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    getrusage(RUSAGE_SELF, &after);
    close(fd);
    std::sort(latencies.begin(), latencies.end());
    const auto cpu = [](const rusage& r) {
        return r.ru_utime.tv_sec + r.ru_stime.tv_sec + (r.ru_utime.tv_usec + r.ru_stime.tv_usec) / 1e6;
    };
    std::printf("{\"seconds\":%.6f,\"mib_s\":%.3f,\"cpu_seconds\":%.6f,\"p95_us\":%.3f,\"p99_us\":%.3f,\"major_faults\":%ld}\n",
        seconds, bytes / 1048576.0 / seconds, cpu(after) - cpu(before),
        latencies[latencies.size() * 95 / 100], latencies[latencies.size() * 99 / 100],
        after.ru_majflt - before.ru_majflt);
}
