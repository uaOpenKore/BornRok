#pragma once
// Portable UTF-8 -> cp949 (UHC) transcoding, so a ZIP whose Korean entry names are stored as UTF-8
// (7-Zip / Windows Explorer set general-purpose bit 11) can be matched against the cp949 byte paths RO
// assets are requested by (유저인터페이스 / 몬스터 / 이펙트 ...). On Windows the ZIP loader uses the OS
// codepage API; on Android/Linux there is no such API, so the whole Korean-folder content silently
// missed -> no UI skin, no sprites, no ground textures (S. 2026-08-06, Android). This embeds the
// Unicode(BMP)->cp949 table so the conversion works on every platform.
#include <cstdint>
#include <string>

namespace uaro {

// cp949 (2-byte) for a BMP Unicode codepoint, or 0 if the char has no cp949 mapping.
uint16_t unicodeToCp949(uint32_t u);

// Transcode a UTF-8 string to cp949 bytes. ASCII passes through; a char with no cp949 mapping keeps its
// raw UTF-8 bytes (so non-Korean names still round-trip). Pure -> unit-testable offline.
std::string utf8ToCp949Portable(const std::string& s);

}  // namespace uaro
