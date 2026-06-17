#include "openxr_host.h"

#include "gpu_transport.h"
#include "image_transport.h"
#include "transport_tcp.h"
#include "video_transport.h"

#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <array>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#define XR_USE_TIMESPEC
#else
#define XR_USE_GRAPHICS_API_D3D11
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#endif

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#if defined(_WIN32)
#else
#include <dlfcn.h>
#include <time.h>
#endif

namespace axrb::host {

namespace {

constexpr float kAppProjectionHalfFovRadians = 0.95f;

bool parse_u16(const char* text, uint16_t* value)
{
    uint32_t parsed = 0;
    const std::string_view input{text};
    const auto result = std::from_chars(input.data(), input.data() + input.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != input.data() + input.size() || parsed > 65535) {
        return false;
    }
    *value = static_cast<uint16_t>(parsed);
    return true;
}

bool parse_u32(const char* text, uint32_t* value)
{
    const std::string_view input{text};
    const auto result = std::from_chars(input.data(), input.data() + input.size(), *value);
    return result.ec == std::errc{} && result.ptr == input.data() + input.size();
}

void print_usage()
{
    std::fprintf(stderr, "Usage:\n");
    std::fprintf(stderr, "  axrb-host-bridge --serve [port] [frames]\n");
    std::fprintf(stderr, "  axrb-host-bridge --serve-openxr [port] [frames]\n");
    std::fprintf(stderr, "  axrb-host-bridge --serve-gpu-fds [socket-path] [frames]\n");
    std::fprintf(stderr, "  axrb-host-bridge --video-recv-udp [port] [frames]\n");
    std::fprintf(stderr, "  axrb-host-bridge --video-send-synthetic [host] [port] [frames] [fps]\n");
    std::fprintf(stderr, "  axrb-host-bridge --video-send-rgba [host] [port] [frames] [fps] [width] [height]\n");
    std::fprintf(stderr, "  axrb-host-bridge --smoke\n");
}

const char* xr_result_name(XrResult result)
{
    switch (result) {
    case XR_SUCCESS:
        return "XR_SUCCESS";
    case XR_TIMEOUT_EXPIRED:
        return "XR_TIMEOUT_EXPIRED";
    case XR_FRAME_DISCARDED:
        return "XR_FRAME_DISCARDED";
    case XR_SESSION_LOSS_PENDING:
        return "XR_SESSION_LOSS_PENDING";
    case XR_EVENT_UNAVAILABLE:
        return "XR_EVENT_UNAVAILABLE";
    case XR_SESSION_NOT_FOCUSED:
        return "XR_SESSION_NOT_FOCUSED";
    case XR_ERROR_RUNTIME_FAILURE:
        return "XR_ERROR_RUNTIME_FAILURE";
    case XR_ERROR_VALIDATION_FAILURE:
        return "XR_ERROR_VALIDATION_FAILURE";
    case XR_ERROR_RUNTIME_UNAVAILABLE:
        return "XR_ERROR_RUNTIME_UNAVAILABLE";
    case XR_ERROR_EXTENSION_NOT_PRESENT:
        return "XR_ERROR_EXTENSION_NOT_PRESENT";
    case XR_ERROR_FORM_FACTOR_UNAVAILABLE:
        return "XR_ERROR_FORM_FACTOR_UNAVAILABLE";
    case XR_ERROR_INITIALIZATION_FAILED:
        return "XR_ERROR_INITIALIZATION_FAILED";
    case XR_ERROR_GRAPHICS_DEVICE_INVALID:
        return "XR_ERROR_GRAPHICS_DEVICE_INVALID";
    case XR_ERROR_CALL_ORDER_INVALID:
        return "XR_ERROR_CALL_ORDER_INVALID";
    default:
        return "XR_RESULT_UNKNOWN";
    }
}

axrb::protocol::Pose to_protocol_pose(const XrPosef& pose)
{
    axrb::protocol::Pose out{};
    out.x = pose.position.x;
    out.y = pose.position.y;
    out.z = pose.position.z;
    out.qx = pose.orientation.x;
    out.qy = pose.orientation.y;
    out.qz = pose.orientation.z;
    out.qw = pose.orientation.w;
    return out;
}

uint64_t monotonic_time_ns()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

bool decode_video_frame_to_image(const axrb::protocol::EncodedVideoFrame& frame,
                                 axrb::protocol::ImageFrameHeader* header,
                                 std::vector<uint8_t>* pixels)
{
    if (frame.codec != axrb::protocol::kEncodedVideoCodecAxrbRgba8) {
        return false;
    }
    const uint64_t expected = static_cast<uint64_t>(frame.width) * frame.height * 4;
    if (frame.width == 0 || frame.height == 0 || frame.payload.size() != expected) {
        return false;
    }

    axrb::protocol::ImageFrameHeader outHeader{};
    outHeader.width = frame.width;
    outHeader.height = frame.height;
    outHeader.layers = 1;
    outHeader.sequence = frame.frame_id;
    outHeader.monotonic_time_ns = frame.capture_time_ns;
    outHeader.payload_size = expected;
    *header = outHeader;
    *pixels = frame.payload;
    return true;
}

#if defined(_WIN32)
template <typename T>
class ComPtr {
public:
    ~ComPtr()
    {
        if (ptr_ != nullptr) {
            ptr_->Release();
        }
    }

    ComPtr() = default;
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ComPtr(ComPtr&& other) noexcept
        : ptr_(other.ptr_)
    {
        other.ptr_ = nullptr;
    }
    ComPtr& operator=(ComPtr&& other) noexcept
    {
        if (this != &other) {
            if (ptr_ != nullptr) {
                ptr_->Release();
            }
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
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
#endif

class OpenXrLoader {
public:
    ~OpenXrLoader()
    {
#if defined(_WIN32)
        if (library_ != nullptr) {
            FreeLibrary(library_);
        }
#else
        if (library_ != nullptr) {
            dlclose(library_);
        }
#endif
    }

    bool load()
    {
#if defined(_WIN32)
        char loaderPath[1024]{};
        const DWORD loaderPathSize = GetEnvironmentVariableA("AXRB_OPENXR_LOADER", loaderPath, sizeof(loaderPath));
        if (loaderPathSize > 0 && loaderPathSize < sizeof(loaderPath)) {
            library_ = LoadLibraryA(loaderPath);
        }
        if (library_ == nullptr) {
            library_ = LoadLibraryA("openxr_loader.dll");
        }
        if (library_ == nullptr) {
            library_ = LoadLibraryA(
                "C:\\Program Files (x86)\\Steam\\steamapps\\common\\SteamVR\\bin\\win64\\openxr_loader.dll");
        }
        if (library_ == nullptr) {
            std::fprintf(stderr, "AXRB OpenXR: failed to load openxr_loader.dll\n");
            return false;
        }
        auto get_symbol = [&](const char* name) -> void* {
            return reinterpret_cast<void*>(GetProcAddress(library_, name));
        };
#else
        library_ = dlopen("libopenxr_loader.so.1", RTLD_NOW | RTLD_LOCAL);
        if (library_ == nullptr) {
            library_ = dlopen("libopenxr_loader.so", RTLD_NOW | RTLD_LOCAL);
        }
        if (library_ == nullptr) {
            std::fprintf(stderr, "AXRB OpenXR: failed to load libopenxr_loader.so.1\n");
            return false;
        }
        auto get_symbol = [&](const char* name) -> void* {
            return dlsym(library_, name);
        };
#endif

        createInstance = reinterpret_cast<PFN_xrCreateInstance>(get_symbol("xrCreateInstance"));
        getInstanceProcAddr = reinterpret_cast<PFN_xrGetInstanceProcAddr>(get_symbol("xrGetInstanceProcAddr"));
        if (createInstance == nullptr || getInstanceProcAddr == nullptr) {
            std::fprintf(stderr, "AXRB OpenXR: loader is missing required exports\n");
            return false;
        }
        return true;
    }

    PFN_xrCreateInstance createInstance = nullptr;
    PFN_xrGetInstanceProcAddr getInstanceProcAddr = nullptr;

private:
#if defined(_WIN32)
    HMODULE library_ = nullptr;
#else
    void* library_ = nullptr;
#endif
};

struct HostImageFrame {
    void store(const axrb::protocol::ImageFrameHeader& newHeader, std::vector<uint8_t>&& newPixels)
    {
        std::lock_guard<std::mutex> lock(mutex);
        header = newHeader;
        pixels = std::make_shared<std::vector<uint8_t>>(std::move(newPixels));
        hasFrame = true;
    }

    bool snapshot(axrb::protocol::ImageFrameHeader* outHeader, std::shared_ptr<const std::vector<uint8_t>>* outPixels)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!hasFrame || pixels == nullptr) {
            return false;
        }
        *outHeader = header;
        *outPixels = pixels;
        return true;
    }

    std::mutex mutex;
    axrb::protocol::ImageFrameHeader header{};
    std::shared_ptr<std::vector<uint8_t>> pixels;
    bool hasFrame = false;
};

class OpenXrPoseSource {
public:
    explicit OpenXrPoseSource(HostImageFrame* imageFrame = nullptr)
        : imageFrame_(imageFrame)
    {
    }

