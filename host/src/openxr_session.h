#pragma once

#include "menu_shortcut.h"
#include "perf_stats.h"
#include "mirror_window.h"
#include "gpu_frame_batch.h"
#include "frame_pool.h"
#include "fps_counter.h"
#include <atomic>
#include "debug_frame_capture.h"
#include "windows_gpu_frame.h"

#include "gpu_transport.h"
#include "image_transport.h"
#include "transport_tcp.h"
#include "video_transport.h"

#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <array>
#include <memory>
#include <mutex>
#include <string_view>
#include <string>
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
#include <d3d11_4.h>
#include <dxgi1_2.h>
#endif

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include "equirect_renderer.h"
#include "quad_renderer.h"

#if defined(_WIN32)
#else
#include <dlfcn.h>
#include <time.h>
#endif

namespace axrb::host::detail {

constexpr float kAppProjectionHalfFovRadians = 0.95f;

const char* xr_result_name(XrResult result);
axrb::protocol::Pose to_protocol_pose(const XrPosef& pose);
uint64_t monotonic_time_ns();

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

struct HostImageSnapshot {
    axrb::protocol::ImageFrameHeader header{};
    axrb::protocol::ImageProjection projection{};
    std::shared_ptr<const std::vector<uint8_t>> pixels;
    std::shared_ptr<GpuFrameBatch> gpu;
    std::chrono::steady_clock::time_point receivedAt{};
};

struct HostImageFrame {
    std::atomic<uint64_t> deliveredFrames{0};
    void store(const axrb::protocol::ImageFrameHeader& header, std::vector<uint8_t>&& pixels,
               const axrb::protocol::ImageProjection& projection = {}, std::shared_ptr<GpuFrameBatch> gpu = {})
    {
        static axrb::protocol::FrameIntervals stats("host-image-arrival");
        stats.record();
        HostImageSnapshot next{header, projection,
            std::make_shared<const std::vector<uint8_t>>(std::move(pixels)), std::move(gpu), std::chrono::steady_clock::now()};
        { std::lock_guard lock(mutex); std::swap(latest, next); }
        deliveredFrames.fetch_add(1, std::memory_order_relaxed);
        // Release the previous slot outside the publication lock.
    }

    HostImageSnapshot snapshot() {
        std::lock_guard lock(mutex);
        return latest;
    }
private:
    std::mutex mutex;
    HostImageSnapshot latest;
};

class OpenXrSession {
public:
    explicit OpenXrSession(HostImageFrame* imageFrame = nullptr)
        : imageFrame_(imageFrame)
    {
    }

    ~OpenXrSession();

    bool initialize(const std::string& gameName);

    bool open_mirror(const std::string& gameName);
    bool pump_mirror();

    axrb::protocol::PoseFrame make_frame(uint64_t sequence);

    axrb::protocol::PoseFrame latest_frame(uint64_t sequence);

    // Receive on the image thread, independently of the OpenXR compositor clock.
    // The same lock protects cross-device cache ownership and its render poses.
    bool receive_image(const axrb::protocol::ImageFrameHeader& header,
                       const axrb::protocol::ImageProjection& projection,
                       std::vector<uint8_t>&& pixels);

    bool drives_frame_loop() const;

private:
    void publish_pose(const axrb::protocol::PoseFrame& frame);

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

    bool load_instance_functions();

    XrTime current_xr_time();

#if defined(_WIN32)
    bool create_d3d11_device();

    bool create_projection_swapchain();
    bool acquire_panel_swapchain(const HostImageSnapshot& frame);

    bool update_projection_layer(
        XrTime displayTime,
        std::array<XrCompositionLayerProjectionView, 2>& projectionViews,
        XrCompositionLayerProjection& projectionLayer,
        std::vector<XrCompositionLayerQuad>& quadLayers,
        std::vector<XrCompositionLayerEquirect2KHR>& sphereLayers,
        uint32_t& quadCount, bool& mixedProjection);

    bool render_equirect(uint32_t slot, XrTime time, ID3D11Texture2D* source,
        const XrCompositionLayerEquirect2KHR& layer, const axrb::protocol::ImageEquirect& metadata);
    bool update_fps_hud(XrCompositionLayerQuad& layer, uint32_t existingLayers);
    bool fill_projection_texture(ID3D11Texture2D* texture, uint32_t imageIndex, const HostImageSnapshot& frame);

    bool upload_android_frame(ID3D11Texture2D* texture, uint32_t imageIndex, const HostImageSnapshot& frame);
#endif

