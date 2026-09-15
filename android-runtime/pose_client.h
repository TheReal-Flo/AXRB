#pragma once

#include "pose_frame.h"
#include "pose_stream_decoder.h"

#if defined(__ANDROID__)
#include <jni.h>
#endif

namespace axrb::runtime {

class PoseClient {
public:
    const axrb::protocol::PoseFrame& latest_pose_frame();
#if defined(__ANDROID__)
    void set_android_context(JavaVM* vm, jobject context);
#endif

private:
    bool query_pose_broker();
    bool ensure_connected();
    bool ensure_emulator_connected();
    void read_available_frames();
    void close_socket();

#if defined(__ANDROID__)
    JavaVM* java_vm_ = nullptr;
    jobject android_context_ = nullptr;
    uint32_t broker_retry_countdown_ = 0;
#endif
    int socket_ = -1;
    bool connecting_ = false;
    uint64_t next_connect_ns_ = 0;
    axrb::protocol::PoseStreamDecoder decoder_;
    uint32_t retry_countdown_ = 0;
    axrb::protocol::PoseFrame latest_{
        axrb::protocol::kPoseFrameMagic,
        axrb::protocol::kPoseFrameVersion,
        axrb::protocol::kPoseFrameType,
        0,
        0,
        {0.0f, 1.65f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f},
        {-0.25f, 1.25f, -0.45f, 0.0f, 0.0f, 0.0f, 1.0f},
        {0.25f, 1.25f, -0.45f, 0.0f, 0.0f, 0.0f, 1.0f},
    };
};

PoseClient& pose_client();

} // namespace axrb::runtime
