cmake_minimum_required(VERSION 3.22.1)
project("seal-deno")

# ---------------------------------------------------------------------------
# ABI → Rust target triple
# ---------------------------------------------------------------------------
if("${ANDROID_ABI}" STREQUAL "armeabi-v7a")
    set(RUST_TARGET "armv7-linux-androideabi")
elseif("${ANDROID_ABI}" STREQUAL "arm64-v8a")
    set(RUST_TARGET "aarch64-linux-android")
elseif("${ANDROID_ABI}" STREQUAL "x86")
    set(RUST_TARGET "i686-linux-android")
elseif("${ANDROID_ABI}" STREQUAL "x86_64")
    set(RUST_TARGET "x86_64-linux-android")
else()
    message(FATAL_ERROR "Unsupported Android ABI: ${ANDROID_ABI}")
endif()

# ---------------------------------------------------------------------------
# Build libseal_deno.so from the deno-android-jni Rust crate via cargo ndk.
#
# cargo ndk reads ANDROID_NDK_HOME / ANDROID_SDK_ROOT from the environment
# (both are exported by the Android Gradle plugin when it invokes CMake).
#
# CMAKE_SOURCE_DIR = <repo>/app/src/main/cpp
# SEAL_ROOT        = <repo>
# ---------------------------------------------------------------------------
set(SEAL_ROOT "${CMAKE_SOURCE_DIR}/../../../..")
set(SEAL_DENO_CARGO_OUT
    "${SEAL_ROOT}/deno-android-jni/target/${RUST_TARGET}/release/libseal_deno.so")
set(SEAL_DENO_DEST "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/libseal_deno.so")

add_custom_command(
    OUTPUT "${SEAL_DENO_CARGO_OUT}"
    COMMAND cargo ndk
            --target  "${RUST_TARGET}"
            --platform 24
            --
            build --release -p deno-android-jni
    WORKING_DIRECTORY "${SEAL_ROOT}"
    COMMENT "cargo ndk: building libseal_deno.so for ${ANDROID_ABI} (this takes a while)"
    VERBATIM
)

add_custom_command(
    OUTPUT "${SEAL_DENO_DEST}"
    COMMAND ${CMAKE_COMMAND} -E copy "${SEAL_DENO_CARGO_OUT}" "${SEAL_DENO_DEST}"
    DEPENDS "${SEAL_DENO_CARGO_OUT}"
    COMMENT "Copying libseal_deno.so → jniLibs/${ANDROID_ABI}"
    VERBATIM
)

add_custom_target(seal-deno-built ALL DEPENDS "${SEAL_DENO_DEST}")

# Expose the Rust library as a CMake IMPORTED target so deno-jni can link it.
add_library(seal-deno SHARED IMPORTED GLOBAL)
set_target_properties(seal-deno PROPERTIES IMPORTED_LOCATION "${SEAL_DENO_DEST}")
add_dependencies(seal-deno seal-deno-built)

# ---------------------------------------------------------------------------
# libdeno-jni — JNI bridge loaded by DenoRuntime.kt via System.loadLibrary
# ---------------------------------------------------------------------------
find_library(log-lib   log)
find_library(android-lib android)

add_library(deno-jni SHARED deno_jni.cpp)
add_dependencies(deno-jni seal-deno-built)
target_link_libraries(deno-jni
    ${log-lib}
    ${android-lib}
    seal-deno)

# ---------------------------------------------------------------------------
# libdeno-cli.so — tiny PIE proxy spawned by yt-dlp
#
# Built as an executable with a .so suffix so the Gradle plugin packages it
# into lib/<abi>/ inside the APK, where Android extracts it to the exec-
# allowed native library directory.
# ---------------------------------------------------------------------------
add_executable(deno-cli deno_cli.cpp)
set_target_properties(deno-cli PROPERTIES
    OUTPUT_NAME "libdeno-cli.so"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
target_link_libraries(deno-cli ${log-lib})