    ~OpenXrPoseSource()
    {
        if (session_ != XR_NULL_HANDLE && endSession_ != nullptr && sessionRunning_) {
            endSession_(session_);
        }
        if (viewSpace_ != XR_NULL_HANDLE && destroySpace_ != nullptr) {
            destroySpace_(viewSpace_);
        }
        if (localSpace_ != XR_NULL_HANDLE && destroySpace_ != nullptr) {
            destroySpace_(localSpace_);
        }
#if defined(_WIN32)
        if (projectionSwapchain_ != XR_NULL_HANDLE && destroySwapchain_ != nullptr) {
            destroySwapchain_(projectionSwapchain_);
            projectionSwapchain_ = XR_NULL_HANDLE;
        }
#endif
        for (XrSpace& handSpace : handSpaces_) {
            if (handSpace != XR_NULL_HANDLE && destroySpace_ != nullptr) {
                destroySpace_(handSpace);
                handSpace = XR_NULL_HANDLE;
            }
        }
        if (handPoseAction_ != XR_NULL_HANDLE && destroyAction_ != nullptr) {
            destroyAction_(handPoseAction_);
            handPoseAction_ = XR_NULL_HANDLE;
        }
        if (actionSet_ != XR_NULL_HANDLE && destroyActionSet_ != nullptr) {
            destroyActionSet_(actionSet_);
            actionSet_ = XR_NULL_HANDLE;
        }
        if (session_ != XR_NULL_HANDLE && destroySession_ != nullptr) {
            destroySession_(session_);
        }
        if (instance_ != XR_NULL_HANDLE && destroyInstance_ != nullptr) {
            destroyInstance_(instance_);
        }
    }

    bool initialize()
    {
        if (!loader_.load()) {
            return false;
        }

        const char* extensions[] = {
#if defined(_WIN32)
            XR_KHR_D3D11_ENABLE_EXTENSION_NAME,
#else
            XR_MND_HEADLESS_EXTENSION_NAME,
            XR_KHR_CONVERT_TIMESPEC_TIME_EXTENSION_NAME,
#endif
        };
        XrInstanceCreateInfo instanceInfo{XR_TYPE_INSTANCE_CREATE_INFO};
        std::strncpy(instanceInfo.applicationInfo.applicationName, "AXRB Host Bridge", XR_MAX_APPLICATION_NAME_SIZE - 1);
        instanceInfo.applicationInfo.applicationVersion = 1;
        std::strncpy(instanceInfo.applicationInfo.engineName, "AXRB", XR_MAX_ENGINE_NAME_SIZE - 1);
        instanceInfo.applicationInfo.engineVersion = 1;
#if defined(_WIN32)
        instanceInfo.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
#else
        instanceInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
#endif
        instanceInfo.enabledExtensionCount = sizeof(extensions) / sizeof(extensions[0]);
        instanceInfo.enabledExtensionNames = extensions;

        XrResult result = loader_.createInstance(&instanceInfo, &instance_);
        if (result != XR_SUCCESS) {
            std::fprintf(
                stderr,
                "AXRB OpenXR: xrCreateInstance failed: %s (%d). Required extension: %s\n",
                xr_result_name(result),
                result,
#if defined(_WIN32)
                XR_KHR_D3D11_ENABLE_EXTENSION_NAME
#else
                XR_MND_HEADLESS_EXTENSION_NAME
#endif
            );
            return false;
        }

        if (!load_instance_functions()) {
            return false;
        }

        XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
        systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        result = getSystem_(instance_, &systemInfo, &systemId_);
        if (result != XR_SUCCESS) {
            std::fprintf(stderr, "AXRB OpenXR: xrGetSystem failed: %s (%d)\n", xr_result_name(result), result);
            return false;
        }

#if defined(_WIN32)
        if (!create_d3d11_device()) {
            return false;
        }
        XrGraphicsBindingD3D11KHR graphicsBinding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
        graphicsBinding.device = d3dDevice_.get();
#endif

        XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
        sessionInfo.systemId = systemId_;
#if defined(_WIN32)
        sessionInfo.next = &graphicsBinding;
#endif
        result = createSession_(instance_, &sessionInfo, &session_);
        if (result != XR_SUCCESS) {
            std::fprintf(stderr, "AXRB OpenXR: xrCreateSession failed: %s (%d)\n", xr_result_name(result), result);
            return false;
        }

        if (!create_reference_space(XR_REFERENCE_SPACE_TYPE_LOCAL, &localSpace_) ||
            !create_reference_space(XR_REFERENCE_SPACE_TYPE_VIEW, &viewSpace_)) {
            return false;
        }

#if defined(_WIN32)
        create_projection_swapchain();
#endif
        initialize_controller_actions();

        std::fprintf(
            stderr,
            "AXRB OpenXR: host tracking source initialized with %s\n",
#if defined(_WIN32)
            XR_KHR_D3D11_ENABLE_EXTENSION_NAME
#else
            XR_MND_HEADLESS_EXTENSION_NAME
#endif
        );
        return true;
    }

    axrb::protocol::PoseFrame make_frame(uint64_t sequence)
    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        pump_events();

        axrb::protocol::PoseFrame frame = latest_;
        frame.sequence = sequence;
        frame.monotonic_time_ns = monotonic_time_ns();

        if (!sessionRunning_) {
            latest_ = frame;
            return frame;
        }

        XrTime locateTime = current_xr_time();
        XrTime frameDisplayTime = locateTime;
        bool beganFrame = false;

