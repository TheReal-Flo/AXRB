#pragma once
#if defined(_WIN32)
#include <d3d11.h>
#include <wrl/client.h>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <string>

// Opt-in diagnostics only. Four occasional readbacks after loading, never part
// of the normal shared-GPU path. Set AXRB_CAPTURE_PREFIX before starting host.
inline void debug_capture_frame(ID3D11DeviceContext* context, ID3D11Texture2D* image,
    UINT width, UINT height, uint64_t sequence) {
    static const std::string prefix = [] {const char* p=std::getenv("AXRB_CAPTURE_PREFIX");return p?std::string(p):std::string();}();
    static unsigned captured=0;
    static auto previous=std::chrono::steady_clock::now();
    if (prefix.empty() || captured>=4 || sequence<2000) return;
    const auto now=std::chrono::steady_clock::now();
    if (now-previous<std::chrono::seconds(5)) return;
    previous=now;
    Microsoft::WRL::ComPtr<ID3D11Device> device; context->GetDevice(&device);
    D3D11_TEXTURE2D_DESC desc{};image->GetDesc(&desc);
    const bool bgra=desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM || desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || desc.Format==DXGI_FORMAT_B8G8R8A8_TYPELESS;
    if (!bgra && desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM && desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB && desc.Format!=DXGI_FORMAT_R8G8B8A8_TYPELESS) return;
    desc.Format=bgra?DXGI_FORMAT_B8G8R8A8_UNORM:DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Width=width;desc.Height=height;desc.ArraySize=1;desc.MipLevels=1;
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device->CreateTexture2D(&desc,nullptr,&staging))) return;
    D3D11_BOX box{0,0,0,width,height,1};
    context->CopySubresourceRegion(staging.Get(),0,0,0,0,image,0,&box);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped))) return;
    const auto file=prefix+"-"+std::to_string(captured++)+"-"+std::to_string(sequence)+".ppm";
    std::ofstream output(file,std::ios::binary);
    output<<"P6\n"<<width<<" "<<height<<"\n255\n";
    std::string row(width*3,'\0');
    for (UINT y=0;y<height;++y) {
        auto* pixels=static_cast<const unsigned char*>(mapped.pData)+y*mapped.RowPitch;
        for (UINT x=0;x<width;++x) {
            row[x*3]=pixels[x*4+(bgra?2:0)];row[x*3+1]=pixels[x*4+1];row[x*3+2]=pixels[x*4+(bgra?0:2)];
        }
        output.write(row.data(),row.size());
    }
    context->Unmap(staging.Get(),0);
    std::fprintf(stderr,"AXRB diagnostic capture: %s\n",file.c_str());
}
#endif
