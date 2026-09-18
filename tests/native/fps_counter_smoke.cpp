#include "../../host/src/fps_counter.h"
#include <cmath>
#include <cstdio>
int main(){
    axrb::host::FpsCounter counter;
    auto start=axrb::host::FpsCounter::Clock::time_point{};
    if(counter.sample(100,start))return 1;
    // Sampling does not count repeated compositor frames as game frames.
    for(int i=1;i<50;++i)if(counter.sample(100,start+std::chrono::milliseconds(i*10)))return 2;
    if(!counter.sample(145,start+std::chrono::milliseconds(500))||std::abs(counter.fps()-90)>0.001)return 3;
    if(!counter.sample(145,start+std::chrono::milliseconds(1000))||counter.fps()!=0)return 4;
    if(!counter.sample(190,start+std::chrono::milliseconds(2000))||counter.fps()!=45)return 5;
    if(!counter.sample(0,start+std::chrono::milliseconds(2500))||counter.fps()!=0)return 6;
    std::puts("FPS sampling, stalled frames and counter reset passed");
}
