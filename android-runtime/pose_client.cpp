#include "pose_client.h"

#include <cstdio>
#include <cstring>
#include <string>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

#if defined(__ANDROID__)
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <jni.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace axrb::runtime {

namespace {

constexpr uint16_t kDefaultBridgePort = 38490;

void log_pose_client(const char* message)
{
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO, "AXRB.PoseClient", "%s", message);
#else
    std::fprintf(stderr, "AXRB.PoseClient: %s\n", message);
#endif
}

bool is_valid_frame(const axrb::protocol::PoseFrame& frame)
{
    return frame.magic == axrb::protocol::kPoseFrameMagic &&
        frame.version == axrb::protocol::kPoseFrameVersion &&
        frame.type == axrb::protocol::kPoseFrameType;
}

#if defined(__ANDROID__)
bool connect_with_timeout(int socket, const sockaddr_in& address, int* out_error)
{
    timeval timeout{};
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;
    if (setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0) {
        *out_error = errno;
        return false;
    }

    const int result = connect(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    if (result == 0) {
        return true;
    }
    *out_error = errno;
    return false;
}

void log_connect_failure(const char* host, int error)
{
    char message[160]{};
    std::snprintf(
        message,
        sizeof(message),
        "connect failed host=%s port=%u errno=%d (%s)",
        host,
        static_cast<unsigned>(kDefaultBridgePort),
        error,
        std::strerror(error));
    log_pose_client(message);
}
#endif

} // namespace

const axrb::protocol::PoseFrame& PoseClient::latest_pose_frame()
{
    if (query_pose_broker()) {
        return latest_;
    }
    if (ensure_connected()) {
        read_available_frames();
    }
    return latest_;
}

#if defined(__ANDROID__)
void PoseClient::set_android_context(JavaVM* vm, jobject context)
{
    if (vm == nullptr || context == nullptr) {
        return;
    }

    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK || env == nullptr) {
        return;
    }

    java_vm_ = vm;
    if (android_context_ != nullptr) {
        env->DeleteGlobalRef(android_context_);
    }
    android_context_ = env->NewGlobalRef(context);
    log_pose_client("captured Android context for pose broker");
}
#endif

bool PoseClient::query_pose_broker()
{
#if !defined(__ANDROID__)
    return false;
#else
    if (java_vm_ == nullptr || android_context_ == nullptr) {
        return false;
    }
    if (broker_retry_countdown_ > 0) {
        --broker_retry_countdown_;
        return false;
    }

    JNIEnv* env = nullptr;
    bool detach = false;
    jint envResult = java_vm_->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (envResult == JNI_EDETACHED) {
        if (java_vm_->AttachCurrentThread(&env, nullptr) != JNI_OK || env == nullptr) {
            broker_retry_countdown_ = 90;
            return false;
        }
        detach = true;
    } else if (envResult != JNI_OK || env == nullptr) {
        broker_retry_countdown_ = 90;
        return false;
    }

    bool ok = false;
    jobject cursor = nullptr;
    jstring uriString = nullptr;
    jobject uri = nullptr;
    jobject resolver = nullptr;

    do {
        jclass contextClass = env->GetObjectClass(android_context_);
        jmethodID getContentResolver = env->GetMethodID(contextClass, "getContentResolver", "()Landroid/content/ContentResolver;");
        resolver = env->CallObjectMethod(android_context_, getContentResolver);
        if (env->ExceptionCheck() || resolver == nullptr) {
            env->ExceptionClear();
            break;
        }

        jclass uriClass = env->FindClass("android/net/Uri");
        jmethodID parse = env->GetStaticMethodID(uriClass, "parse", "(Ljava/lang/String;)Landroid/net/Uri;");
        uriString = env->NewStringUTF("content://org.khronos.openxr.system_runtime_broker/openxr/1/pose/latest");
        uri = env->CallStaticObjectMethod(uriClass, parse, uriString);
        if (env->ExceptionCheck() || uri == nullptr) {
            env->ExceptionClear();
            break;
        }

        jclass resolverClass = env->GetObjectClass(resolver);
        jmethodID query = env->GetMethodID(
            resolverClass,
            "query",
            "(Landroid/net/Uri;[Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;Ljava/lang/String;)Landroid/database/Cursor;");
        cursor = env->CallObjectMethod(resolver, query, uri, nullptr, nullptr, nullptr, nullptr);
        if (env->ExceptionCheck() || cursor == nullptr) {
            env->ExceptionClear();
            break;
        }

        jclass cursorClass = env->GetObjectClass(cursor);
        jmethodID moveToFirst = env->GetMethodID(cursorClass, "moveToFirst", "()Z");
        if (!env->CallBooleanMethod(cursor, moveToFirst)) {
            break;
        }

        jmethodID getLong = env->GetMethodID(cursorClass, "getLong", "(I)J");
        jmethodID getFloat = env->GetMethodID(cursorClass, "getFloat", "(I)F");
        latest_.sequence = static_cast<uint64_t>(env->CallLongMethod(cursor, getLong, 0));
        latest_.monotonic_time_ns = static_cast<uint64_t>(env->CallLongMethod(cursor, getLong, 1));
        latest_.hmd.x = env->CallFloatMethod(cursor, getFloat, 2);
        latest_.hmd.y = env->CallFloatMethod(cursor, getFloat, 3);
        latest_.hmd.z = env->CallFloatMethod(cursor, getFloat, 4);
        latest_.hmd.qx = env->CallFloatMethod(cursor, getFloat, 5);
        latest_.hmd.qy = env->CallFloatMethod(cursor, getFloat, 6);
        latest_.hmd.qz = env->CallFloatMethod(cursor, getFloat, 7);
        latest_.hmd.qw = env->CallFloatMethod(cursor, getFloat, 8);
        latest_.left_controller.x = env->CallFloatMethod(cursor, getFloat, 9);
        latest_.left_controller.y = env->CallFloatMethod(cursor, getFloat, 10);
        latest_.left_controller.z = env->CallFloatMethod(cursor, getFloat, 11);
        latest_.left_controller.qx = env->CallFloatMethod(cursor, getFloat, 12);
        latest_.left_controller.qy = env->CallFloatMethod(cursor, getFloat, 13);
        latest_.left_controller.qz = env->CallFloatMethod(cursor, getFloat, 14);
        latest_.left_controller.qw = env->CallFloatMethod(cursor, getFloat, 15);
        latest_.right_controller.x = env->CallFloatMethod(cursor, getFloat, 16);
        latest_.right_controller.y = env->CallFloatMethod(cursor, getFloat, 17);
        latest_.right_controller.z = env->CallFloatMethod(cursor, getFloat, 18);
        latest_.right_controller.qx = env->CallFloatMethod(cursor, getFloat, 19);
        latest_.right_controller.qy = env->CallFloatMethod(cursor, getFloat, 20);
        latest_.right_controller.qz = env->CallFloatMethod(cursor, getFloat, 21);
        latest_.right_controller.qw = env->CallFloatMethod(cursor, getFloat, 22);
        ok = true;
    } while (false);

    if (cursor != nullptr) {
        jclass cursorClass = env->GetObjectClass(cursor);
        jmethodID close = env->GetMethodID(cursorClass, "close", "()V");
        env->CallVoidMethod(cursor, close);
    }

    if (!ok) {
        broker_retry_countdown_ = 30;
    }

    if (detach) {
        java_vm_->DetachCurrentThread();
    }
    return ok;
#endif
}

