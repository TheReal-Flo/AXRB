#pragma once

#include <cstdint>

struct XrNegotiateLoaderInfo;
struct XrNegotiateRuntimeRequest;
enum XrResult : int32_t;

namespace axrb::runtime {

int session_probe();
XrResult negotiate_loader_runtime_interface(
    const XrNegotiateLoaderInfo* loaderInfo,
    XrNegotiateRuntimeRequest* runtimeRequest);

} // namespace axrb::runtime
