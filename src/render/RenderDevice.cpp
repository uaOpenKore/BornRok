#include "render/RenderDevice.hpp"

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>  // bgfx::setPlatformData (surface re-acquire on Android resume)

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "core/Log.hpp"
#include "render/Shader.hpp"

namespace uaro {

namespace {

// MSAA backbuffer flag. OFF on Android: budget GLES drivers (e.g. ZTE 8050) crash with a null deref
// inside bgfx::gl::RendererContextGL::submit on the MSAA resolve/blit after the world renders (S. log).
// MSAA is barely visible on a phone screen anyway. Desktop/Xbox keep 4x.
#if defined(__ANDROID__)
constexpr u32 kResetMsaa = 0u;
#else
constexpr u32 kResetMsaa = BGFX_RESET_MSAA_X4;
#endif

// Forward bgfx's own diagnostics (renderer pick, GPU, caps, and -- crucially -- shader/format/
// framebuffer errors) into OUR console log. Without this, bgfx writes them only to the platform
// debugger (OutputDebugString on Windows), so a tester just sees a broken window with nothing in the
// log (S.: "неполноценные окна входа на hd6000/rtx3060 -- нужна отладочная инфа по рендеру в консоль").
struct BgfxLogCallback : public bgfx::CallbackI {
    void fatal(const char* path, uint16_t line, bgfx::Fatal::Enum code, const char* str) override {
        log::error("bgfx FATAL (code {}) {}:{}: {}", static_cast<int>(code), path ? path : "?", line,
                   str ? str : "");
    }
    void traceVargs(const char* /*path*/, uint16_t /*line*/, const char* fmt, va_list ap) override {
        char buf[2048];
        std::vsnprintf(buf, sizeof(buf), fmt, ap);
        usize n = std::strlen(buf);
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = '\0';
        if (n > 0) log::info("bgfx: {}", buf);
    }
    void profilerBegin(const char*, uint32_t, const char*, uint16_t) override {}
    void profilerBeginLiteral(const char*, uint32_t, const char*, uint16_t) override {}
    void profilerEnd() override {}
    uint32_t cacheReadSize(uint64_t) override { return 0; }
    bool cacheRead(uint64_t, void*, uint32_t) override { return false; }
    void cacheWrite(uint64_t, const void*, uint32_t) override {}
    void screenShot(const char*, uint32_t, uint32_t, uint32_t, const void*, uint32_t, bool) override {}
    void captureBegin(uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, bool) override {}
    void captureEnd() override {}
    void captureFrame(const void*, uint32_t) override {}
};
BgfxLogCallback g_bgfxCallback;

const char* vendorName(uint16_t id) {
    switch (id) {
        case 0x1002: return "AMD";
        case 0x10de: return "NVIDIA";
        case 0x8086: return "Intel";
        case 0x5143: return "Qualcomm";
        case 0x0000: return "software/none";
        default:     return "unknown";
    }
}

// Log the picked backend, GPU, limits, and whether the texture formats the UI relies on are usable,
// so a broken-render report shows immediately WHICH backend/GPU and WHAT is unsupported.
void logRenderCaps() {
    const bgfx::Caps* caps = bgfx::getCaps();
    if (!caps) return;
    log::info("render: backend={} GPU={} (vendor 0x{:04x} {}, device 0x{:04x})",
              bgfx::getRendererName(caps->rendererType), vendorName(caps->vendorId), caps->vendorId,
              vendorName(caps->vendorId), caps->deviceId);
    log::info("render: maxTextureSize={} maxFBAttachments={} numGPUs={}", caps->limits.maxTextureSize,
              caps->limits.maxFBAttachments, caps->numGPUs);
    auto fmt2d = [&](bgfx::TextureFormat::Enum f) {
        return (caps->formats[f] & BGFX_CAPS_FORMAT_TEXTURE_2D) != 0;
    };
    log::info("render: tex2d BGRA8={} RGBA8={} A8={} ; instancing={} compute={}",
              fmt2d(bgfx::TextureFormat::BGRA8), fmt2d(bgfx::TextureFormat::RGBA8),
              fmt2d(bgfx::TextureFormat::A8), (caps->supported & BGFX_CAPS_INSTANCING) != 0,
              (caps->supported & BGFX_CAPS_COMPUTE) != 0);
}

}  // namespace

RenderDevice::~RenderDevice() { shutdown(); }

void RenderDevice::setClearColor(u32 rgba) {
    clearColor_ = rgba;
    if (inited_) bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, rgba, 1.0f, 0);
}