bool PoseClient::ensure_connected()
{
#if !defined(__ANDROID__)
    return false;
#else
    if (socket_ >= 0) {
        return true;
    }
    if (retry_countdown_ > 0) {
        --retry_countdown_;
        return false;
    }
    retry_countdown_ = 90;

    constexpr const char* kCandidateHosts[] = {
        "172.26.32.1",
        "127.0.0.1",
        "192.168.240.1",
        "10.0.2.2",
    };

    for (const char* host : kCandidateHosts) {
        int candidate = socket(AF_INET, SOCK_STREAM, 0);
        if (candidate < 0) {
            continue;
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(kDefaultBridgePort);
        if (inet_pton(AF_INET, host, &address.sin_addr) != 1) {
            close(candidate);
            continue;
        }

        int connect_error = 0;
        if (connect_with_timeout(candidate, address, &connect_error)) {
            socket_ = candidate;
            char message[96]{};
            std::snprintf(message, sizeof(message), "connected to host bridge host=%s", host);
            log_pose_client(message);
            return true;
        }

        log_connect_failure(host, connect_error);
        close(candidate);
    }

    log_pose_client("host bridge unavailable; using fallback pose");
    return false;
#endif
}

void PoseClient::read_available_frames()
{
#if !defined(__ANDROID__)
    return;
#else
    axrb::protocol::PoseFrame frame{};
    while (true) {
        char* cursor = reinterpret_cast<char*>(&frame);
        size_t remaining = sizeof(frame);
        while (remaining > 0) {
            const ssize_t received = recv(socket_, cursor, remaining, 0);
            if (received > 0) {
                cursor += received;
                remaining -= static_cast<size_t>(received);
                continue;
            }
            if (received == 0) {
                close_socket();
                log_pose_client("host bridge disconnected");
                return;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            close_socket();
            log_pose_client("host bridge recv failed");
            return;
        }

        if (is_valid_frame(frame)) {
            latest_ = frame;
        }
    }
#endif
}

void PoseClient::close_socket()
{
#if defined(__ANDROID__)
    if (socket_ >= 0) {
        close(socket_);
        socket_ = -1;
    }
#else
    socket_ = -1;
#endif
}

PoseClient& pose_client()
{
    static PoseClient client;
    return client;
}

} // namespace axrb::runtime
