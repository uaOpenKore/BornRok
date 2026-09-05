#pragma once
#include <bgfx/bgfx.h>

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Types.hpp"
#include "core/math/Math.hpp"
#include "formats/Act.hpp"
#include "formats/Image.hpp"
#include "formats/Imf.hpp"
#include "formats/Spr.hpp"
#include "render/Texture.hpp"

namespace uaro {

class Vfs;
class SpriteBatch;
struct ComposedQuad;

// Per-frame context for drawing actors as world-space, camera-facing billboards
// in the 3D pass, so the map depth-buffer occludes them. right/up are the camera
// axes in world space; the program is the alpha-cutout model shader.
struct WorldSpritePass {
    bgfx::ViewId view = 0;
    bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle bias = BGFX_INVALID_HANDLE;  // vec4.x = per-layer clip-space depth bias
    bgfx::UniformHandle fade = BGFX_INVALID_HANDLE;  // vec4.x = sprite alpha (corpse dissolve)
    float biasScale = 1.0f;  // scales the clip-space bias down as the camera zooms out
    const bgfx::VertexLayout* layout = nullptr;
    Vec3 right{1, 0, 0};
    Vec3 up{0, 1, 0};
    Vec3 toCamera{0, 0, 1};  // unit dir actor->camera; nudges the billboard forward
    // Lit-sprite path (S.: sprite normal maps): when litProgram is valid, non-additive quads
    // draw with vs_sprite3d + fs_spritelit, binding a per-frame luminance-derived normal map
    // on slot 1 and the sun dir (billboard/tangent space, w = strength) as a uniform.
    bgfx::ProgramHandle litProgram = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle nrmSampler = BGFX_INVALID_HANDLE;   // s_nrm
    bgfx::UniformHandle lightUniform = BGFX_INVALID_HANDLE; // u_spriteLight
    float lightTS[4] = {0, 0, 1, 0};  // xyz = sun dir in billboard space, w = strength (0 = off)
};

// Which motion group of the .act to play. The RO action index is motion*8 + dir;
// idle/walk are motion 0/1 for every actor, but attack/hurt/dead differ by actor
// type (PC bodies: attack=5, hurt=6, sit=2, dead=8; monster sprites: attack=2,
// hurt=3, dead=4), so the mapping lives in renderWorld where the type is known.
// Effect = a single-action looping sprite (e.g. the warp-portal swirl): motion 0,
// played as a continuous loop rather than the static idle frame.
enum class Anim { Idle, Walk, Attack, Hurt, Sit, Dead, Effect, Cast, Ready };

// Register a GRF-driven fallback for player body-sprite folder names: called once by Application
// after the GRF's job-name lua tables load. Classes absent from CharacterActor's hardcoded table
// (3rd/4th jobs etc.) then resolve their sprite from the GRF instead of collapsing to novice.
void setJobSpriteResolver(std::function<std::string(u16)> resolver);

// A composed player character sprite: the job body + the hairstyle head, loaded
// from the GRF (data/sprite/<인간족>/...), assembled via SpriteComposer and drawn
// through SpriteBatch. First pass: idle action, facing south, embedded palettes
// (clothes/hair dye, headgear and other directions come later). Used by the
// char-select slots now and the in-world player later.
class CharacterActor {
public:
    CharacterActor() = default;
    // Move-only: owns bgfx texture handles. The destructor frees them (idempotent
    // destroy()), so actors can live in containers and be erased safely. A default
    // move leaves the source's caches empty, so its destructor is a no-op.
    ~CharacterActor() { destroy(); }
    CharacterActor(const CharacterActor&) = delete;
    CharacterActor& operator=(const CharacterActor&) = delete;
    CharacterActor(CharacterActor&&) = default;
    CharacterActor& operator=(CharacterActor&&) = default;

    // class id + account sex (1=male,0=female) + hairstyle id, plus the three
    // headgear view ids (bottom/mid/top, 0 = none). Returns false if the body
    // sprite could not be loaded (head and headgear are optional).
    bool load(const Vfs& vfs, u16 classId, u8 sex, u16 hair, u16 headBottom = 0, u16 headMid = 0,
              u16 headTop = 0, bool riding = false, u16 weapon = 0, u16 shield = 0,
              u16 hairColor = 0, u16 clothColor = 0);

