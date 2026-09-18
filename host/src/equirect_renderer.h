#pragma once
#if defined(_WIN32)
#include "image_frame.h"
#include <d3d11.h>
#include <openxr/openxr.h>
#include <wrl/client.h>
namespace axrb::host {
class EquirectRenderer {
  public:
    bool render(ID3D11Device *device, ID3D11DeviceContext *context, ID3D11Texture2D *source, uint32_t slice,
                XrExtent2Di extent, DXGI_FORMAT format, const axrb::protocol::ImageEquirect &sphere,
                const XrView &view, ID3D11Texture2D *target, uint32_t eye, uint32_t width, uint32_t height, bool composite = false);

  private:
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer_;
    Microsoft::WRL::ComPtr<ID3D11BlendState> blend_;
};
} // namespace axrb::host
#endif