    bool create_reference_space(XrReferenceSpaceType type, XrSpace* space);

    bool string_to_path(const char* text, XrPath* path);

    enum InputAction { Trigger, Squeeze, Stick, Primary, Secondary, Menu, StickPress,
                       PrimaryContact, SecondaryContact, TriggerContact, StickContact, ThumbrestContact, InputCount };

    void initialize_hand_tracking();

    void locate_hand_joints(axrb::protocol::PoseFrame& frame, XrTime time, uint64_t sequence);

    void initialize_controller_actions();

    void suggest_pose_bindings(const char* interactionProfilePath, const char* poseInputSuffix);

    void locate_controller_spaces(axrb::protocol::PoseFrame& frame, XrTime locateTime, uint64_t sequence);

    void pump_events();

    void handle_session_state(XrSessionState state);

    OpenXrLoader loader_;
    HostImageFrame* imageFrame_ = nullptr;
    XrInstance instance_ = XR_NULL_HANDLE;
    XrSystemId systemId_ = XR_NULL_SYSTEM_ID;
    XrSession session_ = XR_NULL_HANDLE;
    XrSpace localSpace_ = XR_NULL_HANDLE;
    XrSpace appLocalSpace_ = XR_NULL_HANDLE;
    XrReferenceSpaceType trackingSpaceType_ = XR_REFERENCE_SPACE_TYPE_LOCAL;
    bool reportedLocalOrigin_ = false;
    bool localOriginInitialized_ = false;
    XrSpace viewSpace_ = XR_NULL_HANDLE;
    XrActionSet actionSet_ = XR_NULL_HANDLE;
    XrAction handPoseAction_ = XR_NULL_HANDLE;
    XrAction aimPoseAction_ = XR_NULL_HANDLE;
    bool equirectEnabled_ = false;
    bool handTrackingEnabled_ = false, handDataSourceEnabled_ = false;
    PFN_xrCreateHandTrackerEXT createHandTracker_ = nullptr;
    PFN_xrDestroyHandTrackerEXT destroyHandTracker_ = nullptr;
    PFN_xrLocateHandJointsEXT locateHandJoints_ = nullptr;
    std::array<XrHandTrackerEXT, 2> handTrackers_{};
    std::array<XrAction, InputCount> inputActions_{};
    std::array<XrPath, 2> handSubactionPaths_{XR_NULL_PATH, XR_NULL_PATH};
    std::array<XrSpace, 2> handSpaces_{XR_NULL_HANDLE, XR_NULL_HANDLE};
    std::array<XrSpace, 2> aimSpaces_{XR_NULL_HANDLE, XR_NULL_HANDLE};
    bool sessionRunning_ = false;
    bool useFrameLoop_ = true;
    bool controllerActionsReady_ = false;
    MenuShortcut menuShortcut_;
    bool reportedSyncFailure_ = false;
    axrb::protocol::PoseFrame latest_{};
    std::mutex frameMutex_;

