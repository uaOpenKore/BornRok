// CardIllust.hpp -- card item id -> base monster sprite name, for the card ILLUSTRATION shown in the
// item-info popup. The illustration file is data/texture/<유저인터페이스>/cardbmp/ill_<sprite>_card.bmp
// (the classic RO convention; the sprite is the DROPPING monster's aegis name lowercased -- e.g. the
// Swordfish Card 4089 uses ill_sword_fish_card because mob SWORD_FISH's sprite is sword_fish, NOT the
// item name "Swordfish"). Generated from the server mob_db (DropCardid -> lower(Sprite), base mob =
// lowest mob id per card) -- regenerate when mob_db changes.
#pragma once
#include "core/Types.hpp"

namespace uaro {
// Base monster sprite name for a card's illustration, or nullptr if unknown.
const char* cardIllustSprite(u32 cardId);
}
