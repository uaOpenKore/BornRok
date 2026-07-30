// sprdump — unpack a .spr's frames to PNG for HD upscaling. A .spr holds the actual images (indexed
// palette frames and/or truecolor frames); this decodes each to RGBA8 and writes it as a .png, so a
// content maker can upscale them and drop them back as a #109 PNG-sprite override.
//
//   sprdump <sprite.spr> <outdir>                 unpack a .spr file on disk
//   sprdump <archive.grf> <spr-vpath> <outdir>    unpack a .spr straight out of a GRF
//
// Output: indexed frame i -> <outdir>/<i>.png  (matches the client's <name>.png.d/<i>.png override,
// so after upscaling you copy them into data/sprite/<name>.png.d/). Truecolor frames -> t<i>.png.
#include <cstdio>
#include <string>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "third_party/stb_image_write.h"

#include "formats/Spr.hpp"
#include "resource/Grf.hpp"

static bool writePng(const std::string& path, int w, int h, const std::vector<uaro::u8>& rgba) {
    if (w <= 0 || h <= 0 || rgba.size() < static_cast<std::size_t>(w) * h * 4) return false;
    return stbi_write_png(path.c_str(), w, h, 4, rgba.data(), w * 4) != 0;
}

static int dump(const std::vector<uaro::u8>& bytes, const std::string& outdir, const char* label) {
    auto spr = uaro::Sprite::parse(bytes);
    if (!spr) {
        std::printf("ERROR: %s is not a valid .spr\n", label);
        return 3;
    }
    std::size_t wrote = 0;
    const auto& idx = spr->indexedFrames();
    for (std::size_t i = 0; i < idx.size(); ++i) {
        const auto rgba = spr->indexedToRgba(i);  // palette-expanded (index 0 = transparent)
        const std::string p = outdir + "/" + std::to_string(i) + ".png";
        if (writePng(p, idx[i].width, idx[i].height, rgba)) { ++wrote; }
        else std::printf("  WARN: could not write %s\n", p.c_str());
    }
    const auto& tc = spr->rgbaFrames();
    for (std::size_t i = 0; i < tc.size(); ++i) {
        const std::string p = outdir + "/t" + std::to_string(i) + ".png";
        if (writePng(p, tc[i].width, tc[i].height, tc[i].pixels)) { ++wrote; }
        else std::printf("  WARN: could not write %s\n", p.c_str());
    }
    std::printf("%s: %zu indexed + %zu truecolor frame(s) -> %zu PNG(s) in %s/\n", label, idx.size(),
                tc.size(), wrote, outdir.c_str());
    std::printf("  upscale them, then drop into data/sprite/<name>.png.d/ (indexed frames keep <i>.png)\n");
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: sprdump <sprite.spr> <outdir>  |  sprdump <archive.grf> <spr-vpath> <outdir>\n");
        return 1;
    }
    if (argc >= 4) {  // GRF form
        uaro::GrfArchive grf;
        if (!grf.open(argv[1])) { std::printf("ERROR: cannot open GRF %s\n", argv[1]); return 2; }
        auto bytes = grf.read(argv[2]);
        if (!bytes) { std::printf("ERROR: %s not found in %s\n", argv[2], argv[1]); return 3; }
        return dump(*bytes, argv[3], argv[2]);
    }
    // File form
    std::FILE* f = std::fopen(argv[1], "rb");
    if (!f) { std::printf("ERROR: cannot open file %s\n", argv[1]); return 2; }
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uaro::u8> bytes(n > 0 ? static_cast<std::size_t>(n) : 0);
    if (n > 0 && std::fread(bytes.data(), 1, bytes.size(), f) != bytes.size()) {
        std::fclose(f);
        std::printf("ERROR: short read on %s\n", argv[1]);
        return 2;
    }
    std::fclose(f);
    return dump(bytes, argv[2], argv[1]);
}
