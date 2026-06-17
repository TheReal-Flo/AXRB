#include <jni.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <android/log.h>

#include <cstring>
#include <sstream>
#include <string>

namespace {

constexpr const char* kTag = "AXRB.OpenXRLoaderProbe.Native";

struct EglProbeContext {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
};

std::string result_code(const char* operation, XrResult result)
{
    std::ostringstream out;
    out << "FAIL: " << operation << " returned " << static_cast<int>(result);
    return out.str();
}

template <typename Function>
bool load_instance_proc(XrInstance instance, const char* name, Function* function, std::string* error)
{
    PFN_xrVoidFunction raw = nullptr;
    const XrResult result = xrGetInstanceProcAddr(instance, name, &raw);
    if (result != XR_SUCCESS || raw == nullptr) {
        *error = result_code(name, result);
        return false;
    }

    *function = reinterpret_cast<Function>(raw);
    return true;
}

bool create_egl_probe_context(EglProbeContext* egl, std::string* error)
{
    egl->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl->display == EGL_NO_DISPLAY) {
        *error = "FAIL: eglGetDisplay failed";
        return false;
    }
    if (eglInitialize(egl->display, nullptr, nullptr) != EGL_TRUE) {
        *error = "FAIL: eglInitialize failed";
        return false;
    }

    const EGLint configAttributes[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE,
    };
    EGLConfig config = nullptr;
    EGLint configCount = 0;
    if (eglChooseConfig(egl->display, configAttributes, &config, 1, &configCount) != EGL_TRUE || configCount == 0) {
        *error = "FAIL: eglChooseConfig failed";
        return false;
    }

    const EGLint surfaceAttributes[] = {
        EGL_WIDTH, 16,
        EGL_HEIGHT, 16,
        EGL_NONE,
    };
    egl->surface = eglCreatePbufferSurface(egl->display, config, surfaceAttributes);
    if (egl->surface == EGL_NO_SURFACE) {
        *error = "FAIL: eglCreatePbufferSurface failed";
        return false;
    }

    const EGLint contextAttributes[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE,
    };
    egl->context = eglCreateContext(egl->display, config, EGL_NO_CONTEXT, contextAttributes);
    if (egl->context == EGL_NO_CONTEXT) {
        *error = "FAIL: eglCreateContext failed";
        return false;
    }
    if (eglMakeCurrent(egl->display, egl->surface, egl->surface, egl->context) != EGL_TRUE) {
        *error = "FAIL: eglMakeCurrent failed";
        return false;
    }
    return true;
}

