#include <sched.h>
#include <time.h>
#include <unistd.h>
#include <cstdio>
#include <cstdint>

static int64_t nanoseconds() {
    timespec t{}; clock_gettime(CLOCK_MONOTONIC, &t);
    return int64_t(t.tv_sec)*1000000000 + t.tv_nsec;
}
int main() {
    cpu_set_t allowed; CPU_ZERO(&allowed);
    if (sched_getaffinity(0, sizeof(allowed), &allowed)) return 1;
    unsigned backwards = 0, migrations = 0;
    auto previous = nanoseconds();
    for (unsigned round=0;round<1000;++round) for (unsigned cpu=0;cpu<CPU_SETSIZE;++cpu) {
        if (!CPU_ISSET(cpu,&allowed)) continue;
        cpu_set_t one; CPU_ZERO(&one); CPU_SET(cpu,&one);
        if (sched_setaffinity(0,sizeof(one),&one)) return 2;
        auto now=nanoseconds(); if (now<previous) ++backwards;
        previous=now; ++migrations;
    }
    sched_setaffinity(0,sizeof(allowed),&allowed);
    constexpr unsigned reads=100000;
    auto start=nanoseconds();
    for (unsigned i=0;i<reads;++i) {auto now=nanoseconds();if(now<previous)++backwards;previous=now;}
    auto elapsed=nanoseconds()-start;
    std::printf("clock_gettime: %.1f ns/read; migrations=%u backwards=%u monotonic_ns=%lld\n",
        double(elapsed)/reads,migrations,backwards,static_cast<long long>(nanoseconds()));
    return backwards?3:0;
}
