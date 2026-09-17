#pragma once
#include <windows.h>
#include <WinHvPlatform.h>

// Emulator 36.5.11's legacy WHPX bulk register update has exactly 70 entries.
// Keep Hyper-V's running, synchronized TSC instead of restoring stale env->tsc
// on every QEMU context write. Explicit single-register writes pass through.
inline UINT32 filter_clock_registers(const WHV_REGISTER_NAME* names,
    const WHV_REGISTER_VALUE* values, UINT32 count,
    WHV_REGISTER_NAME* filteredNames, WHV_REGISTER_VALUE* filteredValues) {
    if (count != 70 || !names || !values) return count;
    bool tsc = false, rip = false, efer = false;
    for (UINT32 i = 0; i < count; ++i) {
        tsc |= names[i] == WHvX64RegisterTsc;
        rip |= names[i] == WHvX64RegisterRip;
        efer |= names[i] == WHvX64RegisterEfer;
    }
    if (!tsc || !rip || !efer) return count;
    UINT32 n = 0;
    for (UINT32 i = 0; i < count; ++i) {
        if (names[i] == WHvX64RegisterTsc) continue;
        filteredNames[n] = names[i];
        filteredValues[n++] = values[i];
    }
    return n;
}
