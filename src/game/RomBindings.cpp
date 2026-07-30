#include "game/RomBindings.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <span>
#include <unordered_map>

#include "app/Application.hpp"
#include "core/Log.hpp"
#include "net/JobNames.hpp"
#include "game/RomActor.hpp"
#include "world/RomModel.hpp"

namespace uaro {
namespace rombind {
namespace {

// Recolor family: these mobs share ONE base model and differ only by the skin the _ext
// bundle already carries (S.: "poporing/marin/drops — одна и та же модель").
struct Reskin {
    const char* from;
    const char* to;
    float scale;  // render scale on top of the authored size
    bool retex;   // true: pick the texture by the MOB's name (family skins in _ext);
                  // false: pure name alias, keep the base material's own texture
};
static const Reskin kReskin[] = {
    {"poporing", "poring", 1.0f, true},
    {"marin", "poring", 1.0f, true},
    {"drops", "poring", 1.0f, true},
    {"thief_bug_egg", "thifebug_egg", 1.0f, false},  // sic: the typo is in the RoM data
    {"thief_bug_larva", "thiefbug", 1.0f, true},  // the basic culvert bug (class 1051)
    {"thief_bug_female", "thiefbug", 2.0f, true},  // grown bugs are 2x the base (S.)
    {"thief_bug_male", "thiefbug", 2.0f, true},
    {"peco_egg", "pecopeco_egg", 1.0f, false},  // RO table name vs RoM bundle name
    {"grand_peco", "pecopeco", 1.5f, false},    // pecopeco x1.5 (S.)
    // Goblins: RO names goblin_1..5, RoM bundles goblin/goblin2..5.
    {"goblin_1", "goblin", 1.0f, false},
    {"goblin_2", "goblin2", 1.0f, false},
    {"goblin_3", "goblin3", 1.0f, false},
    {"goblin_4", "goblin4", 1.0f, false},
    {"goblin_5", "goblin5", 1.0f, false},
    // Baby desert wolf: the desert wolf model at half size (S. call; littlewolf is a
    // different pet-style pup and read wrong in game).
    {"desert_wolf_b", "wolf", 0.25f, false},  // 0.5 (desert wolf) x 0.5 (baby)
    {"desert_wolf", "wolf", 0.5f, false},  // halve (S.)
    // Poring family variants (skins live in poring_ext; texture picked via the alias
    // table in RomActor): angeling = "поринг с крылышками" (S.) — wings later.
    {"angeling", "poring", 1.0f, true},
    {"mastering", "poring", 1.3f, true},
    {"deviling", "poring", 1.0f, true},
    {"ghostring", "poring", 1.0f, true},
    // Snake family: one model, skins in snake_ext (Greensnake/Blacksnake/Blacksnake_R).
    {"anacondaq", "snake", 1.0f, true},
    {"side_winder", "snake", 1.0f, true},
    // Bat family: farmiliar_ext ships Drainliar/Farmiliar_R skins too.
    {"drainliar", "farmiliar", 1.0f, true},
    // Spore family: the material picks the blue PoisonSpore skin — force the alias
    // table (spore -> Spore_R, the red cap; poison_spore -> PoisonSpore).
    {"spore", "spore", 1.0f, true},
    {"poison_spore", "spore", 1.0f, true},
    // Plant colour variants share one model (plant_ext: Plant / Plant_R / Plant_R2).
    {"green_plant", "plant", 1.0f, true},
    {"red_plant", "plant", 1.0f, true},
    {"blue_plant", "plant", 1.0f, true},
    {"yellow_plant", "plant", 1.0f, true},
    {"white_plant", "plant", 1.0f, true},
    {"shining_plant", "plant", 1.0f, true},
    // Rocker family (miniboss/rocker.unity3d skins: Rocker=green, Rocker_10=brown,
    // Metaller=golden; the material picked the golden one — S. wants green).
    {"rocker", "rocker", 1.5f, true},        // +50% (S.)
    {"vocal", "rocker", 1.875f, true},       // rocker +25%; no blue skin in the pack
    {"metaller", "rocker", 1.5f, true},  // +50% (S.)
    {"hornet", "bee", 1.0f, false},          // hornet has no model; bee fits (S.)
    // Wormtail family (wormtail_ext skins: WormTail=green, _R=brown, _R2=black,
    // StemWorm=WHITE): S. wants the wormtail white; stem worm reuses the model.
    {"worm_tail", "wormtail", 1.0f, true},
    {"stem_worm", "wormtail", 1.0f, true},
    {"creamy", "creamy", 2.0f, false},       // 2x (S.)
    // Petit: 1155 = the green ground dragon (skin 'Petit'), 1156 = the blue flyer
    // (petit_f bundle); the material picked the blue skin for both.
    {"petit", "petit", 1.0f, true},
    {"petit_", "petit_f", 1.0f, false},
    // Deleters = petit reskins (skins Deleter_petit / Deleter_petit_F ship in petit_ext).
    {"deleter", "petit", 1.0f, true},
    {"deleter_", "petit_f", 1.0f, true},
    // Mushrooms share one model (skins Mushroom / Mushroom2).
    {"black_mushroom", "mushroom", 1.0f, true},
    {"red_mushroom", "mushroom", 1.0f, true},
    // Skeleton family: the MODEL is skeleton_archer.unity3d (its _ext ships the
    // Skeleton_Soldier skin too; plain skeleton's diffuse comes via
    // texture/body/skeleton.unity3d). The skeleton_archer/ SUBFOLDER holds per-variant
    // clip bundles only. Zombie has no model in the dump.
    {"skeleton", "skeleton_archer", 0.9f, true},   // S.: "великоваты на пол головы"
    {"skel_soldier", "skeleton_archer", 0.9f, true},
    {"skel_archer", "skeleton_archer", 0.9f, false},
    {"skel_worker", "skeleton_archer", 0.9f, true},  // no own model; soldier skin
    // Scale-only tweaks (same model, S. live-QA sizing).
    {"munak", "munak", 1.15f, false},     // "маловаты на пол головы"
    {"bon_gun", "bon_cun", 1.15f, false},
    {"the_paper", "paper", 1.0f, false},
    {"metaling", "metalring", 0.5f, false},  // sic: RoM romanization; halve (S.)
    {"mineral", "mineral_mini", 1.0f, false},  // only the mini-pet model ships
    // First --view pairs file (S.):
    {"e_minorous", "minorous", 1.0f, false},
    {"mysteltainn", "mysteltainn_mini", 1.0f, false},
    {"rafflesia", "raffliesia", 1.0f, false},  // sic: RoM romanization
    {"demon_pungus", "punk", 1.2f, false},   // punk model; no alt skin in the pack (S.)
    {"lava_golem", "n_lavagolem", 1.0f, false},  // lives in art/role/mvp/
    {"explosion", "redexplosion", 1.0f, false},  // the red bat
    {"kaho", "lordkaho", 1.0f, false},
    {"nightmare_terror", "nightmare", 1.15f, true},  // alt skin n_nightmare_001a_d
    {"c_tower_manager", "tower_manager", 1.0f, false},
    {"elder_wilow", "wilow", 1.2f, true},  // own Wilow_Elder skin in wilow_ext
    {"wolf", "wolf", 0.5f, false},         // halve (S.)
    // Worker ants share one model (ant.unity3d skins Ant/Ant2/Ant3).
    {"andre", "ant", 1.0f, true},
    {"piere", "ant", 1.0f, true},
    {"deniro", "ant", 1.0f, true},
    {"vitata", "ant", 1.1f, false},  // base skin, slightly bigger (honey ant)
    // Town NPCs (log sweep): our sprite names vs RoM bundle names.
    {"job_blacksmith", "blacksmith_m2", 1.0f, false},
    {"novice", "novice_m", 1.0f, false},     // the NPC novice (player-dir model)
    {"postbox", "n_prtmailbox", 1.0f, false},
    {"4w_sailor", "n_sailor_001", 1.0f, false},
    {"ruswoman2", "woman2", 1.0f, false},
    {"twmidman", "man", 1.0f, false},
    {"molgenstein", "chefmaster_alchemist", 1.0f, false},  // the mad chemist look
    // NB: ac01dy_* turned out to be academy decor arches, not people — dropped.
    {"librarygirl", "girl", 1.0f, false},
    {"grandmother", "oldwoman", 1.0f, false},
    {"maid", "maid1", 1.0f, false},
    {"lgtgirl", "girl2", 1.0f, false},
    {"signart", "n_sign001", 1.0f, false},
    {"signalche", "n_sign001", 1.0f, false},
    {"kid3", "boy", 1.0f, false},
    {"humerchant", "merchant_m", 1.0f, false},
    {"alche", "alchemist_m", 1.0f, false},
    {"job_hunter", "hunter_m", 1.0f, false},
    {"jobguider", "adventurer_m", 1.0f, false},
    {"pay_elder", "oldwoman", 1.0f, false},
    {"job_knight", "knight_m", 1.0f, false},
    {"job_knight1", "knight_m", 1.0f, false},
    {"job_knight2", "knight_m", 1.0f, false},
    {"youngknight", "knight_m", 1.0f, false},
    {"knightmaster", "lordknight_m", 1.0f, false},
    {"swordmaster", "swordman_m", 1.0f, false},
    {"eden_officer", "eden_m", 1.0f, false},
    {"manager", "man2", 1.0f, false},
    {"innkeeper", "man", 1.0f, false},
    {"pierrot", "clown", 1.0f, false},
    // Einbroch sweep (S. log): local citizens reuse the town-generic models.
    {"ein_soldier", "guard", 1.0f, false},
    {"einman", "man", 1.0f, false},
    {"einold", "weird_old_man", 1.0f, false},
    {"einwoman", "woman", 1.0f, false},
    {"bulletin_board2", "n_prtbulletin", 1.0f, false},
    {"repair", "repair_m", 1.0f, false},
    {"sage_c", "sage_m", 1.0f, false},
    {"merchant", "merchant_m", 1.0f, false},
    {"job_assassin", "assassin_m", 1.0f, false},
    {"8w_soldier", "guard", 1.0f, false},
    {"bulletin_board", "n_prtbulletin", 1.0f, false},
    {"f", "woman", 1.0f, false},  // the bare "4_F" townswoman sprite
    {"whisper", "whisper", 0.5f, false},       // authored too big — halve (S.)
    {"nine_tail", "ninetail", 0.5f, false},    // halve (S.); glued RoM name
    {"whisper_boss", "whisper", 1.0f, true},  // Giant Whisper = 2x the (halved) whisper;
                                              // re-dump ships Whisper_10 skin in whisper_ext
};
// Mob models are sorted into subfolders (verified on the real RoM.zip: monster/ 439,
// npc/ 1288, mvp/ 106, miniboss/ 41, mount/ 147; poring = body/monster/poring.unity3d).
static const char* kDirs[] = {"Android/art/model/role/body/monster/",
                              "Android/art/model/role/body/mvp/",
                              "Android/art/model/role/body/miniboss/",
                              "Android/art/model/role/body/npc/",
                              "Android/art/model/role/body/",
                              "Android/art/role/mvp/",   // n_* remakes (n_lavagolem)
                              "Android/art/role/npc/",
                              // NPC versions of player jobs (the bard busker) live in
                              // the player dir; harmless for mobs (no name overlap).
                              "Android/art/model/role/body/player/"};

// RO sprite names carry a job/gender prefix the RoM bundles drop: 4_f_kafra1 -> kafra1,
// 2_m_thiefmaster -> thiefmaster, 4w_f_kafra2 -> kafra2. Strip a leading "<digits>[w]_"
// and an optional "m_"/"f_" gender segment so the exact-name miss can retry on the bare
// bundle name. Returns the input unchanged when there is no numeric prefix.
static std::string stripSpritePrefix(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') i++;
    if (i < s.size() && s[i] == 'w') i++;          // 4w_ voting-kafra variant
    if (i == 0 || i >= s.size() || s[i] != '_') return s;  // no "<digits>_" prefix
    size_t j = i + 1;                              // past the underscore
    if (j + 1 < s.size() && (s[j] == 'm' || s[j] == 'f') && s[j + 1] == '_') j += 2;
    return s.substr(j);
}

// Ordered bundle-name candidates for a (reskin-resolved) sprite name: the exact name, the
// underscore-collapsed form (THIEF_BUG -> thiefbug), and — only when it yields a meaningful
// non-numeric token — the prefix-stripped form + its collapsed form. Shared by the in-game
// resolver (actorFor) and the --view auto-pairing (resolveBundle) so the browser mirrors what
// the game actually loads. Additive: the extra candidates are tried only after exact misses.
static std::vector<std::string> bundleCandidates(const std::string& base) {
    std::vector<std::string> out;
    auto add = [&](const std::string& c) {
        if (c.empty()) return;
        for (const auto& e : out)
            if (e == c) return;
        out.push_back(c);
    };
    auto collapse = [](std::string c) {
        c.erase(std::remove(c.begin(), c.end(), '_'), c.end());
        return c;
    };
    add(base);
    add(collapse(base));
    const std::string stripped = stripSpritePrefix(base);
    const bool numeric =
        !stripped.empty() &&
        stripped.find_first_not_of("0123456789") == std::string::npos;
    if (stripped != base && stripped.size() >= 3 && !numeric) {
        add(stripped);
        add(collapse(stripped));
    }
    return out;
}

}  // namespace

// RoM headgear bindings (#135): viewer-headgear-pairs.txt maps a RO headgear view id to a
// RoM head-model name (found in either head folder). Loaded once, lazily.
static const std::unordered_map<u16, std::string>& headgearPairs() {
    static std::unordered_map<u16, std::string> m;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        std::ifstream in("viewer-headgear-pairs.txt");
        std::string line;
        while (std::getline(in, line)) {
            // "id = model_name"
            const auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            const u16 id = static_cast<u16>(std::strtoul(line.c_str(), nullptr, 10));
            std::string name = line.substr(eq + 1);
            // trim spaces
            const auto b = name.find_first_not_of(" \t\r\n");
            const auto e = name.find_last_not_of(" \t\r\n");
            if (b == std::string::npos) continue;
            if (id) m[id] = name.substr(b, e - b + 1);
        }
    }
    return m;
}

// Load a head-accessory model by name from either RoM head folder (+ its _ext).
static bool loadHeadModel(Application& app, const std::string& name, RomModel& out) {
    for (const char* dir : {"Android/art/role/parts/head/", "Android/art/model/role/head/"})
        if (auto b = app.vfs().readFrom(ContentSource::Rom, std::string(dir) + name + ".unity3d")) {
            appendRomBundle(out, *b);
            if (auto e = app.vfs().readFrom(ContentSource::Rom,
                                            std::string(dir) + name + "_ext.unity3d"))
                appendRomBundle(out, *e);
            return out.mesh.vertexCount != 0;
        }
    return false;
}

RomActor* actorFor(Application& app, const std::string& name, Cache& cache,
                   const std::string& skin) {
    if (name.empty()) return nullptr;
    const std::string cacheKey = skin.empty() ? name : name + "#" + skin;
    auto it = cache.find(cacheKey);
    if (it != cache.end()) return it->second.get();
    std::unique_ptr<RomActor> actor;
    std::string base = name;
    float reskinScale = 1.0f;
    bool reskin = false;
    for (const auto& r : kReskin)
        if (base == r.from) {
            base = r.to;
            reskinScale = r.scale;
            reskin = r.retex;
        }
    // RO sprite names use underscores, RoM bundle names usually don't (THIEF_BUG ->
    // thiefbug): when the exact name misses, retry with the underscores stripped.
    const std::vector<std::string> candidates = bundleCandidates(base);
    // Regular mobs split their content across bundles: <name>.unity3d = mesh+skeleton,
    // <name>_ext.unity3d = textures, art/public/animation/body/<name>/<clip>.unity3d = one
    // clip each. Minibosses are self-contained (the extra probes just miss, harmlessly).
    for (const std::string& cand : candidates) {
    if (actor) break;
    // NB: from here down `cand` is the effective bundle name (base or underscore-stripped).
    for (const char* dir : kDirs) {
        auto bytes = app.vfs().readFrom(ContentSource::Rom, std::string(dir) + cand + ".unity3d");
        if (!bytes) continue;
        RomModel model;
        model.wantName = cand;  // exact-name mesh wins over richer variant meshes
        if (!appendRomBundle(model, *bytes)) continue;
        if (auto ext = app.vfs().readFrom(ContentSource::Rom,
                                          std::string(dir) + cand + "_ext.unity3d"))
            appendRomBundle(model, *ext);  // textures live in the companion bundle
        // Validate only AFTER the _ext merge: some mobs keep the whole model in the
        // companion bundle (farmiliar: empty base, mesh+rig+clips in farmiliar_ext — it
        // stayed a sprite, S.).
        if (model.mesh.vertexCount == 0 || model.skeleton.parents.empty()) continue;
        // Some diffuses ship as standalone texture bundles the material points at (the _ext
        // bundle may carry a whole FAMILY of skins, e.g. poring_ext = Drops/Marin/Poporing/...).
        // A few reskins borrow ANOTHER mob's external texture bundle (skel_worker wants the
        // brown-leather base Skeleton diffuse).
        std::string texName = name;
        static const std::pair<const char*, const char*> kTexBundle[] = {
            {"skel_worker", "skeleton"},
        };
        for (const auto& [from, to] : kTexBundle)
            if (texName == from) texName = to;
        if (auto tb = app.vfs().readFrom(ContentSource::Rom,
                                         "Android/art/public/texture/body/" + texName +
                                             ".unity3d"))
            appendRomBundle(model, *tb);
        // Aliased NPCs (einman -> man): the diffuse only exists under the BUNDLE name — the
        // texture-less town bodies rendered black (S.: "Tan/Mark чёрной моделью").
        else if (texName != cand)
            if (auto tb2 = app.vfs().readFrom(ContentSource::Rom,
                                              "Android/art/public/texture/body/" + cand +
                                                  ".unity3d"))
                appendRomBundle(model, *tb2);
        // Variant bundles in a base-named SUBFOLDER (plant/shining_plant.unity3d,
        // skeleton_archer/skeleton.unity3d): per-variant skins/clips — merge when present.
        if (reskin)
            if (auto vb = app.vfs().readFrom(ContentSource::Rom, std::string(dir) + cand + "/" +
                                                                     name + ".unity3d"))
                appendRomBundle(model, *vb);
        static const char* kClips[] = {"wait", "walk", "attack", "hit", "die"};
        for (const char* clip : kClips)
            if (auto cb = app.vfs().readFrom(ContentSource::Rom,
                                             "Android/art/public/animation/body/" + cand + "/" +
                                                 clip + ".unity3d"))
                appendRomBundle(model, *cb);
        finalizeRomModel(model);
        if (reskin) {
            model.name = name;
            model.mainTexPathId = 0;  // base material points at the base skin
        }
        if (!skin.empty()) {  // dyes tab: this exact skin wins over material/alias picks
            model.forceTexName = skin;
            model.mainTexPathId = 0;
        }
        auto ra = std::make_unique<RomActor>();
        if (ra->load(app, std::move(model))) {
            if (reskinScale != 1.0f) ra->scaleBy(reskinScale);
            // NPC hair/head parts live in separate bundles (kafra was bald, S.): try
            // role/head/<name>_head and head_<name>, attach at the CP_1 head joint. The
            // numbered NPC variants share ONE head bundle without the digit (kafra1..6 ->
            // head_kafra/kafra_head), so also probe with trailing digits stripped.
            std::string stem = name;
            while (!stem.empty() && std::isdigit(static_cast<u8>(stem.back()))) stem.pop_back();
            for (const std::string& hn :
                 {"Android/art/model/role/head/" + name + "_head.unity3d",
                  "Android/art/model/role/head/head_" + name + ".unity3d",
                  "Android/art/model/role/head/" + stem + "_head.unity3d",
                  "Android/art/model/role/head/head_" + stem + ".unity3d",
                  "Android/art/model/role/head/" + stem + "_head_f.unity3d",
                  // Also probe by the BODY bundle name (knight_m -> knight_m_head, oldwoman ->
                  // oldwoman_head): job/town-NPC heads are keyed to the body, not the NPC name, so
                  // NPCs that DO have a head get it and stop being headless (S.).
                  "Android/art/model/role/head/" + cand + "_head.unity3d",
                  "Android/art/model/role/head/head_" + cand + ".unity3d",
                  "Android/art/model/role/head/" + cand + "_head_f.unity3d"}) {
                if (auto hb = app.vfs().readFrom(ContentSource::Rom, hn)) {
                    RomModel head;
                    appendRomBundle(head, *hb);
                    // CP_4 = crown (y~1.66), not CP_1 = neck: NPC head models are authored around
                    // their origin, so CP_1 sank the whole head into the neck ("без головы", S.).
                    if (head.mesh.vertexCount != 0 &&
                        ra->attachPart(app, head, "CP_4", 1.0f))
                        break;
                }
            }
            // Town NPC hair ships separately under role/hair/npc/ (hair_man, hair_woman2,
            // cecilia_hair, ...) — without it every generic townsperson is bald (S.: "у
            // кафры нету волос", "без волос"). Probe hair_<x> and <x>_hair for the mob name
            // and the bundle name; attach at the CP_1 head joint like the player hair.
            for (const std::string& base2 : {name, cand}) {
                bool attached = false;
                for (const std::string& hb :
                     {"Android/art/model/role/hair/npc/hair_" + base2 + ".unity3d",
                      "Android/art/model/role/hair/npc/" + base2 + "_hair.unity3d"}) {
                    if (auto hbb = app.vfs().readFrom(ContentSource::Rom, hb)) {
                        RomModel hair;
                        appendRomBundle(hair, *hbb);
                        // CP_4 = crown (y~1.66), not CP_1 = neck. NPC hair is authored around its
                        // origin, so CP_1 dropped it to the neck/face; CP_4 alone then sat it ABOVE
                        // the head (S.: dassy/chief assistant "опустить"), so lower it onto the scalp
                        // with a small yLift. Tunable knob if it needs more/less.
                        if (hair.mesh.vertexCount != 0 &&
                            ra->attachPart(app, hair, "CP_4", 1.0f, false, nullptr, -0.25f)) {
                            attached = true;
                            break;
                        }
                    }
                }
                if (attached) break;
                if (name == cand) break;  // same string: one pass is enough
            }
            // Archer mobs: RoM attaches their bow by game logic, not in the bundle (#119).
            static const std::pair<const char*, const char*> kMobWeapon[] = {
                {"skel_archer", "bow/61_bow"},
                {"gargoyle", "bow/61_bow"},
                {"goblin_archer", "bow/61_bow"},
            };
            for (const auto& [mob, wm] : kMobWeapon)
                if (name == mob) {
                    RomModel wpn;
                    if (auto wb = app.vfs().readFrom(
                            ContentSource::Rom,
                            std::string("Android/art/model/role/weapon/") + wm + ".unity3d"))
                        appendRomBundle(wpn, *wb);
                    if (wpn.mesh.vertexCount != 0)
                        ra->attachPart(app, wpn, "CP_3", 1.0f, /*anchorAtNode=*/true);
                }
            // Angeling = a poring with wings (#122): angeling_wing has no texture in the
            // pack, but role/wing/angel_wing does (skinned, own rig). Attach it to the poring's
            // back point (EP_7) — skinned parts rest-attach like the female hair.
            if (name == "angeling") {
                RomModel wing;
                if (auto wb = app.vfs().readFrom(ContentSource::Rom,
                                                 "Android/art/model/role/wing/angel_wing.unity3d"))
                    appendRomBundle(wing, *wb);
                if (wing.mesh.vertexCount != 0) ra->attachSkinnedPart(app, wing, "EP_7");
            }
            actor = std::move(ra);
        }
        break;
    }
    }
    if (!actor) log::info("game: no RoM model for '{}'", name);
    return cache.emplace(cacheKey, std::move(actor)).first->second.get();
}

// Chars=ROeM: the player model is body/player/<job>_<m|f>.unity3d (mesh with head+face +
// the full wait/walk/attack/die/sit_down moveset in-bundle) plus a HAIR model attached at
// the CP_1 connect point (the head joint). The body already carries the head — attaching
// role/head/* on top gave S. a "человечек вместо головы". Job names: lowercased jobName
// with spaces as underscores (super_novice_m etc.).
// RO weapon VIEW id -> a default RoM weapon model per type (the 61_* series exists for
// every class of weapon; per-item models come once an item->model table lands).
// RO sends a weapon as a small VIEW class (1..16) in other players' spawn packets, but the
// OWN character's charWeapon starts as the equipped ITEM id (1000+). Map the classic weapon
// item-id ranges onto a view class so the own Lord Knight's sabre/lance show too (S.).
static u16 weaponViewFromItem(u16 w) {
    if (w < 100) return w;  // already a view class
    if (w >= 1100 && w < 1150) return 2;   // one-hand sword
    if (w >= 1150 && w < 1200) return 3;   // two-hand sword
    if (w >= 1200 && w < 1250) return 1;   // dagger
    if (w >= 1250 && w < 1300) return 16;  // katar
    if (w >= 1300 && w < 1350) return 6;   // one-hand axe
    if (w >= 1350 && w < 1400) return 7;   // two-hand axe
    if (w >= 1400 && w < 1450) return 4;   // one-hand spear
    if (w >= 1450 && w < 1500) return 5;   // two-hand spear / lance
    if (w >= 1500 && w < 1550) return 8;   // mace
    if (w >= 1550 && w < 1600) return 15;  // book
    if (w >= 1600 && w < 1700) return 10;  // staff / rod
    if (w >= 1700 && w < 1800) return 11;  // bow
    if (w >= 1950 && w < 2000) return 15;  // instrument (bard) -> book-ish grip
    return 0;
}

const char* weaponModelFor(u16 viewRaw) {
    const u16 view = weaponViewFromItem(viewRaw);
    switch (view) {
        case 1: return "knife/61_knife";        // dagger
        case 2: case 3: return "sword/61_sword";
        case 4: case 5: return "spear/61_spear";
        case 6: case 7: return "axe/61_axe";
        case 8: return "mace/61_mace";
        case 10: return "staff/61_staff";
        case 11: return "bow/61_bow";
        case 15: return "book/61_book";
        case 16: return "katar/61_katar";
        default: return nullptr;  // fist/unknown: bare hands
    }
}

// The peco mount (#125): model + its own wait/walk/ride_* clips ship in one bundle;
// under a rider the ride_* pair replaces the plain gaits. Seat = CP_0 (0, 1.10, -0.12).
RomActor* mountFor(Application& app, Cache& cache) {
    const std::string key = "mount:pecopeco";
    auto it = cache.find(key);
    if (it != cache.end()) return it->second.get();
    std::unique_ptr<RomActor> actor;
    if (auto bytes = app.vfs().readFrom(ContentSource::Rom,
                                        "Android/art/model/role/body/mount/mount_pecopeco.unity3d")) {
        RomModel model;
        model.wantName = "mount_pecopeco";
        if (appendRomBundle(model, *bytes)) {
            if (auto ext = app.vfs().readFrom(
                    ContentSource::Rom,
                    "Android/art/model/role/body/mount/mount_pecopeco_ext.unity3d"))
                appendRomBundle(model, *ext);
            if (auto tb = app.vfs().readFrom(
                    ContentSource::Rom, "Android/art/public/texture/body/mount_pecopeco.unity3d"))
                appendRomBundle(model, *tb);
            finalizeRomModel(model);
            for (const auto& [from, to] :
                 {std::pair{"ride_wait", "wait"}, std::pair{"ride_walk", "walk"}})
                if (auto cit = model.clips.find(from); cit != model.clips.end())
                    model.clips[to] = cit->second;
            if (model.mesh.vertexCount != 0 && !model.skeleton.parents.empty()) {
                auto ra = std::make_unique<RomActor>();
                if (ra->load(app, std::move(model))) actor = std::move(ra);
            }
        }
    }
    if (!actor) log::info("game: no RoM mount model");
    return cache.emplace(key, std::move(actor)).first->second.get();
}

RomActor* playerFor(Application& app, u16 jobClass, u8 sex, u16 hair, u16 weapon,
                    bool riding, Cache& cache, u16 headTop, u16 headMid, u16 headBottom) {
    std::string job = net::jobName(jobClass);
    for (char& c : job)
        c = c == ' ' ? '_' : static_cast<char>(std::tolower(static_cast<u8>(c)));
    const std::string key = "player:" + job + (sex ? "_m" : "_f") + ":" +
                            std::to_string(hair) + ":" + std::to_string(weapon) +
                            (riding ? ":r" : "") + ":h" + std::to_string(headTop) + "_" +
                            std::to_string(headMid) + "_" + std::to_string(headBottom);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second.get();
    std::unique_ptr<RomActor> actor;
    // RoM job names: trans classes keep the CLASSIC names (Professor -> scholar,
    // Clown -> minstrel), Stalker has no model (approximate with rogue).
    static const std::pair<const char*, const char*> kJobAlias[] = {
        {"professor", "scholar"},
        {"clown", "minstrel"},
        {"stalker", "rogue"},
    };
    for (const auto& [from, to] : kJobAlias)
        if (job == from) job = to;
    // RoM job bundles glue the words together (highwizard_m, lordknight_f) — try the
    // underscored name first, then with the separators stripped (S.: High Wizard was 2D).
    // Gendered classes (bard/dancer/gypsy/minstrel) have NO _m/_f suffix — try bare too.
    std::string glued = job;
    glued.erase(std::remove(glued.begin(), glued.end(), '_'), glued.end());
    std::string base, jobUsed = job;
    std::optional<std::vector<u8>> bytes;
    const std::string sfx = sex ? "_m" : "_f";
    const std::string tries[] = {job + sfx, glued + sfx, job, glued};
    for (const std::string& jn : tries) {
        base = "Android/art/model/role/body/player/" + jn;
        if ((bytes = app.vfs().readFrom(ContentSource::Rom, base + ".unity3d"))) {
            jobUsed = jn;
            break;
        }
    }
    if (!bytes)  // diagnostic (S. "все чары 2D"): the player body bundle is absent from the RoM
        log::warn("playerFor: no RoM body for job '{}' -> 2D (tried {}_m / {}); is the player/ folder "
                  "in RoM.zip? mobs load from a different folder (monster/)", job, job, glued);
    if (bytes) {
        RomModel model;
        model.wantName = jobUsed;
        if (appendRomBundle(model, *bytes) && model.mesh.vertexCount != 0 &&
            !model.skeleton.parents.empty()) {
            if (auto ext = app.vfs().readFrom(ContentSource::Rom, base + "_ext.unity3d"))
                appendRomBundle(model, *ext);
            // Only the novice ships its moveset in-bundle; the other jobs carry just
            // wait+playshow (the paladin glided in the wait pose, S.). Merge the core clips
            // from animation/body/<job>_<sex>/, and for dirs the device-dump pack lacks
            // borrow from a same-sex donor: the shared Bip001 bones resolve by hash across
            // rigs (verified: knight_m walk resolves 26/35 tracks on the paladin; the
            // job-specific bones just hold their default pose).
            {
                static const char* kClipsRide[] = {"ride_wait", "ride_walk"};
                static const char* kClipsFoot[] = {"wait", "walk", "attack", "hit", "die",
                                                   "sit_down"};
                const auto kClips = riding ? std::span<const char* const>(kClipsRide)
                                           : std::span<const char* const>(kClipsFoot);
                const std::string ownDir =
                    "Android/art/public/animation/body/" + jobUsed + "/";
                const std::string donorDir = sex ? "Android/art/public/animation/body/knight_m/"
                                                 : "Android/art/public/animation/body/priest_f/";
                for (const char* clip : kClips) {
                    if (model.clips.count(clip)) continue;
                    auto cb = app.vfs().readFrom(ContentSource::Rom,
                                                 ownDir + clip + ".unity3d");
                    if (!cb) cb = app.vfs().readFrom(ContentSource::Rom,
                                                     donorDir + clip + ".unity3d");
                    if (cb) appendRomBundle(model, *cb);
                }
            }
            finalizeRomModel(model);
            if (riding)
                for (const auto& [from, to] :
                     {std::pair{"ride_wait", "wait"}, std::pair{"ride_walk", "walk"}})
                    if (auto cit = model.clips.find(from); cit != model.clips.end())
                        model.clips[to] = cit->second;
            // The body texture is clothes-only — no skin/face. EVERY job gets a FACE mesh
            // (role/parts/headface/hf_*: skull + eyes + mouth, own 1024 diffuse). The face
            // preset is keyed to the HAIR style so changing hair swaps the whole head look —
            // skull shape, eyes, mouth, texture (S.: "привязать к смене причёски... 6-8 типов").
            struct FacePreset { const char* form; const char* tex; };
            static const FacePreset kFaces[] = {
                {"hf_babyface_001", "hf_babyface_001f1_d"},        // round baby face
                {"hf_averagesquare_001", "hf_averagesquare_001f1_d"},  // square, eyes/mouth v1
                {"hf_averagesquare_001", "hf_averagesquare_001f2_d"},  // square, eyes/mouth v2
                {"hf_averagemature_002", "hf_averagemature_002f1_d"},  // mature adult
                {"hf_shinsquare_001", "hf_shinsquare_001f1_d"},        // sharp square
            };
            constexpr usize kFaceCount = sizeof(kFaces) / sizeof(kFaces[0]);
            const FacePreset& fp = kFaces[hair % kFaceCount];
            RomModel head;
            if (auto hb = app.vfs().readFrom(
                    ContentSource::Rom,
                    std::string("Android/art/role/parts/headface/") + fp.form + ".unity3d")) {
                appendRomBundle(head, *hb);
                if (auto he = app.vfs().readFrom(
                        ContentSource::Rom,
                        std::string("Android/art/role/parts/headface/") + fp.form + "_ext.unity3d"))
                    appendRomBundle(head, *he);
                head.forceTexName = fp.tex;  // pick this preset's eyes/mouth skin
            }
            // Hair colour changes together with the style/face preset (S.: "цвет волос тоже
            // нужно менять с лицом и причёску"). 8 natural + fantasy tints, keyed to hair id.
            static const float kHairTints[][3] = {
                {0.28f, 0.24f, 0.22f},  // near-black
                {0.62f, 0.42f, 0.26f},  // brown
                {0.92f, 0.78f, 0.42f},  // blond
                {0.80f, 0.30f, 0.24f},  // red
                {0.40f, 0.52f, 0.92f},  // blue
                {0.72f, 0.42f, 0.74f},  // violet
                {0.40f, 0.66f, 0.44f},  // green
                {0.90f, 0.90f, 0.96f},  // silver
            };
            const float* hairTint = kHairTints[hair % 8];
            RomModel hairM;
            {
                const char* sexName = sex ? "male" : "female";
                // Classic RO hair ids (1..N) mostly DON'T exist as RoM bundles, so every style
                // fell back to one bundle -> "причёска только одна" (S.). Map the style id onto
                // the styles the pack actually ships (verified present), so changing hair gives
                // a different model.
                static const int kMaleHair[] = {9, 24, 25, 26, 27, 28, 31, 41};
                static const int kFemHair[] = {1, 23, 24, 25, 26, 27, 28, 51};
                const int realId = sex ? kMaleHair[hair % 8] : kFemHair[hair % 8];
                const std::string tries2[] = {"hair" + std::to_string(realId) + "_" + sexName,
                                              "hair" + std::to_string(hair) + "_" + sexName,
                                              sex ? "hair9_male" : "hair1_female"};
                for (const std::string& hn : tries2)
                    if (auto hb = app.vfs().readFrom(ContentSource::Rom,
                                                     "Android/art/model/role/hair/" + hn +
                                                         ".unity3d")) {
                        appendRomBundle(hairM, *hb);
                        // Female hair keeps its DIFFUSE in a companion bundle (hair1_female
                        // ships mesh-only, texture in hair1_female_ext) — without it women
                        // rendered bald (S.). Merge the _ext when present.
                        if (auto he = app.vfs().readFrom(
                                ContentSource::Rom,
                                "Android/art/model/role/hair/" + hn + "_ext.unity3d"))
                            appendRomBundle(hairM, *he);
                        if (hairM.mesh.vertexCount != 0) break;
                    }
            }
            auto ra = std::make_unique<RomActor>();
            if (ra->load(app, std::move(model))) {
                if (head.mesh.vertexCount != 0) ra->attachPart(app, head, "CP_1", 1.0f);
                // Female hairstyles are SKINNED meshes (own skeleton) — a rigid attach
                // collapsed them (S.: "нету волос у женщин"). attachSkinnedPart binds them to
                // the shared head bones; it falls back to a rigid part for boneless (male) hair.
                // Attach at CP_1 (the head joint the player hair is authored against): the male
                // (boneless) hair takes the rigid fallback, and its authored origin lands it on
                // the scalp here. CP_4 (crown) stacked the authored offset ON the crown and left
                // it floating above the head (S.: "у новиса/хайвиза причёска выше головы").
                if (hairM.mesh.vertexCount != 0)
                    ra->attachSkinnedPart(app, hairM, "CP_1", hairTint);
                // Headgear (#135): each equipped slot's RoM head model (from the bind file)
                // rides the head joint like the hair.
                for (u16 hv : {headTop, headMid, headBottom}) {
                    if (!hv) continue;
                    auto hit = headgearPairs().find(hv);
                    if (hit == headgearPairs().end()) continue;
                    RomModel hg;
                    if (loadHeadModel(app, hit->second, hg))
                        ra->attachSkinnedPart(app, hg, "CP_4");
                }
                // Weapon in the right hand (CP_3), anchored to the palm node (#119).
                if (const char* wm = weaponModelFor(weapon)) {
                    RomModel wpn;
                    if (auto wb = app.vfs().readFrom(
                            ContentSource::Rom,
                            std::string("Android/art/model/role/weapon/") + wm + ".unity3d"))
                        appendRomBundle(wpn, *wb);
                    if (wpn.mesh.vertexCount != 0)
                        ra->attachPart(app, wpn, "CP_3", 1.0f, /*anchorAtNode=*/true);
                }
                actor = std::move(ra);
            }
        }
    }
    if (!actor) log::info("game: no RoM player model for '{}'", key);
    return cache.emplace(key, std::move(actor)).first->second.get();
}

std::string resolveBundle(Application& app, const std::string& name) {
    if (name.empty()) return {};
    std::string base = name;
    for (const auto& r : kReskin)
        if (base == r.from) base = r.to;
    for (const std::string& cand : bundleCandidates(base))
        for (const char* dir : kDirs) {
            const std::string p = std::string(dir) + cand + ".unity3d";
            if (app.vfs().existsFrom(ContentSource::Rom, p)) return p;
        }
    return {};
}

}  // namespace rombind
}  // namespace uaro
