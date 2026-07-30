#pragma once
// Test helper: assemble a valid PKZIP archive in memory (stored or raw-deflate entries)
// so the ZipArchive reader and VFS zip-mount can be verified offline.
#include <zlib.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace testzip {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;

inline void put_u16(std::vector<u8>& v, u16 x) {
    v.push_back(static_cast<u8>(x & 0xff));
    v.push_back(static_cast<u8>((x >> 8) & 0xff));
}
inline void put_u32(std::vector<u8>& v, u32 x) {
    v.push_back(static_cast<u8>(x & 0xff));
    v.push_back(static_cast<u8>((x >> 8) & 0xff));
    v.push_back(static_cast<u8>((x >> 16) & 0xff));
    v.push_back(static_cast<u8>((x >> 24) & 0xff));
}
inline void put_u64(std::vector<u8>& v, std::uint64_t x) {
    put_u32(v, static_cast<u32>(x & 0xffffffffu));
    put_u32(v, static_cast<u32>((x >> 32) & 0xffffffffu));
}
inline void put_bytes(std::vector<u8>& v, const void* p, std::size_t n) {
    const u8* b = static_cast<const u8*>(p);
    v.insert(v.end(), b, b + n);
}

// Raw DEFLATE (no zlib header) — what ZIP method 8 stores.
inline std::vector<u8> raw_deflate(const std::string& s) {
    z_stream st;
    std::memset(&st, 0, sizeof(st));
    deflateInit2(&st, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
    std::vector<u8> out(deflateBound(&st, static_cast<uLong>(s.size())) + 16);
    st.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(s.data()));
    st.avail_in = static_cast<uInt>(s.size());
    st.next_out = out.data();
    st.avail_out = static_cast<uInt>(out.size());
    deflate(&st, Z_FINISH);
    out.resize(st.total_out);
    deflateEnd(&st);
    return out;
}

// files: (name with '/' separators, content, deflate?). deflate=false stores raw.
inline std::vector<u8> build_zip(
    const std::vector<std::tuple<std::string, std::string, bool>>& files) {
    std::vector<u8> zip;
    struct CD {
        std::string name;
        u16 method;
        u32 crc, comp, uncomp, localOff;
    };
    std::vector<CD> cds;

    for (const auto& [name, content, deflate] : files) {
        const u32 localOff = static_cast<u32>(zip.size());
        std::vector<u8> data;
        u16 method;
        if (deflate) {
            data = raw_deflate(content);
            method = 8;
        } else {
            data.assign(content.begin(), content.end());
            method = 0;
        }
        const u32 crc = static_cast<u32>(
            crc32(0, reinterpret_cast<const Bytef*>(content.data()),
                  static_cast<uInt>(content.size())));
        // Local file header.
        put_u32(zip, 0x04034b50);
        put_u16(zip, 20);         // version needed
        put_u16(zip, 0);          // flags
        put_u16(zip, method);
        put_u16(zip, 0);          // mod time
        put_u16(zip, 0);          // mod date
        put_u32(zip, crc);
        put_u32(zip, static_cast<u32>(data.size()));     // compressed
        put_u32(zip, static_cast<u32>(content.size()));  // uncompressed
        put_u16(zip, static_cast<u16>(name.size()));
        put_u16(zip, 0);          // extra len
        put_bytes(zip, name.data(), name.size());
        put_bytes(zip, data.data(), data.size());
        cds.push_back({name, method, crc, static_cast<u32>(data.size()),
                       static_cast<u32>(content.size()), localOff});
    }

    const u32 cdStart = static_cast<u32>(zip.size());
    for (const auto& c : cds) {
        put_u32(zip, 0x02014b50);
        put_u16(zip, 20);         // version made by
        put_u16(zip, 20);         // version needed
        put_u16(zip, 0);          // flags
        put_u16(zip, c.method);
        put_u16(zip, 0);          // mod time
        put_u16(zip, 0);          // mod date
        put_u32(zip, c.crc);
        put_u32(zip, c.comp);
        put_u32(zip, c.uncomp);
        put_u16(zip, static_cast<u16>(c.name.size()));
        put_u16(zip, 0);          // extra len
        put_u16(zip, 0);          // comment len
        put_u16(zip, 0);          // disk start
        put_u16(zip, 0);          // internal attrs
        put_u32(zip, 0);          // external attrs
        put_u32(zip, c.localOff);
        put_bytes(zip, c.name.data(), c.name.size());
    }
    const u32 cdSize = static_cast<u32>(zip.size()) - cdStart;

    // End of central directory.
    put_u32(zip, 0x06054b50);
    put_u16(zip, 0);              // this disk
    put_u16(zip, 0);              // cd start disk
    put_u16(zip, static_cast<u16>(cds.size()));  // entries this disk
    put_u16(zip, static_cast<u16>(cds.size()));  // total entries
    put_u32(zip, cdSize);
    put_u32(zip, cdStart);
    put_u16(zip, 0);              // comment len
    return zip;
}

