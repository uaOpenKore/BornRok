# Overlay triplet: Windows on ARM (arm64), fully static CRT + libs, to match the
# desktop x64-windows-static "single-file exe" layout. vcpkg ships arm64-windows
# (dynamic, community) but NOT a static arm64 variant, so we supply our own.
# Referenced by the "win-arm64" CMake preset via VCPKG_OVERLAY_TRIPLETS.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