        if (useFrameLoop_) {
            XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
            XrFrameState frameState{XR_TYPE_FRAME_STATE};
            XrResult result = waitFrame_(session_, &waitInfo, &frameState);
            if (result == XR_SUCCESS) {
                frameDisplayTime = frameState.predictedDisplayTime;
                locateTime = frameState.predictedDisplayTime;

                XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
                result = beginFrame_(session_, &beginInfo);
                if (result == XR_SUCCESS) {
                    beganFrame = true;
                } else if (result == XR_ERROR_CALL_ORDER_INVALID) {
                    std::fprintf(
                        stderr,
                        "AXRB OpenXR: xrBeginFrame was rejected in headless mode; continuing with pose-only locate\n");
                    useFrameLoop_ = false;
                } else {
                    std::fprintf(stderr, "AXRB OpenXR: xrBeginFrame failed: %s (%d)\n", xr_result_name(result), result);
                    latest_ = frame;
                    return frame;
                }
            } else if (result == XR_FRAME_DISCARDED) {
                latest_ = frame;
                return frame;
            } else {
                std::fprintf(stderr, "AXRB OpenXR: xrWaitFrame failed: %s (%d)\n", xr_result_name(result), result);
                useFrameLoop_ = false;
            }
        }

        if (locateTime == 0) {
            locateTime = current_xr_time();
        }
        if (locateTime == 0) {
            latest_ = frame;
            return frame;
        }

        XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
        XrResult result = locateSpace_(viewSpace_, localSpace_, locateTime, &location);
        if (result == XR_SUCCESS &&
            (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
            (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0) {
            frame.hmd = to_protocol_pose(location.pose);
            latest_ = frame;
            if (sequence % 90 == 0) {
                std::fprintf(
                    stderr,
                    "AXRB OpenXR: pose seq=%llu hmd=(%.3f %.3f %.3f)\n",
                    static_cast<unsigned long long>(sequence),
                    frame.hmd.x,
                    frame.hmd.y,
                    frame.hmd.z);
            }
        }

        locate_controller_spaces(frame, locateTime, sequence);

        if (beganFrame) {
            std::array<XrCompositionLayerProjectionView, 2> projectionViews{};
            XrCompositionLayerProjection projectionLayer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
            const XrCompositionLayerBaseHeader* layers[1]{};
            uint32_t layerCount = 0;
#if defined(_WIN32)
            if (projectionSwapchain_ != XR_NULL_HANDLE &&
                update_projection_layer(locateTime, projectionViews, projectionLayer)) {
                layers[0] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projectionLayer);
                layerCount = 1;
            }
#endif

            XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
            endInfo.displayTime = frameDisplayTime;
            endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
            endInfo.layerCount = layerCount;
            endInfo.layers = layerCount > 0 ? layers : nullptr;
            result = endFrame_(session_, &endInfo);
            if (result != XR_SUCCESS) {
                std::fprintf(stderr, "AXRB OpenXR: xrEndFrame failed: %s (%d)\n", xr_result_name(result), result);
                useFrameLoop_ = false;
            }
        }

        return latest_;
    }

    axrb::protocol::PoseFrame latest_frame(uint64_t sequence)
    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        axrb::protocol::PoseFrame frame = latest_;
        frame.sequence = sequence;
        frame.monotonic_time_ns = monotonic_time_ns();
        return frame;
    }

private:
    template <typename T>
    bool load_func(const char* name, T* out)
    {
        PFN_xrVoidFunction function = nullptr;
        const XrResult result = loader_.getInstanceProcAddr(instance_, name, &function);
        if (result != XR_SUCCESS || function == nullptr) {
            std::fprintf(stderr, "AXRB OpenXR: failed to load %s: %s (%d)\n", name, xr_result_name(result), result);
            return false;
        }
        *out = reinterpret_cast<T>(function);
        return true;
    }

    bool load_instance_functions()
    {
        return load_func("xrDestroyInstance", &destroyInstance_) &&
            load_func("xrGetSystem", &getSystem_) &&
            load_func("xrCreateSession", &createSession_) &&
            load_func("xrDestroySession", &destroySession_) &&
            load_func("xrCreateReferenceSpace", &createReferenceSpace_) &&
            load_func("xrDestroySpace", &destroySpace_) &&
            load_func("xrPollEvent", &pollEvent_) &&
            load_func("xrBeginSession", &beginSession_) &&
            load_func("xrEndSession", &endSession_) &&
            load_func("xrWaitFrame", &waitFrame_) &&
            load_func("xrBeginFrame", &beginFrame_) &&
            load_func("xrEndFrame", &endFrame_) &&
            load_func("xrLocateSpace", &locateSpace_) &&
            load_func("xrLocateViews", &locateViews_) &&
            load_func("xrStringToPath", &stringToPath_) &&
            load_func("xrCreateActionSet", &createActionSet_) &&
            load_func("xrDestroyActionSet", &destroyActionSet_) &&
            load_func("xrCreateAction", &createAction_) &&
            load_func("xrDestroyAction", &destroyAction_) &&
            load_func("xrSuggestInteractionProfileBindings", &suggestInteractionProfileBindings_) &&
            load_func("xrAttachSessionActionSets", &attachSessionActionSets_) &&
            load_func("xrCreateActionSpace", &createActionSpace_) &&
            load_func("xrSyncActions", &syncActions_) &&
            load_func("xrGetActionStatePose", &getActionStatePose_)
#if defined(_WIN32)
            && load_func("xrEnumerateSwapchainFormats", &enumerateSwapchainFormats_)
            && load_func("xrCreateSwapchain", &createSwapchain_)
            && load_func("xrDestroySwapchain", &destroySwapchain_)
            && load_func("xrEnumerateSwapchainImages", &enumerateSwapchainImages_)
            && load_func("xrAcquireSwapchainImage", &acquireSwapchainImage_)
            && load_func("xrWaitSwapchainImage", &waitSwapchainImage_)
            && load_func("xrReleaseSwapchainImage", &releaseSwapchainImage_)
            && load_func("xrGetD3D11GraphicsRequirementsKHR", &getD3D11GraphicsRequirements_)
#endif
#if !defined(_WIN32)
            && load_func("xrConvertTimespecTimeToTimeKHR", &convertTimespecTimeToTime_)
#endif
            ;
    }

    XrTime current_xr_time()
    {
#if !defined(_WIN32)
        if (convertTimespecTimeToTime_ != nullptr) {
            timespec now{};
            if (clock_gettime(CLOCK_MONOTONIC, &now) == 0) {
                XrTime xrTime = 0;
                const XrResult result = convertTimespecTimeToTime_(instance_, &now, &xrTime);
                if (result == XR_SUCCESS) {
                    return xrTime;
                }
            }
        }
#endif
        return 0;
    }

#if defined(_WIN32)
    bool create_d3d11_device()
    {
        XrGraphicsRequirementsD3D11KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
        const XrResult result = getD3D11GraphicsRequirements_(instance_, systemId_, &requirements);
        if (result != XR_SUCCESS) {
            std::fprintf(
                stderr,
                "AXRB OpenXR: xrGetD3D11GraphicsRequirementsKHR failed: %s (%d)\n",
                xr_result_name(result),
                result);
            return false;
        }

        ComPtr<IDXGIFactory1> factory;
        HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(factory.put()));
        if (FAILED(hr)) {
            std::fprintf(stderr, "AXRB OpenXR: CreateDXGIFactory1 failed: 0x%08lx\n", static_cast<unsigned long>(hr));
            return false;
        }

