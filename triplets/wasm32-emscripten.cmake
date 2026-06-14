set(VCPKG_TARGET_ARCHITECTURE wasm32)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Emscripten)

# Use a wrapper toolchain that chains Emscripten.cmake and then appends
# -matomics / -mbulk-memory AFTER, so the flags survive Emscripten's own
# CMAKE_C_FLAGS_INIT assignments.  VCPKG_C_FLAGS alone is not sufficient
# because the chainloaded Emscripten toolchain can overwrite it.
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE
    "${CMAKE_CURRENT_LIST_DIR}/../cmake/emscripten-threads.cmake")
