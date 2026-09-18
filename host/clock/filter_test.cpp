#include "register_filter.h"
#include <cstdio>
#include <cstring>
int main() {
    WHV_REGISTER_NAME names[70], outNames[70];
    WHV_REGISTER_VALUE values[70]{}, outValues[70]{};
    for (unsigned i=0;i<70;++i) { names[i]=WHvX64RegisterRax; values[i].Reg64=i+123; }
    names[0]=WHvX64RegisterRip; names[28]=WHvX64RegisterTsc; names[69]=WHvX64RegisterEfer;
    if (filter_clock_registers(names,values,70,outNames,outValues)!=69) return 1;
    for (unsigned i=0,j=0;i<70;++i) if(i!=28) {
        if(outNames[j]!=names[i] || std::memcmp(&outValues[j],&values[i],sizeof(values[i]))) return 2;
        ++j;
    }
    if (filter_clock_registers(names+28,values+28,1,outNames,outValues)!=1) return 3;
    if (filter_clock_registers(names,values,69,outNames,outValues)!=69) return 4;
    names[0]=WHvX64RegisterRax;
    if (filter_clock_registers(names,values,70,outNames,outValues)!=70) return 5;
    std::puts("Only the recognized bulk TSC write is removed; other registers and explicit writes preserved");
}
