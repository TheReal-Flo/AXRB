#include <jni.h>
#include <chrono>
#include <cstdint>
#include <cstdio>

extern "C" jstring AtomicRun(JNIEnv*, jclass, jint);
extern "C" jstring AtomicDiagnosticRun(JNIEnv*, jclass, jint);

// Deterministic integer workload, independent of LSE support. This is a narrow
// translator microbenchmark, not a prediction of game frame rates.
extern "C" JNIEXPORT jstring JNICALL
Java_com_axrb_mathprobe_MainActivity_run(JNIEnv* env, jclass cls, jint mode) {
    if (mode == 3 || mode == 4) return AtomicDiagnosticRun(env, cls, mode - 3);
    if (mode == 5) {
        uint8_t byte = 7, swapped = 0x87;
        uint16_t half = 0x1234;
        uint32_t expectedByte = 0x12340007, desiredByte = 0x23450009;
        uint32_t expectedHalf = 0xabcd1234, desiredHalf = 0x11115678;
        uint32_t input = 0x12345678;
        asm volatile(".arch armv8.1-a\n casalb %w0,%w2,[%1]"
            : "+r"(expectedByte) : "r"(&byte), "r"(desiredByte) : "memory");
        asm volatile(".arch armv8.1-a\n casalh %w0,%w2,[%1]"
            : "+r"(expectedHalf) : "r"(&half), "r"(desiredHalf) : "memory");
        asm volatile(".arch armv8.1-a\n swpalb %w0,%w0,[%1]"
            : "+r"(input) : "r"(&swapped) : "memory");
        char detail[240];
        snprintf(detail, sizeof(detail),
                 "atomic details: CASB old=%08x mem=%02x; CASH old=%08x mem=%04x; SWPB alias old=%08x mem=%02x",
                 expectedByte, byte, expectedHalf, half, input, swapped);
        return env->NewStringUTF(detail);
    }
    if (mode != 2) return AtomicRun(env, cls, mode);
    uint64_t value = 0x123456789abcdef0ULL;
    const auto start = std::chrono::steady_clock::now();
    for (uint32_t i = 0; i < 200000000; ++i) {
        value ^= value >> 12;
        value ^= value << 25;
        value ^= value >> 27;
        value *= 0x2545f4914f6cdd1dULL;
    }
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start).count();
    char result[160];
    snprintf(result, sizeof(result), "benchmark ns=%lld checksum=%016llx",
             static_cast<long long>(ns), static_cast<unsigned long long>(value));
    return env->NewStringUTF(result);
}
