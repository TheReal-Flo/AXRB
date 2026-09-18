#include "../src/windows_gpu_receiver.h"
#include <cstdio>
#include <vector>
#include <thread>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <string_view>
#include "../src/frame_pool.h"
#include "../src/gpu_frame_batch.h"
using Microsoft::WRL::ComPtr;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "Receiver probe failed at line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(int argc, char** argv) {
    const bool fullSize = argc > 1 && std::string_view(argv[1]) == "--full-size";
    const unsigned width = fullSize ? 3864 : 64, height = fullSize ? 4076 : 64;
    const unsigned sequentialCount = fullSize ? 8 : 100;
    const unsigned concurrentEnd = fullSize ? 125 : 200;
    ComPtr<ID3D11Device> producer, receiver, renderer;
    ComPtr<ID3D11DeviceContext> producerContext, receiveContext, renderContext;
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &producer, nullptr, &producerContext)));
    ComPtr<IDXGIDevice> dxgi; ComPtr<IDXGIAdapter> adapter;
    CHECK(SUCCEEDED(producer.As(&dxgi)) && SUCCEEDED(dxgi->GetAdapter(&adapter)));
    CHECK(SUCCEEDED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &receiver, nullptr, &receiveContext)));
    CHECK(SUCCEEDED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &renderer, nullptr, &renderContext)));
    axrb::protocol::WindowsGpuFrame frame{0x50524f4245000000ULL + GetCurrentProcessId(), {37,37}};
    ComPtr<ID3D11Texture2D> source[2], destination, staging;
    HANDLE handles[2]{};
    D3D11_TEXTURE2D_DESC desc{}; desc.Width=width; desc.Height=height; desc.MipLevels=1;
    desc.ArraySize=1; desc.SampleDesc.Count=1; desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED_NTHANDLE|D3D11_RESOURCE_MISC_SHARED;
    for (unsigned eye=0; eye<2; ++eye) {
        CHECK(SUCCEEDED(producer->CreateTexture2D(&desc,nullptr,&source[eye])));
        ComPtr<IDXGIResource1> resource; CHECK(SUCCEEDED(source[eye].As(&resource)));
        wchar_t name[96]; swprintf_s(name,L"Local\\AXRB_GPU_%016llx_%u",frame.session,eye);
        CHECK(SUCCEEDED(resource->CreateSharedHandle(nullptr,DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,name,&handles[eye])));
    }
    desc.ArraySize=2; desc.MiscFlags=0; desc.BindFlags=0;
    CHECK(SUCCEEDED(renderer->CreateTexture2D(&desc,nullptr,&destination)));
    desc.Usage=D3D11_USAGE_STAGING; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    CHECK(SUCCEEDED(renderer->CreateTexture2D(&desc,nullptr,&staging)));
    axrb::host::WindowsGpuReceiver transfer;
    for (unsigned sequence=0; sequence<sequentialCount; ++sequence) {
        std::vector<unsigned> pixels(static_cast<size_t>(width)*height,0xff000000u|sequence);
        for(unsigned eye=0;eye<2;++eye) producerContext->UpdateSubresource(source[eye].Get(),0,nullptr,pixels.data(),width*4,0);
        ComPtr<ID3D11Query> done; D3D11_QUERY_DESC query{D3D11_QUERY_EVENT,0};
        CHECK(SUCCEEDED(producer->CreateQuery(&query,&done)));
        producerContext->End(done.Get()); producerContext->Flush();
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(1);
        HRESULT status;
        while((status=producerContext->GetData(done.Get(),nullptr,0,0))==S_FALSE) {
            CHECK(std::chrono::steady_clock::now()<deadline); SwitchToThread();
        }
        CHECK(SUCCEEDED(status));
        bool received=false;
        std::thread worker([&] { received=transfer.receive(receiver.Get(),receiveContext.Get(),frame,sequence,width,height,DXGI_FORMAT_R8G8B8A8_UNORM); });
        worker.join(); CHECK(received);
        // Reusing the acknowledged producer resources must not change the cache.
        std::fill(pixels.begin(),pixels.end(),0xffffffffu);
        for(unsigned eye=0;eye<2;++eye) producerContext->UpdateSubresource(source[eye].Get(),0,nullptr,pixels.data(),width*4,0);
        producerContext->Flush();
        CHECK(transfer.copy_to(renderContext.Get(),destination.Get()));
        renderContext->CopyResource(staging.Get(),destination.Get());
        for(unsigned eye=0;eye<2;++eye) {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            CHECK(SUCCEEDED(renderContext->Map(staging.Get(),eye,D3D11_MAP_READ,0,&mapped)));
            bool correct=true;
            for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x)
                if(reinterpret_cast<const unsigned*>(static_cast<const char*>(mapped.pData)+y*mapped.RowPitch)[x]!=(0xff000000u|sequence)) correct=false;
            renderContext->Unmap(staging.Get(),eye); CHECK(correct);
        }
    }
    // Hold a displayed batch while the producer laps it. Three slots must
    // suffice, and each eye/layer must retain its original sequence.
    destination.Reset(); staging.Reset();
    desc.ArraySize = 10; desc.Usage = D3D11_USAGE_DEFAULT; desc.CPUAccessFlags = 0;
    CHECK(SUCCEEDED(renderer->CreateTexture2D(&desc, nullptr, &destination)));
    desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    CHECK(SUCCEEDED(renderer->CreateTexture2D(&desc, nullptr, &staging)));
    axrb::host::GpuCompletion batchCompletion;
    axrb::host::FramePool<axrb::host::GpuFrameBatch> pool;
    std::mutex publication;
    std::shared_ptr<axrb::host::GpuFrameBatch> latest;
    std::atomic<bool> pinned{false}, lapped{false}, failed{false}, finished{false};
    std::atomic<unsigned> displayed{0};
    std::thread consumer([&] {
        uint64_t last = UINT64_MAX;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(fullSize ? 90 : 15);
        while (!failed) {
            if (std::chrono::steady_clock::now() > deadline) { failed = true; break; }
            std::shared_ptr<axrb::host::GpuFrameBatch> snapshot;
            { std::lock_guard lock(publication); snapshot = latest; }
            if (!snapshot || snapshot->parts[0].header.sequence == last) { SwitchToThread(); continue; }
            const auto sequence = snapshot->parts[0].header.sequence;
            if (sequence == 100) {
                pinned = true;
                while (!lapped && !failed && std::chrono::steady_clock::now() < deadline) SwitchToThread();
                if (!lapped) { failed = true; break; }
            }
            for (unsigned part = 0; part < snapshot->count && !failed; ++part) {
                auto& image = snapshot->parts[part];
                if (image.header.sequence != sequence ||
                    !image.receiver.enqueue_copy_to(renderContext.Get(), destination.Get(), part * 2)) { failed = true; break; }
            }
            if (!batchCompletion.wait(renderer.Get(), renderContext.Get())) failed = true;
            if (failed) break;
            renderContext->CopyResource(staging.Get(), destination.Get());
            for (unsigned part = 0; part < snapshot->count && !failed; ++part) {
                for (unsigned eye = 0; eye < 2; ++eye) {
                    D3D11_MAPPED_SUBRESOURCE mapped{};
                    if (FAILED(renderContext->Map(staging.Get(), part * 2 + eye, D3D11_MAP_READ, 0, &mapped))) { failed = true; break; }
                    const unsigned expected = 0xff000000u | (static_cast<unsigned>(sequence) << 8) | (part << 1) | eye;
                    for (unsigned y=0; y<height; ++y) for (unsigned x=0; x<width; ++x)
                        if (reinterpret_cast<const unsigned*>(static_cast<const char*>(mapped.pData)+y*mapped.RowPitch)[x] != expected) failed = true;
                    renderContext->Unmap(staging.Get(), part * 2 + eye);
                }
            }
            last = sequence; ++displayed;
            if (sequence == concurrentEnd - 1) { finished = true; break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });
    for (unsigned sequence = 100; sequence < concurrentEnd && !failed; ++sequence) {
        auto batch = pool.acquire();
        if (!batch) { failed = true; break; }
        batch->count = sequence % 2 ? 1 : 5;
        for (unsigned part = 0; part < batch->count && !failed; ++part) {
            for (unsigned eye = 0; eye < 2; ++eye) {
                std::vector<unsigned> pixels(static_cast<size_t>(width)*height, 0xff000000u | (sequence << 8) | (part << 1) | eye);
                producerContext->UpdateSubresource(source[eye].Get(), 0, nullptr, pixels.data(), width*4, 0);
            }
            ComPtr<ID3D11Query> done; D3D11_QUERY_DESC query{D3D11_QUERY_EVENT,0};
            if (FAILED(producer->CreateQuery(&query, &done))) { failed = true; break; }
            producerContext->End(done.Get()); producerContext->Flush();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            HRESULT status;
            while ((status = producerContext->GetData(done.Get(), nullptr, 0, 0)) == S_FALSE &&
                   std::chrono::steady_clock::now() < deadline) SwitchToThread();
            auto& image = batch->parts[part];
            if (status != S_OK || !image.receiver.receive(receiver.Get(), receiveContext.Get(), frame,
                    sequence * 5 + part, width, height, DXGI_FORMAT_R8G8B8A8_UNORM)) { failed = true; break; }
            image.header.sequence = sequence;
        }
        if (failed) break;
        { std::lock_guard lock(publication); latest = std::move(batch); }
        if (sequence == 100) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (!pinned && !failed && std::chrono::steady_clock::now() < deadline) SwitchToThread();
            if (!pinned) { failed = true; break; }
        }
        if (sequence == 110) lapped = true;
    }
    consumer.join();
    CHECK(!failed && finished && lapped && displayed >= 2);
    std::printf("GPU pool probe passed: concurrent stereo/five-layer batches; %u displayed, producer advanced while old frame pinned.\n", displayed.load());
    for(auto handle:handles) CloseHandle(handle);
    std::puts("GPU receiver probe passed: stereo frames across three devices; acknowledged sources safely reused.");
}
