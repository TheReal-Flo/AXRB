#include "android_surface.h"
#include "pose_client.h"
#include <memory>
static axrb::runtime::PoseClient client;
static std::unique_ptr<axrb::runtime::AndroidSurface> surface;
extern "C" JNIEXPORT jobject JNICALL Java_SurfaceSmoke_create(JNIEnv* env, jclass, jobject context) {
    JavaVM* vm = nullptr; env->GetJavaVM(&vm);
    client.set_android_context(vm, context);
    surface = std::make_unique<axrb::runtime::AndroidSurface>();
    if (!surface->initialize(client, 64, 64)) { surface.reset(); return nullptr; }
    return env->NewLocalRef(surface->java_surface());
}
extern "C" JNIEXPORT jint JNICALL Java_SurfaceSmoke_update(JNIEnv*, jclass) {
    if (!surface || !surface->update()) return -1;
    const bool dirty = surface->needs_copy();
    surface->copied();
    return dirty ? 1 : 0;
}
extern "C" JNIEXPORT void JNICALL Java_SurfaceSmoke_destroy(JNIEnv*, jclass) { surface.reset(); }
