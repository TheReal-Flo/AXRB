#include "quad_renderer.h"
#include <array>
#include <cstdio>
#include <vector>
using Microsoft::WRL::ComPtr;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"quad check failed line %d: %s\n",__LINE__,#x); return 1; } } while (0)
int main() {
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)));
    D3D11_TEXTURE2D_DESC desc{}; desc.Width=desc.Height=64; desc.MipLevels=1; desc.ArraySize=2;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count=1;
    desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> source,target,staging;
    CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&source)));
    CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&target)));
    desc.BindFlags=0; desc.Usage=D3D11_USAGE_STAGING; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&staging)));
    std::vector<uint32_t> red(64*64,0x800000ff), blue(64*64,0xffff0000);
    context->UpdateSubresource(source.Get(),0,nullptr,red.data(),64*4,0);
    context->UpdateSubresource(source.Get(),1,nullptr,blue.data(),64*4,0);
    axrb::host::QuadRenderer renderer;
    XrView view{XR_TYPE_VIEW}; view.pose.orientation.w=1;
    view.fov={-.78539816f,.78539816f,.78539816f,-.78539816f};
    axrb::protocol::ImageQuad quad{}; quad.pose.z=-2; quad.width=quad.height=2; quad.layer_flags=6;
    auto pixel = [&](unsigned eye,unsigned x,unsigned y) {
        context->CopyResource(staging.Get(),target.Get()); D3D11_MAPPED_SUBRESOURCE mapped{};
        std::array<unsigned,4> out{};
        if (FAILED(context->Map(staging.Get(),eye,D3D11_MAP_READ,0,&mapped))) return out;
        auto* p=static_cast<unsigned char*>(mapped.pData)+y*mapped.RowPitch+x*4;
        for (unsigned i=0;i<4;++i) out[i]=p[i]; context->Unmap(staging.Get(),eye); return out;
    };
    for (unsigned eye=0;eye<2;++eye) context->UpdateSubresource(target.Get(),eye,nullptr,blue.data(),64*4,0);
    quad.eye_visibility=1;
    for (unsigned eye=0;eye<2;++eye) CHECK(renderer.render(device.Get(),context.Get(),source.Get(),0,{64,64},desc.Format,quad,view,target.Get(),eye,64,64));
    auto center=pixel(0,32,32); CHECK(center[0]>=127 && center[0]<=129 && center[2]>=126 && center[2]<=128);
    CHECK(pixel(1,32,32)[2]==255 && pixel(0,4,4)[2]==255);
    // Arbitrary layer count, draw order, opaque alpha and camera translation.
    quad.eye_visibility=0; quad.layer_flags=0; quad.pose.x=1; view.pose.position.x=1;
    for (unsigned i=0;i<40;++i) for (unsigned eye=0;eye<2;++eye)
        CHECK(renderer.render(device.Get(),context.Get(),source.Get(),i%2,{64,64},desc.Format,quad,view,target.Get(),eye,64,64));
    CHECK(pixel(0,32,32)[2]==255 && pixel(1,32,32)[2]==255);
    CHECK(renderer.render(device.Get(),context.Get(),source.Get(),0,{64,64},desc.Format,quad,view,target.Get(),0,64,64));
    CHECK(pixel(0,32,32)[0]==255 && pixel(0,32,32)[3]==255);
    // Content behind the viewer must be clipped.
    quad.pose.z=2;
    CHECK(renderer.render(device.Get(),context.Get(),source.Get(),1,{64,64},desc.Format,quad,view,target.Get(),0,64,64));
    CHECK(pixel(0,32,32)[0]==255);
    std::puts("GPU quad compositor: 40 layers, order, alpha, eye visibility, pose and clipping passed");
}
