#include "android_surface.h"
#include "pose_client.h"
#include "perf_stats.h"
#if defined(__ANDROID__)
#include <GLES2/gl2ext.h>
#include <android/log.h>
#include <initializer_list>
namespace axrb::runtime {
namespace {
struct Env {
    JavaVM *vm;
    JNIEnv *env = nullptr;
    bool attached = false;
    explicit Env(JavaVM *v) : vm(v) {
        if (vm && vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) == JNI_EDETACHED)
            attached = vm->AttachCurrentThread(&env, nullptr) == JNI_OK;
    }
    ~Env() {
        if (attached)
            vm->DetachCurrentThread();
    }
    bool clean() {
        if (!env)
            return false;
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            return false;
        }
        return true;
    }
};
struct Current {
    EGLDisplay display = eglGetCurrentDisplay();
    EGLContext context = eglGetCurrentContext();
    EGLSurface draw = eglGetCurrentSurface(EGL_DRAW), read = eglGetCurrentSurface(EGL_READ);
    EGLDisplay target;
    explicit Current(EGLDisplay d) : target(d) {}
    ~Current() {
        if (display != EGL_NO_DISPLAY)
            eglMakeCurrent(display, draw, read, context);
        else
            eglMakeCurrent(target, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
};
GLuint compile(GLenum type, const char *source) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &source, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024]{};
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        __android_log_print(ANDROID_LOG_ERROR, "AXRB.Surface", "shader: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}
} // namespace
bool AndroidSurface::initialize(PoseClient& client, uint32_t width, uint32_t height) {
    JavaVM* vm = client.java_vm();
    vm_ = vm;
    width_ = width;
    height_ = height;
    Env e(vm);
    if (!e.env)
        return false;
    display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(display_, nullptr, nullptr))
        return false;
    EGLint attributes[] = {EGL_SURFACE_TYPE,
                           EGL_PBUFFER_BIT,
                           EGL_RENDERABLE_TYPE,
                           EGL_OPENGL_ES3_BIT,
                           EGL_RED_SIZE,
                           8,
                           EGL_GREEN_SIZE,
                           8,
                           EGL_BLUE_SIZE,
                           8,
                           EGL_ALPHA_SIZE,
                           8,
                           EGL_NONE};
    EGLConfig config;
    EGLint count = 0;
    if (!eglChooseConfig(display_, attributes, &config, 1, &count) || !count)
        return false;
    EGLint ctx[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE}, pb[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    context_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, ctx);
    pbuffer_ = eglCreatePbufferSurface(display_, config, pb);
    Current current(display_);
    if (!eglMakeCurrent(display_, pbuffer_, pbuffer_, context_))
        return false;
    glGenTextures(1, &external_);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, external_);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    jclass tc = e.env->FindClass("android/graphics/SurfaceTexture");
    if (!e.clean() || !tc)
        return false;
    auto constructor = e.env->GetMethodID(tc, "<init>", "(I)V");
    auto size = e.env->GetMethodID(tc, "setDefaultBufferSize", "(II)V");
    update_ = e.env->GetMethodID(tc, "updateTexImage", "()V");
    matrix_ = e.env->GetMethodID(tc, "getTransformMatrix", "([F)V");
    if (!e.clean() || !constructor || !size || !update_ || !matrix_) {
        e.env->DeleteLocalRef(tc);
        return false;
    }
    jobject local = e.env->NewObject(tc, constructor, static_cast<jint>(external_));
    if (!e.clean() || !local) {
        e.env->DeleteLocalRef(tc);
        return false;
    }
    texture_ = e.env->NewGlobalRef(local);
    e.env->CallVoidMethod(texture_, size, static_cast<jint>(width), static_cast<jint>(height));
    e.env->DeleteLocalRef(local);
    e.env->DeleteLocalRef(tc);
    if (!e.clean())
        return false;
    // Load the helper from the installed runtime APK, not the game's class path.
    if (e.env->PushLocalFrame(16) < 0) return false;
    jobject context = client.android_context(e.env);
    if (!context) { e.env->PopLocalFrame(nullptr); return false; }
    jclass contextClass = e.env->FindClass("android/content/Context");
    auto packageContext = e.env->GetMethodID(contextClass, "createPackageContext", "(Ljava/lang/String;I)Landroid/content/Context;");
    auto getLoader = e.env->GetMethodID(contextClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
    if (!e.clean() || !packageContext || !getLoader) { e.env->PopLocalFrame(nullptr); return false; }
    jobject runtimeContext = e.env->CallObjectMethod(context, packageContext,
        e.env->NewStringUTF("com.axrb.openxrruntime"), 3);
    if (!e.clean() || !runtimeContext) { e.env->PopLocalFrame(nullptr); return false; }
    jobject loader = e.env->CallObjectMethod(runtimeContext, getLoader);
    if (!e.clean() || !loader) { e.env->PopLocalFrame(nullptr); return false; }
    jclass loaderClass = e.env->FindClass("java/lang/ClassLoader");
    auto loadClass = e.env->GetMethodID(loaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    auto signalClass = static_cast<jclass>(e.env->CallObjectMethod(loader, loadClass,
        e.env->NewStringUTF("com.axrb.openxrruntime.SurfaceFrameSignal")));
    if (!e.clean() || !signalClass) { e.env->PopLocalFrame(nullptr); return false; }
    auto signalConstructor = e.env->GetMethodID(signalClass, "<init>", "(Landroid/graphics/SurfaceTexture;)V");
    consume_ = e.env->GetMethodID(signalClass, "consume", "()Z");
    if (!e.clean() || !signalConstructor || !consume_) { e.env->PopLocalFrame(nullptr); return false; }
    local = e.env->NewObject(signalClass, signalConstructor, texture_);
    if (e.clean() && local) signal_ = e.env->NewGlobalRef(local);
    e.env->PopLocalFrame(nullptr);
    if (!signal_) return false;
    jclass sc = e.env->FindClass("android/view/Surface");
    if (!e.clean() || !sc)
        return false;
    constructor = e.env->GetMethodID(sc, "<init>", "(Landroid/graphics/SurfaceTexture;)V");
    if (!e.clean() || !constructor) {
        e.env->DeleteLocalRef(sc);
        return false;
    }
    local = e.env->NewObject(sc, constructor, texture_);
    if (!e.clean() || !local) {
        e.env->DeleteLocalRef(sc);
        return false;
    }
    surface_ = e.env->NewGlobalRef(local);
    e.env->DeleteLocalRef(local);
    e.env->DeleteLocalRef(sc);
    local = e.env->NewFloatArray(16);
    transform_ = static_cast<jfloatArray>(e.env->NewGlobalRef(local));
    e.env->DeleteLocalRef(local);
    AHardwareBuffer_Desc desc{};
    desc.width = width;
    desc.height = height;
    desc.layers = 1;
    desc.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
    desc.usage = AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER | AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;
    if (AHardwareBuffer_allocate(&desc, &buffer_) != 0)
        return false;
    auto native = reinterpret_cast<PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC>(
        eglGetProcAddress("eglGetNativeClientBufferANDROID"));
    auto create = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(eglGetProcAddress("eglCreateImageKHR"));
    auto bind = reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
        eglGetProcAddress("glEGLImageTargetTexture2DOES"));
    if (!native || !create || !bind)
        return false;
    image_ = create(display_, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, native(buffer_), nullptr);
    if (image_ == EGL_NO_IMAGE_KHR)
        return false;
    glGenTextures(1, &output_);
    glBindTexture(GL_TEXTURE_2D, output_);
    bind(GL_TEXTURE_2D, image_);
    glGenFramebuffers(1, &framebuffer_);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, output_, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return false;
    const char *vs =
        "#version 300 es\nprecision highp float;out vec2 uv;uniform mat4 transform;void main(){vec2 "
        "p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0,1);uv=(transform*vec4(p.x,1.-p."
        "y,0,1)).xy;}";
    const char *fs =
        "#version 300 es\n#extension GL_OES_EGL_image_external_essl3 : require\nprecision highp float;in "
        "vec2 uv;uniform samplerExternalOES video;out vec4 color;void main(){color=texture(video,uv);}";
    GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) {
        if (v)
            glDeleteShader(v);
        if (f)
            glDeleteShader(f);
        return false;
    }
    program_ = glCreateProgram();
    glAttachShader(program_, v);
    glAttachShader(program_, f);
    glLinkProgram(program_);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint linked = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &linked);
    if (!linked)
        return false;
    glViewport(0, 0, width, height);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
    __android_log_print(ANDROID_LOG_INFO, "AXRB.Surface", "GPU SurfaceTexture %ux%u ready", width, height);
    return e.clean() && glGetError() == GL_NO_ERROR;
}
bool AndroidSurface::update() {
    Env e(vm_);
    if (!e.env)
        return false;
    const bool available = e.env->CallBooleanMethod(signal_, consume_);
    if (!e.clean()) return false;
    if (!available) return true;
    static axrb::protocol::PerfStats drawStats("surface-draw");
    axrb::protocol::PerfScope drawScope(drawStats);
    Current current(display_);
    if (!eglMakeCurrent(display_, pbuffer_, pbuffer_, context_))
        return false;
    e.env->CallVoidMethod(texture_, update_);
    if (!e.clean())
        return false;
    e.env->CallVoidMethod(texture_, matrix_, transform_);
    if (!e.clean())
        return false;
    jfloat values[16];
    e.env->GetFloatArrayRegion(transform_, 0, 16, values);
    if (!e.clean())
        return false;
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glViewport(0, 0, width_, height_);
    glUseProgram(program_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, external_);
    glUniform1i(glGetUniformLocation(program_, "video"), 0);
    glUniformMatrix4fv(glGetUniformLocation(program_, "transform"), 1, GL_FALSE, values);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glFinish();
    if (glGetError() != GL_NO_ERROR) return false;
    dirty_ = true;
    return true;
}
AndroidSurface::~AndroidSurface() {
    Env e(vm_);
    if (e.env) {
        if (signal_) e.env->DeleteGlobalRef(signal_);
        for (jobject obj : {surface_, texture_})
            if (obj) {
                jclass cls = e.env->GetObjectClass(obj);
                auto release = e.env->GetMethodID(cls, "release", "()V");
                if (release)
                    e.env->CallVoidMethod(obj, release);
                e.clean();
                e.env->DeleteLocalRef(cls);
                e.env->DeleteGlobalRef(obj);
            }
        if (transform_)
            e.env->DeleteGlobalRef(transform_);
    }
    if (display_ != EGL_NO_DISPLAY) {
        {
            Current current(display_);
            if (context_ != EGL_NO_CONTEXT && pbuffer_ != EGL_NO_SURFACE &&
                eglMakeCurrent(display_, pbuffer_, pbuffer_, context_)) {
                if (program_)
                    glDeleteProgram(program_);
                if (framebuffer_)
                    glDeleteFramebuffers(1, &framebuffer_);
                if (output_)
                    glDeleteTextures(1, &output_);
                if (external_)
                    glDeleteTextures(1, &external_);
            }
        }
        if (image_ != EGL_NO_IMAGE_KHR) {
            auto destroy =
                reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(eglGetProcAddress("eglDestroyImageKHR"));
            if (destroy)
                destroy(display_, image_);
        }
        if (pbuffer_ != EGL_NO_SURFACE)
            eglDestroySurface(display_, pbuffer_);
        if (context_ != EGL_NO_CONTEXT)
            eglDestroyContext(display_, context_);
    }
    if (buffer_)
        AHardwareBuffer_release(buffer_);
}
} // namespace axrb::runtime
#endif
