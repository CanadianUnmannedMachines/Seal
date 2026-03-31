package com.junkfood.seal.util

import android.net.LocalServerSocket
import android.net.LocalSocket
import android.net.LocalSocketAddress
import android.util.Log
import com.junkfood.seal.App.Companion.context
import java.io.DataInputStream
import java.io.DataOutputStream
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch

/**
 * Bridges Kotlin and the Deno JS runtime loaded from libdeno-core.so via JNI.
 *
 * Also hosts an abstract Unix-domain socket server so that libdeno-cli.so
 * (spawned by yt-dlp via --js-interpreters) can reach the in-process runtime
 * without any exec() of binaries from the app data directory.
 *
 * libdeno-core.so must be pre-built from the Deno Rust crate for each target
 * ABI and placed under app/src/main/jniLibs/<abi>/libdeno-core.so.
 * See docs/building-deno-android.md for build instructions.
 */
object DenoRuntime {

    private const val TAG = "DenoRuntime"

    /** Abstract socket name shared with deno_cli.cpp. */
    const val SOCKET_NAME = "seal_deno_runtime"

    init {
        System.loadLibrary("deno-jni")
    }

    // -----------------------------------------------------------------------
    // JNI — implemented in deno_jni.cpp, linked against libdeno-core.so
    // -----------------------------------------------------------------------

    @JvmStatic external fun nativeInit(): Boolean

    @JvmStatic external fun nativeEvaluate(script: String): String

    @JvmStatic external fun nativeDestroy()

    // -----------------------------------------------------------------------
    // Public API
    // -----------------------------------------------------------------------

    /** Absolute path to the CLI proxy binary extracted to the native lib dir. */
    val cliPath: String
        get() = "${context.applicationInfo.nativeLibraryDir}/libdeno-cli.so"

    /**
     * Initialize the Deno runtime and start the socket server.
     * Call once at application startup (from a background coroutine scope).
     */
    fun initialize(scope: CoroutineScope) {
        scope.launch(Dispatchers.IO) {
            if (!nativeInit()) {
                Log.e(TAG, "Deno runtime failed to initialize")
                return@launch
            }
            runSocketServer()
        }
    }

    fun destroy() = nativeDestroy()

    // -----------------------------------------------------------------------
    // Internal socket server
    // -----------------------------------------------------------------------

    private fun runSocketServer() {
        try {
            LocalServerSocket(SOCKET_NAME).use { server ->
                Log.i(TAG, "Socket server listening on @$SOCKET_NAME")
                while (true) {
                    val client = server.accept() ?: break
                    handleClient(client)
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Socket server error", e)
        }
    }

    private fun handleClient(socket: LocalSocket) {
        try {
            val inp = DataInputStream(socket.inputStream)
            val out = DataOutputStream(socket.outputStream)

            val scriptLen = inp.readInt()
            val scriptBytes = ByteArray(scriptLen).also { inp.readFully(it) }
            val script = scriptBytes.toString(Charsets.UTF_8)

            val result = nativeEvaluate(script)

            val resultBytes = result.toByteArray(Charsets.UTF_8)
            out.writeInt(resultBytes.size)
            out.write(resultBytes)
            out.flush()
        } catch (e: Exception) {
            Log.e(TAG, "Client error", e)
        } finally {
            runCatching { socket.close() }
        }
    }
}