    // Load a single NPC/monster sprite (no head) by base name, trying the npc/ and
    // monster/ sprite folders. For non-player actors. Returns false if not found.
    // Load a single (non-composed) sprite by name. classId selects the sprite FOLDER the
    // way roBrowser's DB.getBodyPath does: a monster (1000-3999) comes from the 몬스터
    // folder, an NPC (46-999) from npc/. npc/ holds wrong placeholder copies for some
    // monsters (e.g. npc/drainliar is a blue familiar), so monsters must NOT read npc/
    // first. classId < 0 keeps the legacy order (cursor / portal / map effects).
    // actName lets a caller load a sprite whose .act has a different base name than its
    // .spr (the falcon ships as ht_falcon.spr + h_falcon.act). Empty => act shares name.
    bool loadActor(const Vfs& vfs, const std::string& name, int classId = -1,
                   const std::string& actName = "");
    void destroy();
    // Free the process-wide shared frame-texture cache. MUST be called once during shutdown while
    // bgfx is still alive (before bgfx::shutdown), else the static maps destroy handles post-shutdown.
    static void clearSharedCache();
    // Drop ONLY the mob/NPC shared cache (parsed .spr/.act + frame/normal textures + meta, all keyed
    // "M:..."), keeping the player-appearance ("P...") entries. Called on a map change so each location
    // caches its own mobs/NPCs and frees the previous map's (S. 2026-07-16). bgfx must be up.
    static void clearActorCache();
    // Every player body (all jobs x both sexes) + hairstyle head .spr/.act vpath, for warming the
    // VFS byte cache on map entry so WoE crowds compose without a GRF-inflate hitch (S.).
    static std::vector<std::string> playerPartPaths();
    bool ready() const { return spr_[0] && act_[0]; }
    // Like ready(), but also requires the body sprite to actually have renderable frames. The mouse
    // cursor uses this: a broken/empty 'cursors' texture (e.g. an undecodable quality-pack override on
    // the 1k content set) can leave spr_/act_ set -> ready()==true -> we hide the OS cursor -> but the
    // sprite draws NOTHING -> no cursor at all. Gating the OS-cursor hide on renderable frames keeps the
    // system cursor as a fallback instead of leaving the user cursorless. (S. "на 1к текстурах пропадает")
    bool hasRenderableBody() const;

    // Draw the idle frame facing `dir` (0..7, RO direction) with the actor's FEET
    // at (x,y). `pxPerWorldUnit` is the on-screen pixel length of one world cell at
    // the actor's depth (projected by the caller): the sprite is sized to a fixed
    // world height from it, so the character stays correctly proportioned to the
    // map at any zoom/resolution. Use renderScaled() (plain scale, faces south) for
    // the 2D char-select slots.
    void render(SpriteBatch& sb, float x, float y, float pxPerWorldUnit, u8 dir, double time);
    void renderScaled(SpriteBatch& sb, float x, float y, float scale, double time);

    // Draw a chosen (action, frame) of a single-sprite actor in 2D at (x,y), scaled, for
    // UI sprites like the mouse cursor where the caller drives the action and frame (the
    // frame is wrapped to that action's length). Only the body part (0) is composed.
    void renderActionFrame(SpriteBatch& sb, float x, float y, int action, int frame, float scale);

