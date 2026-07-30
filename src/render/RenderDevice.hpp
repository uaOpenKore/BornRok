#pragma once
#include <bgfx/bgfx.h>

#include <string>

#include "core/Types.hpp"
#include "platform/Window.hpp"

namespace uaro {

struct RenderConfig {
    int width = 1280;
    int height = 720;
    bool vsync = true;
};

// Owns the bgfx device bound to a native window. View 0 is the primary screen
// view. Higher layers draw through SpriteBatch / future mesh renderers; this
// type only handles device lifetime, resize, and frame begin/end.
class RenderDevice {
public:
    // The 2D UI overlay must draw on THIS view — strictly ABOVE the post-process composite view
    // (kTonemapView = 200, internal) so the UI is never routed into an offscreen and never gets
    // tonemapped / FSR-upscaled / god-ray-smeared. Scene/world uses views 0..2; UI sits on top.
    static constexpr bgfx::ViewId kUiView = 250;

    RenderDevice() = default;
    ~RenderDevice();
    RenderDevice(const RenderDevice&) = delete;
    RenderDevice& operator=(const RenderDevice&) = delete;

    bool init(const NativeWindow& nw, const RenderConfig& cfg);
    void shutdown();

    void resize(int width, int height);
    void setVsync(bool on);  // toggle V-Sync at runtime (bgfx::reset); no-op if unchanged (#104)
    void beginFrame();  // set view rect + clear, ensure the view is touched
    void endFrame();    // advance bgfx (present)

    int width() const { return width_; }
    int height() const { return height_; }
    const char* rendererName() const;

    // Background (sky) clear colour, 0xRRGGBBAA. Set per map: roBrowser keys a sky colour off a
    // small weather table (blue for outdoor "sky" maps, black for everything else).
    void setClearColor(u32 rgba);

    // HDR post-processing (#111). Load the tonemap program + fullscreen geometry once (needs the
    // asset dir for the shaders); safe to call when shaders are missing (HDR just stays off).
    void initPostProcess(const std::string& assetDir);
    // Toggle HDR rendering: the scene draws into an RGBA16F offscreen target that is ACES-tonemapped
    // to the backbuffer. OFF (default) renders straight to the backbuffer, byte-identical to before.
    void setHdr(bool on);
    bool hdr() const { return hdrOn_; }
    bool hdrSupported() const;  // false if the GPU can't render to RGBA16F (HDR stays off)

    // 3D render-scale (#111). Only the 3D view (view 0) is rendered off-screen at native*scale and
    // resampled to the backbuffer; the UI (view 1) always stays native and draws on top, so
    // width()/height() (and the mouse/UI) are NEVER scaled -- only the 3D pass changes resolution.
    //   scale == 1.0  -> off (straight to backbuffer)
    //   scale  < 1.0  -> FSR1 upscale: render cheap, EASU+RCAS upscale to native (performance)
    //   scale  > 1.0  -> SSAA supersample: render at e.g. 2x, bilinear-downsample to native ("top
    //                    quality", S.: "рендер в 2 раза выше разрешении и потом ловскейл на нативный")
    void setFsr(float scale);
    float fsr() const { return fsrScale_; }
    float renderToOutputScale() const { return 1.0f; }  // 3D-only scale: mouse/UI are never scaled

    // Brightness/contrast grade (0.5 = neutral). Applied in the post pass, so it can INCREASE contrast
    // too (the old fullscreen overlay could only lower it). (#111 / S. contrast.)
    void setGrade(float brightness, float contrast);

    // Volumetric light (#117). Modes: 0 = off, 1 = Glow (soft additive halos at each light; the
    // world-space glow billboards are drawn by the scene), 2 = Rays (Glow + screen-space light shafts
    // radiating from the sun and from each local light, via the composite). Both modes also radial-blur
    // the sun. The shaft composite routes the scene through an offscreen (like grade) and is suppressed
    // while HDR/render-scale own the offscreen — in that case Glow's world halos still show, just no
    // shafts. Persists. OFF (default) = byte-identical.
    void setGodrayMode(int mode);
    // True when running on a software rasterizer (WARP: vendor 0x1414 Microsoft — the
    // no-GPU-driver case). Used to default heavy visuals (Normals) to Off.
    bool isSoftwareRenderer() const;
    int godrayMode() const { return godrayMode_; }
    // Per-frame sun position in screen UV (top-left origin) + whether it's on-screen and in front of
    // the camera. Fed by the world scene each frame; the shafts fade out when the sun isn't visible.
    void setGodraySun(float u, float v, bool visible) {
        godraySunU_ = u; godraySunV_ = v; godraySunVisible_ = visible;
    }
    // Per-frame local light screen positions for the Rays mode (#117 B v2). scr = [u,v,visible,0] per
    // light, col = [r,g,b,0] per light, count clamped to kMaxVolLights. Screen-space shafts smear the
    // glow cores toward each light; no depth reconstruction (see the doc). Only consumed in mode 2.
    static constexpr int kMaxVolLights = 16;
    void setVolLights(const float* scr4, const float* col4, int count) {
        volLightCount_ = count < 0 ? 0 : (count > kMaxVolLights ? kMaxVolLights : count);
        for (int i = 0; i < volLightCount_ * 4; ++i) { volScr_[i] = scr4[i]; volCol_[i] = col4[i]; }
    }