bool RenderDevice::init(const NativeWindow& nw, const RenderConfig& cfg) {
    width_ = cfg.width;
    height_ = cfg.height;
    sceneW_ = cfg.width;
    sceneH_ = cfg.height;
    vsync_ = cfg.vsync;

    // NOTE: bgfx manages its own render thread on desktop. Single-threaded mode
    // (calling bgfx::renderFrame() before init) is needed for Metal on macOS/iOS
    // and is wired up with the mobile backends in v6.
    bgfx::Init init;
#if defined(CLIENT_UWP)
    // UWP/Xbox: bgfx binds a SwapChainForCoreWindow, which its Direct3D12 backend drives from the
    // CoreWindow IUnknown* handed in as nwh. Force it (auto-select can't probe a desktop adapter here).
    init.type = bgfx::RendererType::Direct3D12;
#elif defined(CLIENT_NX)
    // Nintendo Switch: force the NVN backend (the vendor's bgfx console renderer, NDA). nwh = the
    // nn::vi native window from NxPlatform.
    init.type = bgfx::RendererType::Nvn;
#elif defined(CLIENT_PROSPERO)
    // PlayStation 5: force the AGC backend (NDA). nwh = the sceVideoOut surface from PsPlatform.
    init.type = bgfx::RendererType::Agc;
#elif defined(CLIENT_ORBIS)
    // PlayStation 4: force the GNM backend (NDA). Same PsPlatform, PS4 SDK.
    init.type = bgfx::RendererType::Gnm;
#else
    init.type = bgfx::RendererType::Count;  // auto-select best backend
#endif
    init.resolution.width = static_cast<u32>(width_);
    init.resolution.height = static_cast<u32>(height_);
    init.resolution.reset = kResetMsaa | (vsync_ ? BGFX_RESET_VSYNC : 0u);
    init.platformData.nwh = nw.nwh;
    init.platformData.ndt = nw.ndt;
    init.callback = &g_bgfxCallback;  // route bgfx's own trace/fatal into our console log

    log::info("render: bgfx::init requested type=auto {}x{} reset={}{}", width_, height_,
              kResetMsaa ? "MSAAx4" : "noMSAA", vsync_ ? "|vsync" : "");
    if (!bgfx::init(init)) {
        log::error("render: bgfx::init FAILED -- no usable graphics backend (driver/GPU?)");
        return false;
    }
    inited_ = true;

    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, clearColor_, 1.0f, 0);
    bgfx::setViewRect(0, 0, 0, static_cast<u16>(width_), static_cast<u16>(height_));

    logRenderCaps();  // backend / GPU / limits / format support -- the render diagnostics (S.)
    return true;
}

void RenderDevice::shutdown() {
    if (inited_) {
        destroyHdrTarget();
        destroyFsrTargets();
        destroyGradeTarget();
        destroyGodrayTarget();
        auto kill = [](bgfx::ProgramHandle& p) { if (bgfx::isValid(p)) bgfx::destroy(p); p = BGFX_INVALID_HANDLE; };
        kill(tonemapProg_);
        kill(easuProg_);
        kill(rcasProg_);
        kill(gradeProg_);
        kill(godrayProg_);
        kill(vollightProg_);
        if (bgfx::isValid(sTex_)) bgfx::destroy(sTex_);
        if (bgfx::isValid(uTonemap_)) bgfx::destroy(uTonemap_);
        if (bgfx::isValid(uFsr_)) bgfx::destroy(uFsr_);
        if (bgfx::isValid(uGrade_)) bgfx::destroy(uGrade_);
        if (bgfx::isValid(uGodray_)) bgfx::destroy(uGodray_);
        if (bgfx::isValid(uGodray2_)) bgfx::destroy(uGodray2_);
        if (bgfx::isValid(uVolParams_)) bgfx::destroy(uVolParams_);
        if (bgfx::isValid(uLightScr_)) bgfx::destroy(uLightScr_);
        if (bgfx::isValid(uLightCol_)) bgfx::destroy(uLightCol_);
        if (bgfx::isValid(fsTri_)) bgfx::destroy(fsTri_);
        sTex_ = uTonemap_ = uFsr_ = uGrade_ = BGFX_INVALID_HANDLE;
        uGodray_ = uGodray2_ = uVolParams_ = uLightScr_ = uLightCol_ = BGFX_INVALID_HANDLE;
        fsTri_ = BGFX_INVALID_HANDLE;
        postReady_ = false;
        bgfx::shutdown();
        inited_ = false;
    }
}