// Same content but written with Zip64 structures forced on (sentinels in the EOCD + per-entry
// local-offset carried in a Zip64 extra field + an EOCD64 record/locator), so the reader's
// Zip64 path can be exercised without producing an actual >4 GiB file. All method 0 (stored).
inline std::vector<u8> build_zip64(const std::vector<std::pair<std::string, std::string>>& files) {
    std::vector<u8> zip;
    struct CD {
        std::string name;
        u32 crc, size;
        std::uint64_t localOff;
    };
    std::vector<CD> cds;
    for (const auto& [name, content] : files) {
        const std::uint64_t localOff = zip.size();
        const u32 crc = static_cast<u32>(
            crc32(0, reinterpret_cast<const Bytef*>(content.data()),
                  static_cast<uInt>(content.size())));
        put_u32(zip, 0x04034b50);
        put_u16(zip, 45);         // version needed (Zip64)
        put_u16(zip, 0);
        put_u16(zip, 0);          // method: stored
        put_u16(zip, 0);
        put_u16(zip, 0);
        put_u32(zip, crc);
        put_u32(zip, static_cast<u32>(content.size()));
        put_u32(zip, static_cast<u32>(content.size()));
        put_u16(zip, static_cast<u16>(name.size()));
        put_u16(zip, 0);
        put_bytes(zip, name.data(), name.size());
        put_bytes(zip, content.data(), content.size());
        cds.push_back({name, crc, static_cast<u32>(content.size()), localOff});
    }

    const std::uint64_t cdStart = zip.size();
    for (const auto& c : cds) {
        put_u32(zip, 0x02014b50);
        put_u16(zip, 45);
        put_u16(zip, 45);
        put_u16(zip, 0);
        put_u16(zip, 0);          // method: stored
        put_u16(zip, 0);
        put_u16(zip, 0);
        put_u32(zip, c.crc);
        put_u32(zip, c.size);
        put_u32(zip, c.size);
        put_u16(zip, static_cast<u16>(c.name.size()));
        put_u16(zip, 12);         // extra len: one Zip64 field (4 header + 8 offset)
        put_u16(zip, 0);          // comment len
        put_u16(zip, 0);
        put_u16(zip, 0);
        put_u32(zip, 0);
        put_u32(zip, 0xFFFFFFFFu);  // local offset sentinel -> forces Zip64 extra
        put_bytes(zip, c.name.data(), c.name.size());
        put_u16(zip, 0x0001);       // Zip64 extra header id
        put_u16(zip, 8);            // data size
        put_u64(zip, c.localOff);   // real 64-bit local header offset
    }
    const std::uint64_t cdSize = zip.size() - cdStart;

    // Zip64 EOCD record.
    const std::uint64_t eocd64Ofs = zip.size();
    put_u32(zip, 0x06064b50);
    put_u64(zip, 44);             // size of remaining record
    put_u16(zip, 45);
    put_u16(zip, 45);
    put_u32(zip, 0);              // this disk
    put_u32(zip, 0);              // cd start disk
    put_u64(zip, cds.size());     // entries this disk
    put_u64(zip, cds.size());     // total entries
    put_u64(zip, cdSize);
    put_u64(zip, cdStart);
    // Zip64 EOCD locator.
    put_u32(zip, 0x07064b50);
    put_u32(zip, 0);              // disk with EOCD64
    put_u64(zip, eocd64Ofs);
    put_u32(zip, 1);              // total disks
    // Classic EOCD with sentinels.
    put_u32(zip, 0x06054b50);
    put_u16(zip, 0);
    put_u16(zip, 0);
    put_u16(zip, 0xFFFF);         // entries this disk sentinel
    put_u16(zip, 0xFFFF);         // total entries sentinel
    put_u32(zip, 0xFFFFFFFFu);    // cd size sentinel
    put_u32(zip, 0xFFFFFFFFu);    // cd offset sentinel
    put_u16(zip, 0);
    return zip;
}

}  // namespace testzip
