#pragma once
// Runtime platform token for the patcher: "<os>-<arch>" where arch is x64 or arm,
// matching the platforms/<token>/ prefixes in downloads.list (win-x64, linux-x64,
// linux-arm, android-arm, ...). Linux is ONE folder per CPU (linux-x64 / linux-arm),
// NOT per distro. Empty string = unknown platform -> the patcher applies only the
// platform-independent root/ files.
#include <string>

namespace uaro {

// Compile-time CPU arch: "x64", "arm", or "" if neither.
const char* cpuArch();

// Parse an /etc/os-release file body and return the distro token we care about
// (ubuntu/debian/fedora/steamos), or "" if it is none of them. Pure + testable.
std::string linuxDistroFromOsRelease(const std::string& osReleaseText);

// Full runtime platform token, e.g. "win-x64" / "ubuntu-arm" / "android-x64".
// Returns "" if the OS or arch can't be matched to the supported set.
std::string currentPlatform();

}  // namespace uaro
