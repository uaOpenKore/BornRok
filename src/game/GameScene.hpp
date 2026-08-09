#pragma once
#include <bgfx/bgfx.h>

#include <string>
#include <set>
#include <unordered_map>
#include <vector>

#include "app/Scene.hpp"
#include "core/math/Math.hpp"
#include "game/CharacterActor.hpp"
#include "game/MapRenderer.hpp"
#include "game/StrEffect.hpp"
#include "game/HomunAi.hpp"
#include "game/Particles.hpp"
#include "game/RomActor.hpp"
#include "game/Gr2Models.hpp"
#include "formats/UnityParticle.hpp"
#include "game/ShadowRenderer.hpp"
#include "game/LightGlowRenderer.hpp"
#include "game/SettingsMenu.hpp"
#include "net/Connection.hpp"
#include "net/Protocol.hpp"
#include "ui/Widgets.hpp"

namespace uaro {

struct InputState;

// Glides an actor along a cell path (A* over the GAT), lerping each segment from
// `from` to `to` between `start` and `end` (seconds), then advancing to the next
// waypoint — so the actor rounds obstacles instead of cutting straight through.
struct Mover {
    std::vector<Vec3> waypoints;  // world points to pass through, in order
    usize idx = 0;                // index of the current target waypoint
    Vec3 from{0, 0, 0}, to{0, 0, 0};
    double start = 0, end = 0;
    double secPerCell = 0.15;  // glide time per cell, from the unit's server speed (ms/1000)
    double cellsPerWorld = 2.0;  // GAT cells per world unit (~2): converts world dist -> cell count
    bool active = false;
    u8 endDir = 0;  // RO facing of the final step (so the unit idles facing it)
};

// One other on-map unit we track and draw. Players get a composed body+head
// sprite; NPCs/monsters use a single sprite resolved from their view id.
struct MapActor {
    net::ActorEntry info;
    Vec3 pos{0, 0, 0};       // world position (cached on each entry update)
    Mover move;              // active glide, if walking
    CharacterActor sprite;
    std::string romName;     // mob model name for the ROeM route (lowercased sprite name)
    std::string spriteName;  // resolved 2D mob/NPC sprite name (for the failed-load retry); "" for PCs
    std::string gr2Path;     // non-empty: draw this Granny 3D model (flags/guardians/emperium) instead of a sprite
    bool spriteTried = false;
    double attackUntil = 0;  // play the attack motion while time < this (set on ZC_NOTIFY_ACT)
    double attackStart = 0;  // when the swing began (its duration varies: brief for mobs, 3s for PCs)
    double hurtUntil = 0;    // play the hurt/flinch motion while time < this (on taking damage)
    double hurtStart = 0;    // when the flinch began (duration = dmotion from the 0x8a packet)
    double dyingUntil = 0;   // >0: playing the death motion; erase once time passes it
    bool sitting = false;    // sit/stand posture, reconciled from ZC_NOTIFY_ACT (type 2/3)
};

// A skill ground unit we draw as a sprite (currently only the warp-portal swirl).
// Keyed by the unit's gid so ZC_SKILL_DISAPPEAR can remove it.
struct WarpUnit {
    Vec3 pos{0, 0, 0};
    CharacterActor sprite;
    double lastEmit = 0.0;  // throttle for the rising-particle composite (#137)
};

// A non-warp skill ground unit (trap / wall / zone: Fire Wall, Sanctuary, Pneuma, Quagmire,
// Ankle Snare, ...). We loop the skill's ground .str at the cell for its lifetime. Keyed by gid
// so ZC_SKILL_DISAPPEAR (0x120) removes it. unitId is the server UNT_* type byte.
struct GroundUnit {
    Vec3 pos{0, 0, 0};
    u8 unitId = 0;
    double born = 0.0;      // scene time the unit appeared (phase the looping effect)
    double lastEmit = 0.0;  // last time this unit emitted coded particles (bard/dancer song throttle)
};

// The in-game scene: connects to the map-server (address from char-select),
// performs the zone handshake (wanttoconnection -> account id -> ZC_ACCEPT_ENTER
// -> loadendack), renders the spawn map with a player-centred camera and a marker
// at the spawn tile, and keeps the link alive with periodic ticksend packets.
// Actor sprites, movement and the full packet stream land in later increments.
class GameScene : public Scene {
public:
    void onEnter(Application& app) override;
    void onExit(Application& app) override;
    void update(Application& app, double dt) override;
    void render(Application& app) override;

private:
    enum class Phase { Connecting, WaitAuth, InGame, Failed };

    void pumpAuth(Application& app);
    void pumpStream(Application& app);                 // frame + handle post-spawn packets
    void upsertActor(Application& app, const net::ActorEntry& e);
    // Play the one-shot onset effect + sound when a body/health ailment newly turns on for an
    // actor (roBrowser EntityState: freeze/stun/poison/curse/blind etc.). Compares old vs new
    // opt1/opt2 so it fires only on the transition, not on every state-change packet.
    void fireAilmentOnset(Application& app, const Vec3& at, u16 oldOpt1, u16 oldOpt2,  // gid: follow unit
                          u16 newOpt1, u16 newOpt2, u32 gid = 0);
    void changeMap(Application& app, const net::MapChange& mc);  // warp: reload map / reconnect
    void computePlayerPos();
    Vec3 cellToWorld(u16 cx, u16 cy) const;            // GAT cell -> world (with ground height)
    float surfaceHeight(float wx, float wz) const;     // world XZ -> walkable GAT surface (world Y)
    bool screenToCell(int mx, int my, u16& cx, u16& cy) const;  // unproject a click to a GAT cell
    void startMove(Mover& m, const Vec3& from, u16 srcX, u16 srcY, u16 dstX, u16 dstY,
                   bool route = true, u16 speedMs = 150);
    void requestWalk(u16 dstX, u16 dstY);  // start walking to a (possibly far) clicked cell
    void sendNextWalkSegment();            // ask the server for the next reachable hop
    bool worldToCell(const Vec3& w, u16& cx, u16& cy) const;  // world point -> GAT cell
    void faceActor(u32 gid);  // turn the player to face a target actor (attack/skill, #combat facing)
    bool cellWalkable(u16 cx, u16 cy) const;  // can a unit stand on this GAT cell? (server rule)
    void initSpritePass(Application& app);             // load the billboard shader/sampler/layout
    void destroySpritePass();
    // Play a positional SFX (data/wav/<name>) at a world point, attenuated by distance to the
    // player so far-off combat is quieter (#103). No-op when built/running without audio.
    void playWorldSfx(Application& app, const std::string& name, const Vec3& at, float gain = 1.0f);
    void playSkillCastSfx(Application& app, u16 skillId, const Vec3& at);  // #144 per-cast skill sound
    void updateMapBgm(Application& app);  // switch BGM to the current map's track (idempotent) (#103)
    void applySkyColor(Application& app);              // background clear from the map's RSW light
    u32 pickActor(int mx, int my, float mag = 1.0f) const;  // gid under the cursor (mag>1 = magnet radius)
    bool actorScreenPos(u32 gid, float& sx, float& sy) const;  // a unit's mid-body screen pos (cursor magnetism)
    bool selfScreenPos(float& sx, float& sy) const;  // the player's own char screen pos (buff magnetism)
    u32 pickGroundItem(int mx, int my) const;          // id of the floor item under the cursor, or 0
    bool isMobActor(u32 gid) const;                    // is this gid an attackable monster?
    bool isEnemyPlayer(u32 gid) const;                 // an attackable enemy player (PvP/GvG map) (S.)
    int mapPvpType_ = 0;                               // 0 normal, 1 PvP, 2 GvG/WoE (from 0x199)
    bool isNpcActor(u32 gid) const;                    // is this gid a talk-to NPC (RMB dialog)?
    bool isWarpActor(u32 gid) const;                   // is this gid a warp portal (class 45)?
    void initOverlayPass(Application& app);            // load the ground-decal (cursor/path) shader
    void destroyOverlayPass();
    void drawGroundOverlays(const InputState& in);     // hovered-cell cursor + walk-path line
    bool handleChatCommand(Application& app, const std::string& msg);  // /sit etc.; true if handled
    const ui::UiImage* itemIcon(Application& app, u32 nameid);  // shop icon (loaded+cached), or null
    const ui::UiImage* itemCollection(Application& app, u32 nameid);  // large collection illustration (desc popup), or null
    const ui::UiImage* skillIcon(Application& app, const std::string& name);  // skill icon, cached
    const ui::UiImage* statusIcon(Application& app, u16 si);  // status-effect .tga icon, cached (or null)
    void drawStatusIcons(Application& app);  // active-status icon row under the minimap
    void requestGuildEmblem(Application& app, u32 guildId, u32 version);  // fetch a guild emblem once
    void setGuildEmblem(const net::GuildEmblem& e);  // decode + cache a received emblem bitmap
    void loadMinimap(Application& app);  // decode the current map's minimap bitmap (once per map)
    void drawMinimap(Application& app);  // top-right minimap overlay + player marker
    void drawBasicInfo(Application& app);  // top-left BasicInfo panel (Alt+V: full/short/collapsed)
    void drawHotbar(Application& app);   // quick-slot / hotkey bar (12 slots x 1..4 rows)
    bool hotbarSlotAt(int mx, int my, int& row, int& col) const;  // slot under the cursor, for drops
    std::string hotbarCfgPath(Application& app) const;  // settings/<login>-<charName>.cfg ("" if unknown)
    void loadHotbar(Application& app);   // restore the quick-slot layout from disk on entry
    void saveHotbar(Application& app);   // mark the layout dirty (debounced); write happens in update()
    void writeHotbar(Application& app);  // actually write the layout to disk (the real save)
    void activateSkill(Application& app, u16 id, u16 level);  // self-cast now or enter target mode (by inf)
    void resolveSkillTarget(Application& app);  // a target-mode click: cast on the unit/cell under cursor
    void queueSkill(u32 gid, bool ground, u16 cx, u16 cy);  // cast now if in range, else walk in and fire
    void tickPendingSkill();                                // per-frame: close on the target, fire in range
    void spawnMapEffects(Application& app);  // spawn RSW effect sprites (torch flames) for the map
    // Draw + PRESENT one "Loading, please wait" frame. Map loading (renderer_.load) is synchronous and
    // blocks the whole frame, so a render()-driven overlay can never appear during it; this presents a
    // standalone frame BEFORE the blocking load so the player sees a loading screen on map entry + warps
    // (S. 2026-07-29: "во время загрузки карты ... нужно вешать экран: Загрузка, пожалуйста ожидайте").
    void drawLoadingOverlay(Application& app);

