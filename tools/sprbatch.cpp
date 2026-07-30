// sprbatch — batch-unpack every .spr under an input folder into PNG frames, mirroring the folder
// structure and file names, ready for HD upscaling + the client's #109 override.
//
//   sprbatch <input_dir> <output_dir>
//
// For each  <input_dir>/<rel>/<name>.spr  it writes the palette-expanded frames (index 0 -> alpha 0,
// so backgrounds are already transparent) to  <output_dir>/<rel>/<name>.png.d/<i>.png . That is
// exactly the layout the client reads as a hi-res sprite override: after you upscale the PNGs in
// place (keeping their names), drop <output_dir>'s tree into data/sprite/ (loose / UaRO.zip / GRF).
// Reads LOOSE .spr files (already extracted from the GRF) — no archive/zlib needed.
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "third_party/stb_image_write.h"

#include "formats/Spr.hpp"

namespace fs = std::filesystem;

static bool writePng(const fs::path& path, int w, int h, const std::vector<uaro::u8>& rgba) {
    if (w <= 0 || h <= 0 || rgba.size() < static_cast<std::size_t>(w) * h * 4) return false;
    return stbi_write_png(path.string().c_str(), w, h, 4, rgba.data(), w * 4) != 0;
}

static bool readFile(const fs::path& p, std::vector<uaro::u8>& out) {
    std::FILE* f = std::fopen(p.string().c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? static_cast<std::size_t>(n) : 0);
    const bool ok = n <= 0 || std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

static bool isSpr(const fs::path& p) {
    std::string e = p.extension().string();
    for (char& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return e == ".spr";
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: sprbatch <input_dir> <output_dir>\n"
                    "  unpacks every .spr under input_dir to <output_dir>/<rel>/<name>.png.d/<i>.png,\n"
                    "  mirroring the folder tree and file names (for HD upscaling + #109 override).\n");
        return 1;
    }
    const fs::path inDir = argv[1], outDir = argv[2];
    std::error_code ec;
    if (!fs::is_directory(inDir, ec)) {
        std::printf("ERROR: input '%s' is not a folder\n", inDir.string().c_str());
        return 2;
    }
    std::size_t sprs = 0, frames = 0, failed = 0;
    for (auto it = fs::recursive_directory_iterator(inDir, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        const fs::path& p = it->path();
        if (!it->is_regular_file(ec) || !isSpr(p)) continue;
        std::vector<uaro::u8> bytes;
        if (!readFile(p, bytes)) { std::printf("  SKIP (read failed): %s\n", p.string().c_str()); ++failed; continue; }
        auto spr = uaro::Sprite::parse(bytes);
        if (!spr) { std::printf("  SKIP (not a .spr): %s\n", p.string().c_str()); ++failed; continue; }
        // Mirror <rel>/<name>.spr -> <out>/<rel>/<name>.png.d/
        const fs::path rel = fs::relative(p, inDir, ec);
        fs::path dstDir = outDir / rel;
        dstDir.replace_extension();               // drop ".spr"
        dstDir += ".png.d";                        // <name>.png.d (client override folder)
        fs::create_directories(dstDir, ec);
        std::size_t wrote = 0;
        const auto& idx = spr->indexedFrames();
        for (std::size_t i = 0; i < idx.size(); ++i)
            if (writePng(dstDir / (std::to_string(i) + ".png"), idx[i].width, idx[i].height,
                         spr->indexedToRgba(i))) ++wrote;
        const auto& tc = spr->rgbaFrames();
        for (std::size_t i = 0; i < tc.size(); ++i)
            if (writePng(dstDir / ("t" + std::to_string(i) + ".png"), tc[i].width, tc[i].height,
                         tc[i].pixels)) ++wrote;
        ++sprs;
        frames += wrote;
        std::printf("  %s -> %s  (%zu frames)\n", rel.string().c_str(), dstDir.filename().string().c_str(), wrote);
    }
    std::printf("done: %zu sprite(s), %zu PNG frame(s), %zu skipped -> %s\n", sprs, frames, failed,
                outDir.string().c_str());
    return 0;
}
