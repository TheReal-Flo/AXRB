#include "quad_renderer.h"
#if defined(_WIN32)
#include <d3dcompiler.h>
#include <cmath>
#include <cstring>
namespace axrb::host {
namespace {
const char* shader = R"(
cbuffer Params : register(b0) {
 float4 inverseCamera;
 float4 panelOrientation;
 float4 delta;
 float4 tangents;
 float4 dimensions;
 float4 boundsFlags;
};
Texture2DArray panel : register(t0);
SamplerState linearSampler : register(s0);
float3 rotate(float4 q, float3 v) { return v + 2*cross(q.xyz,cross(q.xyz,v)+q.w*v); }
struct Vertex { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Vertex vsMain(uint id : SV_VertexID) {
 const float2 uv[6] = {float2(0,0),float2(1,0),float2(0,1),float2(0,1),float2(1,0),float2(1,1)};
 Vertex o; o.uv=uv[id];
 float3 p=rotate(inverseCamera,rotate(panelOrientation,float3((o.uv*float2(2,-2)+float2(-1,1))*dimensions.xy*.5,0))+delta.xyz);
 o.position=float4((2*p.x+(tangents.x+tangents.y)*p.z)/(tangents.y-tangents.x),
                   (2*p.y+(tangents.z+tangents.w)*p.z)/(tangents.z-tangents.w),-p.z-.01,-p.z);
 return o;
}
float4 psMain(Vertex v) : SV_Target {
 float2 uv=clamp(v.uv*dimensions.zw,boundsFlags.xy,dimensions.zw-boundsFlags.xy);
 float4 color=panel.Sample(linearSampler,float3(uv,0));
 if(boundsFlags.z==0) color.a=1;
 else if(boundsFlags.w!=0) color.rgb*=color.a;
 return color;
}
)";
}
bool QuadRenderer::render(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* source,
    uint32_t slice, XrExtent2Di extent, DXGI_FORMAT format, const protocol::ImageQuad& quad,
    const XrView& view, ID3D11Texture2D* target, uint32_t eye, uint32_t width, uint32_t height) {
    using Microsoft::WRL::ComPtr;
    if ((quad.eye_visibility==1 && eye==1) || (quad.eye_visibility==2 && eye==0)) return true;
    if (!vertex_) {
        ComPtr<ID3DBlob> vs, ps;
        if (FAILED(D3DCompile(shader,std::strlen(shader),"quad",nullptr,nullptr,"vsMain","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&vs,nullptr)) ||
            FAILED(D3DCompile(shader,std::strlen(shader),"quad",nullptr,nullptr,"psMain","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&ps,nullptr))) return false;
        if (FAILED(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&pixel_))) return false;
        D3D11_BUFFER_DESC buffer{}; buffer.ByteWidth=96; buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(device->CreateBuffer(&buffer,nullptr,&constants_))) return false;
        D3D11_SAMPLER_DESC sampler{}; sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP; sampler.MaxLOD=D3D11_FLOAT32_MAX;
        if (FAILED(device->CreateSamplerState(&sampler,&sampler_))) return false;
        D3D11_RASTERIZER_DESC raster{}; raster.FillMode=D3D11_FILL_SOLID; raster.CullMode=D3D11_CULL_NONE; raster.DepthClipEnable=TRUE;
        if (FAILED(device->CreateRasterizerState(&raster,&rasterizer_))) return false;
        D3D11_BLEND_DESC blend{}; auto& b=blend.RenderTarget[0]; b.BlendEnable=TRUE;
        b.SrcBlend=b.SrcBlendAlpha=D3D11_BLEND_ONE; b.DestBlend=b.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;
        b.BlendOp=b.BlendOpAlpha=D3D11_BLEND_OP_ADD; b.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(device->CreateBlendState(&blend,&blend_))) return false;
        if (FAILED(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertex_))) return false;
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC srv{}; srv.Format=format; srv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    srv.Texture2DArray.MipLevels=1; srv.Texture2DArray.FirstArraySlice=slice; srv.Texture2DArray.ArraySize=1;
    ComPtr<ID3D11ShaderResourceView> input; if (FAILED(device->CreateShaderResourceView(source,&srv,&input))) return false;
    D3D11_RENDER_TARGET_VIEW_DESC rtv{}; rtv.Format=format; rtv.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
    rtv.Texture2DArray.FirstArraySlice=eye; rtv.Texture2DArray.ArraySize=1;
    ComPtr<ID3D11RenderTargetView> output; if (FAILED(device->CreateRenderTargetView(target,&rtv,&output))) return false;
    D3D11_TEXTURE2D_DESC desc{}; source->GetDesc(&desc);
    const auto& q=quad.pose; const auto& c=view.pose;
    float values[24]={-c.orientation.x,-c.orientation.y,-c.orientation.z,c.orientation.w,
        q.qx,q.qy,q.qz,q.qw,q.x-c.position.x,q.y-c.position.y,q.z-c.position.z,0,
        std::tan(view.fov.angleLeft),std::tan(view.fov.angleRight),std::tan(view.fov.angleUp),std::tan(view.fov.angleDown),
        quad.width,quad.height,float(extent.width)/desc.Width,float(extent.height)/desc.Height,
        .5f/desc.Width,.5f/desc.Height,(quad.layer_flags&2u)?1.f:0.f,(quad.layer_flags&4u)?1.f:0.f};
    context->UpdateSubresource(constants_.Get(),0,nullptr,values,0,0);
    D3D11_VIEWPORT viewport{0,0,float(width),float(height),0,1}; context->RSSetViewports(1,&viewport);
    context->RSSetState(rasterizer_.Get()); context->OMSetRenderTargets(1,output.GetAddressOf(),nullptr);
    context->OMSetBlendState(blend_.Get(),nullptr,~0u); context->OMSetDepthStencilState(nullptr,0);
    context->IASetInputLayout(nullptr); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vertex_.Get(),nullptr,0); context->PSSetShader(pixel_.Get(),nullptr,0);
    context->VSSetConstantBuffers(0,1,constants_.GetAddressOf()); context->PSSetConstantBuffers(0,1,constants_.GetAddressOf());
    context->PSSetSamplers(0,1,sampler_.GetAddressOf()); context->PSSetShaderResources(0,1,input.GetAddressOf());
    context->Draw(6,0);
    ID3D11ShaderResourceView* empty=nullptr; context->PSSetShaderResources(0,1,&empty); context->OMSetRenderTargets(0,nullptr,nullptr);
    return true;
}
}
#endif
