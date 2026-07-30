#pragma once
#include "core/Types.hpp"

namespace uaro {

// NPC/monster view id -> sprite base name (lowercased), from the client's compiled
// jobname/npcidentity tables (ActorSprites.cpp, generated). Returns nullptr for an
// unknown id. The name resolves under data/sprite/npc/<name>.{spr,act} for NPCs or
// data/sprite/<monster>/<name>.{spr,act} for monsters — the loader tries both.
const char* actorSpriteName(u16 id);

// Enumeration for the --view content browser: iterate the whole generated table.
struct ActorSpriteEntry { u16 id; const char* name; };
usize actorSpriteCount();
ActorSpriteEntry actorSpriteAt(usize i);

} // namespace uaro
