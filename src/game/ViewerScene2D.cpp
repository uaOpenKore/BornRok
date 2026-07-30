#include "game/ViewerScene2D.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "app/Application.hpp"
#include "core/math/Math.hpp"
#include "game/CharacterActor.hpp"  // WorldSpritePass
#include "game/EffectFx.hpp"         // effectFxUsedList
#include "render/Shader.hpp"         // load_program
#include "resource/ContentRoute.hpp"
#include "resource/ItemDb.hpp"
#include "resource/Vfs.hpp"

namespace uaro {

namespace {
constexpr float kFovY = 45.0f;
std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
}  // namespace

void ViewerScene2D::onEnter(Application& app) {
    // Sprite billboard pass (mirrors GameScene::initSpritePass) so the .str preview can render.
    layout_.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();
    sampler_ = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);
    bias_ = bgfx::createUniform("u_spriteBias", bgfx::UniformType::Vec4);
    fade_ = bgfx::createUniform("u_spriteFade", bgfx::UniformType::Vec4);
    prog_ = load_program(app.assetDir(), "vs_sprite3d", "fs_sprite3d");

    // Enumerate every .str effect across ALL mounted archives (GRO/UaRO/RoM), not just one tag.
    effects_.clear();
    const std::string base = "data/texture/effect/";
    for (ContentSource src : {ContentSource::Gro, ContentSource::Uaro, ContentSource::Rom}) {
        for (const auto& vp : app.vfs().listFrom(src, base)) {
            if (vp.size() < base.size() + 5) continue;
            // .str AND .ezv (EVFF) effects are both previewable (StrEffect::load handles both).
            if (vp.compare(vp.size() - 4, 4, ".str") != 0 && vp.compare(vp.size() - 4, 4, ".ezv") != 0)
                continue;
            effects_.push_back(vp.substr(base.size(), vp.size() - base.size() - 4));
        }
    }
    // Also scan the LOOSE data/ folder (mountDir), which listFrom (archives only) misses -- S.'s .str
    // effects ship as loose files, so the archive scan came back empty ("Непривязанные пусто").
    {
        std::error_code ec;
        for (const std::string& root : {app.dataDir() + "/data/texture/effect",
                                        app.dataDir() + "/data/texture/effect/"}) {
            std::filesystem::path dir(root);
            if (!std::filesystem::is_directory(dir, ec)) continue;
            for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
                if (ec) break;
                if (!e.is_regular_file(ec)) continue;
                std::string fn = e.path().filename().string();
                const std::string ext = fn.size() > 4 ? lower(fn.substr(fn.size() - 4)) : "";
                if (ext == ".str" || ext == ".ezv")
                    effects_.push_back(fn.substr(0, fn.size() - 4));
            }
            break;  // the first existing form is enough
        }
    }
    std::sort(effects_.begin(), effects_.end());
    effects_.erase(std::unique(effects_.begin(), effects_.end()), effects_.end());

    // Mob/NPC sprite pools (S.: browse NPC/chars/mobs in --view2d like the simple --view). .spr
    // basenames per folder, from every mounted archive + the loose data/ tree.
    auto enumSpr = [&](const std::string& folder, std::vector<std::string>& out) {
        const std::string b = "data/sprite/" + folder + "/";
        for (ContentSource src : {ContentSource::Gro, ContentSource::Uaro, ContentSource::Rom})
            for (const auto& vp : app.vfs().listFrom(src, b)) {
                if (vp.size() < b.size() + 5) continue;
                if (vp.compare(vp.size() - 4, 4, ".spr") != 0) continue;
                out.push_back(vp.substr(b.size(), vp.size() - b.size() - 4));
            }
        // The loose-file scan is a BONUS over the archive listFrom above; guard it hard. Building a
        // std::filesystem::path from a narrow string that carries cp949 folder bytes (the 몬스터 folder)
        // throws std::system_error on MSVC (ACP conversion) -> crashed --view2d on launch (S.). Swallow
        // any filesystem error: archive content already covers these sprites.
        try {
            std::error_code ec;
            std::filesystem::path dir(app.dataDir() + "/" + b);
            if (std::filesystem::is_directory(dir, ec))
                for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
                    if (ec) break;
                    if (!e.is_regular_file(ec)) continue;
                    const std::string fn = e.path().filename().string();
                    if (fn.size() > 4 && lower(fn.substr(fn.size() - 4)) == ".spr")
                        out.push_back(fn.substr(0, fn.size() - 4));
                }
        } catch (const std::exception&) {
            // non-representable path on this OS locale -- archive scan is enough
        }
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
    };
    enumSpr("\xb8\xf3\xbd\xba\xc5\xcd", mobs_);  // 몬스터 monster folder
    enumSpr("npc", npcs_);
    charJobs_ = {0,    1,    2,    3,    4,    5,    6,    7,    8,    9,    10,   11,   12,
                 14,   15,   16,   4008, 4009, 4010, 4011, 4012, 4013, 4015, 4016, 4019, 4049};

    // The effects the client already plays for a server effect-id -> the "Привязанные" tab. These come
    // from the code table (effectFxStr), so this tab is populated even if the raw GRF scan finds nothing.
    used_ = effectFxUsedList();
    usedByName_.clear();
    usedById_.clear();
    for (const auto& [id, nm] : used_) {
        usedByName_[nm] = id;
        usedById_[id] = nm;  // reverse map: effectFxStr is internal to GameScene, so 0x1f3 "+" marks
    }                        // and the read-only "клиент играет" label look the .str up by id here
    // The %d frame-sequence effects (Meteor etc.) also render in-game -> mark them "+" and preview
    // the first frame (a full seq preview would need the runtime player; first frame is enough here).
    for (const auto& [id, first] : effectFxSeqList()) usedById_.emplace(id, first);

    // Item id->name for the "Итем" channel, sorted by id once (ItemDb is an unordered_map).
    items_.assign(app.itemDb().names().begin(), app.itemDb().names().end());
    std::sort(items_.begin(), items_.end());

    loadBindings();
    idField_.maxLen = 6;
    bindField_.maxLen = 40;
    search_.maxLen = 24;
    status_ = std::to_string(effects_.size()) + " .str, " + std::to_string(used_.size()) +
              " используется клиентом";
}

