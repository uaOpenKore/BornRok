#include "formats/Evff.hpp"

#include <cstdlib>
#include <cstring>

namespace uaro {

namespace {

// Pull up to `max` numbers out of a value string (comma- or space-separated). EVFF mixes both
// ("255.0,255.0,212.0,184.0, 5,7" and "-400.0000,-300.0000  400.0000,-300.0000"). Returns count.
usize parseNums(const char* s, f32* out, usize max) {
    usize n = 0;
    while (*s && n < max) {
        while (*s == ',' || *s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') ++s;
        if (!*s) break;
        char* end = nullptr;
        const f32 v = std::strtof(s, &end);
        if (end == s) { ++s; continue; }
        out[n++] = v;
        s = end;
    }
    return n;
}

// Trim leading whitespace and return the value after the first '=' in a "key=value" line, or null.
const char* valueOf(const std::string& line, const char* key) {
    const char* p = line.c_str();
    while (*p == ' ' || *p == '\t') ++p;
    const usize klen = std::strlen(key);
    if (std::strncmp(p, key, klen) != 0) return nullptr;
    p += klen;
    if (*p != '=') return nullptr;
    return p + 1;
}

bool isBrace(const std::string& line, char c) {
    for (char ch : line) {
        if (ch == ' ' || ch == '\t' || ch == '\r') continue;
        return ch == c;
    }
    return false;
}

}  // namespace

std::optional<Evff> Evff::parse(const std::vector<u8>& bytes) {
    if (bytes.size() < 8 || std::memcmp(bytes.data(), "EVFF", 4) != 0) return std::nullopt;

    // Split into lines (the format is line-oriented text).
    std::vector<std::string> lines;
    std::string cur;
    for (u8 b : bytes) {
        if (b == '\n') { lines.push_back(cur); cur.clear(); }
        else if (b != '\r') cur.push_back(static_cast<char>(b));
    }
    if (!cur.empty()) lines.push_back(cur);

    Evff e;
    usize i = 0;
    // Header block: fps / maxkey / layernum before the first layer.
    for (; i < lines.size(); ++i) {
        const std::string& L = lines[i];
        if (const char* v = valueOf(L, "fps")) e.fps_ = static_cast<u32>(std::atoi(v));
        else if (const char* v2 = valueOf(L, "maxkey")) e.maxKey_ = static_cast<u32>(std::atoi(v2));
        else if (valueOf(L, "layernum")) { /* count; we size dynamically */ }
        else if (std::strncmp(L.c_str(), "layer:", 6) == 0 ||
                 L.find("layer:") != std::string::npos)
            break;
    }

    // Layer blocks: `layer:<name>` then `{ fields + N keyframe { } }`.
    while (i < lines.size()) {
        const std::string& L = lines[i];
        const usize lp = L.find("layer:");
        if (lp == std::string::npos) { ++i; continue; }
        EvffLayer layer;
        ++i;
        // opening brace of the layer body
        while (i < lines.size() && !isBrace(lines[i], '{')) ++i;
        if (i < lines.size()) ++i;  // consume '{'
        // layer header fields until we hit the first keyframe '{'
        int depth = 1;  // inside the layer body
        while (i < lines.size() && depth == 1) {
            const std::string& s = lines[i];
            if (isBrace(s, '{')) break;  // start of first keyframe
            if (isBrace(s, '}')) { depth = 0; ++i; break; }  // empty layer body
            if (const char* v = valueOf(s, "texname")) {
                const char* p = v;
                while (*p == ' ' || *p == '\t') ++p;
                std::string tex(p);
                while (!tex.empty() && (tex.back() == ' ' || tex.back() == '\t')) tex.pop_back();
                layer.texture = tex;
            } else if (const char* vt = valueOf(s, "type")) {
                layer.type = std::atoi(vt);
            }
            ++i;
        }
        // keyframe blocks
        while (i < lines.size() && depth == 1) {
            if (isBrace(lines[i], '}')) { ++i; depth = 0; break; }  // end of layer body
            if (!isBrace(lines[i], '{')) { ++i; continue; }
            ++i;  // consume keyframe '{'
            EvffKeyframe k;
            while (i < lines.size() && !isBrace(lines[i], '}')) {
                const std::string& s = lines[i];
                if (const char* v = valueOf(s, "frame")) k.frame = std::atoi(v);
                else if (const char* v2 = valueOf(s, "aniframe")) k.aniframe = std::strtof(v2, nullptr);
                else if (const char* v3 = valueOf(s, "anitype")) k.anitype = static_cast<u32>(std::atoi(v3));
                else if (const char* v4 = valueOf(s, "delay")) k.delay = std::strtof(v4, nullptr);
                else if (const char* v5 = valueOf(s, "pos")) parseNums(v5, k.pos, 2);
                else if (const char* v6 = valueOf(s, "uvs2")) parseNums(v6, k.uvs2, 2);
                else if (const char* v7 = valueOf(s, "uvs")) parseNums(v7, k.uvs, 2);
                else if (const char* v8 = valueOf(s, "uv2")) parseNums(v8, k.uv2, 2);
                else if (const char* v9 = valueOf(s, "uv")) parseNums(v9, k.uv, 2);
                else if (const char* v10 = valueOf(s, "scale")) parseNums(v10, k.scale, 2);
                else if (const char* v11 = valueOf(s, "angle")) parseNums(v11, k.angle, 3);
                else if (const char* v12 = valueOf(s, "color")) {
                    f32 c[6] = {0, 0, 0, 0, 5, 7};
                    const usize n = parseNums(v12, c, 6);
                    for (int j = 0; j < 4; ++j) k.color[j] = c[j] / 255.0f;
                    if (n >= 5) k.srcBlend = static_cast<u32>(c[4]);
                    if (n >= 6) k.dstBlend = static_cast<u32>(c[5]);
                } else if (const char* v13 = valueOf(s, "rpoints")) parseNums(v13, k.rpoints, 8);
                else if (const char* v14 = valueOf(s, "points")) parseNums(v14, k.points, 8);
                ++i;
            }
            if (i < lines.size()) ++i;  // consume keyframe '}'
            layer.keys.push_back(k);
        }
        e.layers_.push_back(std::move(layer));
    }

    if (e.layers_.empty()) return std::nullopt;
    return e;
}

Str Evff::toStr() const {
    constexpr float kDegPer = 1024.0f / 360.0f;  // EVFF angle.z is 1024-per-revolution, like STR
    constexpr float kYShift = 80.0f;             // EVFF 640x480 centre (320,240) -> STR 320-centre
    // Fill a STR source keyframe (type 0, absolute) from an EVFF key.
    auto source = [](const EvffKeyframe& k) {
        StrKeyframe s;
        s.frame = k.frame;
        s.type = 0;
        s.pos[0] = k.pos[0];
        s.pos[1] = k.pos[1] + kYShift;
        for (int j = 0; j < 4; ++j) { s.xy[j] = k.points[2 * j]; s.xy[4 + j] = k.points[2 * j + 1]; }
        s.aniframe = k.aniframe;
        s.anitype = k.anitype;
        s.delay = k.delay;
        s.angle = k.angle[2] / kDegPer;
        for (int j = 0; j < 4; ++j) s.color[j] = k.color[j];
        s.srcAlpha = k.srcBlend;
        s.destAlpha = k.dstBlend;
        return s;
    };
    std::vector<StrLayer> out;
    for (const EvffLayer& L : layers_) {
        if (L.type != 0 || L.texture.empty() || L.keys.empty()) continue;  // container/empty layers
        StrLayer sl;
        sl.textures.push_back(L.texture);
        for (usize i = 0; i < L.keys.size(); ++i) {
            const EvffKeyframe& cur = L.keys[i];
            sl.keys.push_back(source(cur));
            if (i + 1 >= L.keys.size()) break;  // last key: source only (holds to the end)
            const EvffKeyframe& nxt = L.keys[i + 1];
            const float df = static_cast<float>(nxt.frame - cur.frame);
            if (df <= 0.0f) continue;  // no slope pair for a zero/negative span
            // Slope target (type 1) at the SAME frame: value = source + slope*(t-frame).
            StrKeyframe sp = source(cur);
            sp.type = 1;
            StrKeyframe sc = source(cur), sn = source(nxt);
            sp.pos[0] = (sn.pos[0] - sc.pos[0]) / df;
            sp.pos[1] = (sn.pos[1] - sc.pos[1]) / df;
            for (int j = 0; j < 8; ++j) sp.xy[j] = (sn.xy[j] - sc.xy[j]) / df;
            sp.angle = (sn.angle - sc.angle) / df;
            for (int j = 0; j < 4; ++j) sp.color[j] = (sn.color[j] - sc.color[j]) / df;
            sp.anitype = nxt.anitype;
            sp.aniframe = (sn.aniframe - sc.aniframe) / df;
            sp.delay = nxt.delay;
            sl.keys.push_back(sp);
        }
        out.push_back(std::move(sl));
    }
    return Str::build(fps_, maxKey_, std::move(out));
}

}  // namespace uaro
