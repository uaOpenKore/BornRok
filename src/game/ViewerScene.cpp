#include "game/ViewerScene.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <set>

#include "app/Application.hpp"
#include "core/Log.hpp"
#include "formats/UnityFs.hpp"
#include "formats/UnitySerialized.hpp"
#include "formats/UnityTexture.hpp"
#include <bimg/bimg.h>
#include <bx/allocator.h>
#include "game/ActorSprites.hpp"
#include "net/JobNames.hpp"
#include "render/RenderDevice.hpp"
#include "ui/Font.hpp"

namespace uaro {

namespace {

constexpr float kTopBarH = 64.0f;    // category buttons + tab buttons
constexpr float kRowH = 375.0f;      // one list row (2D | 3D); x2.5 (S.: model/sprite cut off)
constexpr float kSearchW = 360.0f;   // right search panel on the unmapped tab
constexpr float kCamDist = 25.0f;    // 3D preview depth (fixed camera at the origin)
constexpr float kFovY = 20.0f;       // narrow FOV: keeps edge previews near-orthographic
constexpr int kMaxLoadsPerFrame = 2; // sprite/model uploads per frame (no scroll hitches)

// The category a RoM body-bundle vpath belongs to, by its subfolder.
bool bundleIsMob(const std::string& vpath) {
    return vpath.find("/monster/") != std::string::npos ||
           vpath.find("/mvp/") != std::string::npos ||
           vpath.find("/miniboss/") != std::string::npos;
}

std::string baseName(const std::string& vpath) {
    const usize s = vpath.find_last_of('/');
    std::string n = s == std::string::npos ? vpath : vpath.substr(s + 1);
    if (n.size() > 8 && n.compare(n.size() - 8, 8, ".unity3d") == 0) n.resize(n.size() - 8);
    return n;
}

}  // namespace

void ViewerScene::onEnter(Application& app) {
    buildLists(app);
    loadSquares();
    log::info("--view: mobs {} mapped / {} sprites without model / {} RoM without pair; "
              "npcs {} / {} / {}; search pool {} sprites",
              mapped_[kCatMobs].size(), spritesWithoutModel_[kCatMobs],
              unmappedRom_[kCatMobs].size(), mapped_[kCatNpcs].size(),
              spritesWithoutModel_[kCatNpcs], unmappedRom_[kCatNpcs].size(),
              allSprites_.size());
}

void ViewerScene::onExit(Application& app) {
    (void)app;
    spriteCache_.clear();
    romCache_.clear();
    romBundleCache_.clear();
    RomActor::shutdownShared();
}

void ViewerScene::buildLists(Application& app) {
    // 1) Walk the generated id->sprite table; a sprite that resolves to a RoM bundle goes to
    // the mapped list of its category, the rest are only counted (the unmapped tab browses
    // RoM models, not sprites — S.).
    std::set<std::string> seenSprite;    // the table aliases many ids to one sprite
    std::set<std::string> takenBundles;  // bundles already claimed by a sprite
    for (usize i = 0; i < actorSpriteCount(); ++i) {
        const ActorSpriteEntry e = actorSpriteAt(i);
        std::string name = e.name;
        for (char& c : name) c = static_cast<char>(std::tolower(static_cast<u8>(c)));
        if (!seenSprite.insert(name).second) continue;
        const bool mob = e.id >= 1000 && e.id < 4000;
        const Cat cat = mob ? kCatMobs : kCatNpcs;
        const std::string bundle = rombind::resolveBundle(app, name);
        if (bundle.empty()) {
            ++spritesWithoutModel_[cat];
            continue;
        }
        mapped_[cat].push_back({name, bundle, e.id});
        // listFrom() returns NORMALIZED (lowercase) vpaths; resolveBundle keeps the authored
        // case — normalize before subtracting or nothing ever matches and every mapped model
        // shows up again in the unmapped tab (S.'s first pairs file was mostly duplicates).
        std::string norm = bundle;
        for (char& c : norm) c = static_cast<char>(std::tolower(static_cast<u8>(c)));
        takenBundles.insert(std::move(norm));
    }
    for (auto& v : mapped_) {
        std::sort(v.begin(), v.end(),
                  [](const MappedRow& a, const MappedRow& b) { return a.sprite < b.sprite; });
    }

    // 2) RoM bundles nothing maps to. Skip companions (_ext), colour/lod variants and the
    // nested per-variant subfolders — they are skins of a base bundle, not standalone mobs.
    const std::string root = "android/art/model/role/body/";
    for (const std::string& p : app.vfs().listFrom(ContentSource::Rom, root)) {
        if (p.size() < 8 || p.compare(p.size() - 8, 8, ".unity3d") != 0) continue;
        const std::string rel = p.substr(root.size());
        const usize firstSlash = rel.find('/');
        const std::string sub = firstSlash == std::string::npos ? "" : rel.substr(0, firstSlash);
        if (sub != "monster" && sub != "mvp" && sub != "miniboss" && sub != "npc") continue;
        if (rel.find('/', firstSlash + 1) != std::string::npos) continue;  // variant subfolder
        const std::string bn = baseName(p);
        if (bn.size() > 4 && bn.compare(bn.size() - 4, 4, "_ext") == 0) continue;
        if (bn.find("_lod") != std::string::npos) continue;
        if (bn.size() > 3 && bn.compare(bn.size() - 3, 3, "_ol") == 0) continue;
        if (takenBundles.count(p)) continue;
        unmappedRom_[bundleIsMob(p) ? kCatMobs : kCatNpcs].push_back(p);
    }
    for (auto& v : unmappedRom_) std::sort(v.begin(), v.end());

    // 3) Chars: the playable jobs (classic + trans; duplicates and event skins skipped).
    charJobs_ = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10,  11,  12,  14,  15,  16,
                 17, 18, 19, 20, 23, 24, 25, 4046, 4047, 4049,
                 // Transcendent 2nd (high classes):
                 4008, 4009, 4010, 4011, 4012, 4013, 4015, 4016, 4017, 4018, 4019, 4020, 4021};

    // 4) Sprite search pool: every .spr base name in the mounted data (all folders — the
    // point of the search is finding a 2D counterpart wherever it lives).
    std::set<std::string> pool;
    for (const std::string& p : app.vfs().listFrom(ContentSource::Uaro, "data/sprite/")) {
        if (p.size() < 4 || p.compare(p.size() - 4, 4, ".spr") != 0) continue;
        std::string bn = baseName(p);
        bn.resize(bn.size() - 4);  // baseName kept ".spr"? no: strips only .unity3d — trim here
        pool.insert(bn);
    }
    allSprites_.assign(pool.begin(), pool.end());

