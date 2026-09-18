#pragma once
#if defined(_WIN32)
#include "image_frame.h"
#include <d3d11.h>
#include <openxr/openxr.h>
#include <wrl/client.h>
namespace axrb::host {
class QuadRenderer {
public:
    bool render(ID3D11Device*, ID3D11DeviceContext*, ID3D11Texture2D* source, uint32_t slice,
        XrExtent2Di extent, DXGI_FORMAT format, const protocol::ImageQuad&, const XrView&,
        ID3D11Texture2D* target, uint32_t eye, uint32_t width, uint32_t height);
private:
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer_;
    Microsoft::WRL::ComPtr<ID3D11BlendState> blend_;
};
}
#endif