void RenderDevice::recomputeRenderSize() {
    sceneW_ = std::max(1, static_cast<int>(std::lround(width_ * fsrScale_)));
    sceneH_ = std::max(1, static_cast<int>(std::lround(height_ * fsrScale_)));
}

void RenderDevice::reacquire(const NativeWindow& nw) {
#if defined(__ANDROID__)
    // Android returns from background with a NEW ANativeWindow; the one bgfx's EGL context was bound to
    // was destroyed while backgrounded. Re-point bgfx at the fresh handle and reset -- this rebuilds the
    // swap chain (and the offscreen HDR/FSR/grade targets), fixing the permanent black screen after
    // minimise -> restore (S. 2026-08-06). Mirrors resize()'s reset + target rebuild.
    bgfx::PlatformData pd{};
    pd.ndt = nw.ndt;
    pd.nwh = nw.nwh;
    bgfx::setPlatformData(pd);
    bgfx::reset(static_cast<u32>(width_), static_cast<u32>(height_),
                kResetMsaa | (vsync_ ? BGFX_RESET_VSYNC : 0u));
    bgfx::setViewRect(0, 0, 0, static_cast<u16>(width_), static_cast<u16>(height_));
    if (hdrActive()) createHdrTarget();
    if (fsrActive()) createFsrTargets();
    if (bgfx::isValid(gradeFb_)) createGradeTarget();
#else
    (void)nw;
#endif
}

void RenderDevice::resize(int width, int height) {
    if (width <= 0 || height <= 0) return;
    if (width == width_ && height == height_) return;
    width_ = width;
    height_ = height;
    recomputeRenderSize();
    bgfx::reset(static_cast<u32>(width_), static_cast<u32>(height_),
                kResetMsaa | (vsync_ ? BGFX_RESET_VSYNC : 0u));
    bgfx::setViewRect(0, 0, 0, static_cast<u16>(width_), static_cast<u16>(height_));
    if (hdrActive()) createHdrTarget();  // resize the offscreen targets to match
    if (fsrActive()) createFsrTargets();
    if (bgfx::isValid(gradeFb_)) createGradeTarget();
}

// ---- HDR post-processing (#111) ------------------------------------------------------------
namespace {
// HDR/grade resolve view. These paths route ALL scene views (0,1,2) into the offscreen and blit once
// to the backbuffer, so a high id past the scene views with the default ascending order is correct
// (no reordering needed). The FSR/SSAA path is different: it keeps the UI native and must reorder, so
// it uses low contiguous ids 2/3 for its resolve passes instead (see beginFrame).
constexpr bgfx::ViewId kTonemapView = 200;
struct FsVert { float x, y, z, u, v; };  // z is required: the layout declares Position = 3 floats
bgfx::VertexLayout fsLayout() {
    bgfx::VertexLayout l;
    l.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();
    return l;
}
}  // namespace

