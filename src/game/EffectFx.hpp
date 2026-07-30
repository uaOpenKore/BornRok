#pragma once
#include <string>
#include <utility>
#include <vector>

namespace uaro {

// The .str effects the client ALREADY plays for a server effect-id (ZC_NOTIFY_EFFECT2 / 0x1f3),
// enumerated straight from the effectFxStr(id) switch in GameScene.cpp -- no duplicated list, so the
// --view2d "Привязанные" tab shows exactly what the client uses (id -> effect name). (S. #129/effects)
std::vector<std::pair<unsigned short, std::string>> effectFxUsedList();

// The FULL master list of server effect constants (EF_*, id -> short name) from the server's
// db/const.txt -- every EFFECTID the server can send via 0x1f3. The --view2d 0x1f3 channel lists
// all of these with a "+"/"-" mark (does the client render one?). Generated in EffectConstList.cpp.
const std::vector<std::pair<unsigned short, std::string>>& effectConstList();

// The FULL skill list (skillId -> display name) from the server's db/skill_db.txt -- every skill the
// server can invoke a visual for. The --view2d "Скилл" channel lists these. In SkillConstList.cpp.
const std::vector<std::pair<unsigned short, std::string>>& skillConstList();

// The effect the CLIENT plays for a SKILL id (skillId -> asset name), so the --view2d "Скилл" channel
// marks which skills have a visual. Returns the .str name (from skillFxDef) for .str skills, or the
// exe-sourced coded sprite/effect name (e.g. 19 Fire Bolt -> "fireball") for coded skills, else "".
std::string skillFxName(unsigned short id);

// The multi-file "%d" effects (Meteor1.str..Meteor4.str etc.) the exe dispatch plays as a frame
// sequence. Returns {EFFECTID -> first-frame .str name} so the --view2d 0x1f3 channel can mark them
// "+" and preview the first frame. The full frame lists + in-game sequence player live in GameScene.
std::vector<std::pair<unsigned short, std::string>> effectFxSeqList();

}  // namespace uaro