    MapRenderer renderer_;
    CharacterActor player_;
    CharacterActor falcon_;        // hunter/sniper falcon (data/sprite/아이템/ht_falcon), drawn when OPTION_FALCON
    bool falconLoaded_ = false;    // lazy-load guard (falcon sprite is map-independent)
    // Blitz Beat (129): the falcon leaves the hunter, dives at the target and pecks, then returns
    // (S.: "должен сокол атаковать цель"). While active the falcon renders along this flight path
    // playing its Attack motion instead of perching on the char. Own-cast only (we only draw our own).
    double lastBlitzAt_ = 0.0;     // dedicated dive debounce (the SHARED lastFxSkill_/lastBurstAt_ is set
                                   // by the element-burst fallback for 129/381 first, which was suppressing
                                   // the dive's own debounce -> falcon never flew)
    double blitzUntil_ = 0.0;      // time_ the dive ends (0 = falcon perches normally)
    double blitzArrive_ = 0.0;     // time_ the falcon reaches the target (flight end = cast end); peck after
    double blitzStart_ = 0.0;      // dive start (Attack animStart)
    Vec3   blitzTarget_{};         // target cell the falcon dives to
    CharacterActor cartSprite_;    // merchant pushcart (data/sprite/이펙트/수레<N>), drawn behind on OPTION_CART
    u16 cartLoadedLevel_ = 0;      // which 수레<N> is currently loaded into cartSprite_ (0 = none loaded yet)
    CharacterActor arrow_;         // flying-arrow projectile sprite (data/sprite/npc/skel_archer_arrow)
    bool arrowLoaded_ = false;     // lazy-load guard for the arrow sprite
    CharacterActor boomerang_;     // CR_SHIELDBOOMERANG (251) flying shield (data/sprite/아이템/cr_shieldboomerang)
    bool boomerangLoaded_ = false; // lazy-load guard for the shield-boomerang sprite
    // Level-up .str effects (#64): angel = base level, joblvup = job level. Lazy-loaded once
    // (effect assets are map-independent), then replayed per ZC_NOTIFY_EFFECT at the actor.
    StrEffect levelFxBase_, levelFxJob_;
    bool levelFxBaseLoaded_ = false, levelFxJobLoaded_ = false;
    struct LevelFx {
        bool job;     // false = base (angel), true = job (joblvup)
        Vec3 pos;     // fixed at the actor's position when the level-up fired
        double born;  // time_ at spawn; pruned when time_-born exceeds the effect duration
    };
    std::vector<LevelFx> levelFx_;
    // Per-skill .str visual effects, played at the target on cast (reuses the level-up renderer).
    struct SkillFx {
        Vec3 pos;            // fallback anchor (ground effects, or the unit's last-known pos if it despawns)
        double born;
        const char* effect;  // .str name (static literal from skillFxDef)
        u32 gid = 0;         // follow this unit (caster/target) if non-zero; 0 = ground-anchored (S. 2026-07-16)
    };
    std::vector<SkillFx> skillFx_;
    // #146 delayed coded bursts (e.g. Magnificat's 2nd flower fires 0.5 cycle after the 1st). Fired by
    // emitSkillBurst when time_ reaches `at`.
    struct PendingBurst { u16 skillId; Vec3 pos; double at; };
    std::vector<PendingBurst> pendingBursts_;
    std::unordered_map<std::string, StrEffect> skillFxCache_;  // .str name -> loaded effect (cached)
    StrEffect* skillEffect(Application& app, const char* name);  // load+cache a .str by name, or null
    // Build the coded skill effects reproduced from the exe (magnum/bash) into skillFxCache_ once, so
    // skillFx_ can play them by name ("coded_magnum"/"coded_bash"). No-op after the first call.
    void ensureCodedFx(Application& app);
    void spawnMagnum(Application& app, const Vec3& at);  // ring of fire billboards + central blast
    bool codedFxBuilt_ = false;
    // Play a multi-file "%d" effect sequence (effectFxSeq id, e.g. 49 FireHit1..3) at `at`: one
    // SkillFx per frame, back-to-back, + the frame-0 .wav. Reused by the 0x1f3 handler and the
    // skill-damage path (element hit of a bolt spell). No-op if the id has no sequence.
    void playEffectSeq(Application& app, u16 effectId, const Vec3& at);
    // Coded skill effects with no .str (Magnum Break fire ring, Grimtooth ground spikes): a particle
    // burst drawn as additive billboards, reusing the world-sprite pass + a generated soft-glow tex.
    ParticleSystem skillParticles_{512};
    bgfx::TextureHandle particleTex_ = BGFX_INVALID_HANDLE;
    bool particleTexReady_ = false;
    double lastBurstAt_ = 0.0;  // debounce a multi-hit coded burst (magnum fires one 0x1de per victim)
    float statusAuraPhase_ = 0.0f;  // throttle timer for the persistent status-aura puffs (#146)
    u16 lastFxSkill_ = 0;       // last skill that spawned a burst/RoM effect (debounce key)
    std::unordered_map<u32, u16> castFxFired_;  // gid->skillId whose coded flower already fired at cast-
                                                // begin (0x13e), so the matching 0x11a doesn't re-fire it
                                                // (S.: "аги и эффект цветка должен начинаться с начала каста")
    double lastSkillSfxAt_ = -1e9;  // debounce the per-cast skill sound (one 0x1de per victim otherwise)
    u16 lastSkillSfxId_ = 0;
    // Spawn a coded skill particle burst. part: 0=all, 1=ground cast circle only (fire at the CASTER),
    // 2=effect only, no ground circle (fire at the TARGET). S.: "круг под ногами у кастующего, эффект на цели".
    void emitSkillBurst(Application& app, u16 skillId, const Vec3& at, int part = 0);
    void emitElementBurst(Application& app, u8 element, const Vec3& at, bool flash = true, int sparkMul = 1);  // #144 element-based fallback burst (flash=false: sparks only; sparkMul scales spray)
    void drawSkillParticles(const WorldSpritePass& pass);  // additive billboard pass over live particles
    // RoM Unity ParticleSystem effects (#132): a skillId -> RoM effect bundle binding drives our CPU
    // particle engine from the parsed ParticleSystem (colour/size/lifetime/speed/gravity). Emitted
    // effects live for their authored duration, dripping particles into skillParticles_.
    std::unordered_map<u16, std::string> skillFxNames_;         // skillId -> RoM effect base name
    std::unordered_map<u16, std::string> itemFxNames_;          // itemId -> RoM effect (on item use, S.)
    std::unordered_map<std::string, UnityParticleDesc> romFxDescs_;  // base name -> parsed desc (cached)
    struct ActiveRomFx {
        const UnityParticleDesc* desc = nullptr;
        Vec3 at;
        float t = 0.0f;    // seconds since spawn
        float acc = 0.0f;  // fractional particles owed to the stream emitter
        int texId = -1;    // registry index of the effect's real RoM texture (-1 = soft glow)
        bool beam = false; // ray/lightning/line effect -> velocity-stretched billboards
    };
    std::vector<ActiveRomFx> activeRomFx_;
    // Real RoM effect textures (#132): resolve effect -> material -> texture via a CAB->bundle index
    // over the fx material/texture bundles, decode ASTC, and billboard the particles with them.
    std::unordered_map<std::string, std::string> romCabIndex_;  // CAB node name -> bundle vpath
    bool romCabIndexBuilt_ = false;
    std::vector<bgfx::TextureHandle> romFxTextures_;         // registry; Particle.fxTex indexes this
    std::unordered_map<std::string, int> romEffectTexId_;    // effect name -> registry index (-1 = none)
    std::unordered_map<std::string, bool> romEffectBeam_;    // effect name -> beam (stretched) look
    std::unordered_map<std::string, int> codedFxTex_;        // data/texture/effect/<name> -> registry index
    // Load a plain GRF effect texture (e.g. ring_yellow.tga) into romFxTextures_ for Particle.fxTex.
    int codedFxTexId(Application& app, const char* effName, bool flipV = false, bool cornerKey = false);
    void buildRomCabIndex(Application& app);                 // index fx mat/tex bundles (one-time)
    int romEffectTexId(Application& app, const std::string& effectName);  // resolve+decode+cache
    void loadSkillFxBindings();  // built-in starter table + optional rom-skill-effects.txt override
    void loadItemFxBindings();   // rom-item-effects.txt: itemId -> RoM effect, played on item use
    const UnityParticleDesc* loadRomFxDesc(Application& app, const std::string& name);  // parse+cache
    bool emitRomEffect(Application& app, u16 skillId, const Vec3& at);  // true if a binding fired
    bool emitRomEffectByName(Application& app, const std::string& name, const Vec3& at);  // /fx preview
    void updateRomFx(Application& app, float dt);  // advance active effects + stream particles
    // Ambient map effects (#32): RSW effect emitters with a CODED look (no .str) — 44 = chimney
    // smoke, 45 = fireflies, 165 = sparkles. Each placed emitter drips particles into ambientFx_;
    // emitters far from the player are skipped, so dense towns (Prontera has 152 smokes) stay cheap.
    struct AmbientEmitter {
        Vec3 pos;
        int id = 0;        // RSW effect id (44/45/165)
        float period = 0;  // seconds between particle spawns
        float acc = 0;     // accumulated time toward the next spawn (staggered at init)
        int seed = 0;      // per-emitter hash seed so neighbours don't move in lockstep
    };
    std::vector<AmbientEmitter> ambientEmitters_;
    ParticleSystem ambientFx_{2048};
    // ROeM 3D mobs (feat/content-sources): model cache by name; nullptr = negative (missing).
    std::unordered_map<std::string, std::unique_ptr<RomActor>> romCache_;
    RomActor* romActorFor(Application& app, const std::string& name);
    RomActor* romPlayerFor(Application& app, u16 jobClass, u8 sex, u16 hair, u16 weapon,
                           bool riding = false, u16 headTop = 0, u16 headMid = 0,
                           u16 headBottom = 0);  // Chars=ROeM: body+head+weapon+headgear
    RomActor* romMountFor(Application& app);  // the peco mount (#125), cached
    void updateAmbientFx(float dt);  // emit from near emitters + advance the pool
    // Weather (#32): the SERVER drives it — rAthena mapflags (sakura/snow/rain/leaves) make
    // clif_weather_check send ZC_NOTIFY_EFFECT2 (0x1f3) with the weather EF id on map entry.
    // We keep the active set and rain the matching particles around the player.
    std::vector<int> weather_;   // active weather EF ids (161 rain / 162 snow / 163 sakura / 333 leaves)
    float weatherAcc_ = 0;       // fractional particles owed to the emission rate
    int weatherSeed_ = 0;
    // Airship sky clouds (ported from roBrowser Renderer/Effects/Sky.js): the airship maps
    // (airplane/airplane_01) and a few "sky" maps render a field of slowly drifting cloud billboards
    // around the player. Purely client-side, keyed on the map name (same list as skyColorForMap).
    // Alpha-blended (not additive) so a white puff reads over the blue sky. The real cloud1..7 textures
    // are broken in this content pack (webp flattened to solid white), so clouds use the soft-glow blob.
    ParticleSystem cloudFx_{192};
    bool cloudsOn_ = false;
    int cloudSeed_ = 0;
    void updateClouds(Application& app, float dt);  // maintain the drifting cloud field on sky maps
    // Quick-slot / hotkey bar: 5 rows x 12 slots, drawn as 3 groups of 4. Default mode = 1 row;
    // the expand button cycles 1->2->3->4->5. Row r is bound to F1-F12 / QWERTY top / home / bottom
    // / number row (platform -> in.hotkeyRow/Col). Slots are filled by dragging a skill (skill
    // window) or an item (inventory) onto them.
    struct HotSlot {
        u8 kind = 0;    // 0 = empty, 1 = skill, 2 = item
        u16 id = 0;     // skill id or item nameid
        u16 level = 0;  // skill cast level
    };
    HotSlot hotbar_[5][12];
    int hotbarRows_ = 1;            // visible rows (1..5) -- the 5 modes
    float hotbarX_ = 0, hotbarY_ = 0;  // top-left on screen (draggable)
    bool hotbarPlaced_ = false;        // false until the default position is computed once
    float hotbarLastW_ = 0.0f;         // logical canvas width last frame; re-center on an orientation change
    bool hotbarDirty_ = false;         // a layout change is pending a (debounced) disk write
    double hotbarSaveAt_ = 0.0;        // flush the pending write once time_ passes this (coalesce bursts)
    bool hotbarPressed_ = false;       // a press landed on the panel (may become a drag or a click)
    bool hotbarPressOnSlot_ = false;   // the press started on a slot -> activate it, never drag (S.)
    // Chat binds (Alt+1..0 -> a chat command): an editable Shortcut List (Alt+M) + a 5x2 clickable
    // panel, both saved per char like the hotbar (S.).
    std::array<std::string, 10> binds_;  // slot 0 = Alt+1 .. slot 8 = Alt+9, slot 9 = Alt+0
    bool bindsWindowOpen_ = false;       // the Shortcut List editor window (Alt+M)
    int bindEditIndex_ = -1;             // which bind row is being edited (-1 = none)
    ui::TextField bindEdit_;             // shared edit field for the focused bind row
    bool bindsPanelOpen_ = false;        // the 5x2 binds panel is shown (toggled by the ALT button)
    bool bindsPanelPlaced_ = false;      // false until the default position (below main menu) is set
    float bindsPanelX_ = 0, bindsPanelY_ = 0;  // panel top-left (draggable, saved)
    bool bindsPanelDragging_ = false;
    void fireBind(Application& app, int slot);   // send a bind's command to chat
    void drawBindsWindow(Application& app);      // the Alt+M Shortcut List editor
    void drawBindsPanel(Application& app);       // the 5x2 clickable binds panel
    bool hotbarRightWasDown_ = false;  // RMB edge tracker for clearing a slot (right-click frees it)
    bool hotbarDrag_ = false;          // press moved enough to drag the panel
    float hotbarGrabX_ = 0, hotbarGrabY_ = 0;  // cursor-to-origin offset while dragging
    int hotbarPressX_ = 0, hotbarPressY_ = 0;  // press point, to tell a click from a drag
    // Skill target-selection: a single-target/ground skill (from the skill window or a hotbar slot)
    // waits for the player to click a unit/cell. The cursor shows the TARGET action; Shift casts on
    // any class (a buff onto a mob, a debuff onto a player). Self skills (endure) skip this. (S. spec.)
    bool skillTargeting_ = false;
    u16 skillTargetId_ = 0, skillTargetLevel_ = 0, skillTargetInf_ = 0;
    bool leadSuppressedUntilUp_ = false;  // after a skill cast, hold-to-lead is off until the button lifts
    // Approach-to-range: the server rejects an out-of-range targeted/ground skill (unit.c returns 0,
    // no server walk), so the client walks into range and then fires. (S.: "баш/соник/радиусные --
    // чар не подходит для атаки/каста".)
    bool pendingSkill_ = false, pendingSkillGround_ = false;
    u16 pendingSkillId_ = 0, pendingSkillLevel_ = 0, pendingSkillRange_ = 1;
    // Last ground-skill WE cast (id+level), captured at send time so the matching ZC_NOTIFY_GROUNDSKILL
    // (0x115, which carries no level) can size level-scaled visuals like Arrow Shower's arrow volley.
    u16 lastGroundSkill_ = 0, lastGroundLevel_ = 0;
    u32 pendingSkillGid_ = 0;                          // targeted unit to close on (0 = ground cast)
    u16 pendingSkillX_ = 0, pendingSkillY_ = 0;        // ground target cell
    u16 pendingWalkX_ = 0xFFFF, pendingWalkY_ = 0xFFFF;  // debounce the chase-walk goal
    double pendingSkillExpire_ = 0.0;                  // give up chasing after this
    CharacterActor cursor_;  // RO mouse-cursor sprite (data/sprite/cursors), drawn in 2D
    CharacterActor emote_;   // shared emoticon sprite (data/sprite/이펙트/emotion), drawn 2D over heads
    bool emoteLoaded_ = false;
    struct Emote {
        u32 gid;
        u8 type;
        double born;
    };
    std::vector<Emote> emotes_;  // active emoticons over units (gid, emote index, spawn time)
    ShadowRenderer shadow_;  // flat ground shadow oval under each actor's feet
    LightGlowRenderer glow_;  // additive glow per RSW point light (dungeon volumetric light, #117 B)
    std::unordered_map<u32, MapActor> actors_;         // other units, keyed by gid
    std::unordered_map<u32, WarpUnit> warps_;          // skill warp-portal swirls, keyed by gid
    std::unordered_map<u32, GroundUnit> groundUnits_;  // trap/wall/zone ground units, keyed by gid
    std::set<u32> dynBlocked_;                          // cells the server blocked at runtime (Ice Wall,
                                                        // 0x192), key = (y<<16)|x; makes cellWalkable false
    std::vector<WarpUnit> mapEffects_;                  // RSW effect sprites (torch flames, ...)
    struct MapStrFx { Vec3 pos; const char* name; };   // RSW .str effects (looped in place), #32
    std::vector<MapStrFx> mapStrFx_;
    std::unordered_map<u32, std::string> names_;       // gid -> name (from ZC_ACK_REQNAME)
    std::unordered_map<u32, std::string> guildNames_;  // gid -> guild name (from 0x195 @54)
    std::unordered_map<u32, std::string> vendingNames_;  // gid -> vend shop title (ZC_STORE_ENTRY)
    std::unordered_map<u32, net::ChatRoomInfo> chatRooms_;  // ownerGid -> chat room over that unit (#78)
    std::unordered_map<u32, int> mobHpPct_;            // gid -> mob HP% (parsed from 0x195 info)
    std::unordered_map<u32, double> nameReqAt_;         // gid -> last name-request time (throttle)
    // Active skill casts (ZC_USESKILL_ACK 0x13e): a progress bar under the caster until end/cancel.
    struct CastBar { double start = 0.0, end = 0.0; u16 skillId = 0; double lastCircle = 0.0; };
    std::unordered_map<u32, CastBar> castBars_;        // gid -> in-progress cast
    // Overhead skill-name "shout" (S.: при касте над головой имя скилла на 1.5с) -> gid -> {name, until}.
    struct SkillShout { std::string name; double until = 0.0; };
    std::unordered_map<u32, SkillShout> skillShouts_;
    void noteSkillShout(u32 gid, u16 skillId);         // record a 1.5s overhead skill-name yell for a caster
    u32 hoveredGid_ = 0;
    u32 plateGid_ = 0;   // name-plate target: hover OR the magnet-snapped mob                                // unit under the cursor this frame
    net::Connection conn_;
    std::string mapBase_;

