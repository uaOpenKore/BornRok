#pragma once

#include <string>

namespace uaro::net {

// English display name for a skill's internal name (e.g. "SM_SWORD" -> "Sword Mastery").
// The skill-list packet only carries the internal name, so the skill window can look up a
// readable label here. Generated from roBrowser's DB/Skills/SkillInfo.js (Name -> SkillName
// pairs); an unmapped internal name falls back to itself. Sergio: skills want "правильными
// надписями". The matching skill icon is data/texture/<유저인터페이스>/item/<internalName>.bmp.
std::string skillName(const std::string& internalName);

}  // namespace uaro::net