        ComPtr<IDXGIAdapter1> selectedAdapter;
        for (UINT i = 0;; ++i) {
            ComPtr<IDXGIAdapter1> adapter;
            hr = factory.get()->EnumAdapters1(i, adapter.put());
            if (hr == DXGI_ERROR_NOT_FOUND) {
                break;
            }
            if (FAILED(hr)) {
                continue;
            }

            DXGI_ADAPTER_DESC1 desc{};
            if (FAILED(adapter.get()->GetDesc1(&desc))) {
                continue;
            }

            if (desc.AdapterLuid.HighPart == requirements.adapterLuid.HighPart &&
                desc.AdapterLuid.LowPart == requirements.adapterLuid.LowPart) {
                selectedAdapter = std::move(adapter);
                break;
            }
        }

        if (selectedAdapter.get() == nullptr) {
            std::fprintf(stderr, "AXRB OpenXR: failed to find D3D11 adapter requested by OpenXR runtime\n");
            return false;
        }

        const D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_12_1,
            D3D_FEATURE_LEVEL_12_0,
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
        };
        D3D_FEATURE_LEVEL createdFeatureLevel{};
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        hr = D3D11CreateDevice(
            selectedAdapter.get(),
            D3D_DRIVER_TYPE_UNKNOWN,
            nullptr,
            flags,
            featureLevels,
            sizeof(featureLevels) / sizeof(featureLevels[0]),
            D3D11_SDK_VERSION,
            d3dDevice_.put(),
            &createdFeatureLevel,
            d3dContext_.put());
        if (FAILED(hr)) {
            std::fprintf(stderr, "AXRB OpenXR: D3D11CreateDevice failed: 0x%08lx\n", static_cast<unsigned long>(hr));
            return false;
        }

        if (createdFeatureLevel < requirements.minFeatureLevel) {
            std::fprintf(stderr, "AXRB OpenXR: D3D11 feature level is below runtime requirement\n");
            return false;
        }

        std::fprintf(stderr, "AXRB OpenXR: D3D11 graphics binding ready\n");
        return true;
    }

