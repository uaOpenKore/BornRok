#pragma once
#include <string>

#include "core/Types.hpp"

namespace uaro::net {

// Mutable per-play-session state threaded through the login -> char-select ->
// in-game scene flow. Filled in stages: the login reply sets the account/auth
// ids and the char-server address; char-select sets the chosen character and the
// map-server address from HC_NOTIFY_ZONESVR.
struct Session {
    // Client identity (from the selected clientinfo connection).
    u32 version = 0;
    u32 langtype = 0;
    u8 clientType = 0;

    // Auth (from AC_ACCEPT_LOGIN, 0x69).
    u32 accountId = 0;
    u32 loginId1 = 0;
    u32 loginId2 = 0;
    u8 sex = 0;
    std::string login;  // the typed account name (set at submit); keys the per-account client-side
                        // config files, e.g. the quick-slot layout settings/<login>-<charName>.cfg

    // Chosen char-server (one entry of the 0x69 server list).
    std::string charHost;
    u16 charPort = 0;

    // Chosen character + its destination map-server (from HC_NOTIFY_ZONESVR, 0x71).
    u32 charId = 0;
    std::string charName;
    u16 charClass = 0;  // job class id (for the in-world player sprite)
    u16 baseLevel = 0, jobLevel = 0;  // from the char-select list, so the HUD shows them immediately
    u32 baseExp = 0, jobExp = 0, zeny = 0;  // also from char-select: the load-end burst sends only
                                            // NEXT-exp (not current exp/zeny), so without seeding
                                            // these the exp bars + zeny read 0 until the first kill
    u16 charHair = 0;   // hairstyle id
    u16 charHairColor = 0;  // hair dye colour (0 = default); -> data/palette/머리 .pal (#86)
    u16 charClothColor = 0; // clothes dye colour (0 = default); -> data/palette/몸 .pal (#86)
    u16 charWeapon = 0; // equipped weapon view id (server sends the item id -> sprite)
    u16 charShield = 0; // equipped shield view id (server sends the item id -> sprite)
    u16 charHeadBottom = 0, charHeadMid = 0, charHeadTop = 0;  // headgear view ids
    std::string mapName;   // e.g. "prontera.gat"
    std::string mapHost;
    u16 mapPort = 0;

    // Camera preferences that persist across map changes AND relogin (a new GameScene is
    // pushed each time, so these live in the session, which outlives any one scene). Only the
    // azimuth/direction snaps back to default on warp/relogin; the zoom and pitch carry over
    // (S.: "зум и угол должны оставаться такие же, только выставляется направление").
    float camZoom = 0.125f;   // closest zoom (start fully zoomed in)
    float camPitch = 0.0f;    // elevation offset added to the default RO tilt

    void reset() { *this = Session{}; }
};

} // namespace uaro::net
