#include "equirect_renderer.h"
#include <cstdio>
#include <d3d11.h>
#include <openxr/openxr.h>
#include <vector>
#include <windows.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x)) {                                                                                          \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                                        \
            return 1;                                                                                        \
        }                                                                                                    \
    } while (0)
int main() {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                      D3D11_SDK_VERSION, &device, nullptr, &context)));
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 128;
    desc.Height = 64;
    desc.ArraySize = desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    std::vector<unsigned char> data(128 * 64 * 4);
    for (unsigned y = 0; y < 64; ++y)
        for (unsigned x = 0; x < 128; ++x) {
            auto p = (y * 128 + x) * 4;
            data[p] = static_cast<unsigned char>(x * 2);
            data[p + 1] = static_cast<unsigned char>(y * 4);
            data[p + 3] = 255;
        }
    D3D11_SUBRESOURCE_DATA initial{data.data(), 128 * 4, 0};
    ComPtr<ID3D11Texture2D> source, target, staging;
    CHECK(SUCCEEDED(device->CreateTexture2D(&desc, &initial, &source)));
    desc.Width = desc.Height = 32;
    desc.ArraySize = 2;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    CHECK(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &target)));
    desc.ArraySize = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    CHECK(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &staging)));
    axrb::host::EquirectRenderer renderer;
    axrb::protocol::ImageEquirect sphere{{0, 0, 0, 0, 0, 0, 1}, 0, 6.2831853f, 1.5707963f, -1.5707963f, 0, 0};
    XrView view{XR_TYPE_VIEW};
    view.pose.orientation.w = 1;
    view.fov = {-.6f, .6f, .6f, -.6f};
    auto render = [&](unsigned eye) {
        return renderer.render(device.Get(), context.Get(), source.Get(), 0, {128, 64},
                               DXGI_FORMAT_R8G8B8A8_UNORM, sphere, view, target.Get(), eye, 32, 32);
    };
    auto pixel = [&](unsigned eye, unsigned x, unsigned y, unsigned channel) {
        context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, target.Get(), eye, nullptr);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
            return -1;
        int value = static_cast<unsigned char *>(mapped.pData)[y * mapped.RowPitch + x * 4 + channel];
        context->Unmap(staging.Get(), 0);
        return value;
    };
    CHECK(render(0));
    CHECK(pixel(0, 16, 16, 0) > 120 && pixel(0, 16, 16, 0) < 140);
    CHECK(pixel(0, 3, 16, 0) < pixel(0, 28, 16, 0));
    CHECK(pixel(0, 16, 3, 1) < pixel(0, 16, 28, 1));
    CHECK(pixel(0, 16, 16, 3) == 255);
    // Per-eye visibility must clear a previously drawn opposite-eye image.
    sphere.eye_visibility = 1;
    CHECK(render(1));
    CHECK(pixel(1, 16, 16, 3) == 0);
    sphere.eye_visibility = 0;
    sphere.horizontal_angle = .1f;
    CHECK(render(0));
    CHECK(pixel(0, 0, 16, 3) == 0);
    sphere.horizontal_angle = 6.2831853f;
    sphere.radius = 2;
    view.pose.position.x = .5f;
    CHECK(render(0));
    CHECK(pixel(0, 16, 16, 0) > 135);
    std::puts(
        "Hardware panoramic compositor: orientation, stereo masking, clipping and finite radius passed");
}