    bool create_projection_swapchain()
    {
        uint32_t formatCount = 0;
        XrResult result = enumerateSwapchainFormats_(session_, 0, &formatCount, nullptr);
        if (result != XR_SUCCESS || formatCount == 0) {
            std::fprintf(stderr, "AXRB OpenXR: xrEnumerateSwapchainFormats failed: %s (%d)\n", xr_result_name(result), result);
            return false;
        }

        std::vector<int64_t> formats(formatCount);
        result = enumerateSwapchainFormats_(session_, formatCount, &formatCount, formats.data());
        if (result != XR_SUCCESS) {
            std::fprintf(stderr, "AXRB OpenXR: xrEnumerateSwapchainFormats(list) failed: %s (%d)\n", xr_result_name(result), result);
            return false;
        }

        std::fprintf(stderr, "AXRB OpenXR: supported swapchain formats:");
        for (int64_t format : formats) {
            std::fprintf(stderr, " %lld", static_cast<long long>(format));
        }
        std::fprintf(stderr, "\n");

        int64_t selectedFormat = formats[0];
        constexpr int64_t preferredFormats[] = {
            DXGI_FORMAT_R8G8B8A8_UNORM,
            DXGI_FORMAT_B8G8R8A8_UNORM,
            DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
            DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
        };
        for (int64_t preferred : preferredFormats) {
            for (int64_t format : formats) {
                if (format == preferred) {
                    selectedFormat = format;
                    break;
                }
            }
            if (selectedFormat == preferred) {
                break;
            }
        }
        projectionFormat_ = selectedFormat;

        XrSwapchainCreateInfo swapchainInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        swapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        swapchainInfo.format = selectedFormat;
        swapchainInfo.sampleCount = 1;
        swapchainInfo.width = projectionWidth_;
        swapchainInfo.height = projectionHeight_;
        swapchainInfo.faceCount = 1;
        swapchainInfo.arraySize = 2;
        swapchainInfo.mipCount = 1;

        result = createSwapchain_(session_, &swapchainInfo, &projectionSwapchain_);
        if (result != XR_SUCCESS) {
            std::fprintf(stderr, "AXRB OpenXR: xrCreateSwapchain failed: %s (%d)\n", xr_result_name(result), result);
            return false;
        }

        uint32_t imageCount = 0;
        result = enumerateSwapchainImages_(projectionSwapchain_, 0, &imageCount, nullptr);
        if (result != XR_SUCCESS || imageCount == 0) {
            std::fprintf(stderr, "AXRB OpenXR: xrEnumerateSwapchainImages failed: %s (%d)\n", xr_result_name(result), result);
            return false;
        }

        projectionImages_.resize(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
        uploadedAndroidSequenceByImage_.assign(imageCount, UINT64_MAX);
        result = enumerateSwapchainImages_(
            projectionSwapchain_,
            imageCount,
            &imageCount,
            reinterpret_cast<XrSwapchainImageBaseHeader*>(projectionImages_.data()));
        if (result != XR_SUCCESS) {
            std::fprintf(stderr, "AXRB OpenXR: xrEnumerateSwapchainImages(list) failed: %s (%d)\n", xr_result_name(result), result);
            return false;
        }

        std::fprintf(
            stderr,
            "AXRB OpenXR: projection swapchain ready %ux%u images=%u\n",
            projectionWidth_,
            projectionHeight_,
            imageCount);
        return true;
    }

    bool update_projection_layer(
        XrTime displayTime,
        std::array<XrCompositionLayerProjectionView, 2>& projectionViews,
        XrCompositionLayerProjection& projectionLayer)
    {
        uint32_t imageIndex = 0;
        XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        XrResult result = acquireSwapchainImage_(projectionSwapchain_, &acquireInfo, &imageIndex);
        if (result != XR_SUCCESS || imageIndex >= projectionImages_.size()) {
            return false;
        }

        XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        waitInfo.timeout = XR_INFINITE_DURATION;
        result = waitSwapchainImage_(projectionSwapchain_, &waitInfo);
        if (result != XR_SUCCESS) {
            return false;
        }

        fill_projection_texture(projectionImages_[imageIndex].texture, imageIndex);

        XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        result = releaseSwapchainImage_(projectionSwapchain_, &releaseInfo);
        if (result != XR_SUCCESS) {
            return false;
        }

        XrViewLocateInfo locateInfo{XR_TYPE_VIEW_LOCATE_INFO};
        locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locateInfo.displayTime = displayTime;
        locateInfo.space = localSpace_;

        XrViewState viewState{XR_TYPE_VIEW_STATE};
        std::array<XrView, 2> views{XrView{XR_TYPE_VIEW}, XrView{XR_TYPE_VIEW}};
        uint32_t viewCount = 0;
        result = locateViews_(session_, &locateInfo, &viewState, static_cast<uint32_t>(views.size()), &viewCount, views.data());
        if (result != XR_SUCCESS || viewCount < 2) {
            return false;
        }

        for (uint32_t i = 0; i < 2; ++i) {
            projectionViews[i] = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
            projectionViews[i].pose = views[i].pose;
            projectionViews[i].fov.angleLeft = -kAppProjectionHalfFovRadians;
            projectionViews[i].fov.angleRight = kAppProjectionHalfFovRadians;
            projectionViews[i].fov.angleUp = kAppProjectionHalfFovRadians;
            projectionViews[i].fov.angleDown = -kAppProjectionHalfFovRadians;
            projectionViews[i].subImage.swapchain = projectionSwapchain_;
            projectionViews[i].subImage.imageRect.offset = {0, 0};
            projectionViews[i].subImage.imageRect.extent = {
                static_cast<int32_t>(projectionWidth_),
                static_cast<int32_t>(projectionHeight_),
            };
            projectionViews[i].subImage.imageArrayIndex = i;
        }

        projectionLayer.space = localSpace_;
        projectionLayer.viewCount = 2;
        projectionLayer.views = projectionViews.data();
        if (!reportedProjectionSubmit_) {
            std::fprintf(stderr, "AXRB OpenXR: submitting projection layer to runtime\n");
            reportedProjectionSubmit_ = true;
        }
        return true;
    }

    void fill_projection_texture(ID3D11Texture2D* texture, uint32_t imageIndex)
    {
        if (texture == nullptr || d3dContext_.get() == nullptr) {
            return;
        }
        if (upload_android_frame(texture, imageIndex)) {
            return;
        }

        const uint32_t stride = projectionWidth_ * 4;
        std::vector<uint8_t> pixels(static_cast<size_t>(stride) * projectionHeight_);
        const uint32_t tick = projectionFrameCounter_++;
        for (uint32_t y = 0; y < projectionHeight_; ++y) {
            for (uint32_t x = 0; x < projectionWidth_; ++x) {
                const size_t offset = static_cast<size_t>(y) * stride + x * 4;
                pixels[offset + 0] = static_cast<uint8_t>((x + tick * 3) & 0xff);
                pixels[offset + 1] = static_cast<uint8_t>((y + tick * 2) & 0xff);
                pixels[offset + 2] = static_cast<uint8_t>((x / 8 + y / 8 + tick) & 0xff);
                pixels[offset + 3] = 255;
            }
        }

        for (uint32_t layer = 0; layer < 2; ++layer) {
            D3D11_BOX box{};
            box.left = 0;
            box.top = 0;
            box.front = 0;
            box.right = projectionWidth_;
            box.bottom = projectionHeight_;
            box.back = 1;
            d3dContext_.get()->UpdateSubresource(texture, layer, &box, pixels.data(), stride, stride * projectionHeight_);
        }
    }

    bool upload_android_frame(ID3D11Texture2D* texture, uint32_t imageIndex)
    {
        if (imageFrame_ == nullptr) {
            return false;
        }
        if (imageIndex >= uploadedAndroidSequenceByImage_.size()) {
            return false;
        }

        axrb::protocol::ImageFrameHeader header{};
        std::shared_ptr<const std::vector<uint8_t>> pixels;
        if (!imageFrame_->snapshot(&header, &pixels)) {
            return false;
        }
        if (header.width == 0 || header.height == 0 || header.layers == 0 || pixels == nullptr || pixels->empty()) {
            return false;
        }
        if (header.sequence == uploadedAndroidSequenceByImage_[imageIndex]) {
            return true;
        }

        const uint32_t copyWidth = header.width < projectionWidth_ ? header.width : projectionWidth_;
        const uint32_t copyHeight = header.height < projectionHeight_ ? header.height : projectionHeight_;
        const uint32_t sourceStride = header.width * header.bytes_per_pixel;
        const bool needsBgra =
            projectionFormat_ == DXGI_FORMAT_B8G8R8A8_UNORM ||
            projectionFormat_ == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;

        std::vector<uint8_t> uploadBuffer(static_cast<size_t>(header.width) * header.height * header.layers * 4);
        for (uint32_t layer = 0; layer < header.layers; ++layer) {
            const uint64_t layerOffset = static_cast<uint64_t>(layer) * header.width * header.height * header.bytes_per_pixel;
            for (uint32_t y = 0; y < header.height; ++y) {
                const uint32_t sourceY = header.height - 1 - y;
                const uint64_t sourceRow = layerOffset + static_cast<uint64_t>(sourceY) * sourceStride;
                const uint64_t destRow =
                    (static_cast<uint64_t>(layer) * header.width * header.height + static_cast<uint64_t>(y) * header.width) * 4;
                for (uint32_t x = 0; x < header.width; ++x) {
                    const uint64_t source = sourceRow + static_cast<uint64_t>(x) * header.bytes_per_pixel;
                    const uint64_t dest = destRow + static_cast<uint64_t>(x) * 4;
                    if (needsBgra) {
                        uploadBuffer[dest + 0] = (*pixels)[source + 2];
                        uploadBuffer[dest + 1] = (*pixels)[source + 1];
                        uploadBuffer[dest + 2] = (*pixels)[source + 0];
                        uploadBuffer[dest + 3] = (*pixels)[source + 3];
                    } else {
                        uploadBuffer[dest + 0] = (*pixels)[source + 0];
                        uploadBuffer[dest + 1] = (*pixels)[source + 1];
                        uploadBuffer[dest + 2] = (*pixels)[source + 2];
                        uploadBuffer[dest + 3] = (*pixels)[source + 3];
                    }
                }
            }
        }

        const uint8_t* uploadPixels = uploadBuffer.data();
        const uint32_t uploadStride = header.width * 4;

        const uint32_t sourceLayers = header.layers;
        for (uint32_t targetLayer = 0; targetLayer < 2; ++targetLayer) {
            const uint32_t sourceLayer = sourceLayers > 1 ? targetLayer % sourceLayers : 0;
            const uint64_t sourceOffset =
                static_cast<uint64_t>(sourceLayer) * header.width * header.height * 4;
            if (sourceOffset >= uploadBuffer.size()) {
                continue;
            }

            D3D11_BOX box{};
            box.left = 0;
            box.top = 0;
            box.front = 0;
            box.right = copyWidth;
            box.bottom = copyHeight;
            box.back = 1;
            d3dContext_.get()->UpdateSubresource(
                texture,
                targetLayer,
                &box,
                uploadPixels + sourceOffset,
                uploadStride,
                uploadStride * header.height);
        }

        if (!reportedAndroidImageSubmit_) {
            std::fprintf(
                stderr,
                "AXRB OpenXR: submitting Android image frames to SteamVR (%ux%u layers=%u)\n",
                header.width,
                header.height,
                header.layers);
            reportedAndroidImageSubmit_ = true;
        }
        uploadedAndroidSequenceByImage_[imageIndex] = header.sequence;
        return true;
    }
#endif

    bool create_reference_space(XrReferenceSpaceType type, XrSpace* space)
    {
        XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        spaceInfo.referenceSpaceType = type;
        spaceInfo.poseInReferenceSpace.orientation.w = 1.0f;
        const XrResult result = createReferenceSpace_(session_, &spaceInfo, space);
        if (result != XR_SUCCESS) {
            std::fprintf(stderr, "AXRB OpenXR: xrCreateReferenceSpace failed: %s (%d)\n", xr_result_name(result), result);
            return false;
        }
        return true;
    }

    bool string_to_path(const char* text, XrPath* path)
    {
        const XrResult result = stringToPath_(instance_, text, path);
        if (result != XR_SUCCESS) {
            std::fprintf(stderr, "AXRB OpenXR: xrStringToPath(%s) failed: %s (%d)\n", text, xr_result_name(result), result);
            return false;
        }
        return true;
    }

    void initialize_controller_actions()
    {
        if (!string_to_path("/user/hand/left", &handSubactionPaths_[0]) ||
            !string_to_path("/user/hand/right", &handSubactionPaths_[1])) {
            return;
        }

        XrActionSetCreateInfo actionSetInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
        std::strncpy(actionSetInfo.actionSetName, "axrb_gameplay", XR_MAX_ACTION_SET_NAME_SIZE - 1);
        std::strncpy(actionSetInfo.localizedActionSetName, "AXRB gameplay", XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1);
        actionSetInfo.priority = 0;
        XrResult result = createActionSet_(instance_, &actionSetInfo, &actionSet_);
        if (result != XR_SUCCESS) {
            std::fprintf(stderr, "AXRB OpenXR: xrCreateActionSet failed: %s (%d)\n", xr_result_name(result), result);
            actionSet_ = XR_NULL_HANDLE;
            return;
        }

        XrActionCreateInfo actionInfo{XR_TYPE_ACTION_CREATE_INFO};
        actionInfo.actionType = XR_ACTION_TYPE_POSE_INPUT;
        std::strncpy(actionInfo.actionName, "hand_pose", XR_MAX_ACTION_NAME_SIZE - 1);
        std::strncpy(actionInfo.localizedActionName, "Hand pose", XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
        actionInfo.countSubactionPaths = static_cast<uint32_t>(handSubactionPaths_.size());
        actionInfo.subactionPaths = handSubactionPaths_.data();
        result = createAction_(actionSet_, &actionInfo, &handPoseAction_);
        if (result != XR_SUCCESS) {
            std::fprintf(stderr, "AXRB OpenXR: xrCreateAction(hand_pose) failed: %s (%d)\n", xr_result_name(result), result);
            return;
        }

        suggest_pose_bindings("/interaction_profiles/oculus/touch_controller", "/input/grip/pose");
        suggest_pose_bindings("/interaction_profiles/valve/index_controller", "/input/grip/pose");
        suggest_pose_bindings("/interaction_profiles/htc/vive_controller", "/input/grip/pose");

        XrSessionActionSetsAttachInfo attachInfo{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
        attachInfo.countActionSets = 1;
        attachInfo.actionSets = &actionSet_;
        result = attachSessionActionSets_(session_, &attachInfo);
        if (result != XR_SUCCESS) {
            std::fprintf(stderr, "AXRB OpenXR: xrAttachSessionActionSets failed: %s (%d)\n", xr_result_name(result), result);
            return;
        }

        for (size_t i = 0; i < handSpaces_.size(); ++i) {
            XrActionSpaceCreateInfo spaceInfo{XR_TYPE_ACTION_SPACE_CREATE_INFO};
            spaceInfo.action = handPoseAction_;
            spaceInfo.subactionPath = handSubactionPaths_[i];
            spaceInfo.poseInActionSpace.orientation.w = 1.0f;
            result = createActionSpace_(session_, &spaceInfo, &handSpaces_[i]);
            if (result != XR_SUCCESS) {
                std::fprintf(
                    stderr,
                    "AXRB OpenXR: xrCreateActionSpace hand %zu failed: %s (%d)\n",
                    i,
                    xr_result_name(result),
                    result);
                handSpaces_[i] = XR_NULL_HANDLE;
            }
        }

        controllerActionsReady_ = handSpaces_[0] != XR_NULL_HANDLE || handSpaces_[1] != XR_NULL_HANDLE;
        if (controllerActionsReady_) {
            std::fprintf(stderr, "AXRB OpenXR: controller pose actions ready\n");
        }
    }

    void suggest_pose_bindings(const char* interactionProfilePath, const char* poseInputSuffix)
    {
        if (handPoseAction_ == XR_NULL_HANDLE) {
            return;
        }

        XrPath profilePath = XR_NULL_PATH;
        if (!string_to_path(interactionProfilePath, &profilePath)) {
            return;
        }

        char leftBindingText[128]{};
        char rightBindingText[128]{};
        std::snprintf(leftBindingText, sizeof(leftBindingText), "/user/hand/left%s", poseInputSuffix);
        std::snprintf(rightBindingText, sizeof(rightBindingText), "/user/hand/right%s", poseInputSuffix);

        std::array<XrActionSuggestedBinding, 2> bindings{};
        if (!string_to_path(leftBindingText, &bindings[0].binding) ||
            !string_to_path(rightBindingText, &bindings[1].binding)) {
            return;
        }
        bindings[0].action = handPoseAction_;
        bindings[1].action = handPoseAction_;

        XrInteractionProfileSuggestedBinding suggested{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        suggested.interactionProfile = profilePath;
        suggested.countSuggestedBindings = static_cast<uint32_t>(bindings.size());
        suggested.suggestedBindings = bindings.data();
        const XrResult result = suggestInteractionProfileBindings_(instance_, &suggested);
        if (result != XR_SUCCESS) {
            std::fprintf(
                stderr,
                "AXRB OpenXR: bindings for %s rejected: %s (%d)\n",
                interactionProfilePath,
                xr_result_name(result),
                result);
        }
    }

    void locate_controller_spaces(axrb::protocol::PoseFrame& frame, XrTime locateTime, uint64_t sequence)
    {
        if (!controllerActionsReady_) {
            return;
        }

        XrActiveActionSet activeActionSet{};
        activeActionSet.actionSet = actionSet_;
        activeActionSet.subactionPath = XR_NULL_PATH;
        XrActionsSyncInfo syncInfo{XR_TYPE_ACTIONS_SYNC_INFO};
        syncInfo.countActiveActionSets = 1;
        syncInfo.activeActionSets = &activeActionSet;
        XrResult result = syncActions_(session_, &syncInfo);
        if (result == XR_SESSION_NOT_FOCUSED) {
            return;
        }
        if (result != XR_SUCCESS) {
            if (!reportedSyncFailure_) {
                std::fprintf(stderr, "AXRB OpenXR: xrSyncActions failed: %s (%d)\n", xr_result_name(result), result);
                reportedSyncFailure_ = true;
            }
            return;
        }

        bool locatedAny = false;
        for (size_t i = 0; i < handSpaces_.size(); ++i) {
            if (handSpaces_[i] == XR_NULL_HANDLE) {
                continue;
            }

            XrActionStateGetInfo stateInfo{XR_TYPE_ACTION_STATE_GET_INFO};
            stateInfo.action = handPoseAction_;
            stateInfo.subactionPath = handSubactionPaths_[i];
            XrActionStatePose state{XR_TYPE_ACTION_STATE_POSE};
            result = getActionStatePose_(session_, &stateInfo, &state);
            if (result != XR_SUCCESS || state.isActive == XR_FALSE) {
                continue;
            }

            XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
            result = locateSpace_(handSpaces_[i], localSpace_, locateTime, &location);
            if (result != XR_SUCCESS ||
                (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) == 0 ||
                (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) == 0) {
                continue;
            }

            axrb::protocol::Pose& target = (i == 0) ? frame.left_controller : frame.right_controller;
            target = to_protocol_pose(location.pose);
            locatedAny = true;
        }

        if (locatedAny) {
            latest_ = frame;
            if (sequence % 90 == 0) {
                std::fprintf(
                    stderr,
                    "AXRB OpenXR: controllers seq=%llu left=(%.3f %.3f %.3f) right=(%.3f %.3f %.3f)\n",
                    static_cast<unsigned long long>(sequence),
                    frame.left_controller.x,
                    frame.left_controller.y,
                    frame.left_controller.z,
                    frame.right_controller.x,
                    frame.right_controller.y,
                    frame.right_controller.z);
            }
        }
    }

    void pump_events()
    {
        XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
        while (pollEvent_(instance_, &event) == XR_SUCCESS) {
            if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                const auto* stateEvent = reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);
                handle_session_state(stateEvent->state);
            }
            event = {XR_TYPE_EVENT_DATA_BUFFER};
        }
    }

    void handle_session_state(XrSessionState state)
    {
        if (state == XR_SESSION_STATE_READY && !sessionRunning_) {
            XrSessionBeginInfo beginInfo{XR_TYPE_SESSION_BEGIN_INFO};
            beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            const XrResult result = beginSession_(session_, &beginInfo);
            if (result == XR_SUCCESS) {
                sessionRunning_ = true;
                std::fprintf(stderr, "AXRB OpenXR: host session running\n");
            } else {
                std::fprintf(stderr, "AXRB OpenXR: xrBeginSession failed: %s (%d)\n", xr_result_name(result), result);
            }
        } else if (state == XR_SESSION_STATE_STOPPING && sessionRunning_) {
            endSession_(session_);
            sessionRunning_ = false;
            std::fprintf(stderr, "AXRB OpenXR: host session stopped\n");
        } else if (state == XR_SESSION_STATE_EXITING || state == XR_SESSION_STATE_LOSS_PENDING) {
            sessionRunning_ = false;
        }
    }

    OpenXrLoader loader_;
    HostImageFrame* imageFrame_ = nullptr;
    XrInstance instance_ = XR_NULL_HANDLE;
    XrSystemId systemId_ = XR_NULL_SYSTEM_ID;
    XrSession session_ = XR_NULL_HANDLE;
    XrSpace localSpace_ = XR_NULL_HANDLE;
    XrSpace viewSpace_ = XR_NULL_HANDLE;
    XrActionSet actionSet_ = XR_NULL_HANDLE;
    XrAction handPoseAction_ = XR_NULL_HANDLE;
    std::array<XrPath, 2> handSubactionPaths_{XR_NULL_PATH, XR_NULL_PATH};
    std::array<XrSpace, 2> handSpaces_{XR_NULL_HANDLE, XR_NULL_HANDLE};
    bool sessionRunning_ = false;
    bool useFrameLoop_ = true;
    bool controllerActionsReady_ = false;
    bool reportedSyncFailure_ = false;
    axrb::protocol::PoseFrame latest_{};
    std::mutex frameMutex_;

    PFN_xrDestroyInstance destroyInstance_ = nullptr;
    PFN_xrGetSystem getSystem_ = nullptr;
    PFN_xrCreateSession createSession_ = nullptr;
    PFN_xrDestroySession destroySession_ = nullptr;
    PFN_xrCreateReferenceSpace createReferenceSpace_ = nullptr;
    PFN_xrDestroySpace destroySpace_ = nullptr;
    PFN_xrPollEvent pollEvent_ = nullptr;
    PFN_xrBeginSession beginSession_ = nullptr;
    PFN_xrEndSession endSession_ = nullptr;
    PFN_xrWaitFrame waitFrame_ = nullptr;
    PFN_xrBeginFrame beginFrame_ = nullptr;
    PFN_xrEndFrame endFrame_ = nullptr;
    PFN_xrLocateSpace locateSpace_ = nullptr;
    PFN_xrLocateViews locateViews_ = nullptr;
    PFN_xrStringToPath stringToPath_ = nullptr;
    PFN_xrCreateActionSet createActionSet_ = nullptr;
    PFN_xrDestroyActionSet destroyActionSet_ = nullptr;
    PFN_xrCreateAction createAction_ = nullptr;
    PFN_xrDestroyAction destroyAction_ = nullptr;
    PFN_xrSuggestInteractionProfileBindings suggestInteractionProfileBindings_ = nullptr;
    PFN_xrAttachSessionActionSets attachSessionActionSets_ = nullptr;
    PFN_xrCreateActionSpace createActionSpace_ = nullptr;
    PFN_xrSyncActions syncActions_ = nullptr;
    PFN_xrGetActionStatePose getActionStatePose_ = nullptr;
#if defined(_WIN32)
    PFN_xrEnumerateSwapchainFormats enumerateSwapchainFormats_ = nullptr;
    PFN_xrCreateSwapchain createSwapchain_ = nullptr;
    PFN_xrDestroySwapchain destroySwapchain_ = nullptr;
    PFN_xrEnumerateSwapchainImages enumerateSwapchainImages_ = nullptr;
    PFN_xrAcquireSwapchainImage acquireSwapchainImage_ = nullptr;
    PFN_xrWaitSwapchainImage waitSwapchainImage_ = nullptr;
    PFN_xrReleaseSwapchainImage releaseSwapchainImage_ = nullptr;
    PFN_xrGetD3D11GraphicsRequirementsKHR getD3D11GraphicsRequirements_ = nullptr;
    ComPtr<ID3D11Device> d3dDevice_;
    ComPtr<ID3D11DeviceContext> d3dContext_;
    XrSwapchain projectionSwapchain_ = XR_NULL_HANDLE;
    std::vector<XrSwapchainImageD3D11KHR> projectionImages_;
    std::vector<uint64_t> uploadedAndroidSequenceByImage_;
    int64_t projectionFormat_ = 0;
    uint32_t projectionWidth_ = 512;
    uint32_t projectionHeight_ = 512;
    uint32_t projectionFrameCounter_ = 0;
    bool reportedProjectionSubmit_ = false;
    bool reportedAndroidImageSubmit_ = false;
#endif
#if !defined(_WIN32)
    PFN_xrConvertTimespecTimeToTimeKHR convertTimespecTimeToTime_ = nullptr;
#endif
};

} // namespace

int OpenXrHost::run(int argc, char** argv)
{
    if (argc <= 1 || std::string_view(argv[1]) == "--smoke") {
        std::fprintf(stderr, "AXRB host bridge smoke OK\n");
        return 0;
    }

    const std::string_view mode(argv[1]);
    if (mode == "--serve-gpu-fds") {
        const char* socket_path = argc >= 3 ? argv[2] : "/tmp/axrb-gpu-frame.sock";
        uint32_t frames = 0;
        if (argc >= 4 && !parse_u32(argv[3], &frames)) {
            std::fprintf(stderr, "Invalid frame count: %s\n", argv[3]);
            return 2;
        }

        axrb::protocol::UnixFdFrameServer server;
        return server.serve(socket_path, frames, [](const axrb::protocol::GpuFrameDescriptor& descriptor,
                                                    std::vector<axrb::protocol::UniqueFd>&& fds) {
            std::fprintf(stderr,
                         "AXRB GPU: frame seq=%llu %ux%u layers=%u drm_format=0x%llx modifier=0x%llx planes=%u fds=%zu\n",
                         static_cast<unsigned long long>(descriptor.sequence),
                         descriptor.width,
                         descriptor.height,
                         descriptor.layers,
                         static_cast<unsigned long long>(descriptor.drm_format),
                         static_cast<unsigned long long>(descriptor.drm_modifier),
                         descriptor.plane_count,
                         fds.size());
        });
    }

    if (mode == "--video-recv-udp") {
        uint16_t port = 38492;
        uint32_t frames = 0;
        if (argc >= 3 && !parse_u16(argv[2], &port)) {
            std::fprintf(stderr, "Invalid port: %s\n", argv[2]);
            return 2;
        }
        if (argc >= 4 && !parse_u32(argv[3], &frames)) {
            std::fprintf(stderr, "Invalid frame count: %s\n", argv[3]);
            return 2;
        }

        uint64_t first_time = 0;
        uint64_t last_time = 0;
        uint32_t received = 0;
        axrb::protocol::UdpVideoReceiver receiver;
        return receiver.receive(port, frames, [&](axrb::protocol::EncodedVideoFrame&& frame) {
            if (first_time == 0) {
                first_time = monotonic_time_ns();
            }
            last_time = monotonic_time_ns();
            ++received;
            if (received == 1 || received % 90 == 0) {
                const double seconds = last_time > first_time ? static_cast<double>(last_time - first_time) / 1000000000.0 : 0.0;
                const double fps = seconds > 0.0 ? static_cast<double>(received - 1) / seconds : 0.0;
                std::fprintf(stderr,
                             "AXRB Video UDP: frame=%llu %ux%u codec=%u bytes=%zu avg_fps=%.2f\n",
                             static_cast<unsigned long long>(frame.frame_id),
                             frame.width,
                             frame.height,
                             frame.codec,
                             frame.payload.size(),
                             fps);
            }
        });
    }

    if (mode == "--video-send-synthetic") {
        const char* host = argc >= 3 ? argv[2] : "127.0.0.1";
        uint16_t port = 38492;
        uint32_t frames = 900;
        uint32_t fps = 90;
        if (argc >= 4 && !parse_u16(argv[3], &port)) {
            std::fprintf(stderr, "Invalid port: %s\n", argv[3]);
            return 2;
        }
        if (argc >= 5 && !parse_u32(argv[4], &frames)) {
            std::fprintf(stderr, "Invalid frame count: %s\n", argv[4]);
            return 2;
        }
        if (argc >= 6 && (!parse_u32(argv[5], &fps) || fps == 0)) {
            std::fprintf(stderr, "Invalid fps: %s\n", argv[5]);
            return 2;
        }

        axrb::protocol::UdpVideoSender sender;
        if (!sender.open(host, port)) {
            return 1;
        }

        const auto frame_interval = std::chrono::nanoseconds(1000000000ull / fps);
        auto next_frame = std::chrono::steady_clock::now();
        for (uint32_t i = 0; i < frames; ++i) {
            auto frame = axrb::protocol::make_synthetic_encoded_frame(i, 1920, 1080, 64 * 1024);
            if (!sender.send_frame(frame)) {
                std::fprintf(stderr, "AXRB Video UDP: send failed at frame %u\n", i);
                return 1;
            }
            next_frame += frame_interval;
            std::this_thread::sleep_until(next_frame);
        }
        return 0;
    }

    if (mode == "--video-send-rgba") {
        const char* host = argc >= 3 ? argv[2] : "127.0.0.1";
        uint16_t port = 38492;
        uint32_t frames = 900;
        uint32_t fps = 90;
        uint32_t width = 320;
        uint32_t height = 180;
        if (argc >= 4 && !parse_u16(argv[3], &port)) {
            std::fprintf(stderr, "Invalid port: %s\n", argv[3]);
            return 2;
        }
        if (argc >= 5 && !parse_u32(argv[4], &frames)) {
            std::fprintf(stderr, "Invalid frame count: %s\n", argv[4]);
            return 2;
        }
        if (argc >= 6 && (!parse_u32(argv[5], &fps) || fps == 0)) {
            std::fprintf(stderr, "Invalid fps: %s\n", argv[5]);
            return 2;
        }
        if (argc >= 7 && !parse_u32(argv[6], &width)) {
            std::fprintf(stderr, "Invalid width: %s\n", argv[6]);
            return 2;
        }
        if (argc >= 8 && !parse_u32(argv[7], &height)) {
            std::fprintf(stderr, "Invalid height: %s\n", argv[7]);
            return 2;
        }

        axrb::protocol::UdpVideoSender sender;
        if (!sender.open(host, port)) {
            return 1;
        }

        const auto frame_interval = std::chrono::nanoseconds(1000000000ull / fps);
        auto next_frame = std::chrono::steady_clock::now();
        for (uint32_t i = 0; i < frames; ++i) {
            auto frame = axrb::protocol::make_synthetic_rgba_frame(i, width, height);
            if (!sender.send_frame(frame)) {
                std::fprintf(stderr, "AXRB Video UDP: RGBA send failed at frame %u\n", i);
                return 1;
            }
            next_frame += frame_interval;
            std::this_thread::sleep_until(next_frame);
        }
        return 0;
    }

    if (mode != "--serve" && mode != "--serve-openxr") {
        print_usage();
        return 2;
    }

    uint16_t port = 38490;
    uint32_t frames = 0;
    if (argc >= 3 && !parse_u16(argv[2], &port)) {
        std::fprintf(stderr, "Invalid port: %s\n", argv[2]);
        return 2;
    }
    if (argc >= 4 && !parse_u32(argv[3], &frames)) {
        std::fprintf(stderr, "Invalid frame count: %s\n", argv[3]);
        return 2;
    }

    axrb::protocol::TcpPoseServer server;
    if (mode == "--serve-openxr") {
        HostImageFrame imageFrame;
        OpenXrPoseSource poseSource(&imageFrame);
        if (!poseSource.initialize()) {
            return 1;
        }
        std::thread imageThread([&] {
            axrb::protocol::TcpImageServer imageServer;
            imageServer.serve_with_callback(
                38491,
                0,
                [&](const axrb::protocol::ImageFrameHeader& header, std::vector<uint8_t>&& pixels) {
                    imageFrame.store(header, std::move(pixels));
                });
        });
        imageThread.detach();

        std::thread videoThread([&] {
            axrb::protocol::UdpVideoReceiver videoReceiver;
            videoReceiver.receive(38492, 0, [&](axrb::protocol::EncodedVideoFrame&& frame) {
                axrb::protocol::ImageFrameHeader header{};
                std::vector<uint8_t> pixels;
                if (!decode_video_frame_to_image(frame, &header, &pixels)) {
                    static bool reportedUnsupported = false;
                    if (!reportedUnsupported) {
                        std::fprintf(stderr, "AXRB Video UDP: received unsupported codec %u for OpenXR image feed\n", frame.codec);
                        reportedUnsupported = true;
                    }
                    return;
                }
                imageFrame.store(header, std::move(pixels));
            });
        });
        videoThread.detach();

        std::thread poseThread([&] {
            server.serve_with_producer(port, frames, [&](uint64_t sequence) {
                return poseSource.latest_frame(sequence);
            });
        });
        poseThread.detach();

        uint64_t sequence = 0;
        while (frames == 0 || sequence < frames) {
            poseSource.make_frame(sequence++);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return 0;
   }

    return server.serve(port, frames);
}

} // namespace axrb::host