    PFN_xrDestroyInstance destroyInstance_ = nullptr;
    PFN_xrGetSystem getSystem_ = nullptr;
    PFN_xrEnumerateViewConfigurationViews enumerateViewConfigurationViews_ = nullptr;
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
    PFN_xrGetActionStateBoolean getActionStateBoolean_ = nullptr;
    PFN_xrGetActionStateFloat getActionStateFloat_ = nullptr;
    PFN_xrGetActionStateVector2f getActionStateVector2f_ = nullptr;
#if defined(_WIN32)
    PFN_xrEnumerateSwapchainFormats enumerateSwapchainFormats_ = nullptr;
    PFN_xrCreateSwapchain createSwapchain_ = nullptr;
    PFN_xrDestroySwapchain destroySwapchain_ = nullptr;
    PFN_xrEnumerateSwapchainImages enumerateSwapchainImages_ = nullptr;
    PFN_xrAcquireSwapchainImage acquireSwapchainImage_ = nullptr;
    PFN_xrWaitSwapchainImage waitSwapchainImage_ = nullptr;
    PFN_xrReleaseSwapchainImage releaseSwapchainImage_ = nullptr;
    PFN_xrGetD3D11GraphicsRequirementsKHR getD3D11GraphicsRequirements_ = nullptr;
    // Overlapping receipt remains opt-in until the reported stereo regression
    // has been isolated on the headset, not just in synthetic GPU tests.
    const bool concurrentGpuFrames_ = [] {
        const char* value = std::getenv("AXRB_ASYNC_GPU_HANDOFF");
        return value && std::strcmp(value, "1") == 0;
    }();
    std::mutex frameHandoffMutex_;
    FramePool<GpuFrameBatch> gpuFrames_;
    // Only the receiving thread assembles pending batches. Publication pins
    // the entire batch so its textures and projection metadata stay together.
    std::shared_ptr<GpuFrameBatch> pendingGpuFrame_;
    uint32_t pendingMixedCount_ = 0, pendingMixedIndex_ = 0;
    uint64_t pendingMixedSequence_ = 0;
    std::vector<std::vector<axrb::protocol::ImageProjection>> uploadedMixedQuads_;
    std::vector<std::vector<XrExtent2Di>> uploadedMixedExtents_;
    std::vector<uint32_t> uploadedMixedCounts_;
    std::vector<uint64_t> uploadedMixedTimes_;
    ComPtr<ID3D11Device> receiveDevice_;
    ComPtr<ID3D11DeviceContext> receiveContext_;
    bool reportedGpuImage_ = false;
    MirrorWindow mirror_;
    ComPtr<ID3D11Device> d3dDevice_;
    ComPtr<ID3D11DeviceContext> d3dContext_;
    GpuCompletion frameCopyCompletion_;
    GpuCompletion batchReceiveCompletion_;
    XrSwapchain projectionSwapchain_ = XR_NULL_HANDLE;
    std::vector<XrSwapchainImageD3D11KHR> projectionImages_;
    std::vector<uint64_t> uploadedAndroidSequenceByImage_;
    std::vector<uint64_t> uploadedGpuSessionByImage_;
    std::vector<axrb::protocol::ImageProjection> uploadedProjectionByImage_;
    std::vector<XrExtent2Di> uploadedExtentByImage_;
    bool reportedStereoProjection_ = false;
    int64_t projectionFormat_ = 0;
    uint32_t projectionArraySize_ = 2;
    uint32_t projectionWidth_ = axrb::protocol::kTransportEyeDimension;
    uint32_t projectionHeight_ = axrb::protocol::kTransportEyeDimension;
    bool debugGraphicsTest_ = [] { const char* value = std::getenv("AXRB_DEBUG_GRAPHICS_TEST"); return value && std::strcmp(value, "1") == 0; }();
    std::vector<uint8_t> splashPixels_;
    std::vector<bool> splashUploaded_;
    uint32_t projectionFrameCounter_ = 0;
    XrSwapchain panelSwapchain_ = XR_NULL_HANDLE;
    uint32_t panelWidth_ = 0, panelHeight_ = 0, panelLayers_ = 0, panelImageIndex_ = 0;
    bool panelAcquired_ = false;
    std::vector<XrSwapchainImageD3D11KHR> panelImages_;
    std::vector<uint64_t> panelSequences_;
    std::vector<uint32_t> sphereFallbackIndices_;
    axrb::host::EquirectRenderer equirectRenderer_;
    axrb::host::QuadRenderer quadRenderer_;
    uint32_t nativeLayerLimit_ = 1;
    std::array<XrView, 2> overflowViews_{};
    bool compose_overflow(ID3D11Texture2D*, const HostImageSnapshot&);
    void trim_composition_resources(uint32_t spheres, bool panels);
    struct EquirectTarget {
        XrSwapchain swapchain = XR_NULL_HANDLE;
        std::vector<XrSwapchainImageD3D11KHR> images;
        std::array<XrCompositionLayerProjectionView, 2> views{};
        XrCompositionLayerProjection layer{};
    };
    std::deque<EquirectTarget> equirectTargets_;
    HANDLE fpsHudEvent_ = nullptr;
    bool fpsHudInitialized_ = false, fpsHudFailed_ = false;
    uint32_t fpsHudMaxLayers_ = 0;
    XrSwapchain fpsHudSwapchain_ = XR_NULL_HANDLE;
    std::vector<XrSwapchainImageD3D11KHR> fpsHudImages_;
    std::vector<uint64_t> fpsHudUploaded_;
    std::vector<uint8_t> fpsHudPixels_;
    uint64_t fpsHudGeneration_ = 1;
    axrb::host::FpsCounter fpsCounter_;

    bool reportedProjectionSubmit_ = false;
    bool reportedAndroidImageSubmit_ = false;
#endif
#if !defined(_WIN32)
    PFN_xrConvertTimespecTimeToTimeKHR convertTimespecTimeToTime_ = nullptr;
#endif
};

} // namespace axrb::host::detail
