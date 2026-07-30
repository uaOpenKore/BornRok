#pragma once
#include <bgfx/bgfx.h>

#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "core/Log.hpp"

// Runtime loader for bgfx shader bytecode produced by CompileShaders.cmake.
// Layout: <baseDir>/shaders/<profile>/<name>.bin, where <profile> matches the
// active renderer. Kept header-only and dependency-light (std::ifstream) so the
// render layer does not depend on the platform layer.
namespace uaro {

inline const char* shader_profile_dir() {
    switch (bgfx::getRendererType()) {
        case bgfx::RendererType::Direct3D11:
        case bgfx::RendererType::Direct3D12: return "dx11";
        case bgfx::RendererType::OpenGLES:
#ifdef __ANDROID__
            return "essl";  // GLES3 shaders (essl 300) on Android; desktop GL uses glsl 120
#else
            return "glsl";
#endif
        case bgfx::RendererType::OpenGL:    return "glsl";
        case bgfx::RendererType::Vulkan:    return "spirv";
        case bgfx::RendererType::Metal:     return "metal";
        default:                            return "glsl";
    }
}

// Bytecode baked into the binary (EmbeddedShaders.cpp, generated at build time for the sprite
// shaders so the patcher UI renders from a bare exe). Returns nullptr when nothing is embedded for
// this (profile, name) — the default stub always returns nullptr, so on-disk shaders win.
const unsigned char* embedded_shader_bytes(const std::string& profile, const std::string& name,
                                           u32& outSize);
// Called by the build-generated EmbeddedSpriteShaders.cpp (bin2c) to register baked-in bytecode.
void register_embedded_shader(const char* key, const unsigned char* data, u32 size);

inline bgfx::ShaderHandle load_shader(const std::string& baseDir, const std::string& name) {
    const std::string profile = shader_profile_dir();
    const std::string path = baseDir + "/shaders/" + profile + "/" + name + ".bin";
    const bgfx::Memory* mem = nullptr;
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (in) {
        const std::streamsize size = in.tellg();
        in.seekg(0);
        mem = bgfx::alloc(static_cast<u32>(size) + 1);
        in.read(reinterpret_cast<char*>(mem->data), size);
        mem->data[size] = '\0';
    } else {
        // No file on disk (e.g. a single-exe distribution): fall back to the baked-in copy.
        u32 embSize = 0;
        if (const unsigned char* emb = embedded_shader_bytes(profile, name, embSize)) {
            mem = bgfx::alloc(embSize + 1);
            std::memcpy(mem->data, emb, embSize);
            mem->data[embSize] = '\0';
            log::info("shader '{}' loaded from the embedded copy (no on-disk {})", name, path);
        } else {
            log::warn("shader not found on disk or embedded: {}", path);
            return BGFX_INVALID_HANDLE;
        }
    }

    bgfx::ShaderHandle h = bgfx::createShader(mem);
    if (bgfx::isValid(h)) {
        bgfx::setName(h, name.c_str());
    } else {
        // The file existed but bgfx rejected the bytecode -- usually the wrong profile for the picked
        // backend (e.g. dx11 .bin fed to a Vulkan device) or a corrupt/short file. This is a prime
        // suspect for "the window renders incomplete" on a given GPU (S.).
        log::error("render: shader '{}' bytecode REJECTED by bgfx (wrong profile for backend or "
                   "corrupt): {}", name, path);
    }
    return h;
}

inline bgfx::ProgramHandle load_program(const std::string& baseDir,
                                        const std::string& vsName,
                                        const std::string& fsName) {
    bgfx::ShaderHandle vs = load_shader(baseDir, vsName);
    bgfx::ShaderHandle fs = load_shader(baseDir, fsName);
    if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) {
        log::error("render: program {}+{} UNAVAILABLE -- backend '{}' shaders missing/invalid (this is "
                   "why parts of the UI/scene stay blank)", vsName, fsName, shader_profile_dir());
        if (bgfx::isValid(vs)) bgfx::destroy(vs);
        if (bgfx::isValid(fs)) bgfx::destroy(fs);
        return BGFX_INVALID_HANDLE;
    }
    bgfx::ProgramHandle prog = bgfx::createProgram(vs, fs, true /* destroy shaders with program */);
    if (bgfx::isValid(prog))
        log::info("render: program {}+{} ok (profile '{}')", vsName, fsName, shader_profile_dir());
    else
        log::error("render: createProgram {}+{} FAILED (profile '{}')", vsName, fsName, shader_profile_dir());
    return prog;
}

} // namespace uaro
