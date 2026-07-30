# Overlay triplet: aarch64 (arm64) Linux, static libs (like the x64-linux release).
# Chain-loads our aarch64 GNU cross toolchain so vcpkg builds the deps (bgfx, zlib,
# SDL3-static, ...) for the arm64 target from an x86_64 host. Selected by the
# "linux-arm-gcc-release" preset via VCPKG_OVERLAY_TRIPLETS + VCPKG_TARGET_TRIPLET.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../cmake/linux-arm64-cross.cmake")
