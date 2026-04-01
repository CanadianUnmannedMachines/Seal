cmake_minimum_required(VERSION 3.22.1)
project("seal-quickjs")

# ---------------------------------------------------------------------------
# QuickJS-NG — built as a static library from the submodule source.
# ---------------------------------------------------------------------------
set(QUICKJS_DIR "${CMAKE_SOURCE_DIR}/../../../../third_party/quickjs-ng")

add_library(quickjs STATIC
    ${QUICKJS_DIR}/quickjs.c
    ${QUICKJS_DIR}/libregexp.c
    ${QUICKJS_DIR}/libunicode.c
    ${QUICKJS_DIR}/dtoa.c
    ${QUICKJS_DIR}/quickjs-libc.c)

target_include_directories(quickjs PUBLIC ${QUICKJS_DIR})
target_compile_definitions(quickjs PRIVATE
    CONFIG_VERSION="0.8.0"
    CONFIG_BIGNUM)

find_library(log-lib log)

# ---------------------------------------------------------------------------
# libquickjs-cli.so — standalone QuickJS evaluator spawned by yt-dlp
#
# Built as an executable with a .so suffix so the Gradle plugin packages it
# into lib/<abi>/ inside the APK, where Android extracts it to the exec-
# allowed native library directory.
# ---------------------------------------------------------------------------
add_executable(quickjs-cli deno_cli.cpp)
set_target_properties(quickjs-cli PROPERTIES
    OUTPUT_NAME "libquickjs-cli.so"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
target_link_libraries(quickjs-cli quickjs ${log-lib})
