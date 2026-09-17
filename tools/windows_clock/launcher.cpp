#include <windows.h>
#include <psapi.h>
#include <cstdio>
#include <string>
#include <vector>
#include <filesystem>

static std::wstring quote(const wchar_t* input) {
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (const wchar_t* p = input; *p; ++p) {
        if (*p == L'\\') { ++slashes; continue; }
        out.append(*p == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0; out += *p;
    }
    out.append(slashes * 2, L'\\');
    return out + L'"';
}
static HMODULE remote_module(HANDLE process, const std::wstring& wanted) {
    HMODULE modules[1024]; DWORD bytes = 0;
    if (!EnumProcessModules(process, modules, sizeof(modules), &bytes) || bytes > sizeof(modules)) return nullptr;
    for (size_t i = 0; i < bytes / sizeof(HMODULE); ++i) {
        wchar_t path[32768];
        if (GetModuleFileNameExW(process, modules[i], path, 32768) &&
            !_wcsicmp(std::filesystem::path(path).filename().c_str(), wanted.c_str())) return modules[i];
    }
    return nullptr;
}
static bool remote_call(HANDLE process, uintptr_t address, void* argument, DWORD& result) {
    HANDLE thread = CreateRemoteThread(process, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(address), argument, 0, nullptr);
    if (!thread) return false;
    bool ok = WaitForSingleObject(thread, 30000) == WAIT_OBJECT_0 && GetExitCodeThread(thread, &result);
    CloseHandle(thread); return ok;
}
int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { std::fwprintf(stderr, L"Usage: axrb_clock_launcher QEMU.exe clock.dll [QEMU arguments]\n"); return 2; }
    bool cold = false;
    for (int i = 3; i < argc; ++i) {
        if (!std::wcscmp(argv[i], L"-no-snapshot")) cold = true;
        if (!std::wcscmp(argv[i], L"-snapshot")) return 2;
    }
    if (!cold) { std::fprintf(stderr, "Clock correction requires -no-snapshot\n"); return 2; }
    std::wstring command = quote(argv[1]);
    for (int i = 3; i < argc; ++i) command += L" " + quote(argv[i]);
    STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    if (!CreateProcessW(argv[1], command.data(), nullptr, nullptr, TRUE,
        CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        std::fprintf(stderr, "Cannot create emulator: %lu\n", GetLastError()); return 3;
    }
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    bool success = job && SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) &&
        AssignProcessToJobObject(job, process.hProcess);
    // A newly suspended process has not initialized its DLL loader yet. A
    // no-op ntdll thread lets Windows finish loader initialization while QEMU's
    // main thread remains suspended. Verify the KnownDLL mapping before using
    // its address; fail closed if this process has a different mapping.
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    auto noOp = GetProcAddress(ntdll, "NtTestAlert");
    MEMORY_BASIC_INFORMATION memory{}; wchar_t mapped[32768]{};
    DWORD bootstrap = 1;
    success = success && noOp && VirtualQueryEx(process.hProcess, reinterpret_cast<void*>(noOp), &memory, sizeof(memory)) &&
        memory.AllocationBase == ntdll &&
        GetMappedFileNameW(process.hProcess, reinterpret_cast<void*>(noOp), mapped, 32768) &&
        !_wcsicmp(std::filesystem::path(mapped).filename().c_str(), L"ntdll.dll") &&
        remote_call(process.hProcess, reinterpret_cast<uintptr_t>(noOp), nullptr, bootstrap);
    auto library = std::filesystem::absolute(argv[2]).wstring();
    SIZE_T size = (library.size() + 1) * sizeof(wchar_t);
    void* remotePath = VirtualAllocEx(process.hProcess, nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    success = success && remotePath && WriteProcessMemory(process.hProcess, remotePath, library.c_str(), size, nullptr);
    // Resolve the actual owner of LoadLibraryW (which can be forwarded).
    auto load = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HMODULE owner = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(load), &owner);
    wchar_t ownerPath[32768]{}; GetModuleFileNameW(owner, ownerPath, 32768);
    HMODULE remoteOwner = remote_module(process.hProcess, std::filesystem::path(ownerPath).filename().wstring());
    DWORD status = 1;
    success = success && remoteOwner && remote_call(process.hProcess,
        reinterpret_cast<uintptr_t>(remoteOwner) + reinterpret_cast<uintptr_t>(load) - reinterpret_cast<uintptr_t>(owner), remotePath, status);
    if (success) VirtualFreeEx(process.hProcess, remotePath, 0, MEM_RELEASE);
    HMODULE remoteLibrary = success ? remote_module(process.hProcess, std::filesystem::path(library).filename().wstring()) : nullptr;
    HMODULE localLibrary = LoadLibraryExW(library.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    auto initialize = localLibrary ? GetProcAddress(localLibrary, "AxrbInitializeClock") : nullptr;
    success = success && remoteLibrary && initialize && remote_call(process.hProcess,
        reinterpret_cast<uintptr_t>(remoteLibrary) + reinterpret_cast<uintptr_t>(initialize) - reinterpret_cast<uintptr_t>(localLibrary), nullptr, status) && status == 0;
    if (localLibrary) FreeLibrary(localLibrary);
    if (!success || ResumeThread(process.hThread) == DWORD(-1)) {
        std::fprintf(stderr, "Clock helper initialization failed: status=%lu win32=%lu\n", status, GetLastError());
        TerminateProcess(process.hProcess, 4);
    } else {
        WaitForSingleObject(process.hProcess, INFINITE);
    }
    DWORD exitCode = 4; GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread); CloseHandle(process.hProcess); if (job) CloseHandle(job);
    return static_cast<int>(exitCode);
}
