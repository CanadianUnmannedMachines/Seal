// quickjs_cli — standalone QuickJS-NG evaluator for yt-dlp
//
// yt-dlp spawns this binary via --js-runtimes quickjs:/path/to/libquickjs-cli.so
// It behaves like the standard `qjs` CLI so yt-dlp can invoke it normally.
//
// Supported usage:
//   libquickjs-cli.so [options] [file]
//   -e CODE        evaluate inline JavaScript
//   --std          enable std/os modules (accepted but ignored — always on)
//   file           evaluate a script file
//   (no args)      read from stdin

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <android/log.h>

extern "C" {
#include "quickjs.h"
#include "quickjs-libc.h"
}

#define TAG "quickjsCli"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

static std::string read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        LOGE("cannot open file: %s", path);
        fprintf(stderr, "quickjs-cli: cannot open %s\n", path);
        exit(1);
    }
    std::string out;
    char buf[4096];
    while (size_t n = fread(buf, 1, sizeof(buf), f)) out.append(buf, n);
    fclose(f);
    LOGD("read file %s (%zu bytes)", path, out.size());
    return out;
}

static std::string read_stdin() {
    std::string out;
    char buf[4096];
    while (true) {
        ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
        if (n <= 0) break;
        out.append(buf, static_cast<size_t>(n));
    }
    LOGD("read stdin (%zu bytes)", out.size());
    return out;
}

int main(int argc, char** argv) {
    LOGI("started, argc=%d", argc);
    for (int i = 0; i < argc; i++) LOGD("  argv[%d]=%s", i, argv[i]);

    std::string script;
    const char* filename = "<input>";

    // Parse arguments like qjs: [-e code] [--std] [file]
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-e") == 0 || strcmp(argv[i], "--eval") == 0) {
            if (++i >= argc) {
                LOGE("-e requires an argument");
                fprintf(stderr, "quickjs-cli: -e requires an argument\n");
                return 1;
            }
            script = argv[i];
            filename = "<cmdline>";
            LOGD("-e script (%zu bytes)", script.size());
        } else if (strcmp(argv[i], "--std") == 0 || strcmp(argv[i], "-m") == 0) {
            // accepted, ignored
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            fprintf(stdout, "QuickJS-ng version 0.1.0\n"
                            "  -e CODE    evaluate inline JavaScript\n"
                            "  --std      enable std/os modules\n"
                            "  file       evaluate a script file\n"
                            "  (no args)  read from stdin\n");
            return 0;
        } else if (argv[i][0] != '-') {
            script = read_file(argv[i]);
            filename = argv[i];
        }
    }

    if (script.empty() && strcmp(filename, "<cmdline>") != 0) {
        script = read_stdin();
    }

    if (script.empty()) {
        LOGE("no script provided");
        fprintf(stderr, "quickjs-cli: no script provided\n");
        return 1;
    }

    LOGI("evaluating %s (%zu bytes)", filename, script.size());

    JSRuntime* rt = JS_NewRuntime();
    if (!rt) { LOGE("JS_NewRuntime failed"); fprintf(stderr, "quickjs-cli: JS_NewRuntime failed\n"); return 1; }

    JSContext* ctx = JS_NewContext(rt);
    if (!ctx) { LOGE("JS_NewContext failed"); JS_FreeRuntime(rt); fprintf(stderr, "quickjs-cli: JS_NewContext failed\n"); return 1; }

    js_std_add_helpers(ctx, 0, NULL);

    JSValue val = JS_Eval(ctx, script.c_str(), script.size(), filename, JS_EVAL_TYPE_GLOBAL);

    int ret = 0;
    if (JS_IsException(val)) {
        JSValue exc = JS_GetException(ctx);
        const char* str = JS_ToCString(ctx, exc);
        LOGE("exception: %s", str ? str : "unknown");
        fprintf(stderr, "quickjs-cli: %s\n", str ? str : "unknown error");
        if (str) JS_FreeCString(ctx, str);
        JS_FreeValue(ctx, exc);
        ret = 1;
    } else if (!JS_IsUndefined(val)) {
        const char* str = JS_ToCString(ctx, val);
        if (str) {
            LOGI("result: %.200s%s", str, strlen(str) > 200 ? "..." : "");
            fputs(str, stdout);
            fputc('\n', stdout);
            JS_FreeCString(ctx, str);
        }
    } else {
        LOGD("result: undefined");
    }
    JS_FreeValue(ctx, val);

    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    LOGI("exit %d", ret);
    return ret;
}
