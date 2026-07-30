#pragma once
#include "core/Types.hpp"

namespace uaro::net {

// After-cast act delay (ms) for a skill, baked from the server's db/skill_cast_db.txt
// (the AfterCastActDelay column, taken at the skill's top level). This server sends NO
// cooldown packet, so the client greys a just-cast skill for this long itself (#72).
// Returns 0 for instant skills (the majority) and for ids not in the table.
u32 skillCastDelayMs(u16 skillId);

} // namespace uaro::net
