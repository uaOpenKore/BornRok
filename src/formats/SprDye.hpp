#pragma once
// Dye (hair/clothes colour) for a WebP sprite. A palette dye recolours the PALETTE of an indexed .spr, but
// the 2k pack's WebP frames are truecolor: their pixels carry no palette index. The 1k .spr of the same
// sprite (the dye guide, patcher kDyeGuideDest) has the indices at half the size, so each WebP pixel takes
// the index of the guide pixel under it -- the nearest-coloured one among the guide's 3x3 neighbourhood
// there, which also settles the 2k edges the 1k grid cuts through -- and moves by that index's dye shift
// (dye colour - guide colour). The 2k shading/anti-aliasing stays, only the hue of the dyed parts changes.
// Colour alone cannot tell cloth from skin in these palettes (68% right on 4 bodies x 5 colours, speckled;
// with the guide 88-94%, clean): measured 2026-11-20 against the 1k exe-way dye.
#include <array>

#include "formats/Spr.hpp"

namespace uaro {

// Recolour the frames of `webp` (a synthetic sprite) that line up with an indexed frame of `guide` (same
// index i < truecolorBase, size a whole multiple of the guide frame), by the dye palette `dye` (already
// remapped to the guide's indices). Returns how many frames were recoloured.
int dyeTruecolorFromGuide(Sprite& webp, const Sprite& guide, const std::array<SprPalColor, 256>& dye);

}  // namespace uaro
