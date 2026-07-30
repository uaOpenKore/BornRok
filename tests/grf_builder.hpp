#pragma once
// Test helper: assemble a valid GRF v0x200 archive in memory (zlib-deflated,
// unencrypted) so the GRF reader and VFS can be verified offline.
#include <zlib.h>

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "resource/GrfDecrypt.hpp"

namespace testgrf {

using u8 = std::uint8_t;
using u32 = std::uint32_t;

inline void put_u32(std::vector<u8>& v, u32 x) {
    v.push_back(static_cast<u8>(x & 0xff));
    v.push_back(static_cast<u8>((x >> 8) & 0xff));
    v.push_back(static_cast<u8>((x >> 16) & 0xff));
    v.push_back(static_cast<u8>((x >> 24) & 0xff));
}

inline void put_bytes(std::vector<u8>& v, const void* p, std::size_t n) {
    const u8* b = static_cast<const u8*>(p);
    v.insert(v.end(), b, b + n);
}

inline void put_u64(std::vector<u8>& v, std::uint64_t x) {
    put_u32(v, static_cast<u32>(x & 0xffffffffu));
    put_u32(v, static_cast<u32>((x >> 32) & 0xffffffffu));
}

inline std::vector<u8> zdeflate(const std::string& s) {
    uLong bound = compressBound(static_cast<uLong>(s.size()));
    std::vector<u8> out(bound ? bound : 1);
    uLongf destLen = static_cast<uLongf>(out.size());
    compress(out.data(), &destLen, reinterpret_cast<const Bytef*>(s.data()),
             static_cast<uLong>(s.size()));
    out.resize(destLen);
    return out;
}

// files: (raw name with backslashes, content)
inline std::vector<u8> build_grf(const std::vector<std::pair<std::string, std::string>>& files) {
    std::vector<std::vector<u8>> blobs;
    std::vector<u32> offsets, comp, uncomp;
    u32 running = 0;
    for (const auto& f : files) {
        auto blob = zdeflate(f.second);
        offsets.push_back(running);
        comp.push_back(static_cast<u32>(blob.size()));
        uncomp.push_back(static_cast<u32>(f.second.size()));
        running += static_cast<u32>(blob.size());
        blobs.push_back(std::move(blob));
    }
    const u32 dataLen = running;

    // Uncompressed file table.
    std::vector<u8> table;
    for (std::size_t i = 0; i < files.size(); ++i) {
        put_bytes(table, files[i].first.data(), files[i].first.size());
        table.push_back(0);             // NUL terminator
        put_u32(table, comp[i]);        // compressed
        put_u32(table, comp[i]);        // compressed_aligned (== compressed; no DES)
        put_u32(table, uncomp[i]);      // uncompressed
        table.push_back(0x01);          // flags: file
        put_u32(table, offsets[i]);     // offset relative to byte 46
    }
    std::string tableStr(reinterpret_cast<const char*>(table.data()), table.size());
    auto ctable = zdeflate(tableStr);

    // Assemble: header | data blobs | [compLen][uncompLen][compressed table].
    std::vector<u8> grf;
    const char sig[16] = {'M', 'a', 's', 't', 'e', 'r', ' ', 'o',
                          'f', ' ', 'M', 'a', 'g', 'i', 'c', '\0'};
    put_bytes(grf, sig, 16);
    for (int i = 0; i < 14; ++i) grf.push_back(0);        // key
    put_u32(grf, dataLen);                                // file_table_offset (rel 46)
    put_u32(grf, 0);                                      // m1 (seed)
    put_u32(grf, static_cast<u32>(files.size()) + 7);     // m2 -> count = m2 - m1 - 7
    put_u32(grf, 0x200);                                  // version
    for (const auto& b : blobs) put_bytes(grf, b.data(), b.size());
    put_u32(grf, static_cast<u32>(ctable.size()));
    put_u32(grf, static_cast<u32>(table.size()));
    put_bytes(grf, ctable.data(), ctable.size());
    return grf;
}

// GRF v0x300 (GRF Editor int64 large-GRF extension): 64-bit table offset in the header
// (bytes 30-37, absorbing the old seed slot), raw file count at byte 38, and per-entry int64
// data offsets (21-byte fixed part) preceded by a 4-byte pad before the table size fields.
// Built small so the reader's v300 path is exercised without an actual >4 GiB file.
inline std::vector<u8> build_grf_v300(
    const std::vector<std::pair<std::string, std::string>>& files) {
    std::vector<std::vector<u8>> blobs;
    std::vector<u32> offsets, comp, uncomp;
    u32 running = 0;
    for (const auto& f : files) {
        auto blob = zdeflate(f.second);
        offsets.push_back(running);
        comp.push_back(static_cast<u32>(blob.size()));
        uncomp.push_back(static_cast<u32>(f.second.size()));
        running += static_cast<u32>(blob.size());
        blobs.push_back(std::move(blob));
    }
    const u32 dataLen = running;

    // Uncompressed file table: name\0 + i32 comp + i32 compAligned + i32 uncomp + u8 flags + i64 offset.
    std::vector<u8> table;
    for (std::size_t i = 0; i < files.size(); ++i) {
        put_bytes(table, files[i].first.data(), files[i].first.size());
        table.push_back(0);
        put_u32(table, comp[i]);
        put_u32(table, comp[i]);
        put_u32(table, uncomp[i]);
        table.push_back(0x01);          // flags: file
        put_u64(table, offsets[i]);     // int64 offset (relative to byte 46)
    }
    std::string tableStr(reinterpret_cast<const char*>(table.data()), table.size());
    auto ctable = zdeflate(tableStr);

    std::vector<u8> grf;
    const char sig[16] = {'M', 'a', 's', 't', 'e', 'r', ' ', 'o',
                          'f', ' ', 'M', 'a', 'g', 'i', 'c', '\0'};
    put_bytes(grf, sig, 16);
    for (int i = 0; i < 14; ++i) grf.push_back(0);       // key
    put_u64(grf, dataLen);                               // int64 table offset (bytes 30-37)
    put_u32(grf, static_cast<u32>(files.size()));        // raw file count (byte 38)
    put_u32(grf, 0x300);                                 // version
    for (const auto& b : blobs) put_bytes(grf, b.data(), b.size());
    put_u32(grf, 0);                                     // v300 4-byte pad before size fields
    put_u32(grf, static_cast<u32>(ctable.size()));
    put_u32(grf, static_cast<u32>(table.size()));
    put_bytes(grf, ctable.data(), ctable.size());
    return grf;
}

// Legacy GRF v0x1xx (#106): uncompressed table at EOF, DES-obfuscated names, magic-masked size
// fields, whole-archive data encryption (header-only DES for .gnd/.gat/.act/.str, mixed DES +
// shuffle otherwise). Uses the reader's own encode inverses (uaro::grf_v1_*), so the test proves
// decode(encode(x)) == x against the eAthena layout.
inline std::vector<u8> build_grf_v1(
    const std::vector<std::pair<std::string, std::string>>& files) {
    std::vector<std::vector<u8>> blobs;
    std::vector<u32> offsets, comp, uncomp;
    std::vector<int> digits;
    u32 running = 0;
    for (const auto& f : files) {
        auto blob = zdeflate(f.second);
        const u32 c = static_cast<u32>(blob.size());
        while (blob.size() % 8) blob.push_back(0);  // DES 8-byte alignment
        // Cycle-digit rule mirrors the reader: 0 for .gnd/.gat/.act/.str, else digit count.
        int d = 0;
        const auto dot = f.first.rfind('.');
        std::string ext = dot == std::string::npos ? "" : f.first.substr(dot);
        for (char& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (ext != ".gnd" && ext != ".gat" && ext != ".act" && ext != ".str") {
            d = 1;
            for (u32 v = c; v >= 10; v /= 10) ++d;
        }
        uaro::grf_v1_data_encode(blob.data(), static_cast<u32>(blob.size()), d);
        offsets.push_back(running);
        comp.push_back(c);
        uncomp.push_back(static_cast<u32>(f.second.size()));
        digits.push_back(d);
        running += static_cast<u32>(blob.size());
        blobs.push_back(std::move(blob));
    }
    const u32 dataLen = running;

    // Table: per entry u32 nameBlockLen(=6+encNameLen) | 2 pad | encoded name | 4 pad |
    // u32 comp+uncomp+715 | u32 aligned+37579 | u32 uncomp | u8 type(1) | u32 offset.
    std::vector<u8> table;
    for (std::size_t i = 0; i < files.size(); ++i) {
        std::vector<u8> nm(files[i].first.begin(), files[i].first.end());
        nm.push_back(0);
        while (nm.size() % 8) nm.push_back(0);
        uaro::grf_v1_filename_encode(nm.data(), nm.size());
        put_u32(table, static_cast<u32>(nm.size()) + 6);
        table.push_back(0);
        table.push_back(0);
        put_bytes(table, nm.data(), nm.size());
        put_u32(table, 0);  // 4 unused bytes before the field block
        put_u32(table, comp[i] + uncomp[i] + 715);
        put_u32(table, static_cast<u32>(blobs[i].size()) + 37579);
        put_u32(table, uncomp[i]);
        table.push_back(0x01);  // type: compressed file
        put_u32(table, offsets[i]);
    }

    std::vector<u8> grf;
    const char sig[16] = {'M', 'a', 's', 't', 'e', 'r', ' ', 'o',
                          'f', ' ', 'M', 'a', 'g', 'i', 'c', '\0'};
    put_bytes(grf, sig, 16);
    for (int i = 0; i < 14; ++i) grf.push_back(0);     // key
    put_u32(grf, dataLen);                             // table offset (rel 46)
    put_u32(grf, 0);                                   // m1
    put_u32(grf, static_cast<u32>(files.size()) + 7);  // m2 -> count = m2 - m1 - 7
    put_u32(grf, 0x102);                               // version
    for (const auto& b : blobs) put_bytes(grf, b.data(), b.size());
    put_bytes(grf, table.data(), table.size());        // raw (uncompressed) table to EOF
    return grf;
}

inline void write_file(const std::string& path, const std::vector<u8>& bytes) {
    std::ofstream o(path, std::ios::binary);
    o.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
}

inline std::string write_temp(const std::vector<u8>& bytes, const std::string& name) {
    auto p = std::filesystem::temp_directory_path() / name;
    write_file(p.string(), bytes);
    return p.string();
}

} // namespace testgrf
