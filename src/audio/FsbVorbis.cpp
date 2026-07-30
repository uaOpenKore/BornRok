#include "audio/FsbVorbis.hpp"

#include "core/Log.hpp"

#ifdef CLIENT_WITH_VORBIS
#include <vorbis/codec.h>

#include <cstring>

namespace uaro {

namespace {

// The two fixed Vorbis headers FMOD strips. Layouts per the Vorbis I spec (little-endian
// bitpacking, but these fields are byte-aligned so plain bytes suffice); FMOD always encodes
// with blocksizes 2^8/2^11 (python-fsb5 rebuilds with the same constants).
std::vector<u8> buildIdHeader(u32 channels, u32 rate) {
    std::vector<u8> h;
    h.push_back(0x01);
    for (char c : {'v', 'o', 'r', 'b', 'i', 's'}) h.push_back(static_cast<u8>(c));
    const auto le32 = [&](u32 v) {
        for (int i = 0; i < 4; ++i) h.push_back(static_cast<u8>(v >> (i * 8)));
    };
    le32(0);  // vorbis_version
    h.push_back(static_cast<u8>(channels));
    le32(rate);
    le32(0);                          // bitrate_maximum
    le32(0);                          // bitrate_nominal
    le32(0);                          // bitrate_minimum
    h.push_back(0x8 | (0xB << 4));    // blocksize_0 = 2^8, blocksize_1 = 2^11
    h.push_back(0x01);                // framing flag
    return h;
}

std::vector<u8> buildCommentHeader() {
    std::vector<u8> h;
    h.push_back(0x03);
    for (char c : {'v', 'o', 'r', 'b', 'i', 's'}) h.push_back(static_cast<u8>(c));
    h.push_back(4);                              // vendor length ("uaro")
    for (int i = 0; i < 3; ++i) h.push_back(0);
    for (char c : {'u', 'a', 'r', 'o'}) h.push_back(static_cast<u8>(c));
    for (int i = 0; i < 4; ++i) h.push_back(0);  // user comment count 0
    h.push_back(0x01);                           // framing flag
    return h;
}

}  // namespace

std::optional<FsbPcm> decodeFsbVorbis(const Fsb5Sample& s) {
    usize setupLen = 0;
    const u8* setup = fsbSetupHeader(s.setupCrc32, setupLen);
    if (!setup) {
        log::warn("fsb: no vorbis setup header for crc32 {}", s.setupCrc32);
        return std::nullopt;
    }

    vorbis_info vi;
    vorbis_info_init(&vi);
    vorbis_comment vc;
    vorbis_comment_init(&vc);
    auto idh = buildIdHeader(s.channels, s.frequency);
    auto cmh = buildCommentHeader();
    ogg_packet p{};
    p.packet = idh.data();
    p.bytes = static_cast<long>(idh.size());
    p.b_o_s = 1;
    p.packetno = 0;
    bool ok = vorbis_synthesis_headerin(&vi, &vc, &p) == 0;
    p = {};
    p.packet = cmh.data();
    p.bytes = static_cast<long>(cmh.size());
    p.packetno = 1;
    ok = ok && vorbis_synthesis_headerin(&vi, &vc, &p) == 0;
    p = {};
    p.packet = const_cast<u8*>(setup);
    p.bytes = static_cast<long>(setupLen);
    p.packetno = 2;
    ok = ok && vorbis_synthesis_headerin(&vi, &vc, &p) == 0;
    if (!ok) {
        log::warn("fsb: vorbis headers rejected (crc32 {})", s.setupCrc32);
        vorbis_comment_clear(&vc);
        vorbis_info_clear(&vi);
        return std::nullopt;
    }

    vorbis_dsp_state vd;
    vorbis_synthesis_init(&vd, &vi);
    vorbis_block vb;
    vorbis_block_init(&vd, &vb);

    FsbPcm out;
    out.channels = s.channels;
    out.frequency = s.frequency;
    out.f32.reserve(static_cast<usize>(s.sampleCount) * s.channels);
    long packetno = 2;
    usize at = 0;
    while (at + 2 <= s.data.size()) {
        u16 sz;
        std::memcpy(&sz, s.data.data() + at, 2);
        at += 2;
        if (sz == 0 || at + sz > s.data.size()) break;
        ogg_packet dp{};
        dp.packet = const_cast<u8*>(s.data.data() + at);
        dp.bytes = sz;
        dp.packetno = ++packetno;
        at += sz;
        if (vorbis_synthesis(&vb, &dp) == 0) vorbis_synthesis_blockin(&vd, &vb);
        float** ch;
        int n;
        while ((n = vorbis_synthesis_pcmout(&vd, &ch)) > 0) {
            for (int i = 0; i < n; ++i)
                for (u32 c = 0; c < s.channels; ++c) out.f32.push_back(ch[c][i]);
            vorbis_synthesis_read(&vd, n);
        }
    }

    vorbis_block_clear(&vb);
    vorbis_dsp_clear(&vd);
    vorbis_comment_clear(&vc);
    vorbis_info_clear(&vi);

    // The codec pads to block boundaries; trim to the authored frame count.
    const usize want = static_cast<usize>(s.sampleCount) * s.channels;
    if (want && out.f32.size() > want) out.f32.resize(want);
    if (out.f32.empty()) return std::nullopt;
    return out;
}

}  // namespace uaro

#else  // !CLIENT_WITH_VORBIS

namespace uaro {
std::optional<FsbPcm> decodeFsbVorbis(const Fsb5Sample&) { return std::nullopt; }
}  // namespace uaro

#endif
