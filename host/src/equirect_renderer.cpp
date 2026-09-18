#include "equirect_renderer.h"
#if defined(_WIN32)
#include <cmath>
#include <cstdio>
#include <cstring>
#include <d3dcompiler.h>
namespace axrb::host {
namespace {
const char *shader = R"(
cbuffer Params : register(b0) {
 float4 cameraOrientation;
 float4 inverseSphereOrientation;
 float4 originRadius;
 float4 tangents;
 float4 anglesScaleX;
 float4 textureScale;
};
Texture2DArray panorama : register(t0);
SamplerState linearSampler : register(s0);
float3 rotate(float4 q, float3 v) { return v + 2 * cross(q.xyz, cross(q.xyz,v) + q.w*v); }
struct Vertex { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Vertex vsMain(uint id : SV_VertexID) {
 Vertex v; v.uv = float2((id<<1)&2,id&2); v.position=float4(v.uv*float2(2,-2)+float2(-1,1),0,1); return v;
}
float4 psMain(Vertex input) : SV_Target {
 float3 ray=normalize(float3(lerp(tangents.x,tangents.y,input.uv.x),lerp(tangents.z,tangents.w,input.uv.y),-1));
 ray=rotate(inverseSphereOrientation,rotate(cameraOrientation,ray));
 float3 sphereDirection=ray;
 if(originRadius.w>0) {
  float b=dot(originRadius.xyz,ray);
  float determinant=b*b-dot(originRadius.xyz,originRadius.xyz)+originRadius.w*originRadius.w;
  if(determinant<0) return 0;
  float distance=-b+sqrt(determinant);
  if(distance<=0) return 0;
  sphereDirection=normalize(originRadius.xyz+distance*ray);
 }
 float longitude=atan2(sphereDirection.x,-sphereDirection.z), latitude=asin(clamp(sphereDirection.y,-1,1));
 if(abs(longitude)>anglesScaleX.x*.5 || latitude>anglesScaleX.y || latitude<anglesScaleX.z) return 0;
 float2 uv=float2(longitude/anglesScaleX.x+.5,(anglesScaleX.y-latitude)/(anglesScaleX.y-anglesScaleX.z));
 // Clamp to the copied subimage, not the unused part of its array slice.
 uv=clamp(uv*float2(anglesScaleX.w,textureScale.x),textureScale.yz,float2(anglesScaleX.w,textureScale.x)-textureScale.yz);
 float4 color=panorama.Sample(linearSampler,float3(uv,0));
 if(textureScale.w==1) color.a=1;
 else if(textureScale.w==2) color.rgb*=color.a;
 return color;
}
)";
}
bool EquirectRenderer::render(ID3D11Device *device, ID3D11DeviceContext *context, ID3D11Texture2D *source,
                              uint32_t slice, XrExtent2Di extent, DXGI_FORMAT format,
                              const axrb::protocol::ImageEquirect &sphere, const XrView &view,
                              ID3D11Texture2D *target, uint32_t eye, uint32_t width, uint32_t height, bool composite) {
    using Microsoft::WRL::ComPtr;
    if (!vertex_) {
        ComPtr<ID3DBlob> vs, ps, error;
        if (FAILED(D3DCompile(shader, std::strlen(shader), "equirect", nullptr, nullptr, "vsMain", "vs_5_0",
                              D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vs, &error)) ||
            FAILED(D3DCompile(shader, std::strlen(shader), "equirect", nullptr, nullptr, "psMain", "ps_5_0",
                              D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &ps, &error))) {
            std::fprintf(stderr, "AXRB equirect shader: %s\n",
                         error ? static_cast<const char *>(error->GetBufferPointer()) : "compile failed");
            return false;
        }
        if (FAILED(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &pixel_)))
            return false;
        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth = 96;
        buffer.Usage = D3D11_USAGE_DEFAULT;
        buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(device->CreateBuffer(&buffer, nullptr, &constants_)))
            return false;
        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(device->CreateSamplerState(&sampler, &sampler_)))
            return false;
        D3D11_RASTERIZER_DESC raster{};
        raster.FillMode = D3D11_FILL_SOLID;
        raster.CullMode = D3D11_CULL_NONE;
        raster.DepthClipEnable = TRUE;
        if (FAILED(device->CreateRasterizerState(&raster, &rasterizer_)))
            return false;
        D3D11_BLEND_DESC blend{}; auto& b = blend.RenderTarget[0]; b.BlendEnable = TRUE;
        b.SrcBlend = b.SrcBlendAlpha = D3D11_BLEND_ONE;
        b.DestBlend = b.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        b.BlendOp = b.BlendOpAlpha = D3D11_BLEND_OP_ADD; b.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(device->CreateBlendState(&blend, &blend_))) return false;
        if (FAILED(
                device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vertex_)))
            return false;
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = format;
    srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    srv.Texture2DArray.MipLevels = 1;
    srv.Texture2DArray.FirstArraySlice = slice;
    srv.Texture2DArray.ArraySize = 1;
    ComPtr<ID3D11ShaderResourceView> texture;
    if (FAILED(device->CreateShaderResourceView(source, &srv, &texture)))
        return false;
    D3D11_RENDER_TARGET_VIEW_DESC rtv{};
    rtv.Format = format;
    rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
    rtv.Texture2DArray.FirstArraySlice = eye;
    rtv.Texture2DArray.ArraySize = 1;
    ComPtr<ID3D11RenderTargetView> output;
    if (FAILED(device->CreateRenderTargetView(target, &rtv, &output)))
        return false;
    const float transparent[4]{};
    if (!composite) context->ClearRenderTargetView(output.Get(), transparent);
    if ((sphere.eye_visibility == 1 && eye == 1) || (sphere.eye_visibility == 2 && eye == 0) ||
        sphere.horizontal_angle == 0 || sphere.upper_angle == sphere.lower_angle)
        return true;
    const auto &q = sphere.pose;
    const auto &c = view.pose;
    float x = c.position.x - q.x, y = c.position.y - q.y, z = c.position.z - q.z;
    const float qx = -q.qx, qy = -q.qy, qz = -q.qz, qw = q.qw;
    float tx = 2 * (qy * z - qz * y), ty = 2 * (qz * x - qx * z), tz = 2 * (qx * y - qy * x);
    D3D11_TEXTURE2D_DESC sourceDesc{};
    source->GetDesc(&sourceDesc);
    float values[24] = {c.orientation.x,
                        c.orientation.y,
                        c.orientation.z,
                        c.orientation.w,
                        qx,
                        qy,
                        qz,
                        qw,
                        x + qw * tx + qy * tz - qz * ty,
                        y + qw * ty + qz * tx - qx * tz,
                        z + qw * tz + qx * ty - qy * tx,
                        std::isfinite(sphere.radius) ? sphere.radius : 0,
                        std::tan(view.fov.angleLeft),
                        std::tan(view.fov.angleRight),
                        std::tan(view.fov.angleUp),
                        std::tan(view.fov.angleDown),
                        sphere.horizontal_angle,
                        sphere.upper_angle,
                        sphere.lower_angle,
                        float(extent.width) / sourceDesc.Width,
                        float(extent.height) / sourceDesc.Height,
                        .5f / sourceDesc.Width,
                        .5f / sourceDesc.Height,
                        (sphere.layer_flags & 2u) ? (composite && (sphere.layer_flags & 4u) ? 2.f : 0.f) : 1.f};
    context->UpdateSubresource(constants_.Get(), 0, nullptr, values, 0, 0);
    D3D11_VIEWPORT viewport{0, 0, float(width), float(height), 0, 1};
    context->RSSetViewports(1, &viewport);
    context->RSSetState(rasterizer_.Get());
    context->OMSetRenderTargets(1, output.GetAddressOf(), nullptr);
    context->OMSetBlendState(composite ? blend_.Get() : nullptr, nullptr, ~0u);
    context->OMSetDepthStencilState(nullptr, 0);
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vertex_.Get(), nullptr, 0);
    context->PSSetShader(pixel_.Get(), nullptr, 0);
    context->PSSetConstantBuffers(0, 1, constants_.GetAddressOf());
    context->PSSetSamplers(0, 1, sampler_.GetAddressOf());
    context->PSSetShaderResources(0, 1, texture.GetAddressOf());
    context->Draw(3, 0);
    ID3D11ShaderResourceView *empty = nullptr;
    context->PSSetShaderResources(0, 1, &empty);
    context->OMSetRenderTargets(0, nullptr, nullptr);
    return true;
}
} // namespace axrb::host
#endif
