#include "../../host-bridge/windows_gpu_receiver.h"
#include <cstdio>
#include <vector>
#include <thread>
#include <algorithm>
using Microsoft::WRL::ComPtr;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "Receiver probe failed at line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main() {
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
    D3D11_TEXTURE2D_DESC desc{}; desc.Width=64; desc.Height=64; desc.MipLevels=1;
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
    for (unsigned sequence=0; sequence<100; ++sequence) {
        std::vector<unsigned> pixels(64*64,0xff000000u|sequence);
        for(unsigned eye=0;eye<2;++eye) producerContext->UpdateSubresource(source[eye].Get(),0,nullptr,pixels.data(),256,0);
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
        std::thread worker([&] { received=transfer.receive(receiver.Get(),receiveContext.Get(),frame,sequence,64,64,DXGI_FORMAT_R8G8B8A8_UNORM); });
        worker.join(); CHECK(received);
        // Reusing the acknowledged producer resources must not change the cache.
        std::fill(pixels.begin(),pixels.end(),0xffffffffu);
        for(unsigned eye=0;eye<2;++eye) producerContext->UpdateSubresource(source[eye].Get(),0,nullptr,pixels.data(),256,0);
        producerContext->Flush();
        CHECK(transfer.copy_to(renderContext.Get(),destination.Get()));
        renderContext->CopyResource(staging.Get(),destination.Get());
        for(unsigned eye=0;eye<2;++eye) {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            CHECK(SUCCEEDED(renderContext->Map(staging.Get(),eye,D3D11_MAP_READ,0,&mapped)));
            bool correct=true;
            for(unsigned y=0;y<64;++y) for(unsigned x=0;x<64;++x)
                if(reinterpret_cast<const unsigned*>(static_cast<const char*>(mapped.pData)+y*mapped.RowPitch)[x]!=(0xff000000u|sequence)) correct=false;
            renderContext->Unmap(staging.Get(),eye); CHECK(correct);
        }
    }
    for(auto handle:handles) CloseHandle(handle);
    std::puts("GPU receiver probe passed: 100 stereo frames across three devices; acknowledged sources safely reused.");
}
