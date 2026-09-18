#include "openxr_dispatch/openxr_minimal.h"

#include <android/log.h>
#include <dlfcn.h>
#include <jni.h>

#include <sstream>
#include <string>

namespace {

constexpr const char* kTag = "AXRB.LoaderProbe.Native";

std::string run_probe()
{
    void* runtime = dlopen("libopenxr_runtime.so", RTLD_NOW | RTLD_LOCAL);
    if (runtime == nullptr) {
        std::ostringstream out;
        out << "FAIL: dlopen(libopenxr_runtime.so): " << dlerror();
        return out.str();
    }

    auto negotiate = reinterpret_cast<PFN_xrNegotiateLoaderRuntimeInterface>(
        dlsym(runtime, "xrNegotiateLoaderRuntimeInterface"));
    if (negotiate == nullptr) {
        std::ostringstream out;
        out << "FAIL: dlsym(xrNegotiateLoaderRuntimeInterface): " << dlerror();
        dlclose(runtime);
        return out.str();
    }

    XrNegotiateLoaderInfo loaderInfo{};
    loaderInfo.structType = XR_LOADER_INTERFACE_STRUCT_LOADER_INFO;
    loaderInfo.structVersion = XR_LOADER_INFO_STRUCT_VERSION;
    loaderInfo.structSize = sizeof(XrNegotiateLoaderInfo);
    loaderInfo.minInterfaceVersion = XR_CURRENT_LOADER_RUNTIME_VERSION;
    loaderInfo.maxInterfaceVersion = XR_CURRENT_LOADER_RUNTIME_VERSION;
    loaderInfo.minApiVersion = XR_MAKE_VERSION(1, 0, 0);
    loaderInfo.maxApiVersion = XR_MAKE_VERSION(1, 0, 0);

    XrNegotiateRuntimeRequest runtimeRequest{};
    const XrResult result = negotiate(&loaderInfo, &runtimeRequest);
    if (result != XR_SUCCESS) {
        std::ostringstream out;
        out << "FAIL: negotiation returned " << result;
        dlclose(runtime);
        return out.str();
    }
    if (runtimeRequest.getInstanceProcAddr == nullptr) {
        dlclose(runtime);
        return "FAIL: negotiation returned null xrGetInstanceProcAddr";
    }

    PFN_xrVoidFunction createInstance = nullptr;
    const XrResult dispatchResult =
        runtimeRequest.getInstanceProcAddr(nullptr, "xrCreateInstance", &createInstance);
    if (dispatchResult != XR_SUCCESS || createInstance == nullptr) {
        std::ostringstream out;
        out << "FAIL: xrGetInstanceProcAddr(xrCreateInstance) returned " << dispatchResult;
        dlclose(runtime);
        return out.str();
    }

    dlclose(runtime);
    return "PASS: runtime negotiation succeeded";
}

} // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_axrb_loaderprobe_MainActivity_runProbe(JNIEnv* env, jclass)
{
    const std::string result = run_probe();
    __android_log_print(ANDROID_LOG_INFO, kTag, "%s", result.c_str());
    return env->NewStringUTF(result.c_str());
}
