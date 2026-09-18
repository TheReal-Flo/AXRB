#include <jni.h>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>
static uint32_t swap32(uint32_t* p,uint32_t v){uint32_t out;asm volatile(".arch armv8.1-a\n swpal %w0,%w1,[%2]" : "+r"(v),"=r"(out):"r"(p):"memory");return out;}
static uint64_t swap64(uint64_t* p,uint64_t v){uint64_t out;asm volatile(".arch armv8.1-a\n swpal %0,%1,[%2]" : "+r"(v),"=r"(out):"r"(p):"memory");return out;}
static uint32_t add32(uint32_t* p,uint32_t v){uint32_t out;asm volatile(".arch armv8.1-a\n ldaddal %w0,%w1,[%2]" : "+r"(v),"=r"(out):"r"(p):"memory");return out;}
extern "C" JNIEXPORT jstring JNICALL Java_com_axrb_mathprobe_MainActivity_run(JNIEnv* env,jclass,jint mode){
 int failures=0;uint32_t value=7;
 if(mode==1){
 uint32_t expected=7,desired=9;asm volatile(".arch armv8.1-a\n casal %w0,%w2,[%1]":"+r"(expected):"r"(&value),"r"(desired):"memory");failures+=(expected!=7||value!=9);
 expected=123;asm volatile(".arch armv8.1-a\n casal %w0,%w2,[%1]":"+r"(expected):"r"(&value),"r"(desired):"memory");failures+=(expected!=9||value!=9);
 uint64_t big=0x1234567812345678ULL,e=big,d=99;asm volatile(".arch armv8.1-a\n casal %0,%2,[%1]":"+r"(e):"r"(&big),"r"(d):"memory");failures+=e!=0x1234567812345678ULL||big!=99;
 uint8_t byte=7;expected=0x12340007;desired=0x23450009;asm volatile(".arch armv8.1-a\n casalb %w0,%w2,[%1]":"+r"(expected):"r"(&byte),"r"(desired):"memory");failures+=expected!=7||byte!=9;
 uint16_t half=0x1234;expected=0xabcd1234;desired=0x11115678;asm volatile(".arch armv8.1-a\n casalh %w0,%w2,[%1]":"+r"(expected):"r"(&half),"r"(desired):"memory");failures+=expected!=0x1234||half!=0x5678;
 value=0;std::vector<std::thread> threads;for(int i=0;i<4;++i)threads.emplace_back([&]{for(int j=0;j<10000;++j){uint32_t old=0;for(;;){uint32_t wanted=old+1,seen=old;asm volatile(".arch armv8.1-a\n casal %w0,%w2,[%1]":"+r"(seen):"r"(&value),"r"(wanted):"memory");if(seen==old)break;old=seen;}}});for(auto& t:threads)t.join();failures+=value!=40000;
 }
 else{failures+=swap32(&value,19)!=7||value!=19;uint64_t big=0x1234567812345678ULL;failures+=swap64(&big,99)!=0x1234567812345678ULL||big!=99;failures+=add32(&value,3)!=19||value!=22;
 uint32_t old,after,flags,input=123;
 asm volatile(".arch armv8.1-a\n sub sp,sp,#16\n str %w3,[sp]\n cmp wzr,wzr\n swpal %w3,%w0,[sp]\n cset %w2,eq\n ldr %w1,[sp]\n add sp,sp,#16":"=&r"(old),"=&r"(after),"=&r"(flags):"r"(input):"memory","cc");failures+=old!=123||after!=123||flags!=1;
 uint8_t byte=0x87;input=0x12345678;asm volatile(".arch armv8.1-a\n swpalb %w0,%w0,[%1]":"+r"(input):"r"(&byte):"memory");failures+=input!=0x87||byte!=0x78;
 uint16_t half=0xffff;input=2;asm volatile(".arch armv8.1-a\n ldaddalh %w0,%w0,[%1]":"+r"(input):"r"(&half):"memory");failures+=input!=0xffff||half!=1;
 value=0;std::vector<std::thread> threads;for(int i=0;i<4;++i)threads.emplace_back([&]{for(int j=0;j<10000;++j)add32(&value,1);});for(auto& t:threads)t.join();failures+=value!=40000;
 }
 char text[160];snprintf(text,sizeof(text),"atomic mode %d: %d failures; value=%u",mode,failures,value);return env->NewStringUTF(text);
}