void destroy_egl_probe_context(const EglProbeContext& egl)
{
    if (egl.display == EGL_NO_DISPLAY) {
        return;
    }
    eglMakeCurrent(egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (egl.context != EGL_NO_CONTEXT) {
        eglDestroyContext(egl.display, egl.context);
    }
    if (egl.surface != EGL_NO_SURFACE) {
        eglDestroySurface(egl.display, egl.surface);
    }
    eglTerminate(egl.display);
}

std::string run_probe(JNIEnv* env, jobject activity)
{
    JavaVM* vm = nullptr;
    if (env->GetJavaVM(&vm) != JNI_OK || vm == nullptr) {
        return "FAIL: GetJavaVM failed";
    }

    PFN_xrInitializeLoaderKHR xrInitializeLoaderKHR = nullptr;
    XrResult result = xrGetInstanceProcAddr(
        XR_NULL_HANDLE,
        "xrInitializeLoaderKHR",
        reinterpret_cast<PFN_xrVoidFunction*>(&xrInitializeLoaderKHR));
    if (result != XR_SUCCESS || xrInitializeLoaderKHR == nullptr) {
        return result_code("xrGetInstanceProcAddr(xrInitializeLoaderKHR)", result);
    }

    XrLoaderInitInfoAndroidKHR initInfo{};
    initInfo.type = XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR;
    initInfo.applicationVM = vm;
    initInfo.applicationContext = activity;

    result = xrInitializeLoaderKHR(
        reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR*>(&initInfo));
    if (result != XR_SUCCESS) {
        return result_code("xrInitializeLoaderKHR", result);
    }

    XrInstanceCreateInfo createInfo{};
    createInfo.type = XR_TYPE_INSTANCE_CREATE_INFO;
    std::strncpy(
        createInfo.applicationInfo.applicationName,
        "AXRB OpenXR Loader Probe",
        XR_MAX_APPLICATION_NAME_SIZE - 1);
    createInfo.applicationInfo.applicationVersion = 1;
    std::strncpy(
        createInfo.applicationInfo.engineName,
        "AXRB",
        XR_MAX_ENGINE_NAME_SIZE - 1);
    createInfo.applicationInfo.engineVersion = 1;
    createInfo.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);

    XrInstance instance = XR_NULL_HANDLE;
    result = xrCreateInstance(&createInfo, &instance);
    if (result != XR_SUCCESS || instance == XR_NULL_HANDLE) {
        return result_code("xrCreateInstance", result);
    }

    PFN_xrGetSystem xrGetSystem = nullptr;
    PFN_xrGetSystemProperties xrGetSystemProperties = nullptr;
    PFN_xrPollEvent xrPollEvent = nullptr;
    PFN_xrEnumerateViewConfigurations xrEnumerateViewConfigurations = nullptr;
    PFN_xrGetViewConfigurationProperties xrGetViewConfigurationProperties = nullptr;
    PFN_xrEnumerateViewConfigurationViews xrEnumerateViewConfigurationViews = nullptr;
    PFN_xrEnumerateEnvironmentBlendModes xrEnumerateEnvironmentBlendModes = nullptr;
    PFN_xrCreateSession xrCreateSession = nullptr;
    PFN_xrBeginSession xrBeginSession = nullptr;
    PFN_xrEndSession xrEndSession = nullptr;
    PFN_xrDestroySession xrDestroySession = nullptr;
    PFN_xrEnumerateSwapchainFormats xrEnumerateSwapchainFormats = nullptr;
    PFN_xrCreateSwapchain xrCreateSwapchain = nullptr;
    PFN_xrDestroySwapchain xrDestroySwapchain = nullptr;
    PFN_xrEnumerateSwapchainImages xrEnumerateSwapchainImages = nullptr;
    PFN_xrAcquireSwapchainImage xrAcquireSwapchainImage = nullptr;
    PFN_xrWaitSwapchainImage xrWaitSwapchainImage = nullptr;
    PFN_xrReleaseSwapchainImage xrReleaseSwapchainImage = nullptr;
    PFN_xrEnumerateReferenceSpaces xrEnumerateReferenceSpaces = nullptr;
    PFN_xrCreateReferenceSpace xrCreateReferenceSpace = nullptr;
    PFN_xrDestroySpace xrDestroySpace = nullptr;
    PFN_xrLocateViews xrLocateViews = nullptr;
    PFN_xrWaitFrame xrWaitFrame = nullptr;
    PFN_xrBeginFrame xrBeginFrame = nullptr;
    PFN_xrEndFrame xrEndFrame = nullptr;

    std::string error;
    if (!load_instance_proc(instance, "xrGetSystem", &xrGetSystem, &error) ||
        !load_instance_proc(instance, "xrGetSystemProperties", &xrGetSystemProperties, &error) ||
        !load_instance_proc(instance, "xrPollEvent", &xrPollEvent, &error) ||
        !load_instance_proc(instance, "xrEnumerateViewConfigurations", &xrEnumerateViewConfigurations, &error) ||
        !load_instance_proc(instance, "xrGetViewConfigurationProperties", &xrGetViewConfigurationProperties, &error) ||
        !load_instance_proc(instance, "xrEnumerateViewConfigurationViews", &xrEnumerateViewConfigurationViews, &error) ||
        !load_instance_proc(instance, "xrEnumerateEnvironmentBlendModes", &xrEnumerateEnvironmentBlendModes, &error) ||
        !load_instance_proc(instance, "xrCreateSession", &xrCreateSession, &error) ||
        !load_instance_proc(instance, "xrBeginSession", &xrBeginSession, &error) ||
        !load_instance_proc(instance, "xrEndSession", &xrEndSession, &error) ||
        !load_instance_proc(instance, "xrDestroySession", &xrDestroySession, &error) ||
        !load_instance_proc(instance, "xrEnumerateSwapchainFormats", &xrEnumerateSwapchainFormats, &error) ||
        !load_instance_proc(instance, "xrCreateSwapchain", &xrCreateSwapchain, &error) ||
        !load_instance_proc(instance, "xrDestroySwapchain", &xrDestroySwapchain, &error) ||
        !load_instance_proc(instance, "xrEnumerateSwapchainImages", &xrEnumerateSwapchainImages, &error) ||
        !load_instance_proc(instance, "xrAcquireSwapchainImage", &xrAcquireSwapchainImage, &error) ||
        !load_instance_proc(instance, "xrWaitSwapchainImage", &xrWaitSwapchainImage, &error) ||
        !load_instance_proc(instance, "xrReleaseSwapchainImage", &xrReleaseSwapchainImage, &error) ||
        !load_instance_proc(instance, "xrEnumerateReferenceSpaces", &xrEnumerateReferenceSpaces, &error) ||
        !load_instance_proc(instance, "xrCreateReferenceSpace", &xrCreateReferenceSpace, &error) ||
        !load_instance_proc(instance, "xrDestroySpace", &xrDestroySpace, &error) ||
        !load_instance_proc(instance, "xrLocateViews", &xrLocateViews, &error) ||
        !load_instance_proc(instance, "xrWaitFrame", &xrWaitFrame, &error) ||
        !load_instance_proc(instance, "xrBeginFrame", &xrBeginFrame, &error) ||
        !load_instance_proc(instance, "xrEndFrame", &xrEndFrame, &error)) {
        return error;
    }

    XrSystemGetInfo systemGetInfo{};
    systemGetInfo.type = XR_TYPE_SYSTEM_GET_INFO;
    systemGetInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

    XrSystemId systemId = XR_NULL_SYSTEM_ID;
    result = xrGetSystem(instance, &systemGetInfo, &systemId);
    if (result != XR_SUCCESS || systemId == XR_NULL_SYSTEM_ID) {
        return result_code("xrGetSystem", result);
    }

    XrSystemProperties systemProperties{};
    systemProperties.type = XR_TYPE_SYSTEM_PROPERTIES;
    result = xrGetSystemProperties(instance, systemId, &systemProperties);
    if (result != XR_SUCCESS) {
        return result_code("xrGetSystemProperties", result);
    }

    uint32_t viewConfigurationCount = 0;
    result = xrEnumerateViewConfigurations(instance, systemId, 0, &viewConfigurationCount, nullptr);
    if (result != XR_SUCCESS || viewConfigurationCount == 0) {
        return result_code("xrEnumerateViewConfigurations(count)", result);
    }

    XrViewConfigurationType viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    result = xrEnumerateViewConfigurations(
        instance,
        systemId,
        1,
        &viewConfigurationCount,
        &viewConfigurationType);
    if (result != XR_SUCCESS || viewConfigurationType != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) {
        return result_code("xrEnumerateViewConfigurations(values)", result);
    }

    XrViewConfigurationProperties viewConfigurationProperties{};
    viewConfigurationProperties.type = XR_TYPE_VIEW_CONFIGURATION_PROPERTIES;
    result = xrGetViewConfigurationProperties(
        instance,
        systemId,
        viewConfigurationType,
        &viewConfigurationProperties);
    if (result != XR_SUCCESS) {
        return result_code("xrGetViewConfigurationProperties", result);
    }

    uint32_t viewCount = 0;
    result = xrEnumerateViewConfigurationViews(instance, systemId, viewConfigurationType, 0, &viewCount, nullptr);
    if (result != XR_SUCCESS || viewCount != 2) {
        return result_code("xrEnumerateViewConfigurationViews(count)", result);
    }

    XrViewConfigurationView views[2]{};
    views[0].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    views[1].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    result = xrEnumerateViewConfigurationViews(instance, systemId, viewConfigurationType, 2, &viewCount, views);
    if (result != XR_SUCCESS || viewCount != 2) {
        return result_code("xrEnumerateViewConfigurationViews(values)", result);
    }

    uint32_t blendModeCount = 0;
    result = xrEnumerateEnvironmentBlendModes(
        instance,
        systemId,
        viewConfigurationType,
        0,
        &blendModeCount,
        nullptr);
    if (result != XR_SUCCESS || blendModeCount == 0) {
        return result_code("xrEnumerateEnvironmentBlendModes(count)", result);
    }

    XrEnvironmentBlendMode blendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    result = xrEnumerateEnvironmentBlendModes(
        instance,
        systemId,
        viewConfigurationType,
        1,
        &blendModeCount,
        &blendMode);
    if (result != XR_SUCCESS || blendMode != XR_ENVIRONMENT_BLEND_MODE_OPAQUE) {
        return result_code("xrEnumerateEnvironmentBlendModes(values)", result);
    }

    XrSessionCreateInfo sessionCreateInfo{};
    sessionCreateInfo.type = XR_TYPE_SESSION_CREATE_INFO;
    sessionCreateInfo.systemId = systemId;

    XrSession session = XR_NULL_HANDLE;
    result = xrCreateSession(instance, &sessionCreateInfo, &session);
    if (result != XR_SUCCESS || session == XR_NULL_HANDLE) {
        return result_code("xrCreateSession", result);
    }

    bool sawReady = false;
    for (int i = 0; i < 8; ++i) {
        XrEventDataBuffer event{};
        event.type = XR_TYPE_EVENT_DATA_BUFFER;
        result = xrPollEvent(instance, &event);
        if (result == XR_EVENT_UNAVAILABLE) {
            break;
        }
        if (result != XR_SUCCESS) {
            return result_code("xrPollEvent", result);
        }
        if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            const XrEventDataSessionStateChanged* stateChanged =
                reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);
            sawReady = sawReady || stateChanged->state == XR_SESSION_STATE_READY;
        }
    }
    if (!sawReady) {
        return "FAIL: did not receive XR_SESSION_STATE_READY";
    }

    EglProbeContext egl{};
    if (!create_egl_probe_context(&egl, &error)) {
        return error;
    }

    XrSessionBeginInfo beginSessionInfo{};
    beginSessionInfo.type = XR_TYPE_SESSION_BEGIN_INFO;
    beginSessionInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    result = xrBeginSession(session, &beginSessionInfo);
    if (result != XR_SUCCESS) {
        return result_code("xrBeginSession", result);
    }

    uint32_t swapchainFormatCount = 0;
    result = xrEnumerateSwapchainFormats(session, 0, &swapchainFormatCount, nullptr);
    if (result != XR_SUCCESS || swapchainFormatCount == 0) {
        return result_code("xrEnumerateSwapchainFormats(count)", result);
    }

    int64_t swapchainFormat = 0;
    result = xrEnumerateSwapchainFormats(session, 1, &swapchainFormatCount, &swapchainFormat);
    if (result != XR_SUCCESS || swapchainFormat == 0) {
        return result_code("xrEnumerateSwapchainFormats(values)", result);
    }

    XrSwapchainCreateInfo swapchainCreateInfo{};
    swapchainCreateInfo.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
    swapchainCreateInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    swapchainCreateInfo.format = swapchainFormat;
    swapchainCreateInfo.sampleCount = 1;
    swapchainCreateInfo.width = 1024;
    swapchainCreateInfo.height = 1024;
    swapchainCreateInfo.faceCount = 1;
    swapchainCreateInfo.arraySize = 2;
    swapchainCreateInfo.mipCount = 1;

    XrSwapchain swapchain = XR_NULL_HANDLE;
    result = xrCreateSwapchain(session, &swapchainCreateInfo, &swapchain);
    if (result != XR_SUCCESS || swapchain == XR_NULL_HANDLE) {
        return result_code("xrCreateSwapchain", result);
    }

    uint32_t swapchainImageCount = 0;
    result = xrEnumerateSwapchainImages(swapchain, 0, &swapchainImageCount, nullptr);
    if (result != XR_SUCCESS || swapchainImageCount != 3) {
        return result_code("xrEnumerateSwapchainImages(count)", result);
    }

    XrSwapchainImageOpenGLESKHR swapchainImages[3]{};
    for (XrSwapchainImageOpenGLESKHR& image : swapchainImages) {
        image.type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
    }
    result = xrEnumerateSwapchainImages(
        swapchain,
        3,
        &swapchainImageCount,
        reinterpret_cast<XrSwapchainImageBaseHeader*>(swapchainImages));
    if (result != XR_SUCCESS || swapchainImageCount != 3) {
        return result_code("xrEnumerateSwapchainImages(values)", result);
    }
    if (swapchainImages[0].image == 0 || swapchainImages[1].image == 0 || swapchainImages[2].image == 0) {
        return "FAIL: OpenGL ES swapchain image texture was zero";
    }

    XrSwapchainImageAcquireInfo acquireInfo{};
    acquireInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;
    uint32_t swapchainImageIndex = 0;
    result = xrAcquireSwapchainImage(swapchain, &acquireInfo, &swapchainImageIndex);
    if (result != XR_SUCCESS || swapchainImageIndex >= 3) {
        return result_code("xrAcquireSwapchainImage", result);
    }

    XrSwapchainImageWaitInfo waitSwapchainInfo{};
    waitSwapchainInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
    waitSwapchainInfo.timeout = 0;
    result = xrWaitSwapchainImage(swapchain, &waitSwapchainInfo);
    if (result != XR_SUCCESS) {
        return result_code("xrWaitSwapchainImage", result);
    }

    XrSwapchainImageReleaseInfo releaseInfo{};
    releaseInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
    result = xrReleaseSwapchainImage(swapchain, &releaseInfo);
    if (result != XR_SUCCESS) {
        return result_code("xrReleaseSwapchainImage", result);
    }

    uint32_t referenceSpaceCount = 0;
    result = xrEnumerateReferenceSpaces(session, 0, &referenceSpaceCount, nullptr);
    if (result != XR_SUCCESS || referenceSpaceCount == 0) {
        return result_code("xrEnumerateReferenceSpaces(count)", result);
    }

    XrReferenceSpaceType referenceSpace = XR_REFERENCE_SPACE_TYPE_LOCAL;
    result = xrEnumerateReferenceSpaces(session, 1, &referenceSpaceCount, &referenceSpace);
    if (result != XR_SUCCESS || referenceSpace != XR_REFERENCE_SPACE_TYPE_VIEW) {
        return result_code("xrEnumerateReferenceSpaces(values)", result);
    }

    XrReferenceSpaceCreateInfo spaceCreateInfo{};
    spaceCreateInfo.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
    spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    spaceCreateInfo.poseInReferenceSpace.orientation.w = 1.0f;

    XrSpace space = XR_NULL_HANDLE;
    result = xrCreateReferenceSpace(session, &spaceCreateInfo, &space);
    if (result != XR_SUCCESS || space == XR_NULL_HANDLE) {
        return result_code("xrCreateReferenceSpace", result);
    }

    XrFrameWaitInfo waitInfo{};
    waitInfo.type = XR_TYPE_FRAME_WAIT_INFO;
    XrFrameState frameState{};
    frameState.type = XR_TYPE_FRAME_STATE;
    result = xrWaitFrame(session, &waitInfo, &frameState);
    if (result != XR_SUCCESS || frameState.shouldRender == XR_FALSE) {
        return result_code("xrWaitFrame", result);
    }

    XrViewLocateInfo locateInfo{};
    locateInfo.type = XR_TYPE_VIEW_LOCATE_INFO;
    locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locateInfo.displayTime = frameState.predictedDisplayTime;
    locateInfo.space = space;

    XrViewState viewState{};
    viewState.type = XR_TYPE_VIEW_STATE;
    XrView locatedViews[2]{};
    locatedViews[0].type = XR_TYPE_VIEW;
    locatedViews[1].type = XR_TYPE_VIEW;
    uint32_t locatedViewCount = 0;
    result = xrLocateViews(session, &locateInfo, &viewState, 2, &locatedViewCount, locatedViews);
    if (result != XR_SUCCESS || locatedViewCount != 2 ||
        (viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) == 0) {
        return result_code("xrLocateViews", result);
    }

    XrFrameBeginInfo beginInfo{};
    beginInfo.type = XR_TYPE_FRAME_BEGIN_INFO;
    result = xrBeginFrame(session, &beginInfo);
    if (result != XR_SUCCESS) {
        return result_code("xrBeginFrame", result);
    }

    XrFrameEndInfo endInfo{};
    endInfo.type = XR_TYPE_FRAME_END_INFO;
    endInfo.displayTime = frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    result = xrEndFrame(session, &endInfo);
    if (result != XR_SUCCESS) {
        return result_code("xrEndFrame", result);
    }

    result = xrEndSession(session);
    if (result != XR_SUCCESS) {
        return result_code("xrEndSession", result);
    }

    result = xrDestroySwapchain(swapchain);
    if (result != XR_SUCCESS) {
        return result_code("xrDestroySwapchain", result);
    }
    destroy_egl_probe_context(egl);

    xrDestroySpace(space);
    xrDestroySession(session);

    PFN_xrDestroyInstance xrDestroyInstance = nullptr;
    result = xrGetInstanceProcAddr(
        instance,
        "xrDestroyInstance",
        reinterpret_cast<PFN_xrVoidFunction*>(&xrDestroyInstance));
    if (result == XR_SUCCESS && xrDestroyInstance != nullptr) {
        xrDestroyInstance(instance);
    }

    std::ostringstream success;
    success << "PASS: OpenXR loader lifecycle probe succeeded"
        << "\nviews=" << locatedViewCount
        << " left_x=" << locatedViews[0].pose.position.x
        << " right_x=" << locatedViews[1].pose.position.x
        << " y=" << locatedViews[0].pose.position.y
        << " z=" << locatedViews[0].pose.position.z
        << "\nswapchainImages=" << swapchainImageCount
        << " acquired=" << swapchainImageIndex
        << " glTex0=" << swapchainImages[0].image;
    return success.str();
}

} // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_axrb_openxrloaderprobe_MainActivity_runProbe(JNIEnv* env, jclass, jobject activity)
{
    const std::string result = run_probe(env, activity);
    __android_log_print(ANDROID_LOG_INFO, kTag, "%s", result.c_str());
    return env->NewStringUTF(result.c_str());
}
