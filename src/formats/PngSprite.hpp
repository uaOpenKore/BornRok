#pragma once
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/Types.hpp"
#include "formats/Act.hpp"
#include "formats/Spr.hpp"

namespace uaro {

// PNG sprite (#109 level 2): a brand-new animated sprite built from loose PNG frames + a
// manifest.cfg, with no Gravity .spr/.act behind it. The parser synthesizes an in-memory
// Sprite (truecolor frames) + Action (the standard action*8+dir layout), so everything
// downstream (SpriteComposer, CharacterActor playback) works unchanged.
//
// manifest.cfg — one entry per line, '#' comments:
//   scale K              frames are K x the logical size (hi-res); geometry stays logical
//   anchor X Y           default anchor in LOGICAL pixels from the frame's top-left
//                        (default: bottom-centre — "between the feet")
//   action <name|index> <delayMs>   starts an action; name = idle|walk|attack|hurt|die
//                        (the monster/NPC motion layout: 0/1/2/3/4), or an explicit index
//   frame <file> [dx dy] [sound <wav>]   appends a frame; dx/dy = extra LOGICAL offset;
//                        sound fires the named event when the frame shows
//
// v1 keeps one direction: every action's frames fill all 8 direction slots. delayMs is
// converted to the ACT unit (ms/25). Frames are RGBA PNGs, alpha = transparency.
struct PngSpriteResult {
    Sprite sprite;   // truecolor frames only
    Action action;   // action*8+dir layout, delays in ACT units
    float scale = 1; // frame pixels per logical pixel (texture density, like hi-res maps)
};

// Reads the file contents of `manifestText`'s referenced PNGs through `readFile`
// (name as written in the manifest -> bytes, nullopt if missing). Returns nullopt and
// fills `err` (if given) on a malformed manifest / missing or undecodable frame.
std::optional<PngSpriteResult> parsePngSprite(
    const std::string& manifestText,
    const std::function<std::optional<std::vector<u8>>(const std::string&)>& readFile,
    std::string* err = nullptr);

}  // namespace uaro