    // Billboard pass: actors drawn as depth-tested quads in the 3D view (view 0)
    // so the map occludes them. Reuses the alpha-cutout model shader.
    bgfx::ProgramHandle spriteProg_ = BGFX_INVALID_HANDLE;
    Gr2Models gr2Models_;  // Granny 3D models for flag/guardian/emperium actors (#31/#71)
    bgfx::ProgramHandle spriteLitProg_ = BGFX_INVALID_HANDLE;   // vs_sprite3d + fs_spritelit
    bgfx::UniformHandle spriteNrmSampler_ = BGFX_INVALID_HANDLE;  // s_nrm (slot 1)
    bgfx::UniformHandle spriteLightU_ = BGFX_INVALID_HANDLE;      // u_spriteLight
    bgfx::UniformHandle spriteSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle spriteBias_ = BGFX_INVALID_HANDLE;  // per-layer sprite depth bias
    bgfx::UniformHandle spriteFade_ = BGFX_INVALID_HANDLE;  // sprite alpha (dying-corpse dissolve)
    bgfx::VertexLayout spriteLayout_;
    // Feet-visibility occlusion (#36): one GPU occlusion query per actor (keyed by gid; the
    // player uses its accountId). Each frame a tiny quad at the actor's feet is submitted with
    // its query on view 0 AFTER the map/model depth (view 0 forced Sequential) but before the
    // sprite; the PREVIOUS frame's result decides whether the sprite draws on top (feet
    // visible -> nothing between camera and feet) or is occluded normally. Caps-gated.
    std::unordered_map<u32, bgfx::OcclusionQueryHandle> feetQuery_;
    std::unordered_map<u32, bgfx::OcclusionQueryHandle> plaqueQuery_;  // overhead vending-plaque occlusion
    bool feetOcclusionOk_ = false;  // BGFX_CAPS_OCCLUSION_QUERY present + overlay program valid
    // Submit a tiny depth-tested probe at `world` (view 0, after the map) and return the PREVIOUS frame's
    // result (Visible = nothing was drawn between camera and the point). `queries` keys the per-actor query
    // handle so different probes (feet vs. head plaque) on the same actor don't clash.
    bool occProbe(std::unordered_map<u32, bgfx::OcclusionQueryHandle>& queries, u32 gid, const Vec3& world);
    bool feetTestVisible(u32 gid, const Vec3& feetWorld) { return occProbe(feetQuery_, gid, feetWorld); }
    bool plaqueTestVisible(u32 gid, const Vec3& world) { return occProbe(plaqueQuery_, gid, world); }

    // Ground-decal pass: flat translucent quads on the terrain (the hovered-cell
    // cursor and the walk-path line). Untextured position+colour.
    bgfx::ProgramHandle overlayProg_ = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout overlayLayout_;

    Phase phase_ = Phase::Connecting;
    bool sentEnter_ = false;
    bool aidConsumed_ = false;
    double connectStartT_ = 0.0;  // when the current map-server handshake began (watchdog timeout)
    double mapEnterT_ = 0.0;      // when we last entered InGame (for the disconnect-cause diagnostic)
    u16    lastRecvCmd_ = 0;      // last packet opcode the server sent us in-game (disconnect-cause diagnostic)
    std::vector<u16> recentRecv_; // ring of the last ~24 server opcodes (disconnect-cause diagnostic, S.)
    int    lastBanCode_ = -1;     // reason code from an in-game SC_NOTIFY_BAN (0x81) if the server sent one before dropping us; -1 = none
    net::MapAuth auth_;
    bool haveAuth_ = false;
    Vec3 playerPos_{0, 0, 0};

