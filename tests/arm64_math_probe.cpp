// JNI regression probe for tools/arm64_vector_math_fallback.py.
// Run the rewritten ARM64 library under the x86_64 NDK native bridge.
#include <jni.h>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>
using V = double __attribute__((vector_size(16)));

static V estimate(V x, bool reciprocal) {
    if (reciprocal) asm volatile("frecpe %0.2d, %0.2d" : "+w"(x));
    else asm volatile("frsqrte %0.2d, %0.2d" : "+w"(x));
    return x;
}
extern "C" int check_preservation();
asm(R"(
.text
.global check_preservation
.type check_preservation,%function
check_preservation:
    sub sp,sp,#32
    stp q30,q31,[sp]
    fmov v31.2d,#4.0
    fmov v30.2d,#1.5
    cmp xzr,xzr
    mrs x9,nzcv
    frsqrte v31.2d,v31.2d
    mrs x10,nzcv
    cmp x9,x10
    b.ne 1f
    fmov d0,#0.5
    fcmp d31,d0
    b.ne 1f
    fmov d0,#1.5
    fcmp d30,d0
    b.ne 1f
    mov w0,#0
    b 2f
1:  mov w0,#1
2:  ldp q30,q31,[sp]
    add sp,sp,#32
    ret
)");

extern "C" JNIEXPORT jstring JNICALL
Java_com_axrb_mathprobe_MainActivity_run(JNIEnv* env, jclass, jint) {
    const double inf = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inputs[] = {4,16,2,3,0.0,-0.0,inf,-inf,-4,nan,1e-200,1e200};
    int failed = check_preservation(), checked = 0;
    for (bool reciprocal : {false,true}) {
        for (int i=0;i<12;i+=2) {
            V out = estimate(V{inputs[i],inputs[i+1]},reciprocal);
            for (int j=0;j<2;++j) {
                double expected = reciprocal ? 1.0/inputs[i+j] : 1.0/std::sqrt(inputs[i+j]);
                bool ok = std::isnan(expected) ? std::isnan(out[j]) :
                    out[j] == expected && std::signbit(out[j]) == std::signbit(expected);
                failed += !ok; ++checked;
            }
        }
    }
    char result[128];
    std::snprintf(result,sizeof(result),"%d math cases + register/NZCV preservation: %d failures",checked,failed);
    return env->NewStringUTF(result);
}
