#pragma once
#if defined(_WIN32)
#include <d3d11_4.h>
#include <wrl/client.h>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include "perf_stats.h"

namespace axrb::host {
// One instance per context/owner. A failed wait poisons this instance: neither
// a timed-out copy nor its completion event may be reused as a completed frame.
class GpuCompletion {
public:
    GpuCompletion() = default;
    GpuCompletion(const GpuCompletion&) = delete;
    GpuCompletion& operator=(const GpuCompletion&) = delete;
    ~GpuCompletion() { if (event_) CloseHandle(event_); }
    bool wait(ID3D11Device* device, ID3D11DeviceContext* context) {
        static const bool profile = std::getenv("AXRB_GPU_WAIT_PROFILE") != nullptr;
        if (!profile) return complete(device, context);
        const auto start = std::chrono::steady_clock::now();
        const uint64_t cpu = thread_cpu();
        const bool result = complete(device, context);
        if (profile) {
            static protocol::PerfStats wall("host-gpu-wait-wall"), cpuStats("host-gpu-wait-cpu");
            const auto end = std::chrono::steady_clock::now();
            const uint64_t cpuEnd = thread_cpu();
            wall.record(std::chrono::duration<double, std::milli>(end - start).count());
            if (cpuEnd >= cpu) cpuStats.record((cpuEnd - cpu) / 10000.0);
        }
        return result;
    }
private:
    static uint64_t thread_cpu() {
        FILETIME created{}, exited{}, kernel{}, user{};
        if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user)) return 0;
        return (uint64_t(kernel.dwHighDateTime) << 32 | kernel.dwLowDateTime) +
               (uint64_t(user.dwHighDateTime) << 32 | user.dwLowDateTime);
    }
    bool complete(ID3D11Device* device, ID3D11DeviceContext* context) {
        if (failed_) return false;
        if (!device_) {
            device_ = device;
            context_ = context;
            const char* mode = std::getenv("AXRB_GPU_WAIT");
            const bool polling = mode && std::strcmp(mode, "poll") == 0;
            Microsoft::WRL::ComPtr<ID3D11Device5> device5;
            if (!polling && SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&device5))) &&
                SUCCEEDED(context->QueryInterface(IID_PPV_ARGS(&context4_))) &&
                SUCCEEDED(device5->CreateFence(0, D3D11_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)))) {
                event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
                if (!event_) fence_.Reset();
            }
            if (!fence_) context4_.Reset();
            static std::atomic<bool> reportedFence{false}, reportedPoll{false};
            auto& reported = fence_ ? reportedFence : reportedPoll;
            if (!reported.exchange(true, std::memory_order_relaxed)) {
                std::fprintf(stderr, "AXRB GPU: completion wait=%s\n", fence_ ? "fence event" : "query polling fallback");
            }
        }
        if (device_.Get() != device || context_.Get() != context) return fail("context changed");
        if (fence_) {
            if (FAILED(context4_->Signal(fence_.Get(), ++value_))) return fail("signal");
            context->Flush();
            auto completed = fence_->GetCompletedValue();
            if (completed == UINT64_MAX) return fail("device removed");
            if (completed < value_) {
                if (FAILED(fence_->SetEventOnCompletion(value_, event_))) return fail("event registration");
                if (WaitForSingleObject(event_, 1000) != WAIT_OBJECT_0) return fail("event timeout/failure");
                completed = fence_->GetCompletedValue();
            }
            if (completed == UINT64_MAX || completed < value_ || FAILED(device->GetDeviceRemovedReason()))
                return fail("incomplete/device removed");
            return true;
        }
        D3D11_QUERY_DESC desc{D3D11_QUERY_EVENT, 0};
        if (!query_ && FAILED(device->CreateQuery(&desc, &query_))) return fail("query creation");
        context->End(query_.Get()); context->Flush();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        HRESULT result;
        while ((result = context->GetData(query_.Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH)) == S_FALSE) {
            if (std::chrono::steady_clock::now() > deadline) return fail("query timeout");
            SwitchToThread();
        }
        return SUCCEEDED(result) || fail("query failure");
    }
    bool fail(const char* reason) {
        failed_ = true;
        std::fprintf(stderr, "AXRB GPU: completion failed: %s\n", reason);
        return false;
    }
    bool failed_ = false;
    uint64_t value_ = 0;
    HANDLE event_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext4> context4_;
    Microsoft::WRL::ComPtr<ID3D11Fence> fence_;
    Microsoft::WRL::ComPtr<ID3D11Query> query_;
};
}
#endif
