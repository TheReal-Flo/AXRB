#include "gpu_completion.h"
#include <cstdio>
using Microsoft::WRL::ComPtr;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main() {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &device, nullptr, &context)));
    axrb::host::GpuCompletion normal;
    for (unsigned i = 0; i < 100; ++i) CHECK(normal.wait(device.Get(), context.Get()));
    ComPtr<ID3D11Device5> device5;
    ComPtr<ID3D11DeviceContext4> context4;
    if (FAILED(device.As(&device5)) || FAILED(context.As(&context4))) {
        std::puts("PASS: fallback completion; fence timeout unavailable"); return 0;
    }
    ComPtr<ID3D11Fence> gate;
    CHECK(SUCCEEDED(device5->CreateFence(0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(&gate))));
    ComPtr<ID3D11Device> releaseDevice;
    ComPtr<ID3D11DeviceContext> releaseContext;
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &releaseDevice, nullptr, &releaseContext)));
    ComPtr<ID3D11Device5> releaseDevice5;
    ComPtr<ID3D11DeviceContext4> releaseContext4;
    CHECK(SUCCEEDED(releaseDevice.As(&releaseDevice5)) && SUCCEEDED(releaseContext.As(&releaseContext4)));
    HANDLE shared = nullptr;
    CHECK(SUCCEEDED(gate->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &shared)));
    ComPtr<ID3D11Fence> releaseGate;
    const auto opened = releaseDevice5->OpenSharedFence(shared, IID_PPV_ARGS(&releaseGate));
    CloseHandle(shared);
    CHECK(SUCCEEDED(opened));
    CHECK(SUCCEEDED(context4->Wait(gate.Get(), 1)));
    axrb::host::GpuCompletion blocked;
    const auto start = std::chrono::steady_clock::now();
    const bool completed = blocked.wait(device.Get(), context.Get());
    const auto duration = std::chrono::steady_clock::now() - start;
    // Always release queued GPU work before assertions and destruction.
    CHECK(SUCCEEDED(releaseContext4->Signal(releaseGate.Get(), 1)));
    releaseContext->Flush();
    CHECK(!completed);
    CHECK(duration >= std::chrono::milliseconds(900) && duration < std::chrono::seconds(3));
    CHECK(!blocked.wait(device.Get(), context.Get()));
    CHECK(normal.wait(device.Get(), context.Get()));
    std::puts("PASS: repeated completion, bounded timeout, poisoned waiter, queue recovery");
    return 0;
}
