#include <time.h>
#include <unistd.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>
static uint64_t ns(clockid_t id){timespec t{};clock_gettime(id,&t);return uint64_t(t.tv_sec)*1000000000+t.tv_nsec;}
static uint64_t counter(){uint64_t v;asm volatile("mrs %0, cntvct_el0":"=r"(v));return v;}
int main(int argc, char** argv){
    // The NDK translator can reject direct counter instructions. Keep them opt-in
    // so the default probe measures the clocks applications obtain from libc.
    bool direct=argc>1 && !std::strcmp(argv[1],"--direct-counter");
    uint64_t freq=1;
    if(direct){asm volatile("mrs %0, cntfrq_el0":"=r"(freq));}
    std::printf("direct_counter=%d frequency=%llu\n",direct,(unsigned long long)freq);
    std::fflush(stdout);
    std::vector<std::thread> threads;
    for(int i=0;i<4;++i)threads.emplace_back([=]{
        auto start=ns(CLOCK_MONOTONIC),raw=ns(CLOCK_MONOTONIC_RAW),boot=ns(CLOCK_BOOTTIME),ticks=direct?counter():0;
        usleep(3000000);
        std::printf("thread=%d monotonic_ms=%.3f raw_ms=%.3f boot_ms=%.3f arm_counter_ms=%.3f\n",i,(ns(CLOCK_MONOTONIC)-start)/1e6,(ns(CLOCK_MONOTONIC_RAW)-raw)/1e6,(ns(CLOCK_BOOTTIME)-boot)/1e6,direct?double(counter()-ticks)/freq*1000:0.0);
    });
    for(auto& t:threads)t.join();
}
