// formatcheck — validate the SPR/ACT parsers against every such file in a real
// GRF. Far stronger than synthetic tests: parses thousands of real assets.
//
//   formatcheck <archive.grf>
#include <cstdio>
#include <cstring>
#include <map>
#include <optional>
#include <string>

#include "core/Log.hpp"
#include "formats/Act.hpp"
#include "formats/Bmp.hpp"
#include "formats/Gat.hpp"
#include "formats/Gnd.hpp"
#include "formats/Rsm.hpp"
#include "formats/Rsw.hpp"
#include "formats/Spr.hpp"
#include "resource/Grf.hpp"
#include "world/GroundMesh.hpp"
#include "world/ModelMesh.hpp"

static bool ends_with(const std::string& s, const char* suf) {
    std::size_t n = std::strlen(suf);
    return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: formatcheck <grf>\n");
        return 1;
    }
    uaro::log::set_level(uaro::log::Level::Off);  // silence per-file noise

    uaro::GrfArchive grf;
    if (!grf.open(argv[1])) {
        std::printf("ERROR: cannot open %s\n", argv[1]);
        return 2;
    }

    std::size_t sprOk = 0, sprFail = 0, actOk = 0, actFail = 0, gatOk = 0, gatFail = 0,
                gndOk = 0, gndFail = 0, rswOk = 0, rswFail = 0, rswComplete = 0,
                rsmOk = 0, rsmFail = 0;
    std::string firstSprFail, firstActFail, firstGatFail, firstGndFail, firstRswFail,
        firstRsmFail;
    std::size_t bmpOk = 0, bmpFail = 0;
    std::string firstBmpFail;
    std::map<int, int> rsmVer;
    unsigned long long groundVerts = 0;  // total ground-mesh vertices built from GNDs
    unsigned long long modelIndices = 0;  // total model-mesh indices built from RSMs
    bool printedRsw = false, printedRsm = false, printedBmp = false, printedMesh = false;
    std::map<int, int> sprVer, actVer;  // version histogram

    for (const auto& [k, e] : grf.entries()) {
        (void)e;
        if (ends_with(k, ".spr")) {
            auto d = grf.read(k);
            if (d) {
                if (auto s = uaro::Sprite::parse(*d)) {
                    ++sprOk;
                    ++sprVer[s->version()];
                } else {
                    ++sprFail;
                    if (firstSprFail.empty()) firstSprFail = k;
                }
            } else {
                ++sprFail;
                if (firstSprFail.empty()) firstSprFail = k + " (read failed)";
            }
        } else if (ends_with(k, ".act")) {
            auto d = grf.read(k);
            if (d) {
                if (auto a = uaro::Action::parse(*d)) {
                    ++actOk;
                    ++actVer[a->version()];
                } else {
                    ++actFail;
                    if (firstActFail.empty()) firstActFail = k;
                }
            } else {
                ++actFail;
                if (firstActFail.empty()) firstActFail = k + " (read failed)";
            }
        } else if (ends_with(k, ".gat")) {
            auto d = grf.read(k);
            if (d && uaro::Gat::parse(*d)) {
                ++gatOk;
            } else {
                ++gatFail;
                if (firstGatFail.empty()) firstGatFail = k;
            }
        } else if (ends_with(k, ".gnd")) {
            auto d = grf.read(k);
            std::optional<uaro::Gnd> g;
            if (d) g = uaro::Gnd::parse(*d);
            if (g) {
                ++gndOk;
                uaro::GroundMesh mesh = uaro::GroundMesh::build(*g);  // also exercise mesh build
                groundVerts += mesh.vertices.size();
                if (!printedMesh) {
                    printedMesh = true;
                    std::printf("  [mesh %s] %ux%u -> %zu verts, %zu indices, %zu tex-batches\n",
                                k.c_str(), g->width(), g->height(), mesh.vertices.size(),
                                mesh.indices.size(), mesh.batches.size());
                }
            } else {
                ++gndFail;
                if (firstGndFail.empty()) firstGndFail = k;
            }
        } else if (ends_with(k, ".rsw")) {
            auto d = grf.read(k);
            std::optional<uaro::Rsw> w;
            if (d) w = uaro::Rsw::parse(*d);
            if (w) {
                ++rswOk;
                if (w->complete()) ++rswComplete;
                if (!printedRsw) {  // eyeball the first parse
                    printedRsw = true;
                    std::printf("  [sample %s] gnd='%s' objects=%d models=%zu complete=%d trailing=%zu\n",
                                k.c_str(), w->gndFile().c_str(), w->objectCount(),
                                w->models().size(), static_cast<int>(w->complete()), w->trailing());
                }
            } else {
                ++rswFail;
                if (firstRswFail.empty()) firstRswFail = k;
            }
        } else if (ends_with(k, ".rsm")) {
            auto d = grf.read(k);
            std::optional<uaro::Rsm> m;
            if (d) m = uaro::Rsm::parse(*d);
            if (m) {
                ++rsmOk;
                ++rsmVer[m->version()];
                uaro::ModelMesh mm = uaro::ModelMesh::build(*m);  // also exercise mesh build
                modelIndices += mm.totalIndices();
                if (!printedRsm) {
                    printedRsm = true;
                    std::size_t verts = m->nodes().empty() ? 0 : m->nodes()[0].vertices.size();
                    std::size_t faces = m->nodes().empty() ? 0 : m->nodes()[0].faces.size();
                    std::printf("  [sample %s] v0x%x textures=%zu nodes=%zu node0(verts=%zu faces=%zu) mesh-tris=%zu\n",
                                k.c_str(), m->version(), m->textures().size(), m->nodes().size(),
                                verts, faces, mm.totalIndices() / 3);
                }
            } else {
                ++rsmFail;
                if (firstRsmFail.empty()) firstRsmFail = k;
            }
        } else if (ends_with(k, ".bmp")) {
            auto d = grf.read(k);
            std::optional<uaro::Image> img;
            if (d) img = uaro::Bmp::decode(*d);
            if (img && img->valid()) {
                ++bmpOk;
                if (!printedBmp) {
                    printedBmp = true;
                    std::printf("  [sample %s] %ux%u rgba\n", k.c_str(), img->width, img->height);
                }
            } else {
                ++bmpFail;
                if (firstBmpFail.empty()) firstBmpFail = k;
            }
        }
    }

    std::printf("SPR: ok=%zu fail=%zu\n", sprOk, sprFail);
    for (const auto& [v, n] : sprVer) std::printf("     v0x%03x: %d\n", v, n);
    if (sprFail) std::printf("     first fail: %s\n", firstSprFail.c_str());

    std::printf("ACT: ok=%zu fail=%zu\n", actOk, actFail);
    for (const auto& [v, n] : actVer) std::printf("     v0x%03x: %d\n", v, n);
    if (actFail) std::printf("     first fail: %s\n", firstActFail.c_str());

    std::printf("GAT: ok=%zu fail=%zu\n", gatOk, gatFail);
    if (gatFail) std::printf("     first fail: %s\n", firstGatFail.c_str());

    std::printf("GND: ok=%zu fail=%zu  (ground mesh: %llu verts total)\n", gndOk, gndFail, groundVerts);
    if (gndFail) std::printf("     first fail: %s\n", firstGndFail.c_str());

    std::printf("RSW: ok=%zu fail=%zu  (all-model maps: %zu)\n", rswOk, rswFail, rswComplete);
    if (rswFail) std::printf("     first fail: %s\n", firstRswFail.c_str());

    std::printf("RSM: ok=%zu fail=%zu  (model mesh: %llu tris total)\n", rsmOk, rsmFail,
                modelIndices / 3);
    for (const auto& [v, n] : rsmVer) std::printf("     v0x%03x: %d\n", v, n);
    if (rsmFail) std::printf("     first fail: %s\n", firstRsmFail.c_str());

    std::printf("BMP: ok=%zu fail=%zu\n", bmpOk, bmpFail);
    if (bmpFail) std::printf("     first fail: %s\n", firstBmpFail.c_str());

    return (sprFail || actFail || gatFail || gndFail || rswFail || rsmFail || bmpFail) ? 10 : 0;
}