    // Combat: the monster we're attacking (continuous attack, server-driven) and
    // floating damage numbers shown above the unit that took the hit.
    u32 attackTarget_ = 0;
    int atkRange_ = 1;  // player's weapon attack range from ZC_ATTACK_RANGE (0x13a); melee=1, bow~5 (S. archer)
    double lastAttackAt_ = 0.0;
    double lastAutoTargetScan_ = 0.0;  // throttle the auto-attack path-distance target scan (#76)
    // Auto-attack (#76): a bottom-right panel of toggles. AutoAttack locks the nearest filtered mob.
    bool autoAttackOn_ = false;
    bool autoHpOpen_ = false, autoSpOpen_ = false, autoFilterOpen_ = false;  // panel sub-windows (WIP)
    std::set<u16> autoMobOff_;  // ACTIVE map's excluded view-classes (mirror of autoMobOffMaps_[mapBase_])
    std::unordered_map<std::string, std::set<u16>> autoMobOffMaps_;  // per-map auto-attack filter (#76)
    std::vector<u16> autoHpItems_, autoSpItems_;  // nameids to auto-use when HP/SP is low (basket)
    int autoHpPct_ = 50, autoSpPct_ = 50;         // use the items below this % (default 50)
    double lastAutoHpUse_ = 0.0, lastAutoSpUse_ = 0.0;  // >=0.5s between auto-uses
    float autoHpRect_[4] = {0, 0, 0, 0}, autoSpRect_[4] = {0, 0, 0, 0};  // basket drag-drop targets
    double lastLeadAt_ = 0.0;   // min-interval floor for click-hold "lead the char toward the cursor" walks
    u16 leadCellX_ = 0xFFFF, leadCellY_ = 0xFFFF;  // last cell we led toward; re-issue when the cursor moves off it
    double playerAttackUntil_ = 0.0;  // play our attack motion while time < this (from ZC_NOTIFY_ACT)
    double playerAttackStart_ = 0.0;  // when our swing began (the attack motion plays from here)
    double playerCastUntil_ = 0.0;    // play the CAST motion (12) while time < this: instant no-damage/buff
                                      // skills (Change Cart etc.) that have no cast bar (S.: "анимация каста, не атаки")
    double playerHurtUntil_ = 0.0;    // play our hurt/flinch motion while time < this (on taking damage)
    double playerHurtStart_ = 0.0;    // when the flinch began (duration = dmotion from the 0x8a packet)
    double moveBlockedUntil_ = 0.0;   // can't start a walk until here after a hit (2x dmotion; S. wanted
                                      // a longer post-hit stagger so you can't outrun a mob crowd)
    enum class DmgKind { Number, Crit, Miss, Lucky, Heal, SpHeal };
    struct DamageText {
        Vec3 pos;
        std::string text;     // digit string (Number/Crit); empty for Miss/Lucky
        double born;
        float drift = 0.0f;   // horizontal scatter (px) the number fans out by
        DmgKind kind = DmgKind::Number;
        bool toSelf = false;    // damage WE take -> red (else dealt -> white)
        bool dbl = false;       // double attack (type 8) -> yellow
        bool fromSelf = false;  // WE are the attacker (src==us) -> a miss WE made shows red
    };
    std::vector<DamageText> dmgTexts_;
    int dmgSeq_ = 0;  // rotates the per-hit horizontal scatter so stacked hits fan out

    // Flying arrows for the player's own bow shots (S.). Only the player spawns them, so ranged
    // mobs/other players never do (the reason the earlier blanket version was dropped).
    struct Arrow {
        Vec3 from, to;  // chest-height endpoints (world)
        double born;
        double dur;     // flight time (s)
    };
    std::vector<Arrow> arrows_;
    std::vector<Arrow> boomerangs_;  // CR_SHIELDBOOMERANG (251) flying shields (same from/to/born/dur model)

    // Magnum Break (7): the original is a 3D translucent hemisphere DOME + a faint ground ring
    // expanding over ~0.7s (S. reference). Billboards render as squares, so this is real geometry:
    // a generated hemisphere mesh drawn translucent, scaled + faded over its life, at the caster.
    struct MagnumFx { Vec3 center; double born; };
    std::vector<MagnumFx> magnumFx_;
    // #146 Ruwach: a comet (bright head + tail) that ORBITS the char for a fixed time (S.: "должна быть
    // комета и летать вокруг чара"). Persistent: each frame we place the comet on its circle + spawn tail.
    // 24=Ruwach comet, 34=Blessing angels. gid = the unit to FOLLOW (S.: effect sprites must track the
    // char globally); center is the fallback spawn pos when gid is unknown/out of view.
    struct RuwachFx { Vec3 center; double born; u16 skill = 24; u32 gid = 0; double lastEmit = 0; };
    std::vector<RuwachFx> ruwachFx_;
    // #146 Persistent status auras on OTHER units. clif_status_change sends 0x196 to AREA for every
    // player status (server status.c: pcdb_checkid -> clif_status_change), so we ALSO receive allies'
    // SI_ on/off accurately — no duration guessing needed (S.: "длительность статусов можешь брать с
    // сервера" -> use the explicit flag=0). Track only aura-relevant SI_ per non-self gid; dropped on
    // flag-off / vanish (0x80) / map change. Self is driven by activeStatus_.
    std::unordered_map<u32, std::set<u16>> unitStatus_;  // gid -> active aura SI_ (allies)
    void drawMagnumDomes(const WorldSpritePass& pass);

    // Bolt-spell projectiles (S. spec, exe-sourced): `div` element balls FALL onto the target, each
    // dealing total/div and showing its own number. Fire Bolt = 이펙트\fireball.spr, Cold Bolt =
    // 이펙트\waterball.spr (verified in uaRO.exe CEffect + data.grf). Same from->to falling-sprite
    // model as arrows_; count/damage come from the server 0x1de (div/damage). `sprite` = effect .spr
    // basename (loaded from data/sprite/이펙트/).
    struct Fireball { Vec3 from, to; double born, dur; const char* sprite; float roll = 0.0f; };
    std::vector<Fireball> fireballs_;
    CharacterActor fireball_;
    std::string fireballSprite_;  // sprite currently loaded into fireball_ ("" = none)

    // RO combat-number sprites (data/sprite/이팩트/): 숫자.spr frames 0-9 = digit glyphs (~10x13px);
    // msg.spr frame 0 = "Miss", 2 = "Critical !!", 3 = the red star burst (the crit "облачко"),
    // 5 = "LUCKY!!". Lazily decoded and drawn as the floating combat feedback instead of the TTF
    // font (S.: "цифры на облачках, криты/мисс/лак как у референса").
    ui::UiImage dmgDigit_[10];
    ui::UiImage dmgMiss_, dmgCritTxt_, dmgBurst_, dmgLucky_;
    bool dmgDigitsTried_ = false;
    void loadDmgDigits(Application& app);


    i32 playerHp_ = 0, playerMaxHp_ = 0;  // our HP / max HP (from ZC_PAR_CHANGE) -> red bar
    i32 playerSp_ = 0, playerMaxSp_ = 0;  // our SP / max SP -> blue bar (drawn above the name)
    bool playerRiding_ = false;    // our own peco mount state (from ZC_STATE_CHANGE)
    bool playerFalcon_ = false;    // our own falcon state (from SI_FALCON status-load, sent to self)
    bool playerHasCart_ = false;   // our own pushcart state (from the cart-list / cart-off packets)
    u32 playerOption_ = 0;         // our own option bits (cart/falcon/riding) for the equip remove button
    u16 playerOpt1_ = 0, playerOpt2_ = 0;  // our own ailment state (opt1 body / opt2 health) for the status row
    bool night_ = false;           // server day/night state (SI_NIGHT via clif_status_load)
    float nightLevel_ = 0.0f;      // eased 0..1 dim level toward night_, for a smooth dusk/dawn
    i32 playerSpeed_ = 150;        // our walk speed, ms/cell (ZC_PAR_CHANGE SP_SPEED); mount/buffs
    net::CharStatus cstat_;        // bulk status (6 stats + derived) from ZC_STATUS (0x00bd)
    double statSettleUntil_ = 0.0; // after a map change the server re-sends stats over ~1-2s and the
                                   // early ones can be short the equip/buff bonus (cross-server equip
                                   // settles late); until this time, don't let a 0x141 LOWER a stat
                                   // bonus (keep the pre-warp value) so the window doesn't flicker down.
    u16 skillPoint_ = 0;           // unspent skill points (ZC_PAR_CHANGE SP_SKILLPOINT) (#skills win)
    i32 baseLevel_ = 0, jobLevel_ = 0;  // from ZC_PAR_CHANGE SP_BASELEVEL / SP_JOBLEVEL
    u32 baseExp_ = 0, jobExp_ = 0;        // current base/job EXP (ZC_PAR_CHANGE) -> BasicInfo bars
    u32 nextBaseExp_ = 0, nextJobExp_ = 0;  // EXP needed for the next base/job level
    int basicInfoState_ = 2;              // BasicInfo panel: 0 collapsed, 1 short, 2 full (Alt+V)
    i32 weight_ = 0, maxWeight_ = 0;    // current / max carry weight (×10 from the server)
    u32 zeny_ = 0;                       // money
    bool sitting_ = false;         // our own sit/stand posture (Insert or /sit; server-reconciled)
    Mover playerMove_;             // our own glide (from ZC_NOTIFY_PLAYERMOVE)
    u16 walkTargetX_ = 0, walkTargetY_ = 0;  // last clicked cell (for long-walk re-request)
    u16 lastDstX_ = 0, lastDstY_ = 0;        // dst the server last walked us to
    u16 predDstX_ = 0, predDstY_ = 0;        // hop cell our client-side prediction is gliding to
    bool predActive_ = false;                // a predicted glide is in flight (skip the 0x0087 restart)
    // Desync guard for client-side prediction: if we keep requesting walks but the server stops
    // confirming them (it is rejecting -- e.g. auto-attack chased onto a server-impassable hill), snap
    // back to its last confirmed cell and briefly suppress prediction so the char can't run away.
    bool awaitingMoveConfirm_ = false;       // sent a walk, still waiting for its 0x0087
    double moveReqAt_ = 0.0;                 // when that walk was sent
    double predSuppressUntil_ = 0.0;         // don't predict until this time (post-recovery cooldown)
    u16 srvConfirmX_ = 0, srvConfirmY_ = 0;  // last cell the server confirmed (0x0087 dst / 0x88 fixpos)
    bool haveSrvConfirm_ = false;
    double lastResyncAt_ = 0.0;              // throttle for the idle position self-resync (S.)
    bool haveWalkTarget_ = false;
    bool fleeQueued_ = false;  // a walk clicked during the post-hit stagger; fires when it clears
    Mat4 lastVp_;                  // last frame's view-proj, for click unprojection
    bool haveLastVp_ = false;
    int lastViewW_ = 0, lastViewH_ = 0;
    Vec3 camUp_{0.0f, 1.0f, 0.0f}; // last frame's camera up (billboard up) — pickActor sizes the
                                   // click box along it so the box matches the drawn sprite

    bool menuOpen_ = false;  // in-game ESC menu shown (drives escMenu_, the shared SettingsMenu)
    SettingsMenu escMenu_;   // the one shared settings/ESC menu (same as the login screen); it owns
                             // all the panel/slider/dropdown state now.

