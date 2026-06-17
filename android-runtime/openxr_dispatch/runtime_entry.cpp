#include "openxr_minimal.h"
#include "session.h"

AXRB_XR_EXPORT int axrb_runtime_probe()
{
    return axrb::runtime::session_probe();
}

AXRB_XR_EXPORT XrResult XRAPI_CALL xrNegotiateLoaderRuntimeInterface(
    const XrNegotiateLoaderInfo* loaderInfo,
    XrNegotiateRuntimeRequest* runtimeRequest)
{
    return axrb::runtime::negotiate_loader_runtime_interface(loaderInfo, runtimeRequest);
}