void RenderDevice::initPostProcess(const std::string& assetDir) {
    if (!inited_ || postReady_) return;
    tonemapProg_ = load_program(assetDir, "vs_fullscreen", "fs_tonemap");
    if (!bgfx::isValid(tonemapProg_)) {
        log::warn("render: tonemap shader unavailable -> HDR disabled");
        return;
    }
    sTex_ = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);
    uTonemap_ = bgfx::createUniform("u_tonemap", bgfx::UniformType::Vec4);
    uFsr_ = bgfx::createUniform("u_fsr", bgfx::UniformType::Vec4);
    uGrade_ = bgfx::createUniform("u_grade", bgfx::UniformType::Vec4);
    uGodray_ = bgfx::createUniform("u_godray", bgfx::UniformType::Vec4);
    uGodray2_ = bgfx::createUniform("u_godray2", bgfx::UniformType::Vec4);
    uVolParams_ = bgfx::createUniform("u_volParams", bgfx::UniformType::Vec4);
    uLightScr_ = bgfx::createUniform("u_lightScr", bgfx::UniformType::Vec4, kMaxVolLights);
    uLightCol_ = bgfx::createUniform("u_lightCol", bgfx::UniformType::Vec4, kMaxVolLights);
    easuProg_ = load_program(assetDir, "vs_fullscreen", "fs_fsr_easu");  // may be invalid -> FSR off
    rcasProg_ = load_program(assetDir, "vs_fullscreen", "fs_fsr_rcas");
    gradeProg_ = load_program(assetDir, "vs_fullscreen", "fs_grade");    // may be invalid -> grade off
    godrayProg_ = load_program(assetDir, "vs_fullscreen", "fs_godray");  // Glow: sun shafts
    vollightProg_ = load_program(assetDir, "vs_fullscreen", "fs_vollight");  // Rays: sun + local shafts
    // Oversized fullscreen triangle in clip space; v flipped on top-left-origin backends so the
    // offscreen texture blits upright regardless of backend.
    const bool flip = !bgfx::getCaps()->originBottomLeft;
    const float vb = flip ? 1.0f : 0.0f, vt = flip ? -1.0f : 2.0f;
    static const bgfx::VertexLayout layout = fsLayout();
    FsVert tri[3] = {{-1.0f, -1.0f, 0.0f, 0.0f, vb},
                     {3.0f, -1.0f, 0.0f, 2.0f, vb},
                     {-1.0f, 3.0f, 0.0f, 0.0f, vt}};
    fsTri_ = bgfx::createVertexBuffer(bgfx::copy(tri, sizeof(tri)), layout);
    postReady_ = true;
    log::info("render: HDR post-process ready");
}

void RenderDevice::createHdrTarget() {
    destroyHdrTarget();
    const u16 w = static_cast<u16>(width_), h = static_cast<u16>(height_);
    const u64 rtFlags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    bgfx::TextureHandle color =
        bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA16F, rtFlags);
    bgfx::TextureHandle depth =
        bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT_WRITE_ONLY);
    bgfx::TextureHandle att[2] = {color, depth};
    hdrFb_ = bgfx::createFrameBuffer(2, att, true /* destroy textures with the fb */);
    fbW_ = width_;
    fbH_ = height_;
}

void RenderDevice::destroyHdrTarget() {
    if (bgfx::isValid(hdrFb_)) bgfx::destroy(hdrFb_);
    hdrFb_ = BGFX_INVALID_HANDLE;
    fbW_ = fbH_ = 0;
}