    // 3D path: draw the actor as a depth-tested billboard with its feet at
    // `feetWorld`, facing `dir`. Frame timing comes from the ACT per-action delay:
    // Idle/Sit hold a static frame, Walk loops on a continuous clock, and
    // Attack/Hurt/Dead play ONCE from `animStart` (the wall-clock time the pose began)
    // and clamp to the last frame — so a swing runs front-to-back in step with the body
    // and a corpse freezes on its final death frame. `time` is the current clock.
    // Sized to a fixed world height, so perspective scales it and buildings occlude it.
    // `depthBias` pulls the whole sprite toward the camera in clip space: 0 = honest
    // depth (buildings occlude it — the default for NPCs/mobs/other players, so they
    // no longer show through walls); a small positive value keeps the OWN player drawn
    // in front of a building between it and the camera (so it is never hidden).
    // `alpha` (< 1) fades the whole sprite via the billboard's fade shader + alpha
    // blending — used to dissolve a dying actor's corpse out over its linger.
    // `sizeScale` enlarges the billboard about its feet/centre (1 = natural size);
    // used to draw the warp-portal effect bigger than a character, like roBrowser's
    // /effect warp.
    // originAnchor: place the sprite's ACT origin at `feetWorld` (the map-author's point)
    // instead of recentring on the visible pixels and standing it on its feet. Used for
    // RSW effect sprites (torch flames) so they sit exactly where they were authored —
    // in the brazier bowl — with their designed lean, the way roBrowser draws effects.
    // additive: blend the sprite additively (BGFX_STATE_BLEND_ADD) so it glows like an RO
    // flame/light effect — used for the torch/brazier fire (S.: "спрайт огня неправильный").
    // onTop: the caller's feet-visibility test found nothing between the camera and this actor's
    // feet, so draw the WHOLE sprite over the world (DEPTH_TEST_ALWAYS) — the fix for camera-
    // facing heads clipping into a wall the actor stands in front of (#36). When false the sprite
    // is depth-tested normally (LEQUAL) so a wall it stands behind still hides it.
    // roll rotates the billboard within its plane (radians, CCW on screen). 0 = upright.
    // Used for the flying arrow, whose single sprite frame must point along its flight
    // (the .act encodes direction only as a per-layer rotation the quad path doesn't apply).
    void renderWorld(const WorldSpritePass& pass, const Vec3& feetWorld, u8 dir, Anim anim,
                     double time, double animStart = 0.0, float depthBias = 0.0f,
                     float alpha = 1.0f, float sizeScale = 1.0f, bool originAnchor = false,
                     bool additive = false, bool onTop = false, bool flat = false,
                     float roll = 0.0f,
                     // Environment light (0..1 rgb) of the ground cell the actor stands on; the ACT tint
                     // is multiplied by it so a sprite in a building's shadow is dimmed (#118). Default
                     // white = no dimming (effects/warps pass this).
                     const Vec3& envLight = Vec3{1.0f, 1.0f, 1.0f},
                     // Frame interpolation (S.): when true, the drawn frame's layer transforms are
                     // blended toward the next frame by the sub-frame progress (smooth motion; the
                     // bitmap still switches at the boundary). Off = the classic snap. Actors pass
                     // app.animInterp(), effect sprites pass app.fxInterp().
                     bool interpolate = false);

    // World-space extent of the last drawn frame's BODY sprite, measured from feetWorld
    // along the camera up (Y) / right (half-width). RO sprites grow UPWARD from the feet
    // and a floating mob (drainliar) sits ABOVE its cell via the .act offset, so a click
    // box pinned to the ground cell lands below the image. renderWorld records the real
    // drawn extent here each frame (like roBrowser's per-frame boundingRect) so pickActor
    // can size the hit box to the sprite. Defaults: a ~1.4u grounded body until first drawn.
    float pickLoY() const { return pickLoY_; }   // sprite bottom above feet (0 grounded, >0 floating)
    float pickHiY() const { return pickHiY_; }   // sprite top above feet
    float pickHalfW() const { return pickHalfW_; }

    // Frame interpolation for this sprite (S.): the caller sets it from app.animInterp() (actors) or
    // app.fxInterp() (effects) each frame; renderWorld interpolates when it (or its `interpolate` arg)
    // is set. Default false = classic snap.
    void setFrameInterp(bool on) { frameInterp_ = on; }
    // Companion sprites like the merchant PUSHCART must be STATIC when the owner stands still (RO spins
    // the wheels only while walking). By default a non-player sprite loops its idle in place (poring
    // bounce / falcon flap); set this so its Idle holds frame 0 instead. Walk still animates. (S.)
    void setHoldIdle(bool on) { holdIdle_ = on; }

private:
    Texture& frameTex(int part, int idx, bool indexed);
    // Per-frame normal map generated from the frame's own luminance (bump-from-diffuse); used
    // by the lit-sprite path. Cached like frameTex; returns failedTex_ (invalid) on failure.
    Texture& frameNrmTex(int part, int idx, bool indexed);
    void buildQuads(int action, int frameSeed, std::vector<ComposedQuad>& out,
                    int frameSeedNext = -1, float t = 0.0f);
    void drawQuads(SpriteBatch& sb, const std::vector<ComposedQuad>& quads, float x, float y,
                   float scale);
    // Compose one optional part (head / headgear) anchored to the body anchor `ba`.
    void composePart(int part, int action, int frameSeed, const std::array<i32, 2>& ba,
                     std::vector<ComposedQuad>& out, int frameSeedNext = -1, float t = 0.0f,
                     bool bodyAnchorPresent = true);
    bool loadHeadgear(const Vfs& vfs, int part, u16 viewId, const std::string& sx);
    // PNG frame override (#109 level 1): probe "<base>.png.d/<i>.png" for every INDEXED frame of
    // spr_[part] and stash the decoded images; frameTex prefers them over the palette expansion.
    // The PNG may be an integer multiple of the frame size (hi-res) — the quad keeps the .spr
    // logical size, only the texture gets denser. Overridden frames ignore .pal dyes (truecolor).
    void loadPngOverrides(const Vfs& vfs, int part, const std::string& base);
    // Lowest OPAQUE pixel of the composed body (part 0) for a given action/frame, in composed-sprite
    // space (q.y + content-bottom row). Used only for the peco mount: its bbox bottom (q.y+q.h) drifts
    // frame-to-frame because the peco frames pad transparent space below the feet differently
    // front/back/mid-step, so grounding by bbox made the mounted char "jump" while walking (S.). Cached
    // per (action,frame) in contentBottomCache_; magenta (truecolor peco key) counts as transparent.
    float bodyContentBottomPx(int action, int frame);
    // Steady ground line for the riding body: the LOWEST content-bottom across all frames of the
    // dir-0 walk action, so the peco stands on one fixed line and never bobs/floats. Cached per action.
    float ridingGroundPx(int refAction, int nframes);
    std::unordered_map<int, float> contentBottomCache_;  // action*10000+frame -> content-bottom px
    std::unordered_map<int, float> ridingGroundCache_;   // refAction -> steady peco ground px

