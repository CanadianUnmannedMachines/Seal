// deno_cli — stdin → DenoRuntime abstract socket → stdout
//
// yt-dlp spawns this binary (extracted to the exec-allowed native library
// directory) when --js-interpreters points to it.  It reads the JavaScript
// expression from stdin, forwards it to the in-process DenoRuntime via an
// Android abstract Unix-domain socket, and writes the result to stdout.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <android/log.h>

#define TAG     "denoCli"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

// Must match DenoRuntime.SOCKET_NAME on the Kotlin side.
static constexpr char SOCKET_NAME[] = "seal_deno_runtime";

static int connect_to_runtime() {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { LOGE("socket() failed: %s", strerror(errno)); return -1; }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    // Abstract socket: first byte is '\0', name follows.
    addr.sun_path[0] = '\0';
    strncpy(addr.sun_path + 1, SOCKET_NAME, sizeof(addr.sun_path) - 2);
    socklen_t len = offsetof(struct sockaddr_un, sun_path) + 1 + strlen(SOCKET_NAME);

    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), len) < 0) {
        LOGE("connect() failed: %s", strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

static bool send_all(int fd, const void* buf, size_t n) {
    const auto* p = static_cast<const char*>(buf);
    while (n > 0) {
        ssize_t sent = send(fd, p, n, 0);
        if (sent <= 0) return false;
        p += sent; n -= sent;
    }
    return true;
}

static bool recv_all(int fd, void* buf, size_t n) {
    auto* p = static_cast<char*>(buf);
    while (n > 0) {
        ssize_t got = recv(fd, p, n, 0);
        if (got <= 0) return false;
        p += got; n -= got;
    }
    return true;
}

int main() {
    // Read all of stdin (the JS expression yt-dlp wants evaluated).
    std::string script;
    {
        char buf[4096];
        while (true) {
            ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
            if (n <= 0) break;
            script.append(buf, static_cast<size_t>(n));
        }
    }

    int fd = connect_to_runtime();
    if (fd < 0) {
        fprintf(stderr, "deno-cli: could not connect to DenoRuntime socket\n");
        return 1;
    }

    // Protocol: [uint32_t length][utf-8 bytes] in both directions.
    uint32_t send_len = static_cast<uint32_t>(script.size());
    if (!send_all(fd, &send_len, sizeof(send_len)) ||
        !send_all(fd, script.data(), send_len)) {
        LOGE("send failed");
        close(fd);
        return 1;
    }

    uint32_t recv_len = 0;
    if (!recv_all(fd, &recv_len, sizeof(recv_len))) {
        LOGE("recv length failed");
        close(fd);
        return 1;
    }
    std::string result(recv_len, '\0');
    if (!recv_all(fd, &result[0], recv_len)) {
        LOGE("recv body failed");
        close(fd);
        return 1;
    }
    close(fd);

    fwrite(result.data(), 1, result.size(), stdout);
    fputc('\n', stdout);
    return 0;
}