bool RenderDevice::hdrSupported() const {
    if (!inited_) return false;
    const bgfx::Caps* caps = bgfx::getCaps();
    if (!caps) return false;
    // The offscreen needs RGBA16F usable AS a framebuffer attachment. Many laptop iGPUs report the
    // texture but not the render-target capability, and rendering to it then hangs (S.: HDR froze).
    return (caps->formats[bgfx::TextureFormat::RGBA16F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) != 0;
}

void RenderDevice::setHdr(bool on) {
#if defined(__ANDROID__)
    (void)on; return;  // post-processing off on Android: mobile GLES FBs (RGBA16F/D24S8) are crash-prone
#endif
    if (on == hdrOn_) return;
    if (on && (!postReady_ || !hdrSupported())) {
        log::warn("render: HDR requested but unavailable ({}), staying off",
                  !postReady_ ? "tonemap shader missing" : "GPU can't render to RGBA16F");
        hdrOn_ = false;
        return;
    }
    hdrOn_ = on;
    if (on) {
        createHdrTarget();
        if (!bgfx::isValid(hdrFb_)) {  // creation failed despite caps -> bail out safely
            log::error("render: HDR framebuffer creation failed, staying off");
            hdrOn_ = false;
            return;
        }
    } else {
        destroyHdrTarget();
        // Detach the scene views so they target the backbuffer again.
        for (bgfx::ViewId v = 0; v <= 2; ++v) bgfx::setViewFrameBuffer(v, BGFX_INVALID_HANDLE);
    }
    log::info("render: HDR {}", on ? "ON" : "OFF");
}

void RenderDevice::createFsrTargets() {
    destroyFsrTargets();
    // Bilinear sampling (no POINT flag) so the SSAA downsample averages neighbouring texels and the
    // EASU upscale reads a smooth source.
    const u64 rt = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    // Off-screen 3D scene target at the scaled size (below native = FSR, above native = SSAA), + depth.
    bgfx::TextureHandle c0 =
        bgfx::createTexture2D(static_cast<u16>(sceneW_), static_cast<u16>(sceneH_), false, 1,
                              bgfx::TextureFormat::RGBA8, rt);
    bgfx::TextureHandle d0 =
        bgfx::createTexture2D(static_cast<u16>(sceneW_), static_cast<u16>(sceneH_), false, 1,
                              bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT_WRITE_ONLY);
    bgfx::TextureHandle a0[2] = {c0, d0};
    fsrFb_ = bgfx::createFrameBuffer(2, a0, true);
    // Native intermediate (EASU output for the upscale path; unused by the SSAA path). No depth.
    bgfx::TextureHandle c1 =
        bgfx::createTexture2D(static_cast<u16>(width_), static_cast<u16>(height_), false, 1,
                              bgfx::TextureFormat::RGBA8, rt);
    easuFb_ = bgfx::createFrameBuffer(1, &c1, true);
    fsrW_ = sceneW_;
    fsrH_ = sceneH_;
}

void RenderDevice::destroyFsrTargets() {
    if (bgfx::isValid(fsrFb_)) bgfx::destroy(fsrFb_);
    if (bgfx::isValid(easuFb_)) bgfx::destroy(easuFb_);
    fsrFb_ = easuFb_ = BGFX_INVALID_HANDLE;
    fsrW_ = fsrH_ = 0;
}

void RenderDevice::setFsr(float scale) {
#if defined(__ANDROID__)
    (void)scale; return;  // FSR/SSAA offscreen RTs off on Android (fragile mobile GLES FBs)
#endif
    scale = std::clamp(scale, 0.5f, 2.0f);
    if (scale == fsrScale_) return;
    const bool wasActive = fsrActive();
    fsrScale_ = scale;
    recomputeRenderSize();
    const bool wantScale = upscaling() || supersampling();
    if (wantScale) {
        // The upscale path needs EASU+RCAS; the SSAA (supersample) path only needs the grade shader
        // as a bilinear resolve blit. Require whatever the chosen direction uses.
        const bool haveShaders = supersampling()
                                     ? bgfx::isValid(gradeProg_)
                                     : (bgfx::isValid(easuProg_) && bgfx::isValid(rcasProg_));
        if (!postReady_ || !haveShaders) {
            log::warn("render: render-scale requested but its shaders are unavailable, staying off");
            fsrScale_ = 1.0f;
            recomputeRenderSize();
            return;
        }
        createFsrTargets();
    } else {
        destroyFsrTargets();
        for (bgfx::ViewId v = 0; v <= 2; ++v) bgfx::setViewFrameBuffer(v, BGFX_INVALID_HANDLE);
        bgfx::setViewOrder(0, UINT16_MAX, nullptr);  // back to default ascending order
    }
    if (wasActive != fsrActive() || !wantScale)
        log::info("render: render-scale {} ({}x -> {}x{} scene {}x{})",
                  !wantScale ? "OFF" : (supersampling() ? "SSAA" : "FSR"), fsrScale_, width_, height_,
                  sceneW_, sceneH_);
}

void RenderDevice::createGradeTarget() {
    destroyGradeTarget();
    const u64 rt = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    bgfx::TextureHandle c =
        bgfx::createTexture2D(static_cast<u16>(width_), static_cast<u16>(height_), false, 1,
                              bgfx::TextureFormat::RGBA8, rt);
    bgfx::TextureHandle d =
        bgfx::createTexture2D(static_cast<u16>(width_), static_cast<u16>(height_), false, 1,
                              bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT_WRITE_ONLY);
    bgfx::TextureHandle a[2] = {c, d};
    gradeFb_ = bgfx::createFrameBuffer(2, a, true);
    fbW_ = width_;
    fbH_ = height_;
}

void RenderDevice::destroyGradeTarget() {
    if (bgfx::isValid(gradeFb_)) bgfx::destroy(gradeFb_);
    gradeFb_ = BGFX_INVALID_HANDLE;
}

void RenderDevice::setGrade(float brightness, float contrast) {
#if defined(__ANDROID__)
    (void)brightness; (void)contrast; return;  // colour-grade offscreen RT off on Android
#endif
    gradeB_ = brightness;
    gradeC_ = contrast;
    // Only need the dedicated grade offscreen when grading is active AND nothing else already routes
    // the scene through a post pass (HDR tonemap / FSR RCAS / SSAA resolve all fold the grade in).
    const bool want = gradeNonNeutral() && postReady_ && bgfx::isValid(gradeProg_) && !hdrOn_ &&
                      !upscaling() && !supersampling();
    if (want && !bgfx::isValid(gradeFb_)) {
        createGradeTarget();
    } else if (!want && bgfx::isValid(gradeFb_)) {
        destroyGradeTarget();
        for (bgfx::ViewId v = 0; v <= 2; ++v) bgfx::setViewFrameBuffer(v, BGFX_INVALID_HANDLE);
    }
}

void RenderDevice::createGodrayTarget() {
    destroyGodrayTarget();
    const u64 rt = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;  // clamp: no wrap on the radial taps
    bgfx::TextureHandle c =
        bgfx::createTexture2D(static_cast<u16>(width_), static_cast<u16>(height_), false, 1,
                              bgfx::TextureFormat::RGBA8, rt);
    bgfx::TextureHandle d =
        bgfx::createTexture2D(static_cast<u16>(width_), static_cast<u16>(height_), false, 1,
                              bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT_WRITE_ONLY);
    bgfx::TextureHandle a[2] = {c, d};
    godrayFb_ = bgfx::createFrameBuffer(2, a, true);
    fbW_ = width_;
    fbH_ = height_;
}

void RenderDevice::destroyGodrayTarget() {
    if (bgfx::isValid(godrayFb_)) bgfx::destroy(godrayFb_);
    godrayFb_ = BGFX_INVALID_HANDLE;
}

bool RenderDevice::isSoftwareRenderer() const {
    const bgfx::Caps* caps = bgfx::getCaps();
    return caps && caps->vendorId == BGFX_PCI_ID_MICROSOFT;  // WARP / software rasterizer
}

void RenderDevice::setGodrayMode(int mode) {
#if defined(__ANDROID__)
    (void)mode; return;  // god-ray/volumetric-light offscreen RT off on Android
#endif
    mode = mode < 0 ? 0 : (mode > 2 ? 2 : mode);
    if (mode == godrayMode_) return;
    godrayMode_ = mode;
    // Glow (mode 1) = soft light halos drawn as world-space additive billboards in the SCENE pass
    // (GameScene::render -> LightGlowRenderer), straight to the backbuffer. It needs NO offscreen
    // and NO fullscreen shader, so it must NOT route the scene through godrayFb_ (doing so made the
    // whole scene depend on the shaft composite — a fragile extra layer for nothing). Only Rays
    // (mode 2 = halos + fullscreen light shafts) needs the offscreen; set it up only then.
    if (mode >= 2 && postReady_ && bgfx::isValid(vollightProg_)) {
        createGodrayTarget();
        if (!bgfx::isValid(godrayFb_))
            log::error("render: god-ray framebuffer failed -> glow halos only (no shafts)");
    } else {
        destroyGodrayTarget();
        for (bgfx::ViewId v = 0; v <= 2; ++v) bgfx::setViewFrameBuffer(v, BGFX_INVALID_HANDLE);
        if (mode >= 2 && !postReady_)
            log::info("render: light shafts need the post pipeline; showing glow halos only");
    }
    log::info("render: volumetric light mode {} ({})", mode,
              mode == 0 ? "off" : mode == 1 ? "glow" : "rays");
}

void RenderDevice::setVsync(bool on) {
    if (!inited_ || on == vsync_) return;
    vsync_ = on;
    bgfx::reset(static_cast<u32>(width_), static_cast<u32>(height_),
                kResetMsaa | (vsync_ ? BGFX_RESET_VSYNC : 0u));
    bgfx::setViewRect(0, 0, 0, static_cast<u16>(width_), static_cast<u16>(height_));
}

void RenderDevice::beginFrame() {
    if (fsrActive()) {
        // 3D-only render-scale: view 0 (3D) draws into the off-screen scene target at the scaled
        // size; the UI (view 1) stays native and draws straight to the backbuffer, ON TOP of the
        // resolve pass. Views run in the explicit order {3D, resolve, sharpen, UI} so the UI is
        // composited last -- otherwise the ascending default would draw the UI before the resolve
        // overwrote the backbuffer.
        if (fsrW_ != sceneW_ || fsrH_ != sceneH_) createFsrTargets();
        bgfx::setViewFrameBuffer(0, fsrFb_);
        bgfx::setViewRect(0, 0, 0, static_cast<u16>(sceneW_), static_cast<u16>(sceneH_));
        bgfx::setViewFrameBuffer(1, BGFX_INVALID_HANDLE);
        bgfx::setViewRect(1, 0, 0, static_cast<u16>(width_), static_cast<u16>(height_));
        bgfx::setViewFrameBuffer(2, BGFX_INVALID_HANDLE);
        bgfx::setViewFrameBuffer(3, BGFX_INVALID_HANDLE);
        // Execute: 3D(0) -> resolve(2) -> sharpen(3) -> UI(1). The remapped views MUST stay in a
        // contiguous low range: bgfx builds an inverse remap over ALL view ids, so a high resolve id
        // (e.g. 200) would keep its identity slot and clobber the UI's position, running the UI
        // before the resolve. Ids 0..3 avoid that collision.
        const bgfx::ViewId order[4] = {0, 2, 3, 1};
        bgfx::setViewOrder(0, 4, order);
    } else if (hdrActive()) {
        if (fbW_ != width_ || fbH_ != height_) createHdrTarget();  // keep the target at window size
        // Route the scene views (0=3D, 1=UI, 2=grade) into the HDR offscreen target.
        for (bgfx::ViewId v = 0; v <= 2; ++v) {
            bgfx::setViewFrameBuffer(v, hdrFb_);
            bgfx::setViewRect(v, 0, 0, static_cast<u16>(width_), static_cast<u16>(height_));
        }
        bgfx::setViewOrder(0, UINT16_MAX, nullptr);
    } else if (godrayActive()) {
        if (fbW_ != width_ || fbH_ != height_) createGodrayTarget();
        for (bgfx::ViewId v = 0; v <= 2; ++v) {
            bgfx::setViewFrameBuffer(v, godrayFb_);
            bgfx::setViewRect(v, 0, 0, static_cast<u16>(width_), static_cast<u16>(height_));
        }
        bgfx::setViewOrder(0, UINT16_MAX, nullptr);
    } else if (gradeActive()) {
        if (fbW_ != width_ || fbH_ != height_) createGradeTarget();
        for (bgfx::ViewId v = 0; v <= 2; ++v) {
            bgfx::setViewFrameBuffer(v, gradeFb_);
            bgfx::setViewRect(v, 0, 0, static_cast<u16>(width_), static_cast<u16>(height_));
        }
        bgfx::setViewOrder(0, UINT16_MAX, nullptr);
    } else {
        for (bgfx::ViewId v = 0; v <= 2; ++v) bgfx::setViewFrameBuffer(v, BGFX_INVALID_HANDLE);
        bgfx::setViewRect(0, 0, 0, static_cast<u16>(width_), static_cast<u16>(height_));
        bgfx::setViewOrder(0, UINT16_MAX, nullptr);
    }
    bgfx::touch(0);  // ensure view 0 is cleared even with no draw calls
}

void RenderDevice::endFrame() {
    auto blit = [&](bgfx::ViewId view, bgfx::FrameBufferHandle target, bgfx::TextureHandle src,
                    int w, int h, bgfx::ProgramHandle prog, bgfx::UniformHandle u, const float p[4]) {
        bgfx::setViewFrameBuffer(view, target);
        bgfx::setViewRect(view, 0, 0, static_cast<u16>(w), static_cast<u16>(h));
        bgfx::setViewClear(view, BGFX_CLEAR_NONE, 0, 1.0f, 0);
        bgfx::setTexture(0, sTex_, src);
        bgfx::setUniform(u, p);
        bgfx::setVertexBuffer(0, fsTri_);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::submit(view, prog);
    };
    const float grade[4] = {gradeB_, gradeC_, 0.0f, 0.0f};
    if (fsrActive() && supersampling()) {
        // SSAA "top quality": the scene was rendered ABOVE native into fsrFb; a single bilinear
        // resolve blit averages it down onto the backbuffer at native (and applies the grade). No
        // EASU/RCAS -- those are for upscaling, and would soften a supersampled image.
        blit(2, BGFX_INVALID_HANDLE, bgfx::getTexture(fsrFb_, 0), width_, height_, gradeProg_,
             uGrade_, grade);
    } else if (fsrActive()) {
        // FSR1 upscale: EASU upscales the below-native scene into the native intermediate FB (rect =
        // native output, while u_fsr carries the INPUT scene texel size for the kernel). RCAS then
        // sharpens it to the backbuffer and applies the grade.
        const float easu[4] = {1.0f / sceneW_, 1.0f / sceneH_, static_cast<float>(sceneW_),
                               static_cast<float>(sceneH_)};
        bgfx::setUniform(uGrade_, grade);
        blit(2, easuFb_, bgfx::getTexture(fsrFb_, 0), width_, height_, easuProg_, uFsr_, easu);
        const float rcas[4] = {1.0f / width_, 1.0f / height_, 0.25f, 0.0f};  // z = sharpness
        bgfx::setUniform(uGrade_, grade);
        blit(3, BGFX_INVALID_HANDLE, bgfx::getTexture(easuFb_, 0), width_, height_, rcasProg_,
             uFsr_, rcas);
    } else if (hdrActive()) {
        const float tone[4] = {1.0f, 0.0f, 0.0f, 0.0f};  // exposure
        bgfx::setUniform(uGrade_, grade);
        blit(kTonemapView, BGFX_INVALID_HANDLE, bgfx::getTexture(hdrFb_, 0), width_, height_,
             tonemapProg_, uTonemap_, tone);
    } else if (godrayActive()) {
        // Volumetric composite: radial-blur the bright scene toward the sun (+ toward each local light
        // in Rays mode) and add, then grade. Extra uniforms are set before the blit (bgfx accumulates
        // them onto the same submit). Sun shafts fade out when the sun is off-screen.
        const float sunI = godraySunVisible_ ? 0.15f : 0.0f;  // S.: at 0.06 no shafts show outdoors even
                                                              // facing the sun; 0.30 blinded. 0.15 = middle
        const float g1[4] = {godraySunU_, godraySunV_, sunI, 0.96f /*decay/step*/};
        const float g2[4] = {1.0f /*density*/, 0.06f /*per-sample weight*/, 0.55f /*bright cut*/, 0.0f};
        bgfx::setUniform(uGodray_, g1);
        bgfx::setUniform(uGodray2_, g2);
        bgfx::ProgramHandle comp = godrayProg_;
        if (godrayMode_ >= 2 && bgfx::isValid(vollightProg_)) {  // Rays: also shaft each local light
            comp = vollightProg_;
            const float vp[4] = {static_cast<float>(volLightCount_), 0.0825f /*intensity; -25%
                                 more (S.: "лучи от факелов очень яркие")*/, 1.0f /*density*/,
                                 0.94f /*decay*/};
            bgfx::setUniform(uVolParams_, vp);
            bgfx::setUniform(uLightScr_, volScr_, kMaxVolLights);
            bgfx::setUniform(uLightCol_, volCol_, kMaxVolLights);
        }
        blit(kTonemapView, BGFX_INVALID_HANDLE, bgfx::getTexture(godrayFb_, 0), width_, height_,
             comp, uGrade_, grade);
    } else if (gradeActive()) {
        // Neither HDR nor render-scale: still route through the grade offscreen so contrast can be
        // raised (the old fullscreen overlay could only lower it).
        blit(kTonemapView, BGFX_INVALID_HANDLE, bgfx::getTexture(gradeFb_, 0), width_, height_,
             gradeProg_, uGrade_, grade);
    }
    bgfx::frame();
}

const char* RenderDevice::rendererName() const {
    return bgfx::getRendererName(bgfx::getRendererType());
}

} // namespace uaro
