// grflub — verify the data-from-GRF lub layer (GrfData) against a real GRF, end to end.
//
// Runs the client's compiled .lub scripts through the SAME resource path the game uses
// (GrfArchive.read -> GrfData), then dumps the resulting lookups so the lub layer can be
// regression-checked offline without the full bgfx/SDL client build.
//
//   grflub <archive.grf>                 summary: what loaded + a few sample lookups
//   grflub <archive.grf> status [id...]  status-effect (EFST) tooltip text for the given ids
//   grflub <archive.grf> mob [id...]     mob/NPC sprite folder name for the given class ids
#include <cstdio>
#include <cstdlib>
#include <string>

#include "resource/GrfData.hpp"
#include "resource/Grf.hpp"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: grflub <grf> [status id... | mob id...]\n");
        return 1;
    }
    uaro::GrfArchive grf;
    if (!grf.open(argv[1])) {
        std::printf("ERROR: cannot open %s\n", argv[1]);
        return 2;
    }
    // GrfData reads .lub bytes through the real archive read path (decompress + normalize).
    uaro::GrfData data;
    const bool ok = data.load([&](const std::string& vpath) { return grf.read(vpath); });
    std::printf("archive : %s\n", argv[1]);
    std::printf("grfdata : %s\n", ok ? "loaded" : "FAILED (no core lub tables)");

    const std::string cmd = argc >= 3 ? argv[2] : "";

    auto printStatus = [&](uaro::u32 id) {
        const uaro::GrfData::StatusDesc* d = data.statusDesc(id);
        if (!d) { std::printf("  EFST %-4u <none>\n", id); return; }
        std::printf("  EFST %-4u %s\n", id, d->title.c_str());
        if (!d->body.empty()) {
            // print the body indented, one line per '\n'
            std::string b = d->body;
            for (std::size_t p = 0; p < b.size();) {
                std::size_t nl = b.find('\n', p);
                if (nl == std::string::npos) nl = b.size();
                std::printf("           | %s\n", b.substr(p, nl - p).c_str());
                p = nl + 1;
            }
        }
    };
    auto printMob = [&](uaro::u32 id) {
        const std::string& n = data.npcSpriteName(id);
        std::printf("  class %-6u %s\n", id, n.empty() ? "<none>" : n.c_str());
    };

    if (cmd == "status") {
        if (argc >= 4)
            for (int i = 3; i < argc; ++i) printStatus(static_cast<uaro::u32>(std::atoi(argv[i])));
        else
            for (uaro::u32 id = 1; id <= 30; ++id) printStatus(id);  // first 30 by default
    } else if (cmd == "mob") {
        if (argc >= 4)
            for (int i = 3; i < argc; ++i) printMob(static_cast<uaro::u32>(std::atoi(argv[i])));
        else
            for (uaro::u32 id : {45u, 46u, 400u, 1002u, 1063u, 1188u, 3000u}) printMob(id);
    } else if (cmd == "skilltree") {
        auto show = [&](uaro::u16 job) {
            const std::vector<uaro::u16>* t = data.skillTree(job);
            if (!t) { std::printf("  job %-5u <no tree>\n", job); return; }
            std::printf("  job %-5u (%zu skills):", job, t->size());
            for (uaro::u16 s : *t) std::printf(" %u", s);
            std::printf("\n");
        };
        if (argc >= 4)
            for (int i = 3; i < argc; ++i) show(static_cast<uaro::u16>(std::atoi(argv[i])));
        else
            for (uaro::u16 j : {0u, 1u, 7u, 9u, 8u, 15u}) show(j);  // novice/swordman/knight/wizard/priest/monk
    } else {
        // summary: a handful of representative lookups from each table
        std::printf("--- status (EFST) samples ---\n");
        for (uaro::u32 id : {1u, 2u, 3u, 5u, 10u, 21u}) printStatus(id);
        std::printf("--- mob/NPC sprite-name samples ---\n");
        for (uaro::u32 id : {45u, 400u, 1002u, 1063u, 3000u}) printMob(id);
    }
    return 0;
}
