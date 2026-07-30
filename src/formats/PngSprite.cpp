#include "formats/PngSprite.hpp"

#include <cmath>
#include <map>
#include <sstream>

#include "formats/ImageIO.hpp"

namespace uaro {

namespace {

void setErr(std::string* err, const std::string& msg) {
    if (err) *err = msg;
}

// Motion slot per action name — the monster/NPC layout (CharacterActor: a monster idles
// at 0, walks 1, attacks 2, hurts 3, dies 4). Explicit numeric indices cover the rest.
int motionForName(const std::string& name) {
    if (name == "idle") return 0;
    if (name == "walk") return 1;
    if (name == "attack") return 2;
    if (name == "hurt") return 3;
    if (name == "die") return 4;
    return -1;
}

struct PendingFrame {
    int sprIndex = 0;   // frame index in the synthesized sprite
    int dx = 0, dy = 0; // extra logical offset
    int eventId = -1;   // index into events, or -1
    int wLog = 0, hLog = 0;
};

struct PendingAction {
    int motion = 0;
    float delayAct = 6.0f;  // ACT units (ms / 25); 150 ms default
    std::vector<PendingFrame> frames;
};

}  // namespace

std::optional<PngSpriteResult> parsePngSprite(
    const std::string& manifestText,
    const std::function<std::optional<std::vector<u8>>(const std::string&)>& readFile,
    std::string* err) {
    float scale = 1.0f;
    bool haveAnchor = false;
    int anchorX = 0, anchorY = 0;  // logical px from the frame's top-left
    std::vector<SprFrame> frames;
    std::vector<ActEvent> events;
    std::map<std::string, int> frameIndexByFile;  // reuse a file referenced twice
    std::map<std::string, int> eventIndexByName;
    std::vector<PendingAction> pending;

    std::istringstream in(manifestText);
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        if (const auto hash = line.find('#'); hash != std::string::npos) line.erase(hash);
        std::istringstream ls(line);
        std::string key;
        if (!(ls >> key)) continue;  // blank / comment-only line

        if (key == "scale") {
            if (!(ls >> scale) || scale <= 0.0f)
                return setErr(err, "manifest line " + std::to_string(lineNo) + ": bad scale"),
                       std::nullopt;
        } else if (key == "directions") {
            int d = 1;  // accepted for forward-compat; v1 replicates one direction into all 8
            ls >> d;
        } else if (key == "anchor") {
            if (!(ls >> anchorX >> anchorY))
                return setErr(err, "manifest line " + std::to_string(lineNo) + ": bad anchor"),
                       std::nullopt;
            haveAnchor = true;
        } else if (key == "action") {
            std::string name;
            float delayMs = 150.0f;
            if (!(ls >> name))
                return setErr(err, "manifest line " + std::to_string(lineNo) + ": action needs a name"),
                       std::nullopt;
            ls >> delayMs;
            int motion = motionForName(name);
            if (motion < 0) {
                try {
                    motion = std::stoi(name);
                } catch (...) {
                    return setErr(err, "manifest line " + std::to_string(lineNo) +
                                           ": unknown action '" + name + "'"),
                           std::nullopt;
                }
            }
            PendingAction a;
            a.motion = motion;
            a.delayAct = (delayMs > 0.0f ? delayMs : 150.0f) / 25.0f;
            pending.push_back(std::move(a));
        } else if (key == "frame") {
            if (pending.empty())
                return setErr(err, "manifest line " + std::to_string(lineNo) +
                                       ": frame before any action"),
                       std::nullopt;
            std::string file;
            if (!(ls >> file))
                return setErr(err, "manifest line " + std::to_string(lineNo) + ": frame needs a file"),
                       std::nullopt;
            PendingFrame pf;
            // Optional trailing tokens: "dx dy" (two ints) and/or "sound <name>".
            std::string tok;
            while (ls >> tok) {
                if (tok == "sound") {
                    std::string snd;
                    if (!(ls >> snd))
                        return setErr(err, "manifest line " + std::to_string(lineNo) +
                                               ": sound needs a name"),
                               std::nullopt;
                    auto [it, inserted] =
                        eventIndexByName.try_emplace(snd, static_cast<int>(events.size()));
                    if (inserted) events.push_back({snd});
                    pf.eventId = it->second;
                } else {
                    int dx = 0, dy = 0;
                    try {
                        dx = std::stoi(tok);
                    } catch (...) {
                        return setErr(err, "manifest line " + std::to_string(lineNo) +
                                               ": unexpected token '" + tok + "'"),
                               std::nullopt;
                    }
                    if (!(ls >> dy))
                        return setErr(err, "manifest line " + std::to_string(lineNo) +
                                               ": frame offset needs dx AND dy"),
                               std::nullopt;
                    pf.dx = dx;
                    pf.dy = dy;
                }
            }
            auto known = frameIndexByFile.find(file);
            if (known == frameIndexByFile.end()) {
                const auto bytes = readFile(file);
                if (!bytes)
                    return setErr(err, "frame file missing: " + file), std::nullopt;
                const auto img = decodeImage(*bytes);
                if (!img || !img->valid())
                    return setErr(err, "frame file undecodable: " + file), std::nullopt;
                SprFrame f;
                f.width = static_cast<u16>(img->width);
                f.height = static_cast<u16>(img->height);
                f.pixels = img->rgba;
                frames.push_back(std::move(f));
                known = frameIndexByFile.emplace(file, static_cast<int>(frames.size()) - 1).first;
            }
            pf.sprIndex = known->second;
            pf.wLog = static_cast<int>(std::lround(frames[pf.sprIndex].width / scale));
            pf.hLog = static_cast<int>(std::lround(frames[pf.sprIndex].height / scale));
            pending.back().frames.push_back(pf);
        } else {
            return setErr(err, "manifest line " + std::to_string(lineNo) + ": unknown key '" + key +
                                   "'"),
                   std::nullopt;
        }
    }