    // 5) Hat models for the manual headgear binder (#135): the two head folders, base bundles.
    for (const char* dir : {"android/art/role/parts/head/", "android/art/model/role/head/"})
        for (const std::string& p : app.vfs().listFrom(ContentSource::Rom, dir)) {
            if (p.size() < 8 || p.compare(p.size() - 8, 8, ".unity3d") != 0) continue;
            const std::string bn = baseName(p);
            if (bn.size() > 4 && bn.compare(bn.size() - 4, 4, "_ext") == 0) continue;
            if (bn.find("_lod") != std::string::npos) continue;
            if (bn.size() > 3 && bn.compare(bn.size() - 3, 3, "_ol") == 0) continue;
            // store the AUTHORED-case dir prefix for readFrom
            hats_.push_back({std::string(dir).find("parts") != std::string::npos
                                 ? "Android/art/role/parts/head/"
                                 : "Android/art/model/role/head/",
                             bn});
        }
    std::sort(hats_.begin(), hats_.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    // 6) Effects tab (#132): every RoM effect bundle base name (for the item->effect binder), plus
    // the current bindings from rom-item-effects.txt.
    for (const std::string& p :
         app.vfs().listFrom(ContentSource::Rom, "android/resources/public/effect/skill/")) {
        if (p.size() < 8 || p.compare(p.size() - 8, 8, ".unity3d") != 0) continue;
        std::string bn = baseName(p);
        if (bn.find("_lod") != std::string::npos) continue;  // simplified LOD copies
        effectNames_.push_back(bn);
    }
    std::sort(effectNames_.begin(), effectNames_.end());
    for (usize i = 0; i < effectNames_.size(); ++i) effectHits_.push_back(i);  // show all until a search
    loadEffectPairs();
}

void ViewerScene::loadEffectPairs() {
    effectPairs_.clear();
    std::ifstream in("rom-item-effects.txt");
    std::string line;
    while (std::getline(in, line)) {
        std::string raw = line;
        if (const auto h = raw.find('#'); h != std::string::npos) raw.resize(h);
        int id = 0;
        char name[128] = {0};
        if (std::sscanf(raw.c_str(), " %d = %127s", &id, name) == 2 && id > 0 && name[0])
            effectPairs_.push_back({std::to_string(id), name});
    }
}

void ViewerScene::writeEffectPairs() {
    std::ofstream out("rom-item-effects.txt");
    out << "# RoM-эффекты предметов (#132): <itemId> = <имя_эффекта>. Редактируется из --view.\n";
    for (const auto& [id, name] : effectPairs_) out << id << " = " << name << "\n";
    effectStatus_ = "сохранено (" + std::to_string(effectPairs_.size()) + ")";
}

RomActor* ViewerScene::hatFor(Application& app, const std::string& dir, const std::string& name) {
    auto it = hatCache_.find(name);
    if (it != hatCache_.end()) return it->second.get();
    if (loadsThisFrame_ >= kMaxLoadsPerFrame) return nullptr;
    ++loadsThisFrame_;
    std::unique_ptr<RomActor> actor;
    // Build a fresh novice_m wearing the hat: body+face+hair, then attach the head model.
    if (auto bb = app.vfs().readFrom(ContentSource::Rom,
                                     "Android/art/model/role/body/player/novice_m.unity3d")) {
        RomModel body;
        body.wantName = "novice_m";
        if (appendRomBundle(body, *bb)) {
            if (auto ex = app.vfs().readFrom(
                    ContentSource::Rom,
                    "Android/art/model/role/body/player/novice_m_ext.unity3d"))
                appendRomBundle(body, *ex);
            finalizeRomModel(body);
            if (body.mesh.vertexCount != 0 && !body.skeleton.parents.empty()) {
                auto ra = std::make_unique<RomActor>();
                if (ra->load(app, std::move(body))) {
                    RomModel hat;
                    if (auto hb = app.vfs().readFrom(ContentSource::Rom, dir + name + ".unity3d")) {
                        appendRomBundle(hat, *hb);
                        if (auto he = app.vfs().readFrom(ContentSource::Rom,
                                                         dir + name + "_ext.unity3d"))
                            appendRomBundle(hat, *he);
                    }
                    if (hat.mesh.vertexCount != 0) ra->attachSkinnedPart(app, hat, "CP_1");
                    actor = std::move(ra);
                }
            }
        }
    }
    return hatCache_.emplace(name, std::move(actor)).first->second.get();
}

CharacterActor* ViewerScene::spriteFor(Application& app, const std::string& name, int classId) {
    auto it = spriteCache_.find(name);
    if (it != spriteCache_.end()) return it->second.get();
    if (loadsThisFrame_ >= kMaxLoadsPerFrame) return nullptr;  // retry next frame
    ++loadsThisFrame_;
    auto actor = std::make_unique<CharacterActor>();
    if (!actor->loadActor(app.vfs(), name, classId)) actor.reset();
    return spriteCache_.emplace(name, std::move(actor)).first->second.get();
}

RomActor* ViewerScene::modelFor(Application& app, const std::string& name) {
    if (romCache_.count(name) == 0) {
        if (loadsThisFrame_ >= kMaxLoadsPerFrame) return nullptr;
        ++loadsThisFrame_;
    }
    return rombind::actorFor(app, name, romCache_);
}

RomActor* ViewerScene::modelForBundle(Application& app, const std::string& bundleVpath) {
    // The unmapped list loads by the bundle BASENAME through the shared loader (same dirs
    // table finds the same bundle; texture/clip companions resolve off that name too). A
    // separate cache avoids colliding with sprite-name entries in romCache_.
    const std::string key = baseName(bundleVpath);
    auto it = romBundleCache_.find(key);
    if (it != romBundleCache_.end()) return it->second.get();
    if (loadsThisFrame_ >= kMaxLoadsPerFrame) return nullptr;
    ++loadsThisFrame_;
    return rombind::actorFor(app, key, romBundleCache_);
}

void ViewerScene::savePair(Application& app, const std::string& bundle, const std::string& sprite) {
    (void)app;
    std::ofstream out("viewer-pairs.txt", std::ios::app);
    if (!out) {
        status_ = "не удалось записать viewer-pairs.txt";
        return;
    }
    out << sprite << " = " << baseName(bundle) << "\n";
    status_ = "связано: " + sprite + " = " + baseName(bundle);
    log::info("--view: pair saved: {} = {}", sprite, baseName(bundle));
}

void ViewerScene::removePair(Application& app, const std::string& sprite) {
    (void)app;
    // Record the unpair for the dev to merge (mirrors savePair's append workflow) and reflect it
    // live: drop the row from the mapped list and return its bundle to the unmapped browse list so
    // it can be re-paired with a better model (S.: кнопка «отвязать»).
    std::ofstream out("viewer-unpair.txt", std::ios::app);
    if (out) out << sprite << "\n";
    auto& v = mapped_[cat_];
    const auto it =
        std::find_if(v.begin(), v.end(), [&](const MappedRow& r) { return r.sprite == sprite; });
    if (it != v.end()) {
        const std::string b = it->bundle;
        if (std::find(unmappedRom_[cat_].begin(), unmappedRom_[cat_].end(), b) ==
            unmappedRom_[cat_].end()) {
            unmappedRom_[cat_].push_back(b);
            std::sort(unmappedRom_[cat_].begin(), unmappedRom_[cat_].end());
        }
        v.erase(it);
    }
    selMappedSprite_.clear();
    status_ = "отвязано: " + sprite;
    log::info("--view: unpair recorded: {}", sprite);
}

void ViewerScene::loadSquares() {
    squares_.clear();
    std::ifstream in("viewer-item-rects.txt");
    std::string line;
    while (std::getline(in, line)) {
        ItemSquare s{};
        char id[64] = {0};
        // "page x y w h [id]"
        const int n = std::sscanf(line.c_str(), "%d %d %d %d %d %63s", &s.page, &s.x, &s.y,
                                  &s.w, &s.h, id);
        if (n >= 5) {
            if (n >= 6) s.id = id;
            squares_.push_back(std::move(s));
        }
    }
}

void ViewerScene::saveSquares() {
    std::ofstream out("viewer-item-rects.txt");
    for (const auto& s : squares_)
        out << s.page << " " << s.x << " " << s.y << " " << s.w << " " << s.h
            << (s.id.empty() ? "" : " " + s.id) << "\n";
}

Texture* ViewerScene::loadAtlas(Application& app, int page) {
    if (auto it = atlasCache_.find(page); it != atlasCache_.end())
        return it->second.valid() ? &it->second : nullptr;
    Texture& slot = atlasCache_[page];
    const std::string path =
        "Android/resources/gui/atlas/picture/item_" + std::to_string(page + 1) + ".unity3d";
    auto bytes = app.vfs().readFrom(ContentSource::Rom, path);
    if (!bytes) return nullptr;
    UnityFsBundle bundle;
    if (!bundle.parse(*bytes)) return nullptr;
    auto resRead = [&](const std::string& p, u64 off,
                       u32 size) -> std::optional<std::vector<u8>> {
        for (usize i = 0; i < bundle.nodes().size(); ++i)
            if (bundle.nodes()[i].path == p) {
                auto d = bundle.nodeData(i);
                if (!d || off + size > d->size()) return std::nullopt;
                return std::vector<u8>(d->begin() + static_cast<std::ptrdiff_t>(off),
                                       d->begin() + static_cast<std::ptrdiff_t>(off + size));
            }
        return std::nullopt;
    };
    for (usize ni = 0; ni < bundle.nodes().size(); ++ni) {
        if (!(bundle.nodes()[ni].flags & 4)) continue;
        auto d = bundle.nodeData(ni);
        UnitySerializedFile sf;
        if (!d || !sf.parse(*d)) continue;
        for (usize oi = 0; oi < sf.objects().size(); ++oi) {
            if (sf.objects()[oi].classId != 28) continue;
            auto od = sf.objectData(oi);
            if (!od) continue;
            auto tex = parseUnityTexture2D(*od, resRead);
            if (!tex || tex->data.empty()) continue;
            // The item atlases are ASTC 4x4 (fmt 48) — decode to RGBA8 on the CPU.
            bimg::TextureFormat::Enum bf = bimg::TextureFormat::ASTC4x4;
            static bx::DefaultAllocator alloc;
            const u32 w = static_cast<u32>(tex->width), h = static_cast<u32>(tex->height);
            std::vector<u8> rgba(static_cast<usize>(w) * h * 4);
            bimg::imageDecodeToRgba8(&alloc, rgba.data(), tex->data.data(), w, h, w * 4, bf);
            // Unity textures are bottom-up — flip vertically so the atlas reads right-side up
            // (S.: "картинка перевёрнута"). Keeps all rect/preview coords in top-down space.
            const usize row = static_cast<usize>(w) * 4;
            std::vector<u8> tmp(row);
            for (u32 y = 0; y < h / 2; ++y) {
                u8* a = rgba.data() + static_cast<usize>(y) * row;
                u8* b = rgba.data() + static_cast<usize>(h - 1 - y) * row;
                std::memcpy(tmp.data(), a, row);
                std::memcpy(a, b, row);
                std::memcpy(b, tmp.data(), row);
            }
            slot.create(static_cast<u16>(w), static_cast<u16>(h), rgba.data(), true);
            return slot.valid() ? &slot : nullptr;
        }
    }
    return nullptr;
}

void ViewerScene::update(Application& app, double dt) {
    time_ += dt;
    if (app.input().escape || app.input().closeRequested) app.requestQuit();
}

void ViewerScene::render(Application& app) {
    SpriteBatch& sb = app.sprites();
    const Font& font = app.font();
    if (!sb.ready()) return;
    const InputState& in = app.input();
    loadsThisFrame_ = 0;

    const int W = app.render().width();
    const int H = app.render().height();
    const float fW = static_cast<float>(W), fH = static_cast<float>(H);

    // ---- 3D pass setup (view 0): fixed camera at the origin looking down -Z. Each visible
    // row computes the world point that projects to its cell centre and stands the model
    // there, so 2D UI (view 250) and 3D previews line up.
    const bool homog = bgfx::getCaps()->homogeneousDepth;
    const Mat4 view3d = Mat4::lookAt(Vec3{0, 0, 0}, Vec3{0, 0, -1}, Vec3{0, 1, 0});
    const float aspect = fH > 0 ? fW / fH : 1.0f;
    const Mat4 proj3d = Mat4::perspective(radians(kFovY), aspect, 0.5f, 200.0f, homog);
    bgfx::setViewTransform(0, view3d.m, proj3d.m);
    const float tanY = std::tan(radians(kFovY) * 0.5f);
    const float tanX = tanY * aspect;
    {
        const float ld[4] = {-0.35f, -0.8f, -0.45f, 0.0f};
        const float am[4] = {0.62f, 0.62f, 0.66f, 1.0f};
        const float dc[4] = {0.85f, 0.83f, 0.80f, 1.0f};
        RomActor::setLight(ld, am, dc);
    }
    // Screen px -> world at depth d (y is screen-down, world-up).
    const auto worldAt = [&](float sx, float sy, float d) {
        return Vec3{(2.0f * sx / fW - 1.0f) * d * tanX,
                    (1.0f - 2.0f * sy / fH) * d * tanY, -d};
    };
    // Depth that fits a model of world height h into `maxPx` on screen: big minibosses
    // overflowed their rows and overlapped (S.) — push them further from the camera.
    const float pxPerUnit = fH / (2.0f * kCamDist * tanY);
    const auto fitDepth = [&](float h, float maxPx) {
        return kCamDist * std::max(1.0f, h * pxPerUnit / std::max(1.0f, maxPx));
    };
    const u8 turnDir = static_cast<u8>(static_cast<int>(time_ * 0.8) % 8);  // slow turntable

    struct ModelDraw {  // deferred 3D submits (issued after the UI pass is assembled)
        RomActor* actor;
        Vec3 pos;
    };
    std::vector<ModelDraw> draws;

    sb.begin(W, H, RenderDevice::kUiView);

    // ---- Top bar: categories + tabs + counters.
    ui::panel(sb, 0, 0, fW, kTopBarH, ui::color::kWinBody);
    struct CatBtn { const char* label; bool enabled; Cat cat; };
    const CatBtn cats[] = {{"Мобы", true, kCatMobs},   {"NPC", true, kCatNpcs},
                           {"Чары", true, kCatChars}, {"Покраски", true, kCatDyes},
                           {"Итемы", true, kCatItems}, {"Шапки", true, kCatHats},
                           {"Эффекты", true, kCatEffects}};
    float bx = 8.0f;
    for (usize ci = 0; ci < sizeof(cats) / sizeof(cats[0]); ++ci) {
        const CatBtn& c = cats[ci];
        const float bw = font.width(c.label, 1.5f) + 24.0f;
        if (c.enabled && cat_ == c.cat)
            ui::panel(sb, bx - 2, 4, bw + 4, 28, ui::color::kWinSelectStrong);
        if (ui::button(sb, font, in, bx, 6, bw, 24, c.label, 1.5f, c.enabled) && c.enabled) {
            cat_ = c.cat;
            selUnmapped_ = -1;
        }
        bx += bw + 8.0f;
    }
    float tx = 8.0f;
    // Mapped/Unmapped tabs + filter + counts only make sense for the mob/NPC lists.
    if (cat_ == kCatMobs || cat_ == kCatNpcs) {
        const char* tabs[] = {"Сопоставленные", "Несопоставленные"};
        for (int t = 0; t < 2; ++t) {
            const float bw = font.width(tabs[t], 1.5f) + 24.0f;
            const bool active = (tab_ == Tab::Mapped) == (t == 0);
            if (active) ui::panel(sb, tx - 2, 32, bw + 4, 28, ui::color::kWinSelect);
            if (ui::button(sb, font, in, tx, 34, bw, 24, tabs[t], 1.5f))
                tab_ = t == 0 ? Tab::Mapped : Tab::Unmapped;
            tx += bw + 8.0f;
        }
        {
            // The same filter box on BOTH tabs: it narrows the mapped rows, or (on the unmapped
            // tab) the left RoM-bundle list (S.: "в несопоставленных нужен фильтр-поиск"). On the
            // unmapped tab it shares keyboard focus with the right sprite-search box — click either
            // to type into it, so a keystroke never lands in both.
            const float fx = tx + 8, fy = 34, fw2 = 220, fh2 = 24;
            const bool focus = tab_ == Tab::Mapped || unmappedFocus_ == 0;
            if (tab_ == Tab::Unmapped && in.mousePressed && in.mouseX >= fx && in.mouseX < fx + fw2 &&
                in.mouseY >= fy && in.mouseY < fy + fh2)
                unmappedFocus_ = 0;
            mapFilter_.fontScale = 1.5f;
            mapFilter_.maxLen = 32;
            mapFilter_.update(in, focus);
            mapFilter_.draw(sb, font, fx, fy, fw2, fh2, focus, time_, "фильтр...");
            tx += 236.0f;
        }
        const std::string counts =
            "сопоставлено " + std::to_string(mapped_[cat_].size()) + " | без модели " +
            std::to_string(spritesWithoutModel_[cat_]) + " | RoM без пары " +
            std::to_string(unmappedRom_[cat_].size());
        font.draw(sb, tx + 16, 40, 1.3f, ui::color::kWinTextDim, counts);
        // Mapped-tab preview controls (S.): play the Attack clip + Unpair the selected row.
        if (tab_ == Tab::Mapped) {
            if (ui::button(sb, font, in, fW - 320.0f, 34, 150.0f, 24,
                           viewAttack_ ? "Анимация: атака" : "Анимация: ожидание", 1.3f))
                viewAttack_ = !viewAttack_;
            const bool canUnpair = !selMappedSprite_.empty();
            if (ui::button(sb, font, in, fW - 160.0f, 34, 150.0f, 24, "Отвязать", 1.3f, canUnpair) &&
                canUnpair)
                removePair(app, selMappedSprite_);
        }
    }

    // Mapped rows filtered by the search box (substring over sprite AND bundle names).
    std::vector<const MappedRow*> visRows;
    if (tab_ == Tab::Mapped) {
        std::string q = mapFilter_.value;
        for (char& c : q) c = static_cast<char>(std::tolower(static_cast<u8>(c)));
        for (const MappedRow& r : mapped_[cat_])
            if (q.empty() || r.sprite.find(q) != std::string::npos ||
                r.bundle.find(q) != std::string::npos)
                visRows.push_back(&r);
    }

    // Unmapped rows filtered by the SAME box (substring over the bundle basename). Holds indices
    // into unmappedRom_[cat_] so the selection/pair still reference the real bundle, not a row.
    std::vector<int> visUnmapped;
    if (tab_ == Tab::Unmapped && (cat_ == kCatMobs || cat_ == kCatNpcs)) {
        std::string q = mapFilter_.value;
        for (char& c : q) c = static_cast<char>(std::tolower(static_cast<u8>(c)));
        for (int idx = 0; idx < static_cast<int>(unmappedRom_[cat_].size()); ++idx) {
            std::string nm = baseName(unmappedRom_[cat_][static_cast<usize>(idx)]);
            for (char& c : nm) c = static_cast<char>(std::tolower(static_cast<u8>(c)));
            if (q.empty() || nm.find(q) != std::string::npos) visUnmapped.push_back(idx);
        }
    }

    // Chars: sex + hair toggles in the second bar row.
    if (cat_ == kCatChars) {
        if (ui::button(sb, font, in, tx + 8, 34, 60, 24, viewSex_ ? "Муж" : "Жен", 1.4f))
            viewSex_ = viewSex_ ? 0 : 1;
        if (ui::button(sb, font, in, tx + 76, 34, 26, 24, "<", 1.4f))
            viewHair_ = viewHair_ > 1 ? viewHair_ - 1 : 29;
        font.draw(sb, tx + 108, 40, 1.4f, ui::color::kWinText,
                  "hair " + std::to_string(viewHair_));
        if (ui::button(sb, font, in, tx + 178, 34, 26, 24, ">", 1.4f))
            viewHair_ = viewHair_ < 29 ? viewHair_ + 1 : 1;
        if (viewRiding_) ui::panel(sb, tx + 210, 34, 74, 24, ui::color::kWinSelectStrong);
        if (ui::button(sb, font, in, tx + 212, 34, 70, 24, "Маунт", 1.4f))
            viewRiding_ = !viewRiding_;
    }

    // ---- Hats: browse RoM head models on a novice, bind to a RO headgear view id (#135).
    if (cat_ == kCatHats) {
        // Search box + id field in the second bar row.
        hatSearch_.fontScale = 1.4f; hatSearch_.maxLen = 32;
        hatSearch_.update(in, true);
        hatSearch_.draw(sb, font, tx + 8, 34, 240, 24, true, time_, "поиск модели шапки");
        if (hatSearch_.value != hatSearchDone_) {
            hatSearchDone_ = hatSearch_.value;
            hatHits_.clear();
            std::string q = hatSearchDone_;
            for (char& c : q) c = static_cast<char>(std::tolower(static_cast<u8>(c)));
            for (usize i = 0; i < hats_.size(); ++i) {
                std::string n = hats_[i].second;
                for (char& c : n) c = static_cast<char>(std::tolower(static_cast<u8>(c)));
                if (q.empty() || n.find(q) != std::string::npos) hatHits_.push_back(i);
            }
            hatScroll_ = 0.0f;
        }
        font.draw(sb, tx + 258, 40, 1.2f, ui::color::kWinTextDim,
                  "моделей " + std::to_string(hatHits_.size()) + "/" +
                      std::to_string(hats_.size()));

        // 3D preview grid — reuses the shared camera/view/lighting/worldAt/pxPerUnit/draws/turnDir
        // set up once at function scope above (dropping the per-category duplication that shadowed
        // those locals on MSVC, C4456).

        const float gx = 10.0f, gy = kTopBarH + 6.0f, cellS = 150.0f, pad = 8.0f;
        const int perRow = std::max(1, (int)((fW - 280.0f) / (cellS + pad)));
        const int first = std::max(0, (int)(hatScroll_ / (cellS + pad)) * perRow);
        if (in.wheel != 0.0f && in.mouseX < fW - 270.0f)
            hatScroll_ = std::clamp(hatScroll_ - in.wheel * (cellS + pad), 0.0f,
                                    std::max(0.0f, (float)hatHits_.size() / perRow * (cellS + pad)));
        for (int k = first; k < (int)hatHits_.size(); ++k) {
            const int rel = k - first;
            const int r = rel / perRow, c = rel % perRow;
            const float cx = gx + c * (cellS + pad), cy = gy + r * (cellS + pad) - std::fmod(hatScroll_, cellS + pad);
            if (cy > fH - 30.0f) break;
            if (cy < gy - cellS) continue;
            const usize hi = hatHits_[static_cast<usize>(k)];
            const bool sel = (int)hi == hatSel_;
            ui::border(sb, cx, cy, cellS, cellS,
                       sel ? ui::color::kWinSelectStrong : ui::color::kWinBorder, sel ? 2.0f : 1.0f);
            if (in.mousePressed && in.hit(cx, cy, cellS, cellS) && in.mouseX < fW - 270.0f)
                hatSel_ = (int)hi;
            if (RomActor* ra = hatFor(app, hats_[hi].first, hats_[hi].second); ra && ra->ready()) {
                const float d = kCamDist * std::max(1.0f, ra->height() * pxPerUnit / (cellS * 0.7f));
                draws.push_back({ra, worldAt(cx + cellS * 0.5f, cy + cellS * 0.82f, d)});
            }
            font.draw(sb, cx + 4, cy + cellS - 16, 1.0f, ui::color::kWinText, hats_[hi].second);
        }

        // Bind panel.
        const float bpx = fW - 260.0f;
        ui::panel(sb, bpx - 8, gy, 258.0f, 150.0f, ui::color::kWinBody);
        ui::border(sb, bpx - 8, gy, 258.0f, 150.0f, ui::color::kWinBorder, 1.0f);
        if (hatSel_ >= 0 && hatSel_ < (int)hats_.size())
            font.draw(sb, bpx, gy + 8, 1.2f, ui::color::kWinText, hats_[hatSel_].second);
        else
            font.draw(sb, bpx, gy + 8, 1.2f, ui::color::kWinTextDim, "выбери модель слева");
        font.draw(sb, bpx, gy + 34, 1.2f, ui::color::kWinText, "RO headgear id:");
        hatIdField_.fontScale = 1.4f; hatIdField_.maxLen = 7;
        hatIdField_.update(in, true);
        hatIdField_.draw(sb, font, bpx, gy + 56, 120, 26, true, time_, "0000");
        const bool can = hatSel_ >= 0 && !hatIdField_.value.empty();
        if (ui::button(sb, font, in, bpx, gy + 90, 160, 28, "Привязать", 1.3f, can) && can) {
            std::ofstream out("viewer-headgear-pairs.txt", std::ios::app);
            if (out) out << hatIdField_.value << " = " << hats_[hatSel_].second << "\n";
            hatStatus_ = "привязано: " + hatIdField_.value + " = " + hats_[hatSel_].second;
        }
        if (!hatStatus_.empty())
            font.draw(sb, bpx, gy + 124, 1.2f, ui::color::kOk, hatStatus_);

        font.draw(sb, fW - 260, 8, 1.3f, ui::color::kWinTextDim, "Esc — выход  |  UaRO --view");
        sb.end();
        for (const ModelDraw& d : draws) d.actor->render(0, d.pos, turnDir, viewAttack_ ? Anim::Attack : Anim::Idle, time_);
        return;
    }

    // ---- Effects (#132, S.): bind an item id to a RoM effect name (rom-item-effects.txt) + unbind.
    // No live 3D preview here (effects need the in-game particle runtime -- preview with /fx in-game).
    if (cat_ == kCatEffects) {
        effectSearch_.fontScale = 1.4f;
        effectSearch_.maxLen = 40;
        effectSearch_.update(in, true);
        effectSearch_.draw(sb, font, tx + 8, 34, 260, 24, true, time_, "поиск эффекта");
        if (effectSearch_.value != effectSearchDone_) {
            effectSearchDone_ = effectSearch_.value;
            effectHits_.clear();
            std::string q = effectSearchDone_;
            for (char& c : q) c = static_cast<char>(std::tolower(static_cast<u8>(c)));
            for (usize i = 0; i < effectNames_.size(); ++i) {
                std::string n = effectNames_[i];
                for (char& c : n) c = static_cast<char>(std::tolower(static_cast<u8>(c)));
                if (q.empty() || n.find(q) != std::string::npos) effectHits_.push_back(i);
            }
            effectScroll_ = 0.0f;
        }
        font.draw(sb, tx + 274, 40, 1.2f, ui::color::kWinTextDim,
                  "эффектов " + std::to_string(effectHits_.size()) + "/" +
                      std::to_string(effectNames_.size()));

        // Effect-name list (left).
        const float listX = 10.0f, listY = kTopBarH + 6.0f, rowH = 22.0f;
        const float listW = fW * 0.45f, listH = fH - listY - 30.0f;
        ui::panel(sb, listX, listY, listW, listH, ui::color::kWinBody);
        const int vis = static_cast<int>(listH / rowH);
        if (in.wheel != 0.0f && in.mouseX < listX + listW)
            effectScroll_ = std::clamp(effectScroll_ - in.wheel * rowH, 0.0f,
                                       std::max(0.0f, static_cast<float>(effectHits_.size()) * rowH - listH));
        const int first = static_cast<int>(effectScroll_ / rowH);
        for (int k = first; k < static_cast<int>(effectHits_.size()) && k < first + vis + 1; ++k) {
            const usize ei = effectHits_[static_cast<usize>(k)];
            const float ry = listY + static_cast<float>(k) * rowH - effectScroll_;
            if (ry < listY || ry + rowH > listY + listH) continue;
            const bool sel = static_cast<int>(ei) == effectSel_;
            if (sel) ui::panel(sb, listX, ry, listW, rowH, ui::color::kWinSelect);
            if (in.mousePressed && in.hit(listX, ry, listW, rowH)) effectSel_ = static_cast<int>(ei);
            font.draw(sb, listX + 6, ry + 4, 1.2f, ui::color::kWinText, effectNames_[ei]);
        }

        // Bind panel (right).
        const float bpx = listX + listW + 16.0f;
        font.draw(sb, bpx, listY + 4, 1.2f,
                  effectSel_ >= 0 ? ui::color::kWinText : ui::color::kWinTextDim,
                  effectSel_ >= 0 ? ("эффект: " + effectNames_[static_cast<usize>(effectSel_)])
                                  : "выбери эффект слева");
        font.draw(sb, bpx, listY + 32, 1.2f, ui::color::kWinText, "id итема:");
        effectIdField_.fontScale = 1.4f;
        effectIdField_.maxLen = 7;
        effectIdField_.update(in, true);
        effectIdField_.draw(sb, font, bpx + 90, listY + 28, 110, 26, true, time_, "0000");
        const bool can = effectSel_ >= 0 && !effectIdField_.value.empty();
        if (ui::button(sb, font, in, bpx, listY + 64, 160, 28, "Привязать", 1.3f, can) && can) {
            const std::string id = effectIdField_.value;
            const std::string name = effectNames_[static_cast<usize>(effectSel_)];
            bool replaced = false;
            for (auto& pr : effectPairs_)
                if (pr.first == id) { pr.second = name; replaced = true; break; }
            if (!replaced) effectPairs_.push_back({id, name});
            writeEffectPairs();
            effectStatus_ = "привязано: " + id + " = " + name;
        }
        if (!effectStatus_.empty())
            font.draw(sb, bpx, listY + 98, 1.2f, ui::color::kOk, effectStatus_);

        // Current bindings + per-row unbind.
        const float clY = listY + 128.0f;
        font.draw(sb, bpx, clY, 1.2f, ui::color::kWinTextDim,
                  "привязки (" + std::to_string(effectPairs_.size()) + "):");
        float cy = clY + 24.0f;
        for (usize i = 0; i < effectPairs_.size() && cy + 24.0f < fH - 30.0f; ++i) {
            font.draw(sb, bpx, cy + 4, 1.1f, ui::color::kWinText,
                      effectPairs_[i].first + " = " + effectPairs_[i].second);
            if (ui::button(sb, font, in, bpx + 260, cy, 90, 20, "Отвязать", 1.1f)) {
                effectPairs_.erase(effectPairs_.begin() + static_cast<long>(i));
                writeEffectPairs();
                effectStatus_ = "отвязано";
                break;
            }
            cy += 24.0f;
        }

        font.draw(sb, fW - 260, 8, 1.3f, ui::color::kWinTextDim, "Esc — выход  |  UaRO --view");
        sb.end();
        return;
    }

    // ---- Items: two-phase (slice atlas into squares, then bind to RO item ids). S.
    if (cat_ == kCatItems) {
        // Mode toggle + page nav in the second bar row.
        if (ui::button(sb, font, in, tx + 8, 34, 84, 24, itemMode_ ? "Привязка" : "Нарезка",
                       1.3f))
            itemMode_ = itemMode_ ? 0 : 1;
        if (ui::button(sb, font, in, tx + 100, 34, 24, 24, "<", 1.3f))
            itemPage_ = itemPage_ > 0 ? itemPage_ - 1 : 61;
        font.draw(sb, tx + 130, 40, 1.3f, ui::color::kWinText,
                  "стр " + std::to_string(itemPage_ + 1) + "/62");
        if (ui::button(sb, font, in, tx + 196, 34, 24, 24, ">", 1.3f))
            itemPage_ = itemPage_ < 61 ? itemPage_ + 1 : 0;
        {
            usize bound = 0, total = squares_.size();
            for (const auto& q : squares_) if (!q.id.empty()) ++bound;
            font.draw(sb, tx + 232, 40, 1.2f, ui::color::kWinTextDim,
                      "квадратов " + std::to_string(total) + ", привязано " +
                          std::to_string(bound));
        }

        Texture* atlas = loadAtlas(app, itemPage_);

        if (itemMode_ == 0) {
            // ===== SLICE: draw/adjust a box, "Добавить" saves it as an unbound square. Already
            // sliced squares on this page get a translucent fill so it's clear they're done.
            const float atlasY = kTopBarH + 6.0f;
            const float side = std::min(fW - 270.0f, fH - atlasY - 20.0f);
            const float vx = 10.0f, vy = atlasY;
            // Clickable zone = the whole left region up to the right control card (S.: right of
            // centre didn't respond) — not just the square the atlas is drawn in.
            const float zoneR = fW - 262.0f, zoneB = fH - 10.0f;
            const bool overV = in.mouseX >= vx && in.mouseX < zoneR && in.mouseY >= vy &&
                               in.mouseY < zoneB;
            if (overV && in.wheel != 0.0f) {
                const float old = itemZoom_;
                itemZoom_ = std::clamp(itemZoom_ * (in.wheel > 0 ? 1.25f : 0.8f), 1.0f, 12.0f);
                const float k = itemZoom_ / old;
                itemPanX_ = in.mouseX - k * (in.mouseX - itemPanX_);
                itemPanY_ = in.mouseY - k * (in.mouseY - itemPanY_);
            }
            if (in.rightDown) {
                if (panPrevX_ != 0.0f || panPrevY_ != 0.0f) {
                    itemPanX_ += in.mouseX - panPrevX_;
                    itemPanY_ += in.mouseY - panPrevY_;
                }
                panPrevX_ = static_cast<float>(in.mouseX);
                panPrevY_ = static_cast<float>(in.mouseY);
            } else panPrevX_ = panPrevY_ = 0.0f;
            const float dispSide = side * itemZoom_;
            const float ax = vx + itemPanX_, ay = vy + itemPanY_;
            const float scale = dispSide / 1024.0f, inv = 1.0f / scale;
            auto sx = [&](float a) { return ax + a * scale; };
            auto sy = [&](float a) { return ay + a * scale; };
            if (atlas && atlas->valid()) {
                sb.draw(ax, ay, dispSide, dispSide, ui::color::kWhite, *atlas);
                // Fill already-sliced squares on this page.
                for (const auto& q : squares_) {
                    if (q.page != itemPage_ + 1) continue;
                    const u32 col = q.id.empty() ? ui::rgba(80, 160, 255, 90)
                                                 : ui::rgba(80, 255, 120, 110);
                    ui::panel(sb, sx((float)q.x), sy((float)q.y), q.w * scale, q.h * scale, col);
                }
                const float grab = 7.0f;
                if (in.mousePressed && overV) {
                    editEdge_ = 0;
                    // First: grabbing a handle of the current box?
                    if (selHave_) {
                        const float L = sx(selL_), T = sy(selT_), R = sx(selR_), B = sy(selB_);
                        const float mx = (float)in.mouseX, my = (float)in.mouseY;
                        const bool inx = mx > L - grab && mx < R + grab;
                        const bool iny = my > T - grab && my < B + grab;
                        if (std::abs(mx - L) < grab && iny) editEdge_ |= 1;
                        if (std::abs(mx - R) < grab && iny) editEdge_ |= 2;
                        if (std::abs(my - T) < grab && inx) editEdge_ |= 4;
                        if (std::abs(my - B) < grab && inx) editEdge_ |= 8;
                    }
                    bool loaded = false;
                    if (editEdge_ == 0) {
                        // Click inside a saved square of this page -> pull it back for editing
                        // (removed from the list; re-added via "Добавить"). (S.)
                        const float apx0 = (in.mouseX - ax) * inv, apy0 = (in.mouseY - ay) * inv;
                        for (usize qi = 0; qi < squares_.size(); ++qi) {
                            const ItemSquare& q = squares_[qi];
                            if (q.page != itemPage_ + 1) continue;
                            if (apx0 >= q.x && apx0 < q.x + q.w && apy0 >= q.y &&
                                apy0 < q.y + q.h) {
                                selL_ = (float)q.x; selT_ = (float)q.y;
                                selR_ = (float)(q.x + q.w); selB_ = (float)(q.y + q.h);
                                selHave_ = true;
                                loaded = true;
                                squares_.erase(squares_.begin() + static_cast<std::ptrdiff_t>(qi));
                                saveSquares();
                                break;
                            }
                        }
                    }
                    if (editEdge_ == 0 && !loaded) {  // empty space -> start a fresh box
                        selL_ = selR_ = (in.mouseX - ax) * inv;
                        selT_ = selB_ = (in.mouseY - ay) * inv;
                        selHave_ = true;
                        editEdge_ = 2 | 8;
                    }
                }
                if (selHave_ && in.mouseDown && editEdge_) {
                    const float apx = (in.mouseX - ax) * inv, apy = (in.mouseY - ay) * inv;
                    if (editEdge_ & 1) selL_ = apx;
                    if (editEdge_ & 2) selR_ = apx;
                    if (editEdge_ & 4) selT_ = apy;
                    if (editEdge_ & 8) selB_ = apy;
                }
                if (!in.mouseDown) editEdge_ = 0;
                if (selHave_) {
                    const float L = sx(std::min(selL_, selR_)), T = sy(std::min(selT_, selB_));
                    const float R = sx(std::max(selL_, selR_)), B = sy(std::max(selT_, selB_));
                    ui::border(sb, L, T, R - L, B - T, ui::color::kWinSelectStrong, 2.0f);
                    for (float hx2 : {L, (L + R) * 0.5f, R})
                        for (float hy2 : {T, (T + B) * 0.5f, B})
                            ui::panel(sb, hx2 - 3, hy2 - 3, 6, 6, ui::color::kWinSelectStrong);
                }
            } else {
                font.draw(sb, vx, vy + 20, 1.4f, ui::color::kError,
                          "атлас item_" + std::to_string(itemPage_ + 1) + " не загрузился");
            }
            // Right-side control card on a white panel (S.), clear of the atlas viewport.
            const float pw2 = 250.0f, pxp = fW - pw2 - 8.0f, pyp = vy;
            ui::panel(sb, pxp, pyp, pw2, 210.0f, ui::color::kWinBody);
            ui::border(sb, pxp, pyp, pw2, 210.0f, ui::color::kWinBorder, 1.0f);
            const float infoX = pxp + 12.0f;
            font.draw(sb, infoX, pyp + 10, 1.3f, ui::color::kWinText, "Колесо — зум");
            font.draw(sb, infoX, pyp + 30, 1.3f, ui::color::kWinText, "ПКМ — двигать атлас");
            font.draw(sb, infoX, pyp + 50, 1.3f, ui::color::kWinText, "ЛКМ на пустом — рамка");
            font.draw(sb, infoX, pyp + 70, 1.3f, ui::color::kWinText, "грани/углы — подгонка");
            if (selHave_) {
                const int px = (int)(std::min(selL_, selR_) + 0.5f);
                const int py = (int)(std::min(selT_, selB_) + 0.5f);
                const int pw = (int)(std::abs(selR_ - selL_) + 0.5f);
                const int ph = (int)(std::abs(selB_ - selT_) + 0.5f);
                font.draw(sb, infoX, pyp + 100, 1.3f, ui::color::kWinAccent,
                          std::to_string(px) + "," + std::to_string(py) + "  " +
                              std::to_string(pw) + "x" + std::to_string(ph));
                if (ui::button(sb, font, in, infoX, pyp + 126, pw2 - 24.0f, 30, "Добавить квадрат",
                               1.3f) &&
                    pw > 2 && ph > 2) {
                    squares_.push_back({itemPage_ + 1, px, py, pw, ph, ""});
                    saveSquares();
                    itemStatus_ = "добавлен квадрат (" + std::to_string(squares_.size()) + ")";
                    selHave_ = false;
                }
            }
            if (!itemStatus_.empty())
                font.draw(sb, infoX, pyp + 170, 1.3f, ui::color::kOk, itemStatus_);
        } else {
            // ===== BIND: grid of UNBOUND squares (icon preview from the atlas) + id field.
            const float gx = 10.0f, gy = kTopBarH + 6.0f, cellS = 84.0f, pad = 8.0f;
            const int perRow = std::max(1, (int)((fW - 280.0f) / (cellS + pad)));
            int shown = 0, idx = 0;
            for (usize i = 0; i < squares_.size(); ++i) {
                if (!squares_[i].id.empty()) continue;
                Texture* at = squares_[i].page == itemPage_ + 1 ? atlas : nullptr;
                // Only preview squares whose page atlas is loaded (current page); others show id.
                const int r = shown / perRow, c = shown % perRow;
                const float cx = gx + c * (cellS + pad), cy = gy + r * (cellS + pad);
                if (cy > fH - 60.0f) break;
                const bool sel = (int)i == bindSel_;
                ui::panel(sb, cx, cy, cellS, cellS,
                          sel ? ui::color::kWinSelect : ui::color::kWinContent);
                const ItemSquare& q = squares_[i];
                if (at && at->valid()) {
                    const float u0 = q.x / 1024.0f, v0 = q.y / 1024.0f;
                    const float u1 = (q.x + q.w) / 1024.0f, v1 = (q.y + q.h) / 1024.0f;
                    sb.draw(cx + 4, cy + 4, cellS - 8, cellS - 8, u0, v0, u1, v1,
                            ui::color::kWhite, *at);
                } else {
                    font.draw(sb, cx + 6, cy + 6, 1.1f, ui::color::kWinTextDim,
                              "стр" + std::to_string(q.page));
                }
                ui::border(sb, cx, cy, cellS, cellS,
                           sel ? ui::color::kWinSelectStrong : ui::color::kWinBorder, 1.0f);
                if (in.mousePressed && in.hit(cx, cy, cellS, cellS)) bindSel_ = (int)i;
                ++shown;
                (void)idx;
            }
            // Bind panel on the right.
            const float bpx = fW - 260.0f;
            font.draw(sb, bpx, gy + 4, 1.4f, ui::color::kWinText, "Непривязано: " +
                          std::to_string(shown));
            font.draw(sb, bpx, gy + 30, 1.3f, ui::color::kWinTextDim,
                      "Выбери квадрат слева,\nвведи RO item id,\nжми Привязать.");
            itemIdField_.fontScale = 1.5f;
            itemIdField_.maxLen = 7;
            itemIdField_.update(in, true);
            itemIdField_.draw(sb, font, bpx, gy + 90, 120, 26, true, time_, "item id");
            const bool can = bindSel_ >= 0 && bindSel_ < (int)squares_.size() &&
                             !itemIdField_.value.empty();
            if (ui::button(sb, font, in, bpx, gy + 124, 160, 28, "Привязать", 1.4f, can) && can) {
                squares_[bindSel_].id = itemIdField_.value;
                saveSquares();
                itemStatus_ = "привязано: " + itemIdField_.value;
                bindSel_ = -1;
                itemIdField_.value.clear();
            }
            if (!itemStatus_.empty())
                font.draw(sb, bpx, gy + 160, 1.3f, ui::color::kOk, itemStatus_);
        }

        font.draw(sb, fW - 260, 8, 1.3f, ui::color::kWinTextDim, "Esc — выход  |  UaRO --view");
        sb.end();
        return;
    }

    const float listTop = kTopBarH;
    const float listH = fH - listTop;
    float& scroll = scroll_[cat_][tab_ == Tab::Mapped ? 0 : 1];
    const usize rowCount =
        cat_ == kCatChars ? charJobs_.size()
        : cat_ == kCatDyes ? mapped_[kCatMobs].size()
                           : (tab_ == Tab::Mapped ? visRows.size() : visUnmapped.size());
    const float listW = tab_ == Tab::Mapped ? fW : fW - kSearchW;
    const bool overList = in.mouseX < listW && in.mouseY >= listTop;
    if (overList && in.wheel != 0.0f) scroll -= in.wheel * kRowH;
    scroll = std::clamp(scroll, 0.0f,
                        std::max(0.0f, static_cast<float>(rowCount) * kRowH - listH));

    // Draggable scrollbar (S.: "нужны скроллбары, что бы мышью листать"). Immediate-mode:
    // click/drag the track sets the scroll; the wheel keeps working as before.
    const auto scrollbar = [&](float x, float top, float viewH, float contentH, float& sc,
                               int id) {
        if (contentH <= viewH) return;
        const float w = 14.0f;
        ui::panel(sb, x, top, w, viewH, ui::color::kWinContent);
        const float thumbH = std::max(30.0f, viewH * viewH / contentH);
        const float maxSc = contentH - viewH;
        const bool over = in.mouseX >= x && in.mouseX < x + w && in.mouseY >= top &&
                          in.mouseY < top + viewH;
        if (in.mousePressed && over) dragScroll_ = id;
        if (!in.mouseDown && dragScroll_ == id) dragScroll_ = 0;
        if (dragScroll_ == id) {
            const float t = (static_cast<float>(in.mouseY) - top - thumbH * 0.5f) /
                            std::max(1.0f, viewH - thumbH);
            sc = std::clamp(t, 0.0f, 1.0f) * maxSc;
        }
        const float ty = top + (sc / maxSc) * (viewH - thumbH);
        ui::panel(sb, x + 2, ty, w - 4, thumbH,
                  dragScroll_ == id || over ? ui::color::kWinSelectStrong
                                            : ui::color::kWinBorder);
    };
    scrollbar(listW - 14.0f, listTop, listH, static_cast<float>(rowCount) * kRowH, scroll, 1);

    const int first = std::max(0, static_cast<int>(scroll / kRowH));
    const int last = std::min(static_cast<int>(rowCount),
                              static_cast<int>((scroll + listH) / kRowH) + 1);

    if (cat_ == kCatDyes) {
        // ---- Dyes: each mob row lists every skin its _ext family carries; clicking a name
        // reloads the model with that texture forced (S.: "варианты цветов у мобов").
        const float chipX = fW * 0.62f;
        for (int i = first; i < last; ++i) {
            const MappedRow& row = mapped_[kCatMobs][static_cast<usize>(i)];
            const float ry = listTop + static_cast<float>(i) * kRowH - scroll;
            ui::border(sb, 0, ry, fW - 14.0f, kRowH - 2, ui::color::kWinBorder, 1.0f);
            const std::string& pick =
                dyePick_.count(i) ? dyePick_[i] : std::string();
            RomActor* ra = pick.empty() ? modelFor(app, row.sprite)
                                        : rombind::actorFor(app, row.sprite, romCache_, pick);
            RomActor* base = pick.empty() ? ra : modelFor(app, row.sprite);
            if (ra && ra->ready()) {
                const float d = fitDepth(ra->height(), kRowH * 0.72f);
                draws.push_back({ra, worldAt(fW * 0.3f, ry + kRowH * 0.85f, d)});
            }
            font.draw(sb, 10, ry + kRowH - 24, 1.4f, ui::color::kWinText, row.sprite);
            // Skin chips from the loaded model's texture inventory (outline maps skipped).
            if (base && base->ready()) {
                float cy = ry + 8.0f;
                ui::panel(sb, chipX - 6, ry + 2, fW - 14.0f - chipX + 4, kRowH - 6,
                          ui::color::kWinBody);
                for (const auto& t : base->model().textures) {
                    std::string low = t.name;
                    for (char& ch : low)
                        ch = static_cast<char>(std::tolower(static_cast<u8>(ch)));
                    if (low.size() > 3 && low.compare(low.size() - 3, 3, "_ol") == 0) continue;
                    if (cy > ry + kRowH - 26.0f) break;
                    const bool sel = low == pick;
                    if (ui::button(sb, font, in, chipX, cy, fW - 24.0f - chipX, 18.0f, t.name,
                                   0.95f))
                        dyePick_[i] = sel ? std::string() : low;  // click again = base skin
                    if (sel)
                        ui::border(sb, chipX, cy, fW - 24.0f - chipX, 18.0f,
                                   ui::color::kWinSelectStrong, 2.0f);
                    cy += 20.0f;
                }
            }
        }
    } else if (cat_ == kCatChars) {
        // ---- Chars: [2D composed player sprite | 3D ROeM player] per job, sex/hair applied.
        const float colW = fW * 0.5f;
        for (int i = first; i < last; ++i) {
            const u16 job = charJobs_[static_cast<usize>(i)];
            // Gendered classes exist for one sex only (bard = male, dancer/gypsy = female):
            // force that sex so the 2D sprite AND the 3D model agree (S.: gypsy showed a
            // female model but no male sprite).
            u8 rowSex = viewSex_;
            if (job == 19 || job == 4020) rowSex = 1;               // bard / clown = male
            if (job == 20 || job == 4021) rowSex = 0;               // dancer / gypsy = female
            const float ry = listTop + static_cast<float>(i) * kRowH - scroll;
            ui::panel(sb, 0, ry, colW, kRowH - 2,
                      i % 2 ? ui::color::kWinContent : ui::color::kWinBody);
            ui::border(sb, colW, ry, colW, kRowH - 2, ui::color::kWinBorder, 1.0f);
            const std::string key = "job:" + std::to_string(job) + ":" +
                                    std::to_string(rowSex) + ":" + std::to_string(viewHair_);
            auto sit = spriteCache_.find(key);
            if (sit == spriteCache_.end() && loadsThisFrame_ < kMaxLoadsPerFrame) {
                ++loadsThisFrame_;
                auto ca = std::make_unique<CharacterActor>();
                if (!ca->load(app.vfs(), job, rowSex, static_cast<u16>(viewHair_))) ca.reset();
                sit = spriteCache_.emplace(key, std::move(ca)).first;
            }
            if (sit != spriteCache_.end() && sit->second)
                sit->second->renderScaled(sb, colW * 0.5f, ry + kRowH * 0.78f, 2.0f, time_);
            font.draw(sb, 10, ry + kRowH - 24, 1.4f, ui::color::kWinText,
                      net::jobName(job) + (rowSex ? " (m)" : " (f)"));
            RomActor* ra = rombind::playerFor(app, job, rowSex, static_cast<u16>(viewHair_),
                                              0, viewRiding_, romCache_);
            RomActor* mount = viewRiding_ ? rombind::mountFor(app, romCache_) : nullptr;
            if (ra && ra->ready()) {
                const float d = fitDepth(ra->height(), kRowH * 0.72f);
                const Vec3 base = worldAt(colW * 1.5f, ry + kRowH * 0.85f, d);
                if (mount && mount->ready()) {
                    draws.push_back({mount, base});
                    draws.push_back({ra, {base.x, base.y + 0.38f, base.z}});
                } else {
                    draws.push_back({ra, base});
                }
            } else {
                font.draw(sb, colW + 12, ry + kRowH * 0.45f, 1.3f, ui::color::kError,
                          "нет RoM модели");
            }
        }
    } else if (tab_ == Tab::Mapped) {
        // ---- Mapped: rows of [2D sprite + name | 3D model + bundle name].
        const float colW = fW * 0.5f;
        for (int i = first; i < last; ++i) {
            const MappedRow& row = *visRows[static_cast<usize>(i)];
            const float ry = listTop + static_cast<float>(i) * kRowH - scroll;
            const bool sel = !selMappedSprite_.empty() && row.sprite == selMappedSprite_;
            if (in.mousePressed && in.mouseX < fW - 14.0f && in.mouseY >= ry &&
                in.mouseY < ry + kRowH && in.mouseY >= listTop)
                selMappedSprite_ = row.sprite;  // click a row to select it (for the Unpair button)
            ui::panel(sb, 0, ry, colW, kRowH - 2,
                      i % 2 ? ui::color::kWinContent : ui::color::kWinBody);
            ui::border(sb, colW, ry, colW, kRowH - 2, ui::color::kWinBorder, 1.0f);
            if (sel) ui::border(sb, 0, ry, colW * 2.0f, kRowH - 2, ui::color::kWinSelectStrong, 3.0f);
            // 2D: feet at ~78% of the row, sized by the sprite's own pixels (x2).
            if (CharacterActor* ca = spriteFor(app, row.sprite, row.classId))
                ca->renderScaled(sb, colW * 0.5f, ry + kRowH * 0.78f, 2.0f, time_);
            font.draw(sb, 10, ry + kRowH - 24, 1.4f, ui::color::kWinText, row.sprite);
            // 3D: authored size 1:1 (same convention as in game), feet near the row bottom.
            if (RomActor* ra = modelFor(app, row.sprite); ra && ra->ready()) {
                const float d = fitDepth(ra->height(), kRowH * 0.72f);
                draws.push_back({ra, worldAt(colW * 1.5f, ry + kRowH * 0.85f, d)});
            }
            else
                font.draw(sb, colW + 12, ry + kRowH * 0.45f, 1.3f,
                          romCache_.count(row.sprite) ? ui::color::kError
                                                      : ui::color::kWinTextDim,
                          romCache_.count(row.sprite) ? "модель не собралась" : "загрузка...");
            font.draw(sb, colW + 10, ry + kRowH - 24, 1.4f, ui::color::kWinAccent,
                      baseName(row.bundle));
        }
    } else {
        // ---- Unmapped: RoM-only rows (left) + the sprite search panel (right).
        for (int i = first; i < last; ++i) {
            const int idx = visUnmapped[static_cast<usize>(i)];
            const std::string& bundle = unmappedRom_[cat_][static_cast<usize>(idx)];
            const float ry = listTop + static_cast<float>(i) * kRowH - scroll;
            const bool sel = selUnmapped_ == idx;
            // Selection is a border + left bar, NOT a fill — a fill on the UI view would
            // cover the 3D preview living on view 0 underneath.
            ui::border(sb, 0, ry, listW, kRowH - 2,
                       sel ? ui::color::kWinSelectStrong : ui::color::kWinBorder,
                       sel ? 3.0f : 1.0f);
            if (sel) ui::panel(sb, 0, ry, 6, kRowH - 2, ui::color::kWinSelectStrong);
            if (in.mousePressed && in.mouseY >= ry && in.mouseY < ry + kRowH &&
                in.mouseX < listW - 14.0f && in.mouseY >= listTop)
                selUnmapped_ = idx;
            if (RomActor* ra = modelForBundle(app, bundle); ra && ra->ready()) {
                const float d = fitDepth(ra->height(), kRowH * 0.72f);
                draws.push_back({ra, worldAt(listW * 0.4f, ry + kRowH * 0.85f, d)});
            }
            else if (romBundleCache_.count(baseName(bundle)))
                font.draw(sb, 12, ry + kRowH * 0.45f, 1.3f, ui::color::kError,
                          "модель не собралась");
            font.draw(sb, 10, ry + kRowH - 24, 1.5f,
                      sel ? ui::color::kWinAccent : ui::color::kWinText, baseName(bundle));
        }

        // Search panel.
        const float px = listW;
        ui::panel(sb, px, listTop, kSearchW, listH, ui::color::kWinBody);
        ui::border(sb, px, listTop, kSearchW, listH, ui::color::kWinBorder, 1.0f);
        font.draw(sb, px + 10, listTop + 8, 1.4f, ui::color::kWinText, "Поиск спрайта:");
        search_.fontScale = 1.5f;
        search_.maxLen = 32;
        const float sfx = px + 10, sfy = listTop + 30, sfw = kSearchW - 20, sfh = 26;
        if (in.mousePressed && in.mouseX >= sfx && in.mouseX < sfx + sfw && in.mouseY >= sfy &&
            in.mouseY < sfy + sfh)
            unmappedFocus_ = 1;
        const bool sfocus = unmappedFocus_ == 1;
        search_.update(in, sfocus);
        search_.draw(sb, font, sfx, sfy, sfw, sfh, sfocus, time_);
        if (search_.value != searchDone_) {
            searchDone_ = search_.value;
            searchHits_.clear();
            std::string q = searchDone_;
            for (char& c : q) c = static_cast<char>(std::tolower(static_cast<u8>(c)));
            if (q.size() >= 2)
                for (usize s = 0; s < allSprites_.size(); ++s)
                    if (allSprites_[s].find(q) != std::string::npos) searchHits_.push_back(s);
            searchScroll_ = 0.0f;
        }
        // Result list (click a name to preview the sprite below).
        const float resTop = listTop + 66.0f;
        const float lineH = 20.0f;
        const float resH = listH * 0.45f;
        if (in.mouseX >= px && in.wheel != 0.0f && in.mouseY < resTop + resH)
            searchScroll_ = std::clamp(
                searchScroll_ - in.wheel * lineH * 3,
                0.0f,
                std::max(0.0f, static_cast<float>(searchHits_.size()) * lineH - resH));
        scrollbar(px + kSearchW - 16.0f, resTop, resH,
                  static_cast<float>(searchHits_.size()) * lineH, searchScroll_, 2);
        const int rFirst = static_cast<int>(searchScroll_ / lineH);
        const int rLast = std::min(static_cast<int>(searchHits_.size()),
                                   rFirst + static_cast<int>(resH / lineH));
        for (int r = rFirst; r < rLast; ++r) {
            const std::string& nm = allSprites_[searchHits_[static_cast<usize>(r)]];
            const float ryy = resTop + static_cast<float>(r) * lineH - searchScroll_;
            const bool hov = in.mouseX >= px && in.mouseY >= ryy && in.mouseY < ryy + lineH;
            if (nm == previewSprite_ || hov)
                ui::panel(sb, px + 4, ryy, kSearchW - 8, lineH,
                          nm == previewSprite_ ? ui::color::kWinSelect : ui::color::kWinContent);
            if (hov && in.mousePressed) previewSprite_ = nm;
            font.draw(sb, px + 10, ryy + 3, 1.3f, ui::color::kWinText, nm);
        }
        if (searchHits_.empty() && searchDone_.size() >= 2)
            font.draw(sb, px + 10, resTop + 4, 1.3f, ui::color::kWinTextDim, "ничего не найдено");

        // Sprite preview + the pair button.
        const float pvY = resTop + resH + 8.0f;
        if (!previewSprite_.empty()) {
            if (CharacterActor* ca = spriteFor(app, previewSprite_, -1))
                ca->renderScaled(sb, px + kSearchW * 0.5f, pvY + 130.0f, 2.0f, time_);
            else
                font.draw(sb, px + 10, pvY + 40, 1.3f, ui::color::kError, "спрайт не читается");
            font.draw(sb, px + 10, pvY + 140, 1.3f, ui::color::kWinText, previewSprite_);
            const bool can = selUnmapped_ >= 0 &&
                             selUnmapped_ < static_cast<int>(unmappedRom_[cat_].size());
            if (ui::button(sb, font, in, px + 10, pvY + 162, kSearchW - 20, 28, "Связать", 1.5f,
                           can) &&
                can)
                savePair(app, unmappedRom_[cat_][static_cast<usize>(selUnmapped_)],
                         previewSprite_);
        } else {
            font.draw(sb, px + 10, pvY + 8, 1.3f, ui::color::kWinTextDim,
                      "клик по имени — превью спрайта");
        }
        if (!status_.empty())
            font.draw(sb, px + 10, listTop + listH - 26, 1.3f, ui::color::kOk, status_);
    }

    font.draw(sb, fW - 260, 8, 1.3f, ui::color::kWinTextDim, "Esc — выход  |  UaRO --view");
    sb.end();

    // 3D submits after the UI batch is flushed (they live on view 0, under the UI overlay;
    // the rows deliberately leave the model cells unfilled so view 0 shows through).
    for (const ModelDraw& d : draws)
        d.actor->render(0, d.pos, turnDir, viewAttack_ ? Anim::Attack : Anim::Idle, time_);
}

}  // namespace uaro