    // Gamepad control modes (#102/#115). The d-pad selects one exclusive base mode; Mouse (L3) and
    // Camera (R3) are independent overlays that stack on top. Skill panels are momentary (held
    // shoulders) so they are not stored here. A bottom-of-screen strip lists the active modes.
    enum class PadMode { Standard, Menu, Mob, Npc, Char };
    PadMode padMode_ = PadMode::Standard;
    // Touchscreen control (#102/#114): Pad (fixed follow-cam + L/R stick zones), Standard (drag walks),
    // Camera (drag orbits). Chosen via the bottom-centre 3-button panel (drawGamepadModeStrip sibling).
    enum class TouchMode { Pad, Standard, Camera };
    TouchMode touchMode_ = TouchMode::Standard;
    bool touchAutoTried_ = false;  // one-shot auto-enable of touch mode on a touch device
    // Touch gesture state (per primary finger), driven by the finger edges in InputState::ScreenTouch.
    bool touchDragging_ = false;   // the primary finger has moved past the tap threshold -> a drag/gesture
    bool touchLongFired_ = false;  // long-press already emitted for the current finger-down
    double touchDownT_ = 0.0;      // scene time the primary finger went down (for long-press = RMB)
    float touchPinchDist_ = -1.0f; // last 2-finger distance (px) for pinch-zoom; <0 = not pinching
    bool touchOnUi_ = false;       // the primary finger went down over a UI rect -> route it as a plain mouse
    bool touchMultiTouched_ = false;  // a 2nd finger touched during this gesture -> not a clean tap (pinch)
    // Visual joystick overlay under the dragging finger (#102 refinement). Logical (UI-scale) coords.
    bool touchStickShow_ = false;  // draw the joystick this frame
    bool touchStickWalk_ = false;  // walk stick (green) vs camera stick (blue)
    float touchStickCx_ = 0, touchStickCy_ = 0;  // base centre (finger-down origin)
    float touchStickKx_ = 0, touchStickKy_ = 0;  // knob (current finger, clamped to radius)
    // Second joystick, for Pad mode's simultaneous two-stick control (2nd finger = 2nd stick).
    bool touchStick2Show_ = false, touchStick2Walk_ = false;
    float touchStick2Cx_ = 0, touchStick2Cy_ = 0, touchStick2Kx_ = 0, touchStick2Ky_ = 0;
    bool padMouseMode_ = false;   // L3: left stick drives the cursor
    bool padCameraMode_ = false;  // R3: right stick orbits/zooms the camera
    bool padCamOrbiting_ = false; // this frame the right stick is actively orbiting under lock (suppress follow-ease)
    bool touchCamOrbiting_ = false; // touch Pad right stick actively orbiting under lock; released -> ease back to FOV
    void updateGamepadModes(Application& app, const InputState& in);  // Phase 1: d-pad/L3/R3 switching
    void drawGamepadModeStrip(Application& app);                      // bottom-screen active-modes line
    void drawTouchModePanel(Application& app);                        // bottom-centre 3-mode touch panel (#102/#114)
    void drawTouchJoystick(Application& app);                         // visual joystick under the dragging finger (#102)
    void updateTouchInput(Application& app);                          // touchscreen gestures (#102/#114)
    void drawGamepadPrompt(Application& app);                         // mid-session "switch to gamepad?" modal
    void updateGamepadTargeting(Application& app, const InputState& in);  // Phase 3: mob/npc/char targeting
    u32 padTarget_ = 0;              // currently selected target in a gamepad targeting mode
    double padStickPickAt_ = 0.0;    // debounce for right-stick directional target picking
    bool padPrevConn_ = false;       // pad connected last frame (edge-detect connect/disconnect)
    bool padPromptOpen_ = false;     // "switch to gamepad mode?" prompt shown (mid-session connect)
    double padNavAt_ = 0.0;          // debounce for right-stick focus navigation in menu mode
    PadMode padPrevMode_ = PadMode::Standard;  // last frame's base mode (edge-detect Menu entry)
    float padStatusCx_ = -1.0f, padStatusCy_ = -1.0f;  // Status button centre (focus target on menu entry)
    double padShoulderAt_ = 0.0;     // debounce for L1/R1/L2/R2 (tabs / window switch) in menu mode
    float padCursorX_ = -1.0f, padCursorY_ = -1.0f;  // virtual cursor (physical px) for mouse mode / touchpad
    bool padHotbar_ = false;         // hotbar drawn as the 2x8 gamepad layout this frame (hit-test must match)
    bool touchHotbar_ = false;       // touch: same compact 2x8 grid, 1.5x, no button glyphs (hit-test must match)
    bool padHideCursor_ = false;     // hide the RO cursor + hovered-cell highlight (gamepad, not mouse mode)
    u8 camTurnDir_ = 0;              // camera's target octant: turns advance it NOW; the sprite trails (S.)
    bool camTurnInit_ = false;      // camTurnDir_ seeded to auth_.dir
    double spriteTurnAt_ = 0.0;     // when the sprite (auth_.dir) next steps toward camTurnDir_ (0 = idle)
    // Gamepad rumble (#102/#115, S.): a tiny time-scheduled pulse queue so a "double tick" (two pulses
    // with a gap) works without blocking. Each pulse fires via app.padRumble() when time_ >= at.
    enum class RumbleEvent { Damage, Kill, Level, Menu, Death, Crit };
    struct RumblePulse { double at; unsigned short lo, hi; unsigned int ms; };
    std::vector<RumblePulse> rumbleQueue_;
    std::unordered_map<u32, double> selfDamagedAt_;  // gid -> last time self dealt damage (kill attribution)
    void triggerRumble(Application& app, RumbleEvent e);

    // Minimap: the map's overview bitmap (data/texture/<유저인터페이스>/map/<map>.bmp)
    // drawn top-right with a player marker; Tab toggles it. Loaded once per map.
    ui::UiImage minimap_;
    std::string minimapFor_;       // map the minimap_ was decoded for ("" = none/failed)
    ui::UiImage minimapArrow_;     // map_arrow.bmp — player position/facing marker
    bool minimapArrowTried_ = false;
    bool minimapOpen_ = true;
    // Last-drawn minimap rect, captured by drawMinimap so update() can do click-to-walk with the
    // full set of UI block rects in hand (a window over the minimap must swallow the click).
    float mmX_ = 0.0f, mmY_ = 0.0f, mmSize_ = 0.0f;
    bool mmDrawn_ = false;
    bool inventoryOpen_ = false;   // inventory grid window (toggled by the on-screen Items button)
    bool statusOpen_ = false;      // character status (stats) window
    bool equipOpen_ = false;       // equipment window (worn items by slot)
    bool skillsOpen_ = false;      // skill list window
    std::unordered_map<u16, u16> skillSelLevel_;  // skill id -> chosen CAST level (1..learned)
    std::unordered_map<u16, double> skillCooldownUntil_;  // skill id -> time_ when its cooldown ends (#72)
    void startSkillCooldown(u16 id);              // begin a skill's after-cast-delay cooldown (baked, #72)
    // Veil a skill icon at (x,y,w,h) while it's on cooldown + show remaining seconds (#72). No-op otherwise.
    void drawSkillCooldown(SpriteBatch& sb, Font& font, u16 id, float x, float y, float w, float h);
    bool identifyOpen_ = false;    // Magnifier: item-identify list window (server 0x177)
    std::vector<u16> identifyList_;  // inventory indices awaiting identification (click sends 0x178)
    u16 equippedAmmoIndex_ = 0;    // inv index (slot+2) of the worn arrow/ammo from 0x013c; 0 = none (S.: archer arrow)
    u16 lastAmmoNameId_ = 0;       // item id of the LAST-equipped arrow/ammo, kept even after the stack is
                                   // spent -> re-buying the same arrow auto-re-equips it (S.: client forgot arrows)
    bool storageOpen_ = false;     // Kafra storage window (opened by the server's count packet 0xf2)
    u16 storageCur_ = 0, storageMax_ = 0;  // storage slots used / capacity (ZC_STORE_COUNTINFO)
    int storageScroll_ = 0;        // storage list wheel-scroll offset (rows)
    int storageTab_ = 0;           // storage item-type tab: 0 usable, 1 equip, 2 etc
    ui::TextField storageFilter_;  // name-substring filter box (between titlebar and tabs)
    bool storageFilterFocus_ = false;
    int storageSort_ = 0;          // 0 = Group (clump like items), 1 = Alphabet
    bool cartOpen_ = false;        // pushcart window (Alt+W) — merchant cart, mirrors storage
    int cartScroll_ = 0;           // cart list wheel-scroll offset (rows)
    int cartTab_ = 0;              // cart item-type tab: 0 usable, 1 equip, 2 etc

    // Quest journal (#136, Alt+U). The known quests with objectives + progress, driven by the
    // server's ZC quest packets (0x2b1/0x2b2/0x2b3/0x2b4/0x2b5/0x2b7).
    std::vector<net::QuestInfo> quests_;
    bool questOpen_ = false;       // Alt+U quest journal window
    std::unordered_map<u32, std::string> questTitles_;  // quest id -> display title (#136)
    bool questTitlesLoaded_ = false;
    float questScroll_ = 0.0f;     // journal list scroll offset (px)

    // Friend list (#78, Alt+H). The list, window state, and the incoming-request dialog.
    std::vector<net::FriendInfo> friends_;
    bool friendsOpen_ = false;     // Alt+H window
    int friendTab_ = 0;            // 0 = Friend, 1 = Party (party content is a separate slice)
    int friendSel_ = -1;           // selected friend row (-1 = none)
    int friendScroll_ = 0;         // friend/party list wheel-scroll offset (rows)
    // (Background asset preloading removed 2026-07-16, S.: "убрать всю предзагрузку" — assets load lazily
    // and stay cached for the session; caches free on disconnect, location renderer frees on map change.)
    int friendRows_ = 8;           // visible rows in the friend/party list (drag the bottom edge to resize, S.)
    bool friendResizing_ = false;  // dragging the friend/party window's bottom resize edge
    bool friendAddOpen_ = false;   // the "add a friend by name" input box is showing
    ui::TextField friendAddField_; // name typed into the add box
    u32 friendReqAid_ = 0, friendReqCid_ = 0;  // pending incoming request (0 = none)
    std::string friendReqName_;    // requester name shown in the accept/decline dialog

    // Party (#78 part 2). Shown under the Friend window's Party tab.
    std::string partyName_;
    std::vector<net::PartyMember> party_;
    u32 partyInviteAid_ = 0;       // pending incoming party invite (0 = none)
    std::string partyInviteName_;  // inviter / party name in the accept dialog
    bool partyCreateOpen_ = false; // the "create party" name box is showing
    ui::TextField partyCreateField_;

    // Chat room create window (#78 part 3, Alt+C).
    bool chatRoomOpen_ = false;
    ui::TextField chatRoomTitle_;
    ui::TextField chatRoomLimit_;  // numeric capacity
    ui::TextField chatRoomPass_;
    bool chatRoomPublic_ = true;
    int chatRoomFocus_ = 0;        // which chat-room field has focus: 0 title, 1 limit, 2 password
    // The chat room the player is currently inside (created or joined): its own window.
    bool roomActive_ = false;
    u32 roomId_ = 0;
    std::string roomTitle_, pendingRoomTitle_;
    u16 roomLimit_ = 20;
    std::vector<std::string> roomMembers_;
    ui::TextField roomInput_;
    float roomW_ = 440.0f, roomH_ = 220.0f;  // chat-room window size (resizable, S.)
    bool roomResizing_ = false;