void ViewerScene2D::onExit(Application&) {
    preview_.reset();
    if (bgfx::isValid(prog_)) bgfx::destroy(prog_);
    if (bgfx::isValid(sampler_)) bgfx::destroy(sampler_);
    if (bgfx::isValid(bias_)) bgfx::destroy(bias_);
    if (bgfx::isValid(fade_)) bgfx::destroy(fade_);
    prog_ = BGFX_INVALID_HANDLE;
    sampler_ = bias_ = fade_ = BGFX_INVALID_HANDLE;
    spritePreview_.reset();
}

// Out-of-line so unique_ptr<CharacterActor> (fwd-declared in the header) destructs where the type is
// complete (CharacterActor.hpp is included above).
ViewerScene2D::~ViewerScene2D() = default;

void ViewerScene2D::loadBindings() {
    bindings_.clear();
    std::ifstream in("viewer-effect-bindings.txt");
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        Bind b;
        if (ss >> b.effect >> b.kind >> b.id) bindings_.push_back(b);
    }
}

void ViewerScene2D::saveBindings() {
    std::ofstream out("viewer-effect-bindings.txt");
    if (!out) {
        status_ = "не удалось записать viewer-effect-bindings.txt";
        return;
    }
    out << "# <effect> <item|action> <id> -- --view2d bindings (S.)\n";
    for (const auto& b : bindings_) out << b.effect << ' ' << b.kind << ' ' << b.id << '\n';
}

bool ViewerScene2D::isBound(const std::string& eff) const {
    if (usedByName_.find(eff) != usedByName_.end()) return true;  // wired in client code (effectFxStr)
    for (const auto& b : bindings_)
        if (b.effect == eff) return true;  // bound via the viewer file
    return false;
}