    // Render-scale (FSR/SSAA) reroutes view 0 through a non-native off-screen target, which only makes
    // sense for the in-game 3D scene (3D on view 0, UI on view 1). UI-only scenes (login/char-select/
    // patcher) draw their UI on view 0 with a native ortho, and SpriteBatch::begin resets view 0's rect
    // to native -- that fights the scaled off-screen and squashes them into a corner. So scenes flag
    // whether the frame is the 3D world; render-scale is suppressed otherwise. HDR/grade are unaffected
    // (their off-screen is native-sized, so the native UI rect matches). Default false = safe (native).
    void setWorldScene(bool v) { worldScene_ = v; }

private:
    void createHdrTarget();   // (re)create the offscreen HDR framebuffer at the current render size
    void destroyHdrTarget();
    void createFsrTargets();  // (re)create the scaled scene FB (sceneW_/H_) + the native resolve FB
    void destroyFsrTargets();
    void createGradeTarget(); // (re)create the RGBA8 grade offscreen (native render size)
    void destroyGradeTarget();
    void createGodrayTarget(); // (re)create the RGBA8 god-ray scene offscreen (native render size)
    void destroyGodrayTarget();
    void recomputeRenderSize();  // sceneW_/sceneH_ = round(native * fsrScale_) for the 3D view
    bool upscaling() const { return fsrScale_ < 0.999f; }     // FSR1 (render below native)
    bool supersampling() const { return fsrScale_ > 1.001f; }  // SSAA (render above native)
    bool hdrActive() const { return hdrOn_ && !fsrActive() && postReady_ && bgfx::isValid(hdrFb_); }
    bool fsrActive() const {
        return worldScene_ && (upscaling() || supersampling()) && postReady_ && bgfx::isValid(fsrFb_) &&
               bgfx::isValid(easuFb_);
    }
    bool gradeNonNeutral() const { return gradeB_ != 0.5f || gradeC_ != 0.5f; }
    bool gradeActive() const {
        return gradeNonNeutral() && !hdrActive() && !fsrActive() && !godrayActive() && postReady_ &&
               bgfx::isValid(gradeProg_) && bgfx::isValid(gradeFb_);
    }
    bool godrayActive() const {  // the shaft composite (sun always, local lights in mode 2)
        return godrayMode_ >= 1 && worldScene_ && !hdrActive() && !fsrActive() && postReady_ &&
               bgfx::isValid(godrayFb_) &&
               bgfx::isValid(godrayMode_ >= 2 ? vollightProg_ : godrayProg_);
    }

    int width_ = 0;   // native window / backbuffer size (what everything renders + hit-tests at)
    int height_ = 0;
    int sceneW_ = 0, sceneH_ = 0;  // off-screen 3D-view size (= native * fsrScale_); <native = FSR, >native = SSAA
    bool vsync_ = true;
    bool inited_ = false;
    bool worldScene_ = false;  // true only while the in-game 3D scene renders (gates render-scale)
    u32 clearColor_ = 0x202830ffu;  // default until a map sets its sky colour

    // Post-process resources (HDR tonemap + FSR upscale).
    bool postReady_ = false;  // programs + fullscreen geometry loaded
    bool hdrOn_ = false;      // user toggle
    float fsrScale_ = 1.0f;   // 1 = off
    bgfx::FrameBufferHandle hdrFb_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle fsrFb_ = BGFX_INVALID_HANDLE;   // low-res scene target (RGBA8 + depth)
    bgfx::FrameBufferHandle easuFb_ = BGFX_INVALID_HANDLE;  // native EASU output (RGBA8)
    bgfx::FrameBufferHandle gradeFb_ = BGFX_INVALID_HANDLE;  // RGBA8 native scene target (grade only)
    bgfx::FrameBufferHandle godrayFb_ = BGFX_INVALID_HANDLE;  // RGBA8 native scene target (god rays, #117)
    bgfx::ProgramHandle tonemapProg_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle easuProg_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle rcasProg_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle gradeProg_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle godrayProg_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle vollightProg_ = BGFX_INVALID_HANDLE;  // Rays mode: sun + per-light shafts (#117 B v2)
    bgfx::UniformHandle sTex_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uTonemap_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uFsr_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uGrade_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uGodray_ = BGFX_INVALID_HANDLE;   // xy=sun uv, z=intensity, w=decay (#117)
    bgfx::UniformHandle uGodray2_ = BGFX_INVALID_HANDLE;  // x=density, y=weight, z=bright threshold
    bgfx::UniformHandle uVolParams_ = BGFX_INVALID_HANDLE;  // x=count, y=intensity, z=density, w=decay
    bgfx::UniformHandle uLightScr_ = BGFX_INVALID_HANDLE;   // vec4[kMaxVolLights]: xy=screen uv, z=visible
    bgfx::UniformHandle uLightCol_ = BGFX_INVALID_HANDLE;   // vec4[kMaxVolLights]: rgb
    int godrayMode_ = 0;  // 0 off, 1 glow, 2 rays
    float godraySunU_ = 0.5f, godraySunV_ = 0.5f;
    bool godraySunVisible_ = false;
    float volScr_[kMaxVolLights * 4] = {0};
    float volCol_[kMaxVolLights * 4] = {0};
    int volLightCount_ = 0;
    float gradeB_ = 0.5f, gradeC_ = 0.5f;  // brightness/contrast, 0.5 = neutral
    bgfx::VertexBufferHandle fsTri_ = BGFX_INVALID_HANDLE;  // fullscreen triangle
    int fbW_ = 0, fbH_ = 0;   // size the current hdrFb_ was created at
    int fsrW_ = 0, fsrH_ = 0;  // size the current fsrFb_ was created at
};

} // namespace uaro
