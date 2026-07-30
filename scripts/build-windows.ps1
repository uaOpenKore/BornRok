# One command -> Windows x64 AND ARM64 builds of the BornRok client (static deps, exe + MSIX).
#
# Requires: Visual Studio 2022 with the Desktop C++ workload INCLUDING the ARM64 build tools
# ("MSVC v143 - VS 2022 C++ ARM64 build tools"), CMake 3.24+, vcpkg (VCPKG_ROOT env).
# Run from a normal PowerShell (CMake drives the "Visual Studio 17 2022" generator).
#
# Usage:
#   $env:VCPKG_ROOT = 'C:\vcpkg'
#   .\scripts\build-windows.ps1            # x64 + arm64
#   .\scripts\build-windows.ps1 -X64Only   # x64 only
#
# Results:
#   build\win-msvc\src\Release\BornRok.exe   (+ build\win-msvc\BornRok.msix)
#   build\win-arm64\src\Release\BornRok.exe  (+ build\win-arm64\BornRok.msix)
param([switch]$X64Only)
$ErrorActionPreference = "Stop"
if (-not $env:VCPKG_ROOT) { throw "Set VCPKG_ROOT to your vcpkg checkout, e.g.  `$env:VCPKG_ROOT = 'C:\vcpkg'" }
Set-Location (Join-Path $PSScriptRoot "..")

# 1) x64 first — this also builds the host shaderc.exe that the ARM64 build reuses
#    (an arm64 shaderc can't run on an x64 host). The win-arm64 preset already points
#    CLIENT_HOST_SHADERC at build\win-msvc\vcpkg_installed\...\shaderc.exe.
Write-Host "==> [1/2] x64 (win-msvc)"
cmake --preset win-msvc
if ($LASTEXITCODE) { throw "configure win-msvc failed" }
cmake --build --preset win-msvc
if ($LASTEXITCODE) { throw "build win-msvc failed" }

if (-not $X64Only) {
  Write-Host "==> [2/2] arm64 (win-arm64, reuses host shaderc from x64)"
  cmake --preset win-arm64
  if ($LASTEXITCODE) { throw "configure win-arm64 failed" }
  cmake --build --preset win-arm64
  if ($LASTEXITCODE) { throw "build win-arm64 failed" }
}

Write-Host ""
Write-Host "Done:"
Write-Host "  x64  : build\win-msvc\src\Release\BornRok.exe  (+ build\win-msvc\BornRok.msix)"
if (-not $X64Only) {
  Write-Host "  arm64: build\win-arm64\src\Release\BornRok.exe (+ build\win-arm64\BornRok.msix)"
}
Write-Host "  (MSIX is unsigned; sign with win\sign-msix.ps1)"