    // Sprite parts, by composition order: 0 body, 1 head, 2/3/4 headgear
    // bottom/mid/top, 5 weapon (the equipped right-hand sprite), 6 shield (the
    // equipped left-hand sprite — drawn BEHIND the body for back-facing octants,
    // in FRONT for front-facing ones, like roBrowser). All animated in sync with
    // the body. Frame textures live in a PROCESS-WIDE shared cache keyed by appearanceKey_ (below),
    // so N identical actors (esp. same-class mobs in a crowd) reference ONE texture set instead of
    // uploading N copies -- this is what keeps the bgfx texture pool from exhausting on a large view
    // radius (S.: area_size 100 flooded the client). See CharacterActor.cpp's s_frameTex/s_nrmTex.
    // 0 body, 1 head, 2 headgear-bottom, 3 headgear-mid, 4 headgear-top, 5 weapon, 6 shield,
    // 7 weapon-trail (검광 gleam overlay, shown only during the swing — its non-attack frames are empty).
    static constexpr int kParts = 8;
    // Parsed .spr/.act held via shared_ptr so identical actors reference ONE parsed copy instead of N
    // (frame TEXTURES were already shared; this shares the parsed .spr pixels + .act too). Mobs/NPCs
    // (loadActor, no per-instance mutation) fetch from a process-wide cache keyed by asset path; player
    // parts get their own copy (they are dyed/mutated in-place -> sharing would leak one PC's palette
    // onto others). (S.: optimise RAM for crowds)
    std::shared_ptr<Sprite> spr_[kParts];
    std::shared_ptr<Action> act_[kParts];
    std::shared_ptr<Imf> imf_;   // optional data/imf/<job>_<sex>.imf: per-frame body layer/attach adj
    std::string appearanceKey_;                   // identity string (all pixel-affecting load params) -> shared-cache prefix
    std::unordered_map<int, Image> pngOverride_;  // part*100000 + indexed idx -> decoded PNG (#109)
    Texture failedTex_;  // invalid placeholder returned UNCACHED when a frame upload fails, so the
                         // next frame retries instead of caching a dead handle that hides the unit (#95)
    bool frameInterp_ = false;  // smooth frame interpolation for this sprite (S., set per frame by caller)
    bool holdIdle_ = false;     // non-player companion (cart) holds idle frame 0 instead of looping (S.)
    bool isPlayer_ = false;  // composed PC body vs single mob/NPC (attack motion 2)
    bool riding_ = false;    // body is a peco mount -> its walk plays 2x faster (S.)
    // An armed PC stands in the READYFIGHT pose and attacks with a weapon-specific
    // variant (ATTACK1/2/3 = motion 5/10/11), where the equipped-weapon sprite actually
    // has frames — its idle/attack1 motions are empty. Detected from the weapon sprite in
    // load() (data-driven equivalent of roBrowser's getWeaponAction + READYFIGHT idle).
    bool armed_ = false;     // a weapon sprite is loaded for part 5
    int idleMotion_ = 0;     // resting pose: 4 (ready, weapon shown) when armed, else 0
    int attackMotion_ = 5;   // the attack motion whose frames actually carry the weapon
    float pickLoY_ = 0.0f, pickHiY_ = 1.4f, pickHalfW_ = 0.5f;  // see pickLoY()/pickHiY()/pickHalfW()
};

} // namespace uaro
