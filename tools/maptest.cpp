#include <cmath>
// maptest — load a full map from a GRF through the VFS and report what assembled.
// End-to-end exercise of GRF -> VFS -> RSW -> GND/GAT -> ground mesh -> textures
// -> models, with no GPU.
//
//   maptest <archive.grf> <mapname>     e.g. maptest data.grf prontera
#include <cstdio>

#include <map>

#include <cstdlib>

#include "core/Log.hpp"
#include "formats/Act.hpp"
#include "formats/Gat.hpp"
#include "formats/Spr.hpp"
#include "formats/Str.hpp"
#include "resource/Vfs.hpp"
#include "world/GroundMesh.hpp"
#include "world/MapLoader.hpp"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: maptest <grf> <mapname>\n");
        return 1;
    }
    uaro::log::set_level(uaro::log::Level::Warn);

    uaro::Vfs vfs;
    if (!vfs.mountGrf(argv[1])) {
        std::printf("ERROR: cannot mount %s\n", argv[1]);
        return 2;
    }
    // STRPROBE=<grf path>: parse a .str effect and report fps/maxKey/layers/keys, plus whether the
    // structure's computed byte size equals the file size (MATCH verifies the 124-byte keyframe
    // record + layer layout end-to-end). Used to build the .str effect renderer (#64 level-up etc).
    if (const char* sp = std::getenv("STRPROBE")) {
        auto bytes = vfs.read(sp);
        if (!bytes) { std::printf("STRPROBE: cannot read '%s'\n", sp); return 8; }
        auto str = uaro::Str::parse(*bytes);
        if (!str) { std::printf("STRPROBE: parse failed '%s'\n", sp); return 9; }
        uaro::usize keys = 0, computed = 36;  // 36-byte header
        for (const auto& L : str->layers()) {
            keys += L.keys.size();
            computed += 4 + L.textures.size() * 128 + 4 + L.keys.size() * 124;
        }
        std::printf("STRPROBE '%s': fps=%u maxKey=%u layers=%zu keys=%zu | computed=%zu file=%zu %s\n",
                    sp, str->fps(), str->maxKey(), str->layers().size(), keys, computed, bytes->size(),
                    computed == bytes->size() ? "MATCH" : "MISMATCH");
        return 0;
    }
    // SPRPROBE=<grf path>: load a .spr and report its frame counts + first dims. Used to confirm
    // the RO damage-digit sprite (data/sprite/이팩트/숫자.spr) decodes for #66 damage numbers.
    if (const char* sp = std::getenv("SPRPROBE")) {
        auto bytes = vfs.read(sp);
        if (!bytes) { std::printf("SPRPROBE: cannot read '%s'\n", sp); return 6; }
        auto spr = uaro::Sprite::parse(*bytes);
        if (!spr) { std::printf("SPRPROBE: parse failed '%s'\n", sp); return 7; }
        std::printf("SPRPROBE '%s': %zu rgba frames, %zu indexed frames\n", sp,
                    spr->rgbaFrames().size(), spr->indexedFrames().size());
        const auto& idx = spr->indexedFrames();
        for (uaro::usize i = 0; i < idx.size() && i < 12; ++i)
            std::printf("  idx %zu: %ux%u\n", i, static_cast<unsigned>(idx[i].width),
                        static_cast<unsigned>(idx[i].height));
        const auto& rg = spr->rgbaFrames();
        for (uaro::usize i = 0; i < rg.size() && i < 12; ++i)
            std::printf("  rgba %zu: %ux%u\n", i, static_cast<unsigned>(rg[i].width),
                        static_cast<unsigned>(rg[i].height));
        if (std::getenv("SPRDUMP")) {  // write each indexed frame as a PPM (transparent -> white)
            for (uaro::usize f = 0; f < idx.size(); ++f) {
                std::vector<uaro::u8> rgba = spr->indexedToRgba(f);
                if (idx[f].width == 0 || idx[f].height == 0 || rgba.empty()) continue;
                char path[64];
                std::snprintf(path, sizeof(path), "/tmp/spr_%zu.ppm", f);
                FILE* fp = std::fopen(path, "wb");
                if (!fp) continue;
                std::fprintf(fp, "P6\n%u %u\n255\n", static_cast<unsigned>(idx[f].width),
                             static_cast<unsigned>(idx[f].height));
                const uaro::usize n = static_cast<uaro::usize>(idx[f].width) * idx[f].height;
                for (uaro::usize p = 0; p < n; ++p) {
                    uaro::u8 px[3];
                    if (rgba[p * 4 + 3] == 0) { px[0] = px[1] = px[2] = 255; }
                    else { px[0] = rgba[p * 4]; px[1] = rgba[p * 4 + 1]; px[2] = rgba[p * 4 + 2]; }
                    std::fwrite(px, 1, 3, fp);
                }
                std::fclose(fp);
                std::printf("  dumped /tmp/spr_%zu.ppm\n", f);
            }
        }
        return 0;
    }
    // ACTPROBE=<grf path>: load a single .act (e.g. a weapon sprite) and report whether its
    // frames carry per-frame anchors. composePart applies the weapon's own anchor (ba-pa) ONLY
    // when the frame has one; roBrowser ignores weapon anchors entirely (is_main). So this tells
    // us whether my weapon draw diverges from the reference for the mounted Saber (#5).
    if (const char* ap = std::getenv("ACTPROBE")) {
        auto bytes = vfs.read(ap);
        if (!bytes) { std::printf("ACTPROBE: cannot read '%s'\n", ap); return 4; }
        auto act = uaro::Action::parse(*bytes);
        if (!act) { std::printf("ACTPROBE: parse failed '%s'\n", ap); return 5; }
        int withAnchor = 0, total = 0;
        for (uaro::usize a = 0; a < act->actions().size(); ++a)
            for (const auto& fr : act->actions()[a].frames) {
                ++total;
                if (!fr.anchors.empty()) ++withAnchor;
            }
        std::printf("ACTPROBE '%s': %zu actions, %d/%d frames carry an anchor\n", ap,
                    act->actions().size(), withAnchor, total);
        for (uaro::usize a = 0; a < act->actions().size() && a < 8; ++a) {
            const auto& frames = act->actions()[a].frames;
            if (!frames.empty() && !frames[0].anchors.empty())
                std::printf("  dir %zu frame0: anchor=(%d,%d)\n", a, frames[0].anchors[0][0],
                            frames[0].anchors[0][1]);
            else
                std::printf("  dir %zu frame0: (no anchor)\n", a);
        }
        // Per-motion weapon-layer presence: the equipped weapon only DRAWS in the motion whose
        // frames carry layers (sprIndex>=0). motionHasWeapon checks attack 5/10/11. For the katar
        // (#new): does its .act actually have weapon frames in those motions?
        for (int m : {0, 4, 5, 10, 11}) {
            const int a0 = m * 8;  // direction 0 of motion m
            if (a0 >= static_cast<int>(act->actions().size())) {
                std::printf("  motion %d: (absent)\n", m);
                continue;
            }
            int withLayers = 0;
            for (const auto& fr : act->actions()[a0].frames)
                for (const auto& L : fr.layers)
                    if (L.sprIndex >= 0) {
                        ++withLayers;
                        break;
                    }
            std::printf("  motion %d (action %d): %d/%zu frames have layers\n", m, a0, withLayers,
                        act->actions()[a0].frames.size());
        }
        return 0;
    }

    auto m = uaro::MapLoader::load(vfs, argv[2]);
    if (!m) {
        std::printf("ERROR: failed to load map '%s'\n", argv[2]);
        return 3;
    }

    std::printf("map: %s\n", m->name.c_str());
    std::printf("  rsw: v0x%x, gnd='%s' gat='%s' water.level=%.1f\n", m->rsw.version(),
                m->rsw.gndFile().c_str(), m->rsw.gatFile().c_str(), m->rsw.water().level);
    std::printf("  gnd: %ux%u, ground mesh: %zu verts / %zu indices / %zu tex-batches\n",
                m->gnd.width(), m->gnd.height(), m->ground.vertices.size(),
                m->ground.indices.size(), m->ground.batches.size());
    std::printf("  textures: %zu loaded / %zu\n", m->texturesLoaded(), m->textures.size());
    std::printf("  models: %zu placements, %zu resolved, %zu unique loaded\n",
                m->placements.size(), m->placementsResolved(), m->rsmCache.size());
    // RSW effect emitters (type-4 objects): tally by built-in effect id, so map weather /
    // particle effects can be matched against roBrowser's EffectTable (id 47 = torch flame).
    if (!m->rsw.effects().empty()) {
        std::map<int, int> byId;
        for (const auto& e : m->rsw.effects()) byId[e.id]++;
        std::printf("  effects: %zu emitter(s):", m->rsw.effects().size());
        for (const auto& [id, cnt] : byId) std::printf(" id%d x%d", id, cnt);
        std::printf("\n");
        // #56 probe: for the first few torch (id47) emitters, dump the raw emitter pos and the
        // NEAREST model placement (same raw RSW space) — to see if the flame emitter is
        // co-located with the brazier model and how their heights (pos[1], RSM Y points down)
        // relate, so the flame lands in the bowl.
        int shown = 0;
        for (const auto& e : m->rsw.effects()) {
            if (e.id != 47 || shown >= 4) continue;
            const uaro::RswModel* best = nullptr;
            float bestD = 1e30f;
            for (const auto& mo : m->rsw.models()) {
                const float dx = mo.pos[0] - e.pos[0], dz = mo.pos[2] - e.pos[2];
                const float d = dx * dx + dz * dz;
                if (d < bestD) { bestD = d; best = &mo; }
            }
            std::printf("    torch[%d] pos=(%.1f, %.1f, %.1f)", shown, e.pos[0], e.pos[1], e.pos[2]);
            if (best)
                std::printf("  nearest model '%s' pos=(%.1f, %.1f, %.1f) dXZ=%.2f dY=%.1f",
                            best->filename.c_str(), best->pos[0], best->pos[1], best->pos[2],
                            std::sqrt(bestD), best->pos[1] - e.pos[1]);
            std::printf("\n");
            ++shown;
        }
        // #56 probe: torch_01 native frame size + act layer scale, to size the flame to the
        // brazier bowl (stand is 8 raw units = 0.8 world tall) rather than guess.
        if (auto sb = vfs.read("data/sprite/\xc0\xcc\xc6\xd1\xc6\xae/torch_01.spr")) {
            if (auto spr = uaro::Sprite::parse(*sb)) {
                const auto& fr = !spr->rgbaFrames().empty() ? spr->rgbaFrames() : spr->indexedFrames();
                if (!fr.empty())
                    std::printf("    torch_01.spr: %zu frames, frame0 = %ux%u px\n", fr.size(),
                                fr[0].width, fr[0].height);
            }
        }
        if (auto ab = vfs.read("data/sprite/\xc0\xcc\xc6\xd1\xc6\xae/torch_01.act")) {
            if (auto act = uaro::Action::parse(*ab); act && !act->actions().empty() &&
                                                     !act->actions()[0].frames.empty() &&
                                                     !act->actions()[0].frames[0].layers.empty()) {
                const auto& L = act->actions()[0].frames[0].layers[0];
                std::printf("    torch_01.act: act0 frame0 layer0 pos=(%d,%d) scale=(%.2f,%.2f)\n", L.x,
                            L.y, L.scaleX, L.scaleY);
            }
        }
    }
    // CELL=cx,cy: dump the GAT cell type + corner heights for that cell and its neighbours, to
    // check walkability/height (e.g. a "bench" you should step onto and stand a little above).
    if (const char* cell = std::getenv("CELL"); cell && m->gat) {
        int qx = 0, qy = 0;
        if (std::sscanf(cell, "%d,%d", &qx, &qy) == 2) {
            const uaro::Gat& g = *m->gat;
            std::printf("  CELL probe around (%d,%d) on %dx%d GAT:\n", qx, qy, g.width(), g.height());
            for (int dy = 1; dy >= -1; --dy) {
                std::printf("   ");
                for (int dx = -1; dx <= 1; ++dx) {
                    const int x = qx + dx, y = qy + dy;
                    if (x < 0 || y < 0 || x >= static_cast<int>(g.width()) ||
                        y >= static_cast<int>(g.height())) { std::printf("  [---] "); continue; }
                    const uaro::GatCell& c = g.at(static_cast<uaro::u32>(x), static_cast<uaro::u32>(y));
                    const float h = (c.h[0] + c.h[1] + c.h[2] + c.h[3]) * 0.25f;
                    std::printf(" %s(t%d h%.1f)", (dx == 0 && dy == 0) ? "*" : " ", c.type, h);
                }
                std::printf("\n");
            }
        }
    }
    // UVRANGE: scan every GND surface's texcoords. The texture-seam fix (CLAMP wrap) is only
    // safe if no tile tiles past [0,1]; this reports the u/v extent, how many tile UVs fall
    // outside [0,1] (would break under CLAMP), and how many sit on the texture border (u/v ~0
    // or ~1, where REPEAT-wrap bilinear bleeds the opposite edge -> the seam S. reported).
    if (std::getenv("UVRANGE")) {
        float umin = 1e9f, umax = -1e9f, vmin = 1e9f, vmax = -1e9f;
        int over = 0, border = 0, total = 0;
        for (const auto& s : m->gnd.surfaces()) {
            if (s.textureId < 0) continue;
            ++total;
            bool onBorder = false, over01 = false;
            for (int i = 0; i < 4; ++i) {
                if (s.u[i] < umin) umin = s.u[i];
                if (s.u[i] > umax) umax = s.u[i];
                if (s.v[i] < vmin) vmin = s.v[i];
                if (s.v[i] > vmax) vmax = s.v[i];
                if (s.u[i] < -0.001f || s.u[i] > 1.001f || s.v[i] < -0.001f || s.v[i] > 1.001f)
                    over01 = true;
                if (s.u[i] < 0.01f || s.u[i] > 0.99f || s.v[i] < 0.01f || s.v[i] > 0.99f)
                    onBorder = true;
            }
            if (over01) ++over;
            if (onBorder) ++border;
        }
        std::printf("  UVRANGE: %d surfaces; u[%.3f..%.3f] v[%.3f..%.3f]; %d tiles outside [0,1]; "
                    "%d touch the texture border\n",
                    total, umin, umax, vmin, vmax, over, border);
    }
    // DUMP_LM=1: write the assembled lightmap atlas (shadow alpha as grey, coloured light as
    // rgb) to /tmp PPMs so the baked shadow can be eyeballed for #58 (blotchy floor).
    if (std::getenv("DUMP_LM")) {
        int lw = 0, lh = 0;
        const std::vector<uaro::u8> lm = uaro::GroundMesh::buildLightmap(m->gnd, lw, lh);
        if (!lm.empty() && lw > 0 && lh > 0) {
            auto writePpm = [&](const char* path, bool shadow) {
                FILE* f = std::fopen(path, "wb");
                if (!f) return;
                std::fprintf(f, "P6\n%d %d\n255\n", lw, lh);
                for (int i = 0; i < lw * lh; ++i) {
                    uaro::u8 px[3];
                    if (shadow) { px[0] = px[1] = px[2] = lm[i * 4 + 3]; }
                    else { px[0] = lm[i * 4]; px[1] = lm[i * 4 + 1]; px[2] = lm[i * 4 + 2]; }
                    std::fwrite(px, 1, 3, f);
                }
                std::fclose(f);
            };
            writePpm("/tmp/lm_shadow.ppm", true);
            writePpm("/tmp/lm_color.ppm", false);
            std::printf("  DUMP_LM: wrote /tmp/lm_shadow.ppm + /tmp/lm_color.ppm (%dx%d)\n", lw, lh);
        }
    }
    return 0;
}
