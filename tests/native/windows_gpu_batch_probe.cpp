// Run alongside vulkan_android_smoke --batch through adb reverse tcp:38506.
#include "windows_gpu_receiver.h"
#include "gpu_frame_packet.h"
#include "image_transport.h"
#include <array>
#include <deque>
#include <cstdio>
using Microsoft::WRL::ComPtr;
int main() {
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &device, nullptr, &context))) return 1;
    std::deque<axrb::host::WindowsGpuReceiver> receivers;
    const unsigned counts[] = {5,17,40,2,24,3};
    uint64_t sequence = 100;
    std::vector<uint64_t> previous;
    axrb::host::GpuCompletion completion;
    unsigned calls = 0; bool passed = true;
    axrb::protocol::TcpImageServer server;
    const int status = server.serve_with_callback(38506, 6, [&](const auto& h, const auto&, std::vector<uint8_t>&& payload) {
        using namespace axrb::protocol;
        const unsigned iteration = calls++;
        const unsigned count = counts[iteration];
        sequence += count;
        bool good = valid_gpu_batch(h, payload.data(), payload.size()) && h.reserved == count && h.sequence == sequence - 1;
        receivers.resize(count);
        std::vector<GpuBatchPart> parts(count);
        if (!good) { passed = false; return false; }
        std::memcpy(parts.data(), payload.data(), payload.size());
        for (unsigned i = 0; i < count; ++i)
            good &= receivers[i].enqueue_receive(device.Get(), context.Get(), parts[i].gpu, parts[i].header.sequence,
                parts[i].header.width, parts[i].header.height, DXGI_FORMAT_R8G8B8A8_UNORM);
        good &= completion.wait(device.Get(), context.Get());
        if (!good) { passed = false; return false; }
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = 768; desc.Height = 512; desc.MipLevels = 1; desc.ArraySize = 2*count;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
        ComPtr<ID3D11Texture2D> target, staging;
        good &= SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &target));
        desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        good &= SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &staging));
        if (!good) { passed = false; return false; }
        for (unsigned i = 0; i < count; ++i) {
            receivers[i].commit_receive(parts[i].header.sequence);
            good &= receivers[i].enqueue_copy_to(context.Get(), target.Get(), 2*i, 2);
        }
        good &= completion.wait(device.Get(), context.Get());
        context->CopyResource(staging.Get(), target.Get());
        for (unsigned i = 0; i < count; ++i) for (unsigned eye = 0; eye < 2; ++eye) {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(context->Map(staging.Get(), i*2+eye, D3D11_MAP_READ, 0, &mapped))) { good = false; continue; }
            auto* top = static_cast<const unsigned char*>(mapped.pData);
            auto* bottom = top + (parts[i].header.height-1)*mapped.RowPitch;
            const bool flip = (i + iteration) % 2 != 0;
            if (eye == 1 && parts[i].gpu.formats[1]) good &= top[0] == 0 && top[1] == 255 && top[2] == 0 && bottom[1] == 255;
            else good &= top[0] == (flip ? 0 : 255) && top[2] == (flip ? 255 : 0) &&
                         bottom[0] == (flip ? 255 : 0) && bottom[2] == (flip ? 0 : 255);
            context->Unmap(staging.Get(), i*2+eye);
        }
        // Deleted export slots must no longer have a named shared resource.
        ComPtr<ID3D11Device1> device1; good &= SUCCEEDED(device.As(&device1));
        for (size_t i = count; i < previous.size(); ++i) {
            wchar_t name[96]; swprintf_s(name,L"Local\\AXRB_GPU_%016llx_0",previous[i]);
            ComPtr<ID3D11Texture2D> removed;
            good &= FAILED(device1->OpenSharedResourceByName(name,DXGI_SHARED_RESOURCE_READ,IID_PPV_ARGS(&removed)));
        }
        previous.clear(); for (auto& part : parts) previous.push_back(part.gpu.session);
        passed &= good;
        std::fprintf(stderr, "GPU batch %u: %s (dynamic layers, retirement, mono/stereo transitions and distinct flip patterns)\n", iteration, good ? "PASS" : "FAIL");
        return good && iteration != 5; // Exercise rejection/poisoning after successful copies.
    });
    return status || !passed || calls != 6;
}
