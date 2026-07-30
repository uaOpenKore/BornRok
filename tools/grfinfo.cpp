// grfinfo — inspect / extract a GRF using the client's own resource layer.
//
//   grfinfo <archive.grf>                         summary (version, counts, encryption)
//   grfinfo <archive.grf> list [N]                list first N entries (default 30)
//   grfinfo <archive.grf> find <substr> [N]       list entries whose path contains substr
//   grfinfo <archive.grf> extract <vpath> <out>   extract one file to disk
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "resource/Grf.hpp"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: grfinfo <grf> [list N | find substr N | extract vpath out]\n");
        return 1;
    }
    uaro::GrfArchive grf;
    if (!grf.open(argv[1])) {
        std::printf("ERROR: cannot open %s\n", argv[1]);
        return 2;
    }

    std::size_t total = 0, encrypted = 0;
    for (const auto& [k, e] : grf.entries()) {
        (void)k;
        ++total;
        if (e.isEncrypted()) ++encrypted;
    }
    std::printf("archive : %s\n", argv[1]);
    std::printf("version : 0x%x\n", grf.version());
    std::printf("entries : %zu  (encrypted: %zu, plain: %zu)\n", total, encrypted, total - encrypted);

    const std::string cmd = argc >= 3 ? argv[2] : "";

    if (cmd == "list") {
        int n = argc >= 4 ? std::atoi(argv[3]) : 30;
        int i = 0;
        for (const auto& [k, e] : grf.entries()) {
            (void)k;
            if (i++ >= n) break;
            std::printf("  %-44s c=%u a=%u u=%u flags=0x%02x off=%llu\n", e.name.c_str(),
                        e.compressed, e.compressedAligned, e.uncompressed, e.flags,
                        static_cast<unsigned long long>(e.offset));
        }
    } else if (cmd == "find" && argc >= 4) {
        const std::string needle = uaro::GrfArchive::normalize(argv[3]);
        int n = argc >= 5 ? std::atoi(argv[4]) : 50;
        int i = 0;
        for (const auto& [k, e] : grf.entries()) {
            if (k.find(needle) == std::string::npos) continue;
            if (i++ >= n) break;
            std::printf("  %-44s c=%u a=%u u=%u flags=0x%02x off=%llu\n", e.name.c_str(),
                        e.compressed, e.compressedAligned, e.uncompressed, e.flags,
                        static_cast<unsigned long long>(e.offset));
        }
        std::printf("(%d shown)\n", i);
    } else if (cmd == "head" && argc >= 4) {
        const std::string needle = uaro::GrfArchive::normalize(argv[3]);
        int nb = argc >= 5 ? std::atoi(argv[4]) : 32;
        for (const auto& [k, e] : grf.entries()) {
            (void)e;
            if (k.find(needle) == std::string::npos) continue;
            auto d = grf.read(k);
            std::printf("entry: %s (%zu bytes)\n", k.c_str(), d ? d->size() : 0);
            if (d) {
                for (int i = 0; i < nb && i < static_cast<int>(d->size()); ++i)
                    std::printf("%02x ", (*d)[i]);
                std::printf("\n");
            }
            break;
        }
    } else if (cmd == "extract" && argc >= 5) {
        auto data = grf.read(argv[3]);
        if (!data) {
            std::printf("ERROR: read failed (missing / encrypted / corrupt): %s\n", argv[3]);
            return 3;
        }
        FILE* f = std::fopen(argv[4], "wb");
        if (!f) {
            std::printf("ERROR: cannot write %s\n", argv[4]);
            return 4;
        }
        std::fwrite(data->data(), 1, data->size(), f);
        std::fclose(f);
        std::printf("wrote %zu bytes to %s\n", data->size(), argv[4]);
    }
    return 0;
}
