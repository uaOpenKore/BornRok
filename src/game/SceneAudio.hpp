#pragma once
#include <string>

#include "resource/Mp3NameTable.hpp"
#include "resource/Vfs.hpp"

namespace uaro {

// The title/login/char-select theme: the BGM the mp3nametable maps for new_zone01 (S.). Falls back
// to the classic 01.mp3 main theme when the table or the entry is missing. Shared by LoginScene and
// CharSelectScene so the music plays unbroken across the server-select -> login -> char-select flow
// (Audio::playBgm is idempotent, so repeating the same path never restarts the track). (#103)
inline std::string titleBgmPath(const Vfs& vfs) {
    auto mt = vfs.read("data/english/mp3nametable.txt");
    if (!mt) mt = vfs.read("data/mp3nametable.txt");
    if (mt) {
        auto tbl = parseMp3NameTable(*mt);
        if (auto it = tbl.find("new_zone01"); it != tbl.end()) return it->second;
    }
    return "bgm/01.mp3";
}

} // namespace uaro
