#pragma once
#if defined(__ANDROID__)
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <android/hardware_buffer.h>
#include <jni.h>
namespace axrb::runtime {
class PoseClient;
// A real Android Surface producer, consumed on the GPU into an importable RGBA buffer.
class AndroidSurface {
  public:
    ~AndroidSurface();
    bool initialize(PoseClient& client, uint32_t width, uint32_t height);
    bool update();
    bool needs_copy() const { return dirty_; }
    void copied() { dirty_ = false; }
    jobject java_surface() const {
        return surface_;
    }
    AHardwareBuffer *buffer() const {
        return buffer_;
    }

  private:
    JavaVM *vm_ = nullptr;
    jobject surface_ = nullptr, texture_ = nullptr, signal_ = nullptr;
    jmethodID consume_ = nullptr;
    bool dirty_ = true;
    jmethodID update_ = nullptr, matrix_ = nullptr;
    jfloatArray transform_ = nullptr;
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLSurface pbuffer_ = EGL_NO_SURFACE;
    EGLImageKHR image_ = EGL_NO_IMAGE_KHR;
    AHardwareBuffer *buffer_ = nullptr;
    GLuint external_ = 0, output_ = 0, framebuffer_ = 0, program_ = 0;
    uint32_t width_ = 0, height_ = 0;
};
} // namespace axrb::runtime
#endif