void ViewerScene2D::select(Application& app, const std::string& name) {
    selName_ = name;
    preview_ = std::make_unique<StrEffect>();
    effectSprPreview_.reset();
    if (preview_->load(app.vfs(), name)) {
        status_ = name;
    } else {
        preview_.reset();
        // Not a .str/.ezv -> try a coded effect SPRITE (data/sprite/이펙트/<name>.spr, e.g. Fire Bolt's
        // fireball / Cold Bolt's waterball), which the exe animates for the coded skills.
        effectSprPreview_ = std::make_unique<CharacterActor>();
        if (effectSprPreview_->loadActor(app.vfs(), name, -1)) {
            status_ = name + " (.spr)";
        } else {
            effectSprPreview_.reset();
            status_ = "нет .str/.spr: " + name;
        }
    }
    previewStart_ = time_;
}

void ViewerScene2D::update(Application&, double dt) { time_ += dt; }

void ViewerScene2D::render(Application& app) {
    SpriteBatch& sb = app.sprites();
    if (!sb.ready()) return;
    const Font& font = app.font();
    InputState& in = app.inputMutable();
    // The viewer panels are dark, so text must be LIGHT (kWinText is dark = for the light in-game
    // windows; on this dark bg it was unreadable, S.).
    const u32 kTxt = ui::rgba(232, 232, 238, 255);
    const u32 kDim = ui::rgba(150, 152, 162, 255);

    const int W = app.render().width();
    const int H = app.render().height();
    const float fW = static_cast<float>(W);
    const float fH = static_cast<float>(H);
    sb.begin(W, H, RenderDevice::kUiView);  // UI batch on view 250 (else nothing flushes -> blank screen)

    // ---- 3D pass (view 0): fixed camera at the origin looking down -Z, same as --view.
    const bool homog = bgfx::getCaps()->homogeneousDepth;
    const Mat4 view3d = Mat4::lookAt(Vec3{0, 0, 0}, Vec3{0, 0, -1}, Vec3{0, 1, 0});
    const float aspect = fH > 0 ? fW / fH : 1.0f;
    const Mat4 proj3d = Mat4::perspective(radians(kFovY), aspect, 0.5f, 200.0f, homog);
    bgfx::setViewTransform(0, view3d.m, proj3d.m);
    const float tanY = std::tan(radians(kFovY) * 0.5f);
    const float tanX = tanY * aspect;
    const auto worldAt = [&](float sx, float sy, float d) {
        return Vec3{(2.0f * sx / fW - 1.0f) * d * tanX, (1.0f - 2.0f * sy / fH) * d * tanY, -d};
    };

    // ---- Layout (2D UI on the sprite-batch view, drawn on top of the effect).
    const float pad = 12.0f;
    const float listW = 320.0f;
    // Two header rows in the Effects tab: row 1 = title + content-type tabs, row 2 = the
    // Непривязанные/Привязанные tabs (they used to share row 1 and overlapped the type tabs, S.).
    const float headH = 68.0f;
    const float lx = pad, ly = pad + headH;
    const float lh = fH - ly - pad;
    const float rx = lx + listW + pad;
    const float rw = fW - rx - pad;

    // Header + tabs.
    font.draw(sb, lx, pad + 6.0f, 1.6f, kTxt, "--view2d");
    // Top-level content-type tabs (S.: view NPC/chars/mobs like --view). Effects = the .str tool.
    {
        static const char* const kTypeLbl[4] = {"Эффекты", "Мобы", "NPC", "Чары"};
        const Type kTypeVal[4] = {Type::Effects, Type::Mobs, Type::Npcs, Type::Chars};
        for (int t = 0; t < 4; ++t) {
            const float bx = lx + 96.0f + static_cast<float>(t) * 92.0f;
            if (ui::button(sb, font, in, bx, pad + 4.0f, 88.0f, 26.0f, kTypeLbl[t], 1.0f)) {
                if (type_ != kTypeVal[t]) {
                    type_ = kTypeVal[t];
                    listScroll_ = 0.0f;
                    selName_.clear();
                    selKey_.clear();
                    spriteName_.clear();
                    spritePreview_.reset();
                    preview_.reset();
                }
            }
            if (type_ == kTypeVal[t]) ui::border(sb, bx, pad + 4.0f, 88.0f, 26.0f, ui::rgba(80, 200, 80, 255));
        }
    }
    if (type_ != Type::Effects) {
        renderSpriteBrowse(app);
        sb.end();
        return;
    }
    // Effect channels (S.): four tabs = the four ways the SERVER invokes an effect. Each lists EVERY
    // possible call with a "+" (client renders it) or "-" (nothing yet). Replaces the old
    // Непривязанные/Привязанные split -- the mark carries that state inline now.
    //   0 Действие = ZC_NOTIFY_ACT action-types + level-up (client-coded reactions)
    //   1 Скилл    = skillId visuals (list from GRF skill table -- next step)
    //   2 Итем     = item-use effects (itemskill -- next step)
    //   3 Другие   = 0x1f3 ZC_NOTIFY_EFFECT2 EFFECTID master list (const.txt, 1022 EF_*)
    static const char* const kChanLbl[4] = {"Действие", "Скилл", "Итем", "Другие"};
    for (int c = 0; c < 4; ++c) {
        const float cw = listW / 4.0f;
        const float bx = lx + static_cast<float>(c) * cw;
        if (ui::button(sb, font, in, bx, pad + 34.0f, cw - 4.0f, 26.0f, kChanLbl[c], 1.0f) &&
            boundCat_ != c) {
            boundCat_ = c;
            listScroll_ = 0.0f;
            selName_.clear();
            selKey_.clear();
            selCodeFx_.clear();
            selId_ = -1;
            preview_.reset();
        }
        if (boundCat_ == c) ui::border(sb, bx, pad + 34.0f, cw - 4.0f, 26.0f, ui::rgba(80, 200, 80, 255));
    }

    // ---- Left column: the active channel's call list.
    ui::panel(sb, lx, ly, listW, lh, ui::rgba(28, 28, 34, 235));
    float rowY = ly + 4.0f;
    const float rowH = 20.0f;
    font.draw(sb, lx + 6.0f, rowY + 2.0f, 1.0f, kDim, "поиск:");
    if (in.mousePressed && in.hit(lx + 54.0f, rowY, listW - 62.0f, 18.0f)) focusField_ = 0;
    search_.update(in, focusField_ == 0);
    ui::panel(sb, lx + 54.0f, rowY, listW - 62.0f, 18.0f, ui::rgba(245, 245, 250, 255));
    font.draw(sb, lx + 58.0f, rowY + 2.0f, 1.0f, ui::rgba(20, 20, 26, 255), search_.value);
    rowY += 24.0f;

    const std::string q = lower(search_.value);
    // Rows for the active channel. code = the .str the client code plays for this call (read-only);
    // fx = the .str to preview (code, else a viewer-file binding); has drives the "+/-" mark.
    struct Row { int id; std::string label; bool has; std::string fx; std::string code; };
    std::vector<Row> rows;
    auto boundFx = [&](const std::string& kind, int id) -> std::string {  // viewer-file binding for a call
        for (const auto& b : bindings_)
            if (b.kind == kind && b.id == id) return b.effect;
        return {};
    };
    auto passes = [&](const std::string& name, int id) {
        if (q.empty()) return true;  // skip the per-row lower() when not searching (Итем has ~10k rows)
        return lower(name).find(q) != std::string::npos ||
               std::to_string(id).find(q) != std::string::npos;
    };
    auto makeRow = [&](int id, const std::string& name, const std::string& kind,
                       const std::string& code, bool weather) {
        const std::string bound = code.empty() ? boundFx(kind, id) : std::string();
        const std::string fx = !code.empty() ? code : bound;  // previewable .str
        const bool has = !fx.empty() || weather;
        rows.push_back({id, std::string(has ? "+ " : "- ") + name + "  (" + std::to_string(id) + ")",
                        has, weather ? std::string() : fx, code});
    };
    if (boundCat_ == 3) {  // 0x1f3 EFFECTID master list -- every EF_ the server can send
        for (const auto& [id, name] : effectConstList()) {
            if (!passes(name, id)) continue;
            std::string code;
            if (auto it = usedById_.find(id); it != usedById_.end()) code = it->second;  // client renders it
            makeRow(id, name, "effect", code, id == 161 || id == 162 || id == 163 || id == 333);
        }
    } else if (boundCat_ == 0) {  // ZC_NOTIFY_ACT action-types + level-up. code = the .str the client
        // already plays (level-up: angel/joblvup, GameScene). The rest render nothing yet -> "-",
        // bindable so S. can attach an animation (крит и т.п.).
        static const struct { int id; const char* ru; const char* code; } kAct[] = {
            {0, "Обычный удар / промах", ""}, {8, "Мульти-хит", ""}, {10, "Крит", ""},
            {11, "Lucky dodge", ""},          {1, "Подбор предмета", ""},
            {2, "Сесть", ""},                 {3, "Встать", ""},
            {-100, "Лвл-ап (base)", "angel"}, {-101, "Лвл-ап (job)", "joblvup"},
        };
        for (const auto& a : kAct)
            if (passes(a.ru, a.id)) makeRow(a.id, a.ru, "action", a.code, false);
    } else if (boundCat_ == 1) {  // Скилл: every skill from the server skill_db, bindable
        for (const auto& [id, name] : skillConstList())
            if (passes(name, id))
                makeRow(id, name, "skill", skillFxName(id), false);  // real client effect -> "+/-" mark
    } else {  // Итем: every item from the client ItemDb, bindable (use effect on ZC_AUTORUN_SKILL etc.)
        for (const auto& [id, name] : items_)
            if (passes(name, static_cast<int>(id)))
                makeRow(static_cast<int>(id), name, "item", {}, false);
    }

    const float listTop = rowY, listBot = ly + lh - 4.0f;
    const float viewH = listBot - listTop;
    const float contentH = static_cast<float>(rows.size()) * rowH;
    const float maxScroll = std::max(0.0f, contentH - viewH);
    // Pick a row: highlight it, load its .str into the preview (or clear it for unbound/coded calls).
    auto pickRow = [&](const Row& r) {
        selId_ = r.id;
        selKey_ = r.label;
        selCodeFx_ = r.code;
        if (!r.fx.empty()) {
            select(app, r.fx);
        } else {
            preview_.reset();
            selName_.clear();
        }
        bindField_.value.clear();
        bindField_.caret = 0;
    };
    // Up/Down arrows step the selection through the list + auto-scroll to keep it visible (S.).
    if ((in.keyUp || in.keyDown) && !rows.empty()) {
        int cur = -1;
        for (usize i = 0; i < rows.size(); ++i)
            if (rows[i].label == selKey_) { cur = static_cast<int>(i); break; }
        int nx = (cur < 0) ? (in.keyDown ? 0 : static_cast<int>(rows.size()) - 1)
                           : cur + (in.keyDown ? 1 : -1);
        nx = std::clamp(nx, 0, static_cast<int>(rows.size()) - 1);
        pickRow(rows[static_cast<usize>(nx)]);
        const float rowTop = static_cast<float>(nx) * rowH;
        if (rowTop < listScroll_) listScroll_ = rowTop;
        else if (rowTop + rowH > listScroll_ + viewH) listScroll_ = rowTop + rowH - viewH;
    }
    if (in.wheel != 0.0f && in.hit(lx, ly, listW, lh))
        listScroll_ = std::clamp(listScroll_ - in.wheel * 40.0f, 0.0f, maxScroll);
    listScroll_ = std::clamp(listScroll_, 0.0f, maxScroll);
    float ry2 = listTop - listScroll_;
    for (const auto& r : rows) {
        if (ry2 + rowH >= listTop && ry2 <= listBot) {
            const bool seld = r.label == selKey_;
            if (seld) ui::panel(sb, lx + 2.0f, ry2, listW - 8.0f, rowH, ui::rgba(60, 90, 160, 255));
            if (in.mousePressed && in.hit(lx + 2.0f, ry2, listW - 8.0f, rowH) && r.id >= -101) pickRow(r);
            // "+" green, "-" dim so coverage reads at a glance.
            const u32 col = r.has ? ui::rgba(120, 220, 120, 255) : kDim;
            font.draw(sb, lx + 8.0f, ry2 + 2.0f, 1.0f, r.id < -101 ? kDim : col, r.label);
        }
        ry2 += rowH;
    }
    // Count line: how many of this channel's calls have an effect.
    {
        int have = 0;
        for (const auto& r : rows) if (r.has) ++have;
        font.draw(sb, lx + 6.0f, listBot - 2.0f, 0.9f, kDim,
                  std::to_string(have) + " с эффектом / " + std::to_string(rows.size()));
    }
    // Scrollbar (right edge) when the content overflows; draggable.
    if (maxScroll > 0.0f) {
        const float sbx = lx + listW - 6.0f, sbw = 4.0f;
        ui::panel(sb, sbx, listTop, sbw, viewH, ui::rgba(0, 0, 0, 90));
        const float thumbH = std::max(20.0f, viewH * viewH / contentH);
        const float thumbY = listTop + (viewH - thumbH) * (maxScroll > 0.0f ? listScroll_ / maxScroll : 0.0f);
        ui::panel(sb, sbx, thumbY, sbw, thumbH, ui::rgba(150, 150, 165, 220));
        if (in.mouseDown && in.hit(sbx - 4.0f, listTop, sbw + 10.0f, viewH)) {
            const float f = std::clamp((static_cast<float>(in.mouseY) - listTop) / viewH, 0.0f, 1.0f);
            listScroll_ = f * maxScroll;
        }
    }

    // ---- Right column: the preview + the bind/unbind controls.
    ui::border(sb, rx, ly, rw, lh - 120.0f, ui::rgba(120, 120, 130, 255));
    if (preview_ && preview_->ready()) {
        const double dur = std::max(0.2, preview_->duration());
        double e = time_ - previewStart_;
        if (e > dur) {  // loop
            previewStart_ = time_ - std::fmod(e, dur);
            e = std::fmod(e, dur);
        }
        WorldSpritePass pass;
        pass.view = 0;
        pass.program = prog_;
        pass.sampler = sampler_;
        pass.bias = bias_;
        pass.fade = fade_;
        pass.layout = &layout_;
        pass.right = {1, 0, 0};
        pass.up = {0, 1, 0};
        pass.toCamera = {0, 0, 1};
        const float cx = rx + rw * 0.5f, cy = ly + (lh - 120.0f) * 0.6f;
        if (bgfx::isValid(prog_)) preview_->render(pass, worldAt(cx, cy, 25.0f), e);
    } else if (effectSprPreview_ && effectSprPreview_->ready()) {
        // Coded effect .spr (fireball/waterball) preview -- drawn like the mob/NPC sprite preview.
        const float cx = rx + rw * 0.5f, cy = ly + (lh - 120.0f) * 0.6f;
        effectSprPreview_->renderScaled(sb, cx, cy, 2.5f, time_);
    } else if (selId_ != -1) {
        font.draw(sb, rx + 10.0f, ly + 10.0f, 1.1f, kDim, "нет эффекта на этом вызове");
    } else {
        font.draw(sb, rx + 10.0f, ly + 10.0f, 1.1f, kDim, "выбери вызов слева");
    }

    // Controls under the preview: attach / detach a .str to the SELECTED server call.
    const float cyB = ly + lh - 108.0f;
    // Every channel is .str-bindable, so S. can attach an animation to a call that plays nothing yet
    // (крит, a skill, an item-use). Calls the client already renders (level-up) are read-only (selCodeFx_).
    if (selId_ != -1 && boundCat_ >= 0 && boundCat_ <= 3) {
        static const char* const kKind[4] = {"action", "skill", "item", "effect"};
        const std::string kind = kKind[boundCat_];
        const std::string curBound = boundFx(kind, selId_);
        if (!selCodeFx_.empty()) {  // already wired in client code -> read-only
            font.draw(sb, rx, cyB, 1.1f, kTxt, "клиент играет: " + selCodeFx_ + ".str (в коде)");
        } else if (!curBound.empty()) {  // bound via the viewer file -> allow detach
            font.draw(sb, rx, cyB, 1.1f, kTxt, "привязан: " + curBound + ".str");
            if (ui::button(sb, font, in, rx, cyB + 26.0f, 120.0f, 26.0f, "Отвязать", 1.1f)) {
                bindings_.erase(std::remove_if(bindings_.begin(), bindings_.end(),
                                               [&](const Bind& b) {
                                                   return b.kind == kind && b.id == selId_;
                                               }),
                                bindings_.end());
                saveBindings();
                status_ = "отвязан " + kind + " " + std::to_string(selId_);
                preview_.reset();
                selName_.clear();
            }
        } else {  // unbound -> type a .str basename, live-preview, Привязать
            font.draw(sb, rx, cyB, 1.0f, kDim,
                      "нет эффекта. привязать .str к " + kind + " " + std::to_string(selId_) + ":");
            if (in.mousePressed && in.hit(rx, cyB + 24.0f, 200.0f, 20.0f)) focusField_ = 2;
            bindField_.update(in, focusField_ == 2);
            ui::panel(sb, rx, cyB + 24.0f, 200.0f, 20.0f, ui::rgba(245, 245, 250, 255));
            font.draw(sb, rx + 4.0f, cyB + 26.0f, 1.0f, ui::rgba(20, 20, 26, 255), bindField_.value);
            // Live-preview the typed name once it resolves to a .str.
            if (!bindField_.value.empty() && lower(bindField_.value) != selName_)
                select(app, lower(bindField_.value));
            if (ui::button(sb, font, in, rx, cyB + 52.0f, 120.0f, 26.0f, "Привязать", 1.1f) && preview_ &&
                preview_->ready()) {
                Bind b;
                b.effect = lower(bindField_.value);
                b.kind = kind;
                b.id = selId_;
                bindings_.push_back(b);
                saveBindings();
                status_ = "привязан " + b.effect + " -> " + kind + " " + std::to_string(selId_);
            }
        }
    }

    // Status line.
    font.draw(sb, lx, fH - pad + 2.0f, 1.0f, kDim, status_);

    sb.end();  // flush the UI batch (view 250)
}

