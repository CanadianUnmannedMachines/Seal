#include <jni.h>
#include <android/log.h>
#include <string>

#define TAG "DenoJNI"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)

// Symbols exported by libdeno-core.so (the Deno runtime built as a Rust cdylib).
// The Rust-side signatures (in deno-android-jni crate) are:
//
//   #[no_mangle] pub extern "C" fn deno_runtime_new() -> *mut DenoRuntime
//   #[no_mangle] pub extern "C" fn deno_runtime_eval(rt: *mut DenoRuntime,
//                                                     src: *const c_char) -> *mut c_char
//   #[no_mangle] pub extern "C" fn deno_runtime_free(rt: *mut DenoRuntime)
//   #[no_mangle] pub extern "C" fn deno_string_free(s: *mut c_char)
extern "C" {
    void* deno_runtime_new();
    char* deno_runtime_eval(void* rt, const char* source);
    void  deno_runtime_free(void* rt);
    void  deno_string_free(char* s);
}

static void* g_runtime = nullptr;

extern "C" JNIEXPORT jboolean JNICALL
Java_com_junkfood_seal_util_DenoRuntime_nativeInit(JNIEnv* /*env*/, jclass /*cls*/) {
    if (g_runtime) return JNI_TRUE;
    g_runtime = deno_runtime_new();
    if (!g_runtime) {
        LOGE("deno_runtime_new() returned null");
        return JNI_FALSE;
    }
    LOGI("Deno runtime initialized");
    return JNI_TRUE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_junkfood_seal_util_DenoRuntime_nativeEvaluate(
        JNIEnv* env, jclass /*cls*/, jstring script) {
    if (!g_runtime) {
        return env->NewStringUTF("{\"error\":\"runtime not initialized\"}");
    }
    const char* src = env->GetStringUTFChars(script, nullptr);
    char* result = deno_runtime_eval(g_runtime, src);
    env->ReleaseStringUTFChars(script, src);

    jstring jresult = env->NewStringUTF(result ? result : "");
    if (result) deno_string_free(result);
    return jresult;
}

extern "C" JNIEXPORT void JNICALL
Java_com_junkfood_seal_util_DenoRuntime_nativeDestroy(JNIEnv* /*env*/, jclass /*cls*/) {
    if (g_runtime) {
        deno_runtime_free(g_runtime);
        g_runtime = nullptr;
        LOGI("Deno runtime destroyed");
    }
}
