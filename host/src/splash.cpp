#include "splash.h"
#if defined(_WIN32)
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cmath>
#include <cstdio>
namespace axrb::host::detail {
std::vector<uint8_t> load_splash_pixels(uint32_t size, bool bgra, bool linear) {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    struct Apartment { bool owned; ~Apartment() { if (owned) CoUninitialize(); } } apartment{SUCCEEDED(initialized)};
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICBitmapScaler> scaler;
    ComPtr<IWICFormatConverter> converter;
    std::vector<uint8_t> pixels(size * size * 4, 0);
    const auto module = GetModuleHandleW(nullptr);
    const auto resource = FindResourceW(module, MAKEINTRESOURCEW(101), MAKEINTRESOURCEW(10));
    const auto data = resource ? LoadResource(module, resource) : nullptr;
    if (!data || FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromMemory(static_cast<BYTE*>(LockResource(data)), SizeofResource(module, resource))) ||
        FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)) || FAILED(factory->CreateBitmapScaler(&scaler)) ||
        FAILED(scaler->Initialize(frame.Get(), size, size, WICBitmapInterpolationModeFant)) ||
        FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(scaler.Get(), bgra ? GUID_WICPixelFormat32bppBGRA : GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) ||
        FAILED(converter->CopyPixels(nullptr, size * 4, static_cast<UINT>(pixels.size()), pixels.data()))) {
        std::fprintf(stderr, "AXRB: unable to decode splash resource\n");
    }
    // Composite transparent corners onto black; UNORM swapchains need linear RGB.
    for (size_t i = 0; i < pixels.size(); i += 4) {
        for (int c = 0; c < 3; ++c) {
            float v = pixels[i+c] / 255.0f;
            if (linear) v = v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
            pixels[i+c] = static_cast<uint8_t>(v * pixels[i+3] + 0.5f);
        }
        pixels[i+3] = 255;
    }
    return pixels;
}
}
#endif