    // Buying store window (Alt+B). A UI wrapper over the server's @buystore/@buymarket/@sellto
    // chat commands (the native PV2010+ buying-store packets are no-ops on this PV7 server, so the
    // feature is driven through chat). Buttons compose the @command; results land in the chat log.
    bool buyStoreOpen_ = false;
    ui::TextField bsItemId_, bsAmount_, bsPrice_, bsTitle_, bsStoreId_;
    int bsFocus_ = 0;              // which buying-store field has focus (0..4)

    // Guild (#78 part 4, Alt+G). Tabbed window: Info / Member / Position / Skill / Expel / Notice.
    u32 guildId_ = 0;              // the player's own guild id (from 0x16c), 0 = none
    net::GuildInfo guildInfo_;
    std::vector<net::GuildMember> guildMembers_;
    std::vector<net::GuildPosition> guildPositions_;
    std::vector<net::GuildExpel> guildExpels_;
    std::vector<net::SkillInfo> guildSkills_;
    u16 guildSkillPoint_ = 0;
    int guildPosEdit_ = -1;  // position id whose title the master is renaming (-1 = none)
    ui::TextField guildPosField_;
    bool emblemPickerOpen_ = false;       // the "choose an emblem .bmp" dropdown is showing
    std::vector<std::string> emblemFiles_;  // .bmp files found in the game's Emblem/ folder
    std::string guildNoticeSubj_, guildNoticeBody_;
    std::vector<net::GuildAlly> guildAllies_;   // 0x14c: alliances (enemy=false) + antagonists (enemy=true)
    bool guildNoticeEdit_ = false;              // master is editing the notice text
    ui::TextField guildNoticeSubjField_, guildNoticeBodyField_;
    int guildNoticeFocus_ = 0;                  // which notice field has focus: 0 subject, 1 body
    bool guildOpen_ = false;
    bool guildOpenPrev_ = false;  // edge-detect the guild window opening (Guild button) -> refresh info
    // True when any interactive UI window is open -> the gamepad auto-enables its virtual cursor so a
    // controller can operate it, and the RO cursor stays visible (#115).
    bool anyGameWindowOpen() const;
    int guildTab_ = 0;             // 0 Info, 1 Member, 2 Position, 3 Skill, 4 Expel, 5 Notice
    int guildScroll_ = 0;
    u32 guildInviteId_ = 0;        // pending incoming guild invite (0 = none)
    std::string guildInviteName_;

    // Mercenary status window (#79). 0x29b opens it; 0x2a2 live-updates a single stat.
    net::MercInfo mercInfo_;
    bool mercActive_ = false;  // a mercenary is summoned (0x29b received)
    bool mercOpen_ = false;    // the merc status window is shown
    std::vector<net::HomSkill> mercSkills_;  // last ZC_MER_SKILLINFO_LIST (0x29d); same 37B entry as homun

    // Homunculus + Rental windows (#79). Toggled by the HUD buttons above ALT. Homun is
    // fed by ZC_PROPERTY_HOMUN (0x22e).
    // Pet (#131): status from ZC_PET_INFO (0x1a2) / value updates (0x1a4); the window drives
    // CZ_PET_MENU (feed/performance/return-to-egg). petEggs_ = hatch candidates from 0x1a6.
    struct PetState {
        bool have = false;   // a 0x1a2 arrived (a pet is out)
        std::string name;
        u8 renameFlag = 0;
        u16 level = 0, hungry = 0, intimacy = 0, accessory = 0;
        u32 gid = 0;         // pet unit gid (0x1a4 carries it)
    };
    PetState pet_;
    bool petOpen_ = false;
    std::vector<u16> petEggs_;
    bool petEggOpen_ = false;
    bool petCatching_ = false;   // 0x19e received: the next mob click arms the taming target
    // Pet-taming slot machine (#149). uaRO presents the capture as a spinning-reel minigame
    // (server clif_pet_roulette 0x1a0; assets data/sprite/slotmachine.spr/.act). Flow: 0x19e opens
    // the machine + arms; click a mob to pick the target; press Spin (button/click/Tab/gamepad-A) to
    // send CZ_PET_CATCH 0x19f; 0x1a0 lands the reels on win(egg)/lose.
    bool slotOpen_ = false;          // the slot-machine window is showing
    u32 slotTarget_ = 0;             // the mob gid the player picked to tame
    int slotResult_ = -2;            // -2 idle(no spin), -1 spinning (awaiting 0x1a0), 0 lose, 1 win
    double slotSpinStart_ = 0.0;     // scene time the reels began spinning
    double slotDoneAt_ = 0.0;        // scene time to auto-close after the result lands (0 = not set)
    CharacterActor slotSprite_;      // slotmachine.spr/.act reels
    bool slotSpriteTried_ = false;
    ui::TextField petRename_;
    bool petRenameFocus_ = false;
    bool isPetActor(u32 gid) const;
    bool homunOpen_ = false;
    bool rentOpen_ = false;
    net::HomunInfo homunInfo_;    // last ZC_PROPERTY_HOMUN (0x22e)
    bool homunActive_ = false;    // a (non-vaporized) homunculus is present
    std::vector<net::HomSkill> homunSkills_;  // last ZC_HOSKILLINFO_LIST (0x235)
    // Rental items: nameid -> scene time_ at which it expires (from ZC_CASH_TIME_COUNTER 0x298); the
    // Rental window lists them with a live countdown. Removed on ZC_CASH_ITEM_DELETE (0x299).
    std::vector<std::pair<u16, double>> rentItems_;
    bool homunShowSkills_ = false;            // skill-tree sub-panel toggle in the Homun window
    // Homun behavior mode (S.: агрессивный/защита/пассив toggle for console/touch). 2=Aggressive
    // (engage any monster), 1=Defensive (engage only monsters attacking master/homun), 0=Passive
    // (never auto-attack; follow + manual commands only). Steers the AI bridge (listActors + homunType).
    int homunBehavior_ = 2;
    // Manual-command lock (S.: "если указал гомункулу атаковать/бежать, он не должен переключаться").
    // While set, the client AI is suppressed so it can't re-target: homunManualTarget_ = the ordered
    // attack target (held until it dies/leaves view), homunManualUntil_ = a walk window for an ordered
    // move (no target). Set by Ctrl+RMB / the Attack buttons.
    u32 homunManualTarget_ = 0;
    double homunManualUntil_ = 0.0;
    bool homunRenameOpen_ = false;            // rename-homunculus modal (one-time; gated on !renamed)
    bool homunDeleteConfirm_ = false;         // "выгнать" (permanent delete) confirmation modal
    ui::TextField homunRenameField_;          // typed new homunculus name

    // Homunculus AI (#148): the original client runs data/ai/AI.lua in an embedded Lua VM and calls
    // AI(homunId) each tick; all the follow/chase/attack/defend logic lives in the scripts. We host
    // it via HomunAiHost, feeding the scripts our live actor registry and turning their Move/Attack/
    // Skill calls into homun command packets (0x232/0x233/0x234, 0x113/0x116). See HomunAi.hpp.
    struct HomunAiBridge : HomunAiHost {
        GameScene* s = nullptr;
        u32 homunGid() const override;
        u32 ownerGid() const override;
        bool actorCell(u32 gid, int& x, int& y) const override;
        int actorMotion(u32 gid) const override;
        u32 actorTarget(u32 gid) const override;
        bool isMonster(u32 gid) const override;
        void listActors(std::vector<u32>& out) const override;
        int homunType() const override;
        int attackRange() const override;
        int skillRange(int skillId) const override;
        u32 tickMs() const override;
        bool popCommand(std::vector<int>& out) override;
        bool peekReserved(std::vector<int>& out) override;
        void moveTo(int x, int y) override;
        void moveToOwner() override;
        void attack(u32 target) override;
        void skillObject(int level, int skill, u32 target) override;
        void skillGround(int level, int skill, int x, int y) override;
        void trace(const char* msg) override;
    };
    HomunAiBridge homunHost_;
    HomunAi homunAi_;
    bool homunAiInit_ = false;                       // scripts loaded (once, lazily on first homun)
    u32 homunGid_ = 0;                               // cached gid of our homun actor (0 = none)
    u32 homunOwner_ = 0;                             // our own account id (master), cached for the bridge
    double homunAiNext_ = 0.0;                       // next scene time to run an AI tick (throttled)
    std::vector<std::vector<int>> homunCmdQueue_;    // owner commands from the homun window (GetMsg)
    std::vector<int> homunResCmd_;                   // reserved/repeating command (GetResMsg); empty = none
    std::unordered_map<u32, u32> actorTarget_;       // gid -> whom it last attacked (for V_TARGET)
    u32 resolveHomunGid() const;                     // find our homun actor by homun-range view class
    void updateHomunAi(Application& app);            // per-frame driver (init + throttled tick)

    // Drag-and-drop of items (#61): press-and-move on an item starts a drag; the icon follows the
    // cursor; releasing over the equip window equips, over the bag unequips, over the map drops
    // (via the quantity dialog). dragSrc_: 0 none, 1 bag, 2 equip, 3 storage, 4 cart. Rects of the
    // windows are cached each frame so the release can tell what was dropped on.
    int dragSrc_ = 0;
    u16 dragIndex_ = 0, dragNameid_ = 0, dragAmount_ = 0, dragEquipMask_ = 0;
    bool dragActive_ = false;      // moved far enough past the press to count as a drag (not a click)
    int dragStartX_ = 0, dragStartY_ = 0;
    float invRect_[4] = {0, 0, 0, 0}, equipRect_[4] = {0, 0, 0, 0}, storageRect_[4] = {0, 0, 0, 0};  // x,y,w,h
    float cartRect_[4] = {0, 0, 0, 0};  // pushcart window rect, for drag-drop hit-testing
    float tradeRect_[4] = {0, 0, 0, 0};  // trade "my offer" pane rect, for drag-drop into the exchange
    // Drop-quantity dialog: opened when a stack is dragged onto the map; default = the whole stack,
    // Enter/OK confirms (CZ_ITEM_THROW), a click outside cancels (S. spec).
    bool dropDialogOpen_ = false;
    u16 dropIndex_ = 0;
    int dropQty_ = 1, dropMax_ = 1;
    int dropAction_ = 0;  // OK: 0 drop, 1 store, 2 retrieve, 3 shop qty, 4 vending qty, 5 to-cart, 6 from-cart
    int dropShopIdx_ = -1;  // shop-list row the qty dialog is editing (dropAction_ == 3)
    ui::TextField dropQtyField_;  // typeable quantity (S.: a real input field, not "1/17")