// Mobs/Npcs/Chars browser (S.: "как в простом вьювере"): a searchable list on the left, the selected
// sprite's live 2D animation on the right. Called from render() with the UI batch already begun.
void ViewerScene2D::renderSpriteBrowse(Application& app) {
    SpriteBatch& sb = app.sprites();
    const Font& font = app.font();
    InputState& in = app.inputMutable();
    const u32 kTxt = ui::rgba(232, 232, 238, 255);
    const u32 kDim = ui::rgba(150, 152, 162, 255);
    const float fW = static_cast<float>(app.render().width());
    const float fH = static_cast<float>(app.render().height());
    const float pad = 12.0f, listW = 320.0f, headH = 40.0f;
    const float lx = pad, ly = pad + headH, lh = fH - ly - pad;
    const float rx = lx + listW + pad, rw = fW - rx - pad;

    // Rows for the active type: {selectKey, label}. Chars key = job id string.
    std::vector<std::pair<std::string, std::string>> rows;
    if (type_ == Type::Mobs)
        for (const auto& n : mobs_) rows.push_back({n, n});
    else if (type_ == Type::Npcs)
        for (const auto& n : npcs_) rows.push_back({n, n});
    else
        for (u16 j : charJobs_) rows.push_back({std::to_string(j), "job " + std::to_string(j)});

    ui::panel(sb, lx, ly, listW, lh, ui::rgba(28, 28, 34, 235));
    float rowY = ly + 4.0f;
    const float rowH = 20.0f;
    // Search box.
    font.draw(sb, lx + 6.0f, rowY + 2.0f, 1.0f, kDim, "поиск:");
    if (in.mousePressed && in.hit(lx + 54.0f, rowY, listW - 62.0f, 18.0f)) focusField_ = 0;
    search_.update(in, focusField_ == 0);
    ui::panel(sb, lx + 54.0f, rowY, listW - 62.0f, 18.0f, ui::rgba(245, 245, 250, 255));
    font.draw(sb, lx + 58.0f, rowY + 2.0f, 1.0f, ui::rgba(20, 20, 26, 255), search_.value);
    rowY += 24.0f;
    const std::string q = lower(search_.value);
    std::vector<std::pair<std::string, std::string>> vis;
    for (auto& r : rows)
        if (q.empty() || lower(r.second).find(q) != std::string::npos) vis.push_back(r);

    const float listTop = rowY, listBot = ly + lh - 4.0f, viewH = listBot - listTop;
    const float contentH = static_cast<float>(vis.size()) * rowH;
    const float maxScroll = std::max(0.0f, contentH - viewH);

    // Load (or reload) the sprite for a row key -> the right-pane preview.
    auto loadSel = [&](const std::string& key) {
        if (key == spriteName_) return;
        selName_ = key;
        spriteName_ = key;
        spritePreview_ = std::make_unique<CharacterActor>();
        bool ok = false;
        if (type_ == Type::Chars)
            ok = spritePreview_->load(app.vfs(), static_cast<u16>(std::stoi(key)), 0, 1);
        else
            ok = spritePreview_->loadActor(app.vfs(), key, type_ == Type::Mobs ? 1002 : 100);
        if (!ok) {
            spritePreview_.reset();
            status_ = "нет спрайта: " + key;
        } else {
            status_ = key;
        }
    };

    // Up/Down arrows step the selection through the list + auto-scroll to keep it visible (S.).
    if ((in.keyUp || in.keyDown) && !vis.empty()) {
        int cur = -1;
        for (usize i = 0; i < vis.size(); ++i)
            if (vis[i].first == selName_) { cur = static_cast<int>(i); break; }
        int nx = (cur < 0) ? (in.keyDown ? 0 : static_cast<int>(vis.size()) - 1)
                           : cur + (in.keyDown ? 1 : -1);
        nx = std::clamp(nx, 0, static_cast<int>(vis.size()) - 1);
        loadSel(vis[static_cast<usize>(nx)].first);
        const float rowTop = static_cast<float>(nx) * rowH;
        if (rowTop < listScroll_) listScroll_ = rowTop;
        else if (rowTop + rowH > listScroll_ + viewH) listScroll_ = rowTop + rowH - viewH;
    }
    if (in.wheel != 0.0f && in.hit(lx, ly, listW, lh))
        listScroll_ = std::clamp(listScroll_ - in.wheel * 40.0f, 0.0f, maxScroll);
    listScroll_ = std::clamp(listScroll_, 0.0f, maxScroll);
    float ry2 = listTop - listScroll_;
    for (const auto& [key, label] : vis) {
        if (ry2 + rowH >= listTop && ry2 <= listBot) {
            const bool seld = key == selName_;
            if (seld) ui::panel(sb, lx + 2.0f, ry2, listW - 8.0f, rowH, ui::rgba(60, 90, 160, 255));
            if (in.mousePressed && in.hit(lx + 2.0f, ry2, listW - 8.0f, rowH)) loadSel(key);
            font.draw(sb, lx + 8.0f, ry2 + 2.0f, 1.0f, kTxt, label);
        }
        ry2 += rowH;
    }
    if (maxScroll > 0.0f) {
        const float sbx = lx + listW - 6.0f, sbw = 4.0f;
        ui::panel(sb, sbx, listTop, sbw, viewH, ui::rgba(0, 0, 0, 90));
        const float thumbH = std::max(20.0f, viewH * viewH / contentH);
        const float thumbY = listTop + (viewH - thumbH) * (listScroll_ / maxScroll);
        ui::panel(sb, sbx, thumbY, sbw, thumbH, ui::rgba(150, 150, 165, 220));
    }

    // Preview pane: the selected sprite's live 2D animation.
    ui::border(sb, rx, ly, rw, lh, ui::rgba(120, 120, 130, 255));
    if (spritePreview_) {
        const float cx = rx + rw * 0.5f, cy = ly + lh * 0.58f;
        spritePreview_->renderScaled(sb, cx, cy, 2.5f, time_);
        font.draw(sb, rx + 10.0f, ly + 10.0f, 1.2f, kTxt, spriteName_);
    } else {
        font.draw(sb, rx + 10.0f, ly + 10.0f, 1.1f, kDim,
                  selName_.empty() ? "выбери слева" : ("нет превью: " + selName_));
    }
    font.draw(sb, lx, fH - pad + 2.0f, 1.0f, kDim,
              std::to_string(vis.size()) + " / " + std::to_string(rows.size()));
}

}  // namespace uaro
