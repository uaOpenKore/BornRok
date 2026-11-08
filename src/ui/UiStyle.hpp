#pragma once
#include "core/Types.hpp"

// uaRO-HD interface design system ("style A"), ported from the uaRO Graphics driver
// (github.com/kwebmn/ragnarok-graphics, src/core/ui_style.* + window_panel.*). That client
// draws its whole HD interface procedurally — dark translucent rounded panels with a soft
// drop shadow and a thin gold edge ring, warm-gold accents on a near-black navy ground — rather
// than from bitmap skins. This module reproduces that look for BornRok: the design tokens
// (palette, opacity, radii) plus the procedural panel primitive, built on SpriteBatch.
//
// How it works: BornRok's SpriteBatch uses straight-alpha blending, so each rounded/soft shape is
// a small RGBA8 coverage MASK (white RGB, alpha = anti-aliased coverage) generated on the CPU once,
// cached by size, and drawn as a bilinear-filtered quad tinted by the wanted colour (the tint's
// alpha scales the whole shape). No shader or new render state is needed.

namespace uaro {

class SpriteBatch;

namespace ui {
namespace style {

// --- Colour helper + palette (tokens from the uaRO-HD map; see memory uaro-hd-interface-design-system).
// Packs into the ABGR word SpriteBatch/Font expect (low byte = red), same layout as ui::rgba.
constexpr u32 rgb(u8 r, u8 g, u8 b, u8 a = 255) {
    return (static_cast<u32>(a) << 24) | (static_cast<u32>(b) << 16) |
           (static_cast<u32>(g) << 8) | static_cast<u32>(r);
}

constexpr u32 kGround = rgb(0x10, 0x14, 0x1C);    // #10141C card/window ground (very dark navy)
constexpr u32 kGroundAlt = rgb(0x0E, 0x12, 0x1A); // #0E121A alt ground (cards)
constexpr u32 kTile = rgb(0x1B, 0x1D, 0x24);      // #1B1D24 inset tile / slot plate
constexpr u32 kGold = rgb(0xC9, 0xA5, 0x5B);      // #C9A55B primary accent gold
constexpr u32 kGold2 = rgb(0xE6, 0xCB, 0x8A);     // #E6CB8A lighter gold (edge ring, highlight)
constexpr u32 kText = rgb(0xEC, 0xEF, 0xF5);      // #ECEFF5 near-white body text
constexpr u32 kMuted = rgb(0x9A, 0xA3, 0xB5);     // #9AA3B5 muted label text
constexpr u32 kFaint = rgb(0x7E, 0x88, 0x9A);     // #7E889A faint text
constexpr u32 kOnGold = rgb(0x1A, 0x14, 0x08);    // #1A1408 dark text on a gold badge
constexpr u32 kHp = rgb(0x4C, 0xC7, 0x77);        // #4CC777 HP green / positive
constexpr u32 kSp = rgb(0x3D, 0x7E, 0xEB);        // #3D7EEB SP blue
constexpr u32 kDanger = rgb(0xFF, 0x5A, 0x4E);    // #FF5A4E danger / debuff red
constexpr u32 kWarn = rgb(0xF0, 0xA0, 0x50);      // #F0A050 warning amber
constexpr u32 kBaseExp = rgb(0xB7, 0xA6, 0xFF);   // #B7A6FF base-exp lavender
constexpr u32 kJobExp = rgb(0x7F, 0xD3, 0xC3);    // #7FD3C3 job-exp teal
constexpr u32 kWhite = rgb(255, 255, 255);
constexpr u32 kBlack = rgb(0, 0, 0);

// --- Default opacities (uaRO-HD: window 0.80-1.0 default 0.90; HUD panel 0.50-1.0 default 0.80).
constexpr float kWindowOpacity = 0.90f;
constexpr float kPanelOpacity = 0.80f;

// --- Geometry tokens (uaRO-HD window_panel kRadius=9/10, card 7, tile 5; shadow spread 12 / 40% /
// drop 3; gold edge ring 1.5px @ #E6CB8A 0.85). Expressed at 1.0 scale; pass a scaled value in.
constexpr float kWindowRadius = 10.0f;
constexpr float kCardRadius = 7.0f;
constexpr float kTileRadius = 5.0f;
constexpr float kShadowSpread = 12.0f;
constexpr float kShadowAlpha = 0.40f;
constexpr float kShadowDrop = 3.0f;
constexpr float kRingWidth = 1.5f;
constexpr float kRingAlpha = 0.85f;

// Return `color` with its alpha replaced by `opacity` (0..1). Used to draw a ground at window/panel
// opacity, or an accent at a chosen strength.
constexpr u32 withOpacity(u32 color, float opacity) {
    u32 a = static_cast<u32>((opacity < 0.0f ? 0.0f : opacity > 1.0f ? 1.0f : opacity) * 255.0f + 0.5f);
    return (color & 0x00FFFFFFu) | (a << 24);
}

// --- Primitives -----------------------------------------------------------------------------------

// Draw a style-A WINDOW frame at (x,y,w,h): a soft drop shadow, then the dark translucent rounded
// ground, then the thin gold edge ring. `radius` and `scale` scale the corner + shadow/ring metrics
// (pass the window's render scale so a 1.5x window keeps proportionate corners). `ground`/`opacity`
// default to the navy ground at window opacity. This is the reusable chrome every redesigned window
// sits in; draw the title + content on top afterwards.
void windowFrame(SpriteBatch& sb, float x, float y, float w, float h, float radius = kWindowRadius,
                 float scale = 1.0f, u32 ground = kGround, float opacity = kWindowOpacity);

// Draw a style-A HUD panel frame: the same rounded translucent ground but with NO drop shadow and a
// faint white hairline edge instead of the gold ring (uaRO-HD gives shadows to windows only). Used
// for always-on HUD grounds (character block, buffs, hotbar, ...).
void hudPanel(SpriteBatch& sb, float x, float y, float w, float h, float radius = kCardRadius,
              float scale = 1.0f, u32 ground = kGround, float opacity = kPanelOpacity);

// Draw a plain tinted rounded rect (no shadow, no ring): inset tiles, chips, bar tracks/fills.
void roundedFill(SpriteBatch& sb, float x, float y, float w, float h, float radius, u32 color);

}  // namespace style
}  // namespace ui
}  // namespace uaro