    // Movable in-game windows. Each window keeps its own top-left so the titlebar drag and
    // update()'s click-blocking agree with where render() draws it; a window snaps to its
    // default spot the first time it opens (placed=false). When the cursor sits inside any
    // open window / the hotbar / the minimap (gathered into uiBlockRects_ as they're drawn),
    // the world click and the ground-cell highlight are suppressed, so clicking a panel no
    // longer also walks the character (S.: "не могу кликнуть по окну — чар бежит").
    enum WinId { WIN_STATUS, WIN_INVENTORY, WIN_EQUIP, WIN_SKILLS, WIN_MENU, WIN_STORAGE, WIN_CART, WIN_FRIENDS, WIN_GUILD, WIN_QUEST, WIN_CRAFT, WIN_COUNT };
    struct WinPos { float x = 0.0f, y = 0.0f; bool placed = false; };
    WinPos winPos_[WIN_COUNT];
    int winRows_[WIN_COUNT] = {};                // user-chosen visible rows (0 = auto-fit); set by the
    int resizingWin_ = -1;                       // resize grip. Window being height-resized, or -1.
    int draggingWin_ = -1;                       // window followed by the cursor, or -1
    float dragGrabX_ = 0.0f, dragGrabY_ = 0.0f;  // cursor offset from the window top-left
    bool pointerOverUi_ = false;                 // cursor over a panel this frame (gates world)
    bool heldOnUi_ = false;                       // the current mouse-hold began over UI -> no hold-walk
    // Z-order click capture: when movable windows overlap, only the TOP-MOST one under the cursor
    // reacts (S.: "клики уходят на нижнее окно"). Each window records its bounds + shown flag every
    // frame; next frame picks clickWin_ = the latest-drawn window under the cursor.
    float winRect_[WIN_COUNT][4] = {};           // last-frame full bounds (x,y,w,h) per window
    bool winShown_[WIN_COUNT] = {};              // was this window drawn last frame
    int clickWin_ = -1;                          // top-most window under the cursor; only it takes clicks
    // Standalone dialog windows (drawn with ui::window, not the WIN_* skinned system) are ALSO
    // draggable + click-blocking (S.: "все окна должны двигаться за заголовок и не пропускать клик").
    // popupWindow() stores each one's position by id here; the main chat is the deliberate exception.
    enum PopupId {
        POPUP_PARTY, POPUP_CREATE_PARTY, POPUP_ADD_FRIEND, POPUP_FRIEND_REQ, POPUP_CREATE_ROOM,
        POPUP_ROOM, POPUP_BUY_STORE, POPUP_VENDING, POPUP_VEND_SETUP, POPUP_MY_SHOP, POPUP_SHOP,
        POPUP_BUYSELL, POPUP_TRADE, POPUP_EXCHANGE, POPUP_ITEM_DESC, POPUP_SKILL_DESC,
        POPUP_AUTOATTACK, POPUP_SHORTCUT, POPUP_STATUS_ALT, POPUP_EQUIP_ALT, POPUP_DEAD,
        POPUP_GUILD_INVITE, POPUP_POT_HP, POPUP_POT_SP, POPUP_MERC, POPUP_HOMUN, POPUP_RENT,
        POPUP_PET, POPUP_PET_EGG, POPUP_CARD_DESC, POPUP_SENSE, POPUP_AUTOSPELL
    };
    struct PopupPos { float x = 0.0f, y = 0.0f; bool placed = false; };
    std::unordered_map<int, PopupPos> popupPos_;
    int draggingPopup_ = -1;                      // standalone dialog being dragged, or -1
    float popupGrabX_ = 0.0f, popupGrabY_ = 0.0f; // cursor offset from the dialog top-left
    bool windowsDirty_ = false;                   // a window moved/resized -> persist on mouse-up (S.)
    // Sense / Estimation window (0x18c): a mob's properties (Hunter/Merchant "Sense"). Filled from the
    // packet, shown as a draggable popup until closed.
    bool senseOpen_ = false;
    struct SenseInfo {
        u16 cls = 0, level = 0, size = 0, race = 0, defEle = 0;
        i32 hp = 0;
        i16 def = 0, mdef = 0;
        u8 ele[9] = {};        // damage % taken from Water..Undead (elements 1..9)
        std::string name;
    } sense_;
    // Sage Autospell (0x1cd): the server offers up to 7 castable bolt skills; the player picks one and
    // we reply with ZC-select (0x1ce). Held open until a pick or close.
    bool autospellOpen_ = false;
    std::vector<u16> autospellIds_;
    struct UiBlockRect { float x, y, w, h; };
    std::vector<UiBlockRect> uiBlockRects_;      // filled in render(), read by next-frame update()
    int invTab_ = 0;                             // inventory tab: 0 usable, 1 equip, 2 etc
    ui::TextField invFilter_;                    // name-substring filter box (between titlebar and tabs)
    bool invFilterFocus_ = false;
    int invSort_ = 0;                            // 0 = Group (clump like items), 1 = Alphabet
    int invScroll_ = 0, skillScroll_ = 0;        // wheel scroll offset (rows) for the inv/skill lists
    u32 descItemId_ = 0;                         // item whose RMB description popup is open (0 = none)
    u16 descCards_[4] = {0, 0, 0, 0};            // its inserted cards (vending items only; 0 = none)
    u32 cardDescId_ = 0;                          // a card RMB'd inside the info popup -> its own description window (S.)
    std::unordered_map<std::string, std::string> skillDesc_;  // internalName -> English description (GRF)
    std::unordered_map<std::string, std::string> bgmTable_;   // map base -> BGM file (mp3nametable, #103)
    std::string skillDescName_;                  // skill (internal name) whose RMB info window is open ("" = none)
    int skillDescScroll_ = 0;                     // first visible line in the skill-info window (wheel scroll)
    u32 charMenuGid_ = 0;                         // player whose RMB context menu is open (0 = none)
    u16 padItemMenu_ = 0;                          // inv index of the usable whose gamepad context menu is open (0=none)
    u16 padItemMenuNameid_ = 0;                    // its nameid (for the Auto HP/SP baskets)
    float charMenuX_ = 0.0f, charMenuY_ = 0.0f;  // its screen anchor (where the right-click landed)
    // Player trade / exchange window (#77). Flow over ZC/CZ 0xe4..0xf0.
    struct TradeOffer { u16 nameid = 0, amount = 0, index = 0; u8 refine = 0; };
    bool tradeOpen_ = false;             // the exchange window is up
    std::string tradeWith_;              // partner name (window title)
    std::string tradeRequestFrom_;       // pending incoming request -> accept/decline dialog ("" = none)
    std::vector<TradeOffer> tradeMine_;          // items I offered (optimistic; a 0xea fail pops the last)
    std::vector<net::TradeAddItem> tradeTheirs_; // items the partner offered (from 0xe9)
    u32 tradeMyZeny_ = 0, tradeTheirZeny_ = 0;   // zeny each side offered
    bool tradeMyLock_ = false, tradeTheirLock_ = false;  // each side pressed OK (locked in)
    ui::TextField tradeZenyField_;       // my zeny offer (sent on Enter)

    // NPC dialog: a script-driven say/next/close/menu/input exchange opened by RMB on an NPC.
    // Input = a number box (0x142), InputStr = a string box (0x1d4) — used by e.g. the mail NPC
    // to ask for the recipient name (S.: "почта... нету окна ввода имени получателя").
    enum class DialogMode { None, Next, Close, Menu, Input, InputStr };
    bool dialogOpen_ = false;
    u32 dialogNpc_ = 0;                   // the NPC we are talking to
    std::string dialogText_;              // accumulated say text (cleared on next/menu)
    std::string cutinImage_;              // ZC_SHOW_IMAGE 0x1b3: illust .bmp basename ("" = none)
    u8 cutinType_ = 255;                  // 0 bottom-left, 1 bottom-mid, 2 bottom-right, 255 = cleared
    ui::UiImage cutinTex_;                // lazily decoded illust image for cutinImage_
    // Spirit spheres (0x1d0/0x1e1) + devotion links (0x1cf): per-unit visual overlays (S. packet audit).
    std::unordered_map<u32, u16> spiritBalls_;  // gid -> orbiting spirit-sphere count (Monk/Gunslinger)
    struct DevotionLink { u32 target[5]{}; };
    std::unordered_map<u32, DevotionLink> devotion_;  // Crusader gid -> up to 5 devoted-target gids
    ui::UiImage orbTex_;                   // lazily-built soft-glow orb texture for the spheres
    struct Viewpoint { u16 x = 0, y = 0; u32 color = 0xFFFF00; };  // ZC_COMPASS 0x144 minimap marker
    std::unordered_map<u8, Viewpoint> viewpoints_;  // NPC-placed markers, keyed by slot id
    // Warp Portal destination picker (ZC_WARPLIST 0x11c): up to 4 map names + the skill id; a click
    // sends CZ_SELECTWARPPOINT 0x11b with the chosen map. (S. packet audit)
    std::vector<std::string> warpDests_;
    u16 warpSkillId_ = 0;
    bool warpOpen_ = false;
    int warpSel_ = 0;  // keyboard/gamepad focus row in the warp/teleport picker (last row = Cancel)
    // Card-insert picker: double-click a card -> 0x17a -> server 0x17b lists the equippable bag items
    // it fits (inventory only, equipped excluded). cardTargets_ holds their InvItem.index values.
    bool cardOpen_ = false;
    u16 cardIdx_ = 0;                  // InvItem.index of the card being inserted (from the double-click)
    std::vector<u16> cardTargets_;     // InvItem.index of each candidate equip; last picker row = Cancel
    int cardSel_ = 0;                  // keyboard/gamepad focus row in the card-insert picker
    // Crafting pick-list window (#1): produce-mix / arrow-craft / repair / refine. The server
    // opens it (0x18d/0x1ad/0x1fc/0x221); a click sends the matching reply and closes it.
    net::CraftList craftList_;
    bool craftOpen_ = false;
    int craftScroll_ = 0;
    // Arrow Crafting result plaque (S.): after the arrow-craft skill produces arrows, a small plaque
    // shows the made arrow's icon + name + quantity for 3s. Set when an Arrow-kind craft row is picked;
    // filled by the next ZC_ITEM_ADD that lands inside the pending window.
    double arrowCraftPending_ = 0.0;   // time_ deadline to catch the crafted-arrow item add (0 = idle)
    double craftToastUntil_ = 0.0;     // time_ the plaque hides (0 = hidden)
    u32    craftToastNameId_ = 0;      // made arrow item id (icon + name)
    u16    craftToastAmount_ = 0;      // how many were made
    // Pickup feed (S.: "клиент должен показывать всё что поднимает - иконку, название и количество"):
    // a fading stack of the items just picked up off the ground, each icon + name + xN.
    struct PickupToast { u32 nameId; u32 amount; double until; };
    std::vector<PickupToast> pickupToasts_;
    DialogMode dialogMode_ = DialogMode::None;
    std::vector<std::string> dialogMenu_;  // menu options (when dialogMode_ == Menu)
    int dialogMenuSel_ = 0;  // highlighted menu row (arrow keys move it, Enter picks it)
    ui::TextField dialogInput_;  // editable field for Input/InputStr modes (number / recipient name)
    bool rightWasDown_ = false;            // RMB state last frame (click-vs-orbit edge)
    float rightDragDist_ = 0.0f;           // RMB drag accumulated while held (px)
    double lastRightClickAt_ = -1.0;       // last RMB click time (double-click -> reset camera)

    bool dead_ = false;  // player is dead: show the respawn window, block world input

