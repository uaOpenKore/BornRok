// strdump — list the LAYER TEXTURES of a .str effect, so a content maker knows exactly which
// .bmp/.tga files to replace with a higher-res same-named .png (the client already prefers a
// sibling .png over the .bmp for .str layers, so the effect turns HD with no .str editing).
//
//   strdump <effect.str>                 parse a .str file on disk
//   strdump <archive.grf> <str-vpath>    parse a .str straight out of a GRF
//
// A .str is NOT images: it is keyframe animation (STRM, fps/maxKey/layers) that references bare
// texture filenames. The pictures live in  data/texture/effect/<effect>/  as .bmp/.tga. Replace
// those with same-named .png (any resolution — the renderer samples UV 0..1) to get an HD effect.
#include <cstdio>
#include <string>
#include <vector>

#include "formats/Str.hpp"
#include "resource/Grf.hpp"

static void dump(const std::vector<uaro::u8>& bytes, const char* label) {
    auto str = uaro::Str::parse(bytes);
    if (!str) {
        std::printf("ERROR: %s is not a valid .str (need STRM version 0x94)\n", label);
        return;
    }
    std::printf("%s\n  fps=%u  frames(maxKey)=%u  layers=%zu\n", label, str->fps(), str->maxKey(),
                str->layers().size());
    std::vector<std::string> uniq;
    for (std::size_t li = 0; li < str->layers().size(); ++li) {
        const auto& L = str->layers()[li];
        std::printf("  layer %2zu: %zu texture(s), %zu keyframe(s)\n", li, L.textures.size(),
                    L.keys.size());
        for (const auto& t : L.textures) {
            std::printf("      %s\n", t.c_str());
            bool seen = false;
            for (const auto& u : uniq)
                if (u == t) { seen = true; break; }
            if (!seen) uniq.push_back(t);
        }
    }
    std::printf("  -> %zu unique texture file(s) to replace with same-named .png under "
                "data/texture/effect/<effect>/:\n", uniq.size());
    for (const auto& u : uniq) std::printf("       %s\n", u.c_str());
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: strdump <effect.str>  |  strdump <archive.grf> <str-vpath>\n");
        return 1;
    }
    const std::string a1 = argv[1];
    // Two-arg form: pull the .str out of a GRF.
    if (argc >= 3) {
        uaro::GrfArchive grf;
        if (!grf.open(a1)) {
            std::printf("ERROR: cannot open GRF %s\n", a1.c_str());
            return 2;
        }
        auto bytes = grf.read(argv[2]);
        if (!bytes) {
            std::printf("ERROR: %s not found in %s\n", argv[2], a1.c_str());
            return 3;
        }
        dump(*bytes, argv[2]);
        return 0;
    }
    // One-arg form: a .str file on disk.
    std::FILE* f = std::fopen(a1.c_str(), "rb");
    if (!f) {
        std::printf("ERROR: cannot open file %s\n", a1.c_str());
        return 2;
    }
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uaro::u8> bytes(n > 0 ? static_cast<std::size_t>(n) : 0);
    if (n > 0 && std::fread(bytes.data(), 1, bytes.size(), f) != bytes.size()) {
        std::fclose(f);
        std::printf("ERROR: short read on %s\n", a1.c_str());
        return 2;
    }
    std::fclose(f);
    dump(bytes, a1.c_str());
    return 0;
}
