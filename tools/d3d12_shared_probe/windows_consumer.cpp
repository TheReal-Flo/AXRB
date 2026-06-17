#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d12.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

std::wstring widen(const char* text)
{
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (needed <= 0) {
        return L"AXRB_WSL_D3D12_TestTexture";
    }
    std::wstring out(static_cast<size_t>(needed - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, out.data(), needed);
    return out;
}

void print_hr(const char* label, HRESULT hr)
{
    std::fprintf(stderr, "%s failed: 0x%08lx\n", label, static_cast<unsigned long>(hr));
}

template <typename T>
class ComPtr {
public:
    ~ComPtr()
    {
        if (ptr_ != nullptr) {
            ptr_->Release();
        }
    }

    T* get() const { return ptr_; }
    T** put()
    {
        if (ptr_ != nullptr) {
            ptr_->Release();
            ptr_ = nullptr;
        }
        return &ptr_;
    }

private:
    T* ptr_ = nullptr;
};

} // namespace

int main(int argc, char** argv)
{
    const char* name_arg = argc >= 2 ? argv[1] : "AXRB_WSL_D3D12_TestTexture";
    const std::wstring name = widen(name_arg);

    ComPtr<ID3D12Device> device;
    HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(device.put()));
    if (FAILED(hr)) {
        print_hr("D3D12CreateDevice", hr);
        return 1;
    }

    HANDLE nt_handle = nullptr;
    hr = device.get()->OpenSharedHandleByName(name.c_str(), GENERIC_ALL, &nt_handle);
    if (FAILED(hr)) {
        print_hr("OpenSharedHandleByName", hr);
        return 2;
    }

    ComPtr<ID3D12Resource> resource;
    hr = device.get()->OpenSharedHandle(nt_handle, IID_PPV_ARGS(resource.put()));
    CloseHandle(nt_handle);
    if (FAILED(hr)) {
        print_hr("OpenSharedHandle", hr);
        return 3;
    }

    const D3D12_RESOURCE_DESC desc = resource.get()->GetDesc();
    std::fprintf(stdout,
                 "AXRB Windows D3D12 consumer OPENED name=%s width=%llu height=%u format=%u flags=0x%x\n",
                 name_arg,
                 static_cast<unsigned long long>(desc.Width),
                 desc.Height,
                 static_cast<unsigned int>(desc.Format),
                 static_cast<unsigned int>(desc.Flags));
    return 0;
}
