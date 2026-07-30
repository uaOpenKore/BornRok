#pragma once

#include <string>

#include "core/Types.hpp"

namespace uaro::net {

// Human-readable class name for a job/class id (the value the server sends in class_).
// Covers the classic 1st/2nd/transcendent/baby classes, the expanded jobs (Taekwon/Star
// Gladiator/Soul Linker) and the renewal 3rd jobs plus their common mounted variants; an
// unrecognised id falls back to "Job <id>". Ids are roBrowser's JobConst paired with the
// standard English client names, so the status / character windows can print a name
// instead of a raw number (Sergio: "профа должна писаться надписью, а не цифрами").
std::string jobName(u16 classId);

}  // namespace uaro::net