    // Chat (bottom-left, in place of the old Disconnect button; exit lives in the ESC
    // menu now). Enter focuses the box and then sends; Esc closes it.
    ui::TextField chatInput_;
    bool chatFocused_ = false;
    std::vector<std::string> chatHistory_;  // sent lines, oldest first (Up/Down recalls them, S.)
    int chatHistoryIdx_ = -1;               // current recall position; -1 = editing a fresh line
    // Chat lines carry a colour: server/system = green, players = blue, whispers = cyan, errors = red.
    struct ChatLine { std::string text; u32 color = 0xffffffffu; };
    std::vector<ChatLine> chatLog_;     // recent lines (newest last), capped for display
    std::vector<ChatLine> roomChat_;    // chat-room messages only (kept separate from the public chatLog_)
    int chatScroll_ = 0;                // lines scrolled back from the newest (0 = bottom)
    bool chatCollapsed_ = true;         // collapsed -> only a small "expand" button; default collapsed (S.)
    // Transient chat pop-ups: while the chat is collapsed, an incoming line ("name: text") briefly
    // floats above the Chat button for a few seconds then fades, so the player still sees chat (S.).
    struct ChatToast { std::string text; u32 color; double born; };
    std::vector<ChatToast> chatToasts_;
    usize lastChatLogSize_ = 0;         // funnel cursor: every NEW chatLog_ line auto-becomes a toast (S.:
                                        // "при свёрнутом чате нету сообщений" -- server/system lines never
                                        // called pushChatToast, so they were invisible when collapsed)
    void pushChatToast(const std::string& text, u32 color);  // records a toast iff chat is collapsed
    // Chat window is movable + resizable (S.). The bar is the bottom input row; the log grows upward
    // from it. Placed once (default bottom-left), then stays where dragged. Width + visible-line count
    // are the resize axes.
    float chatBarX_ = 16.0f, chatBarY_ = 0.0f, chatWidth_ = 567.0f;
    float chatScreenW_ = 0.0f;  // logical UI width, set each frame; chatBarRect clamps the bar to it
    int chatVisLines_ = 31;
    bool chatPlaced_ = false, chatMoving_ = false, chatResizing_ = false;
    float chatGrabX_ = 0.0f, chatGrabY_ = 0.0f;
    void chatBarRect(float H, float& x, float& y, float& w, float& h);  // current bottom-bar rect
    std::vector<std::string> whisperNames_;  // recent PM partners (newest first) for the whisper select
    bool whisperSelectOpen_ = false;    // the whisper-target dropdown is open (S.: кнопка селекта шёпота)
    void addWhisperName(const std::string& name);  // push a PM partner to the front of the history
    ui::TextField chatWhisperField_;    // bottom-bar whisper target; non-empty -> messages go as PMs
    bool chatWhisperFocused_ = false;   // the whisper-name field has keyboard focus (vs the message)

    // NPC merchant shop. ZC_SELECT_DEALTYPE opens the buy/sell chooser; the chosen mode
    // shows the buy or sell list with a per-row quantity. Modal like the NPC dialog.
    enum class ShopMode { None, Choose, Buy, Sell };
    ShopMode shopMode_ = ShopMode::None;
    u32 shopNpc_ = 0;
    std::vector<net::ShopItem> shopBuy_;   // buy list (ZC_PC_PURCHASE_ITEMLIST)
    std::vector<net::SellItem> shopSell_;  // sell list (ZC_PC_SELL_ITEMLIST)
    std::vector<int> shopQty_;             // chosen quantity per row (parallel to the active list)
    bool sellAll_ = true;                  // sell window: whole-stack, skip the qty dialog; ON by default (S.)
    int shopScroll_ = 0;                   // first visible row in the buy/sell showcase list
    int shopCartScroll_ = 0;               // first visible row in the buy/sell CART pane (S.: scrollbar)
    std::string shopMsg_;                  // last buy/sell result text
    double shopMsgUntil_ = 0.0;            // hide shopMsg_ after this time

    // Vending: another player's merchant shop. Clicking the title above a vendor sends
    // CZ_REQ_BUY_FROMMC (0x130); the server replies with the wares (0x133). Same staging
    // window as the NPC buy side, but items are bought via 0x134 keyed by the vendor's AID.
    bool vendingOpen_ = false;
    u32 vendingVendorAid_ = 0;                 // whose shop is open (echoed in the purchase)
    std::vector<net::VendItem> vendingItems_;  // the vendor's wares (ZC_..._FROMMC 0x133)
    std::vector<int> vendingQty_;              // chosen quantity per row (parallel to vendingItems_)
    int vendingScroll_ = 0;                    // first visible row in the vending showcase

    // Opening MY OWN shop (Merchant Vending skill). ZC_OPENSTORE (0x12d) opens this setup window;
    // pick cart items + a qty/price for each, name the shop, OK sends CZ_REQ_OPENSTORE (0x1b2).
    bool vendSetupOpen_ = false;               // the "Vend a Shop" setup window is up
    int vendSetupMax_ = 0;                     // max wares the skill level allows (from 0x12d)
    int vendSetupScroll_ = 0;                  // cart list scroll in the setup window
    ui::TextField vendNameField_;              // the shop name
    std::vector<net::VendSellEntry> vendSell_; // staged wares (cart index + amount + price)
    // Add-a-ware modal (opened by clicking a cart item in the setup): pick the qty + unit price.
    bool vendAddOpen_ = false;
    u16 vendAddIdx_ = 0;                        // cart wire-index being added
    int vendAddMax_ = 1;                        // owned amount (cap for the qty field)
    bool vendAddPriceFocus_ = false;           // false = qty field focused, true = price field
    ui::TextField vendAddQtyField_, vendAddPriceField_;
    // MY open shop's management panel (ZC_PC_PURCHASE_MYITEMLIST 0x136): the wares + remaining qty;
    // 0x137 decrements as they sell; a Close button takes the shop down (CZ_REQ_CLOSESTORE 0x12e).
    bool myVendOpen_ = false;
    std::vector<net::VendItem> myVend_;

    // Drag-to-cart inside the buy/sell + vending windows (S.: "за пиктограмку — драг-н-дроп").
    // A self-contained drag (NOT the inventory dragSrc_ system): grab a showcase row, drop it on
    // the cart pane. Only one of those windows is ever open, so a single state serves both.
    int shopDragIdx_ = -1;        // showcase source index being dragged (-1 = none)
    u16 shopDragNameid_ = 0;      // item id, for the cursor-follow icon
    int shopDragX0_ = 0, shopDragY0_ = 0;  // press origin, for the move threshold
    bool shopDragOn_ = false;     // moved past the threshold -> a real drag (show the icon)

    // Inventory index -> item, from ZC_INVENTORY_LIST / ZC_EQUIPMENT_ITEMLIST (sent on
    // map entry). The sell list identifies items only by inventory index, so this lets
    // the sell window resolve each slot to a name and an icon.
    std::unordered_map<u16, net::InvItem> inventory_;
    std::unordered_map<u16, net::InvItem> storage_;  // Kafra storage contents, by storage index (slot+1)
    std::unordered_map<u16, net::InvItem> cart_;     // pushcart contents, by cart index (slot+2)
    std::unordered_map<u32, net::GroundItem> groundItems_;  // items lying on the floor, by object id
    u32 pendingPickup_ = 0;  // floor item we walked toward to pick up; sent once we're adjacent
    std::vector<net::SkillInfo> skills_;  // learned skills (ZC_SKILLINFO_LIST 0x10f)
    // nameid -> decoded inventory-icon texture, lazily loaded for the shop rows and
    // cached (an invalid entry caches a "no icon" result so we don't re-probe the GRF).
    std::unordered_map<u32, ui::UiImage> itemIcons_;
    std::unordered_map<u32, ui::UiImage> itemCollections_;  // nameid -> large collection art (desc popup), cached
    // RoM item icons (#133): viewer-item-rects.txt maps a RO item id to an atlas page + pixel
    // rect; itemIcon() crops that from the decoded RoM atlas instead of the GRF BMP.
    struct RomIconRect { int page, x, y, w, h; };
    std::unordered_map<u32, RomIconRect> romItemRects_;
    std::unordered_map<int, std::vector<u8>> romAtlasRgba_;  // page -> decoded 1024x1024 RGBA
    void loadRomItemRects();
    const std::vector<u8>* romAtlasPage(Application& app, int page);
    std::unordered_map<std::string, ui::UiImage> skillIcons_;  // skill name -> icon (lazy, cached)
    std::unordered_map<u16, ui::UiImage> statusIcons_;  // SI_ index -> decoded .tga icon (lazy, cached)
    std::set<u16> activeStatus_;  // SI_ indices currently active on the player (from 0x196), for the row
    std::unordered_map<u32, ui::UiImage> guildEmblems_;  // guildId -> emblem bitmap (magenta-keyed)
    std::unordered_map<u32, u32> guildEmblemVer_;        // guildId -> emblem version requested/cached

    std::string status_;
    double time_ = 0.0;
    double lastPing_ = 0.0;
    float zoom_ = 1.0f;     // camera distance multiplier (mouse wheel)
    float azimuth_ = 0.0f;  // camera orbit angle (right-drag, horizontal)
    float pitch_ = 0.0f;    // camera elevation offset (shift+right-drag, vertical)

    // Double-RMB camera reset: glide azimuth/pitch/zoom back to the default view over
    // ~0.5s (smoothstep) instead of snapping. Cancelled if the user orbits/zooms mid-glide.
    bool camAnim_ = false;
    double camAnimStart_ = 0.0;
    float camStartAz_ = 0.0f;   // azimuth at the double-RMB reset start (only direction glides)
    float camTargetAz_ = 0.0f;  // default azimuth on the shortest angular path from the start

    // Camera Lock (Settings / gamepad, #104): a close, low, behind-the-character follow-cam. These
    // hold the eased state (azimuth/zoom/elevation) used by the eye calc while the lock is active;
    // camLockInit_ snaps them to the live view on the frame the lock turns on. Turn rate ~0.15s.
    bool camLockInit_ = false;
    float camLockAz_ = 0.0f, camLockZoom_ = 0.125f, camLockPhi_ = 0.0f;
    float camLockOffset_ = 0.0f;  // azimuth offset (camera angle relative to the char's 'behind') kept while locked
    bool overlayOnTop_ = false;  // draw the cell cursor/path on top (Camera Lock x-ray) (#104)
    // Arrow-key Down = step BACK without turning (moonwalk): hold the facing so the camera doesn't
    // spin. Released by any turn (Left/Right), a forward step (Up), or a mouse walk. (#104)
    bool faceLocked_ = false;
    u8 lockedFace_ = 0;
    bool flatPick_ = false;  // Camera Lock: pick cells off the player's flat floor plane (x-ray) (#104)
    bool playerVisible_ = true;  // last frame's feet-occlusion result; gates the Camera Lock x-ray (#104)
    u8 camHeadingCand_ = 0, camHeadingCommitted_ = 0;  // debounce the lock-cam heading (#104)
    double camHeadingChangedAt_ = 0.0;                 // a heading must persist 0.05s before the cam turns
    double lastNavTurn_ = 0.0;   // throttle held Left/Right turning (one 45 deg step per interval)
    double lastNavReq_ = 0.0;    // throttle re-issuing the continuous forward/back walk
    bool navWalking_ = false;    // an arrow-key walk is in progress (stop it cleanly on release)
};

} // namespace uaro