    if (pending.empty())
        return setErr(err, "manifest has no actions"), std::nullopt;
    for (const PendingAction& a : pending)
        if (a.frames.empty())
            return setErr(err, "an action has no frames"), std::nullopt;

    // Lay the actions out as the standard RO grid: action index = motion*8 + direction, one
    // identical copy per direction (v1). Fill gaps (unused motions below the max) with the
    // first action so `motion*8+dir` never indexes out of range in the player.
    int maxMotion = 0;
    for (const PendingAction& a : pending) maxMotion = a.motion > maxMotion ? a.motion : maxMotion;
    std::vector<ActAction> actions(static_cast<usize>(maxMotion + 1) * 8);

    auto buildAct = [&](const PendingAction& src) {
        ActAction out;
        out.delay = src.delayAct;
        for (const PendingFrame& pf : src.frames) {
            ActFrame fr;
            fr.eventId = pf.eventId;
            ActLayer L;
            L.sprIndex = pf.sprIndex;
            L.sprType = 1;  // truecolor frame
            // Layer (x,y) = the sprite CENTRE relative to the actor origin (composeFrame). The
            // manifest anchor is a pixel of the frame that must sit at the origin; default =
            // bottom-centre (feet).
            const int ax = haveAnchor ? anchorX : pf.wLog / 2;
            const int ay = haveAnchor ? anchorY : pf.hLog;
            L.x = pf.wLog / 2 - ax + pf.dx;
            L.y = pf.hLog / 2 - ay + pf.dy;
            // Hi-res frames render at logical size: composeFrame does size = frame_px * |scale|.
            L.scaleX = L.scaleY = 1.0f / scale;
            L.width = pf.wLog;
            L.height = pf.hLog;
            fr.layers.push_back(L);
            out.frames.push_back(std::move(fr));
        }
        return out;
    };
    for (const PendingAction& a : pending) {
        const ActAction built = buildAct(a);
        for (int dir = 0; dir < 8; ++dir) actions[static_cast<usize>(a.motion) * 8 + dir] = built;
    }
    // Gap motions (e.g. only idle+attack defined -> walk empty): reuse the first defined action.
    const ActAction fallback = buildAct(pending.front());
    for (ActAction& a : actions)
        if (a.frames.empty()) a = fallback;

    PngSpriteResult res{Sprite::fromRgbaFrames(std::move(frames)),
                        Action::fromParts(std::move(actions), std::move(events)), scale};
    return res;
}

}  // namespace uaro
