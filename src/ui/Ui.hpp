#pragma once
// STUB — implemented in v5 (UI + game states).
//
// Retained-mode widget tree rendered through SpriteBatch, skinned from the
// original client's bitmaps (Client/winEXE/skin/). Hosts the login, char-select
// and in-game HUD scenes, and integrates the Lua layer.
#include "core/Types.hpp"

namespace uaro::ui {

// Planned interface:
//   class Widget { virtual void layout(); virtual void draw(SpriteBatch&); ... };
//   class Screen; class Button; class Label; class Window;

} // namespace uaro::ui
