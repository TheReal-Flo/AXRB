#include "register_filter.h"
#include <MinHook.h>
#include <cstdio>
#include <cwchar>

namespace {
using SetRegisters = decltype(&WHvSetVirtualProcessorRegisters);
SetRegisters original = nullptr;
HRESULT WINAPI set_registers(WHV_PARTITION_HANDLE partition, UINT32 cpu,
    const WHV_REGISTER_NAME* names, UINT32 count, const WHV_REGISTER_VALUE* values) {
    WHV_REGISTER_NAME filteredNames[70];
    WHV_REGISTER_VALUE filteredValues[70];
    const UINT32 filtered = filter_clock_registers(names, values, count, filteredNames, filteredValues);
    return filtered == count ? original(partition, cpu, names, count, values) :
        original(partition, cpu, filteredNames, filtered, filteredValues);
}
}

// Called by our launcher before QEMU's main thread runs, outside DllMain.
// Cold boot only: preserving the host counter is not snapshot time restoration.
extern "C" __declspec(dllexport) DWORD WINAPI AxrbInitializeClock(void*) {
    wchar_t path[32768];
    if (!GetModuleFileNameW(nullptr, path, 32768)) return 1;
    const wchar_t* name = std::wcsrchr(path, L'\\');
    if (!name || _wcsicmp(name + 1, L"qemu-system-x86_64-headless.exe")) return 2;
    HMODULE module = LoadLibraryExW(L"WinHvPlatform.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) return 3;
    void* target = reinterpret_cast<void*>(GetProcAddress(module, "WHvSetVirtualProcessorRegisters"));
    if (!target || MH_Initialize() != MH_OK) return 4;
    if (MH_CreateHook(target, reinterpret_cast<void*>(set_registers), reinterpret_cast<void**>(&original)) != MH_OK) return 5;
    if (MH_EnableHook(target) != MH_OK) return 6;
    std::fprintf(stderr, "AXRB clock: preserving WHPX TSC in legacy 70-register updates; kernel stability checks remain enabled\n");
    return 0;
}
