#pragma once
#include <string>
#include <vector>

#include "core/Types.hpp"

// Login/char-server packet codec for the uaRO protocol (packet_ver ~20, the
// langtype-14 client described by sclientinfo.xml). Byte layouts are taken
// straight from the server sources: login.c (parse_login) and char.c
// (parse_char / mmo_char_tobuf). These packet ids are FIXED across client
// versions — only the map-server (clif.c) uses the version-shifted packet_db
// table, which is handled separately.
//
// Decoders take a raw buffer and return the number of bytes consumed, or 0 when
// the buffer does not yet hold the whole packet (the caller waits for more). The
// caller dispatches on peekId() before calling a decoder.
namespace uaro::net {

// Maximum characters per account = the char-server's MAX_CHARS (src/common/mmo.h),
// validated by make_new_char_sql (slot must be < MAX_CHARS). 9 slots = 3 pages of 3.
constexpr int kMaxChars = 9;

// ---- packet ids -----------------------------------------------------------
enum : u16 {
    PKT_CA_LOGIN          = 0x0064,  // C->L  login request (plaintext)
    PKT_CH_ENTER          = 0x0065,  // C->C  char-server connect
    PKT_CH_SELECT_CHAR    = 0x0066,  // C->C  pick a character slot
    PKT_CH_MAKE_CHAR      = 0x0067,  // C->C  create a character (name + stats + slot + hair)
    PKT_CH_DELETE_CHAR    = 0x0068,  // C->C  delete a character (char_id + email)
    PKT_HC_DELETE_ACCEPT  = 0x006f,  // C->C  character deleted OK
    PKT_HC_DELETE_REFUSE  = 0x0070,  // C->C  deletion refused (+ reason byte)
    PKT_AC_ACCEPT_LOGIN   = 0x0069,  // L->C  accepted: auth ids + char-server list
    PKT_AC_REFUSE_LOGIN   = 0x006a,  // L->C  refused: error code (+ ban date)
    PKT_HC_ACCEPT_ENTER   = 0x006b,  // C->C  character list
    PKT_HC_REFUSE_ENTER   = 0x006c,  // C->C  char-server connect refused
    PKT_HC_ACCEPT_MAKECHAR = 0x006d,  // C->C  new character created (one char record)
    PKT_HC_REFUSE_MAKECHAR = 0x006e,  // C->C  char creation refused (reason byte)
    PKT_HC_NOTIFY_ZONESVR = 0x0071,  // C->C  selected char -> map-server address
    PKT_SC_NOTIFY_BAN     = 0x0081,  // *->C  forced disconnect / server closed

    // Map-server (zone) — packet_ver 5 layout. The server auto-detects the version
    // from the connect packet, so the clean contiguous v5 packet is the safe choice.
    PKT_CZ_ENTER          = 0x0072,  // C->M  wanttoconnection (aid/cid/lid1/tick/sex)
    PKT_CZ_REQUEST_MOVE   = 0x0085,  // C->M  walktoxy (packet_ver 5: id + 3-byte cell)
    PKT_CZ_NOTIFY_ACTORINIT = 0x007d,// C->M  loadendack ("map fully loaded")
    PKT_CZ_REQUEST_TIME   = 0x007e,  // C->M  ticksend (keepalive)
    PKT_ZC_ACCEPT_ENTER   = 0x0073,  // M->C  authok: start tick + spawn position
    PKT_ZC_NOTIFY_MOVE    = 0x0086,  // M->C  another unit walks: gid + src/dst + tick
    PKT_CZ_REQUEST_ACT    = 0x0089,  // C->M  action request (attack: target + action)
    PKT_ZC_NOTIFY_ACT     = 0x008a,  // M->C  damage/action (src, dst, damage, type)
    PKT_ZC_NOTIFY_SKILL2  = 0x01de,  // M->C  skill damage (PACKETVER>=3): skillId, src, dst, damage(L), 33B
    PKT_ZC_SKILL_NODAMAGE = 0x011a,  // M->C  no-damage skill (buff/heal): skillId, heal, dst, src, fail, 15B
    PKT_ZC_NOTIFY_GROUNDSKILL = 0x0117,  // M->C  ground/AoE skill placed at a cell: skillId, src, x, y, 18B
    PKT_ZC_USESKILL_ACK   = 0x013e,  // M->C  a unit started casting: src, dst, x, y, skillId, casttime, 24B
    PKT_ZC_USESKILL_CANCEL = 0x01b9,  // M->C  a cast was interrupted: caster gid (6B)
    PKT_CZ_REQNAME        = 0x0094,  // C->M  request a unit's name (gid)
    PKT_ZC_ACK_REQNAME    = 0x0095,  // M->C  unit name (gid + name[24])
    PKT_ZC_ACK_REQNAMEALL = 0x0195,  // M->C  unit name + party/guild (name still @6)
    PKT_CZ_REQ_GUILD_EMBLEM_IMG = 0x0151,  // C->M  ask for a guild's emblem bitmap by guild id
    PKT_ZC_GUILD_EMBLEM_IMG     = 0x0152,  // M->C  guild emblem: guildId + version + (zlib'd) BMP
    PKT_ZC_PLAYERMOVE     = 0x0087,  // M->C  own walk accepted: tick + src/dst cells
    PKT_ZC_STOPMOVE       = 0x0088,  // M->C  clif_fixpos: snap a unit to (x,y) (walk cancel/knockback)
    PKT_ZC_SLIDE          = 0x01ff,  // M->C  clif_slide: knockback/blow-back a unit to (x,y); same 10B layout as 0x88
    PKT_ZC_MOVETOATTACK   = 0x0139,  // M->C  close in to attack: target + cells + range
    PKT_ZC_ATTACK_RANGE   = 0x013a,  // M->C  player's weapon attack range (u16 @2), on equip/login, 4B
    PKT_ZC_CHANGE_MAP     = 0x0091,  // M->C  warp on the same server: map + x/y
    PKT_ZC_CHANGE_MAPSVR  = 0x0092,  // M->C  warp to another map-server: + ip/port
    PKT_ZC_STATE_CHANGE   = 0x0229,  // M->C  unit option/state change (PACKETVER 7)
    PKT_ZC_PAR_CHANGE     = 0x00b0,  // M->C  a player stat changed (type + value; HP/SP/...)
    PKT_ZC_LONGPAR_CHANGE = 0x00b1,  // M->C  large stat (exp/zeny/next-exp); same 8B layout as 0xb0
    PKT_ZC_CHANGE_DIRECTION = 0x009c, // M->C  a unit turned in place: gid + head + body dir (9B)
    PKT_ZC_RECOVERY       = 0x013d,  // M->C  HP/SP recovery amount to float over self (6B): type, val
    PKT_ZC_STATUS         = 0x00bd,  // M->C  bulk character status (6 stats + derived), 44B
    PKT_CZ_STATUS_CHANGE  = 0x00bb,  // C->M  raise a base stat (statusID@2; server applies +1/packet)
    PKT_ZC_STATUS_CHANGE_ACK = 0x00bc,  // M->C  raise result: statusID + ok + new base value, 6B
    PKT_ZC_STATUS_CHANGE  = 0x00be,  // M->C  a 1-byte stat update (e.g. SP_Uxxx raise cost), 5B
    PKT_ZC_COUPLESTATUS   = 0x0141,  // M->C  one stat's base + buff/equip bonus (SP_STR..LUK), 14B
    PKT_ZC_RESURRECTION   = 0x0148,  // M->C  a unit was resurrected (gid + type), 8B
    PKT_ZC_SPRITE_CHANGE  = 0x01d7,  // M->C  a unit's look changed (weapon/shield/...), 11B
    PKT_ZC_STATUS_EFFECT  = 0x0196,  // M->C  status icon load (carries SI_RIDING on login)
    PKT_ZC_PET_INFO       = 0x01a2,  // M->C  pet status: name/rename/level/hungry/friendly/...
    PKT_ZC_PET_FOOD       = 0x01a3,  // M->C  feed result: ok + food item id (6B)
    PKT_ZC_PET_INFO2      = 0x01a4,  // M->C  pet value update: type + pet gid + value (11B)
    PKT_ZC_PETEGG_LIST    = 0x01a6,  // M->C  hatchable eggs (u16 inventory indices, var)
    PKT_ZC_PET_EMOTION    = 0x01aa,  // M->C  pet emotion: pet gid + emote id (10B)
    PKT_ZC_PET_CATCH_PROCESS = 0x019e,  // M->C  taming started: click the target mob (2B)
    PKT_ZC_PET_CATCH_RESULT  = 0x01a0,  // M->C  taming result: ok flag (3B)
    PKT_CZ_PET_CATCH      = 0x019f,  // C->M  taming target: mob gid (6B)
    PKT_CZ_RENAME_PET     = 0x01a5,  // C->M  new pet name (26B: Z24)
    PKT_CZ_PET_MENU       = 0x01a1,  // C->M  pet action: 0 info,1 feed,2 performance,3 to egg
    PKT_CZ_SELECT_PETEGG  = 0x01a7,  // C->M  hatch: chosen egg's inventory index (4B)
    PKT_ZC_NOTIFY_EFFECT  = 0x019b,  // M->C  misc effect: type 0 = base lvl up, 1 = job lvl up
    PKT_ZC_NOTIFY_EFFECT2 = 0x01f3,  // M->C  clif_specialeffect: id(2) aid(4) EFFECTID(4) -- 10B, same
                                     // layout as 0x019b. EFFECTID indexes the client effect table
                                     // (see Client/docs/effect-ids-from-exe.md, extracted from uaRO.exe)
    PKT_ZC_NOTIFY_TIME    = 0x007f,  // M->C  server tick

    // NPC dialog: contact an NPC, then a script-driven say/next/close/menu exchange.
    PKT_CZ_CONTACTNPC     = 0x0090,  // C->M  click/contact an NPC (gid + type)
    PKT_ZC_SAY_DIALOG     = 0x00b4,  // M->C  NPC dialog text (var: len + gid + msg)
    PKT_ZC_WAIT_DIALOG    = 0x00b5,  // M->C  show the "next" button (gid)
    PKT_ZC_CLOSE_DIALOG   = 0x00b6,  // M->C  show the "close" button (gid)
    PKT_ZC_MENU_LIST      = 0x00b7,  // M->C  menu of options (var: len + gid + ':'-list)
    PKT_CZ_CHOOSE_MENU    = 0x00b8,  // C->M  pick a menu option (gid + 1-based choice)
    PKT_CZ_REQ_NEXT       = 0x00b9,  // C->M  "next" clicked (gid)
    PKT_CZ_CLOSE_DIALOG   = 0x0146,  // C->M  "close" clicked (gid)
    PKT_ZC_OPEN_EDITDLG   = 0x0142,  // M->C  open a NUMBER input box (gid); 6B
    PKT_CZ_INPUT_EDITDLG  = 0x0143,  // C->M  number box result: gid.L value.L (10B)
    PKT_ZC_OPEN_EDITDLGSTR = 0x01d4, // M->C  open a STRING input box (gid); 6B
    PKT_CZ_INPUT_EDITDLGSTR = 0x01d5,// C->M  string box result: len.W gid.L str NUL (var)
    PKT_CZ_REQ_REMOVEOPTION = 0x012a,// C->M  remove cart/peco/falcon (no payload); 2B

    // NPC merchant shop: choose buy/sell, then a list -> selection -> result.
    PKT_ZC_SELECT_DEALTYPE      = 0x00c4,  // M->C  open buy/sell chooser (npc id)
    PKT_CZ_ACK_SELECT_DEALTYPE  = 0x00c5,  // C->M  picked: type 0 = buy, 1 = sell
    PKT_ZC_PC_PURCHASE_ITEMLIST = 0x00c6,  // M->C  buy list  (var: 11B entries)
    PKT_ZC_PC_SELL_ITEMLIST     = 0x00c7,  // M->C  sell list (var: 10B entries)
    PKT_CZ_PC_PURCHASE_ITEMLIST = 0x00c8,  // C->M  buy these  (var: amount+nameid)
    PKT_CZ_PC_SELL_ITEMLIST     = 0x00c9,  // C->M  sell these (var: index+amount)
    PKT_ZC_PC_PURCHASE_RESULT   = 0x00ca,  // M->C  buy result code  (3B)
    PKT_ZC_PC_SELL_RESULT       = 0x00cb,  // M->C  sell result code (3B)

    // Inventory lists (sent on map entry). The sell list above references items by
    // inventory index, so we keep an index->nameid map from these to name/icon them.
    PKT_ZC_EQUIPMENT_ITEMLIST = 0x00a4,  // M->C  equippable items (var: 20B entries)
    PKT_ZC_INVENTORY_LIST     = 0x01ee,  // M->C  stackable items  (var: 18B entries)
    PKT_ZC_ITEM_ADD           = 0x00a0,  // M->C  an item entered the bag (23B)
    PKT_ZC_ITEM_ENTRY         = 0x009d,  // M->C  item already lying on the ground (17B, on view enter)
    PKT_ZC_ITEM_FALL          = 0x009e,  // M->C  item just dropped onto the ground (17B, fall anim)
    PKT_CZ_ITEM_PICKUP        = 0x009f,  // C->M  pick up a ground item (object id), 6B
    PKT_ZC_ITEM_DISAPPEAR     = 0x00a1,  // M->C  a ground item was removed/picked up (object id), 6B
    PKT_ZC_DELETE_ITEM        = 0x00af,  // M->C  an item was consumed/removed (index + amount), 6B
    PKT_CZ_USE_ITEM           = 0x00a7,  // C->M  use/consume a bag item (index + accountID), 8B
    PKT_ZC_USE_ITEM_ACK       = 0x00a8,  // M->C  use result (only on FAILURE for ver>=3): index+amt+ok, 7B
    PKT_ZC_USE_ITEM_ACK2      = 0x01c8,  // M->C  use OK (ver>=3): index@2 nameid@4 aid@6 amount@10 ok@12, 13B
    PKT_CZ_ITEM_THROW         = 0x00a2,  // C->M  drop a bag item onto the floor (index + amount), 6B
    PKT_CZ_REQ_WEAR_EQUIP     = 0x00a9,  // C->M  equip a bag item (index + EQP_* location), 6B
    PKT_CZ_REQ_ITEMCOMPOSITION_LIST = 0x017a,  // C->M  double-click a card: ask which items it fits (idx+2), 4B
    PKT_ZC_ITEMCOMPOSITION_LIST     = 0x017b,  // M->C  list of equippable inv indices (+2) the card can slot, -1
    PKT_CZ_REQ_ITEMCOMPOSITION      = 0x017c,  // C->M  insert card: equip idx(+2) + card idx(+2), 6B
    PKT_ZC_ACK_ITEMCOMPOSITION      = 0x017d,  // M->C  ack: equip idx(+2) + card idx(+2) + flag(0=ok,1=fail), 7B
    PKT_ZC_EQUIP_ACK          = 0x00aa,  // M->C  equip result (index + worn location + ok), 7B
    PKT_CZ_REQ_TAKEOFF_EQUIP  = 0x00ab,  // C->M  unequip a worn item (index), 4B
    PKT_ZC_TAKEOFF_ACK        = 0x00ac,  // M->C  unequip result (index + location + ok), 7B
    PKT_ZC_ACK_ITEMREFINING   = 0x0188,  // M->C  refine result: result(2) index(2) refine(2), 8B
    // --- Audit follow-up (S. "всё делать"): server-sent packets that were framed+skipped. ---
    PKT_ZC_ACK_WEAPONREFINE   = 0x0223,  // M->C  refine result MESSAGE: result(L)@2 nameid(W)@6, 8B
    PKT_ZC_ARROW_FAIL         = 0x013b,  // M->C  arrow-equip fail: type(W)@2, 4B
    PKT_ZC_TELEPORT_MSG       = 0x0189,  // M->C  skill teleport/memo msg: flag(W)@2, 4B
    PKT_ZC_PRODUCE_EFFECT     = 0x018f,  // M->C  produce/forge result: flag(W)@2 nameid(W)@4, 6B
    PKT_ZC_REPAIR_EFFECT      = 0x01fe,  // M->C  repair result: nameid(W)@2 flag(B)@4, 5B
    PKT_ZC_HOM_FOOD           = 0x022f,  // M->C  homun feed result: fail(B)@2 foodid(W)@3, 5B
    PKT_ZC_MVP_EXP            = 0x010b,  // M->C  MVP exp reward: exp(L)@2, 6B
    PKT_ZC_ACK_REQNAME_BYGID  = 0x0194,  // M->C  name-by-charid: char_id(L)@2 name.24@6, 30B
    PKT_ZC_HOM_SKILLUP        = 0x0239,  // M->C  homun skill up: skillnum(W)@2 lv(W)@4 sp(W)@6 range(W)@8 up(B)@10, 11B
    PKT_ZC_GUILD_CREATE_ACK   = 0x0167,  // M->C  guild-create result: flag(B)@2, 3B
    PKT_ZC_GUILD_INVITE_ACK   = 0x0169,  // M->C  guild-invite result: flag(B)@2, 3B
    PKT_ZC_GUILD_BROKEN       = 0x015e,  // M->C  guild disbanded: reason(L)@2, 6B
    PKT_ZC_GUILD_MASTER_MEMBER= 0x014e,  // M->C  you-are master/member: mode(L)@2 (0xd7=master), 6B
    PKT_ZC_GUILD_REQ_ALLIANCE = 0x0171,  // M->C  alliance request: account_id(L)@2 name.24@6, 30B
    PKT_ZC_GUILD_ALLIANCE_ACK = 0x0173,  // M->C  alliance result: flag(L)@2, 6B
    PKT_ZC_GUILD_OPPOSITION_ACK=0x0181,  // M->C  opposition result: flag(B)@2, 3B
    PKT_ZC_GUILD_DEL_ALLIANCE = 0x0184,  // M->C  alliance removed: guild_id(L)@2 flag(L)@6, 10B
    PKT_ZC_GUILD_MEMBER_XY    = 0x01eb,  // M->C  guild member pos: account_id(L)@2 x(W)@6 y(W)@8, 10B

    // Kafra storage (and GUILD storage — identical packet ids; the server routes by storage_flag,
    // so the client codec is shared). "Open" = the server just sends the two lists + the count.
    PKT_ZC_STORE_ITEMLIST_NORMAL  = 0x01f0,  // M->C  storage stackable list (var: 18B entries)
    PKT_ZC_STORE_ITEMLIST_EQUIP   = 0x00a6,  // M->C  storage equippable list (var: 20B entries)
    PKT_ZC_STORE_COUNTINFO        = 0x00f2,  // M->C  storage capacity cur(2) max(2), 6B (opens window)
    PKT_ZC_ADD_ITEM_TO_STORE      = 0x00f4,  // M->C  an item entered storage, 21B
    PKT_ZC_DELETE_ITEM_FROM_STORE = 0x00f6,  // M->C  an item left storage (index + amount), 8B
    PKT_ZC_CLOSE_STORE            = 0x00f8,  // M->C  storage session closed, 2B
    PKT_CZ_MOVE_ITEM_BODY_TO_STORE = 0x00f3, // C->M  inventory -> storage (index + amount), 8B
    PKT_CZ_MOVE_ITEM_STORE_TO_BODY = 0x00f5, // C->M  storage -> inventory (index + amount), 8B
    PKT_CZ_CLOSE_STORE            = 0x00f7,  // C->M  close storage, 2B

    // Merchant cart (Alt+W). Same per-item record layout as storage/inventory; cart index = slot+2.
    PKT_ZC_CART_ITEMLIST_NORMAL  = 0x01ef,  // M->C  cart stackable list (var: 18B entries)
    PKT_ZC_CART_ITEMLIST_EQUIP   = 0x0122,  // M->C  cart equippable list (var: 20B entries)
    PKT_ZC_ADD_ITEM_TO_CART      = 0x0124,  // M->C  item entered the cart, 21B (reuse decodeStorageAdd)
    PKT_ZC_DELETE_ITEM_FROM_CART = 0x0125,  // M->C  item left the cart (index + amount), 8B
    PKT_ZC_CART_CLEAR            = 0x012b,  // M->C  cart emptied, 2B
    PKT_CZ_MOVE_ITEM_BODY_TO_CART  = 0x0126, // C->M  inventory -> cart (index + amount), 8B
    PKT_CZ_MOVE_ITEM_CART_TO_BODY  = 0x0127, // C->M  cart -> inventory, 8B
    PKT_CZ_MOVE_ITEM_CART_TO_STORE = 0x0128, // C->M  cart -> storage, 8B
    PKT_CZ_MOVE_ITEM_STORE_TO_CART = 0x0129, // C->M  storage -> cart, 8B

    // Public chat: send a line, hear others, and get our own line echoed back.
    PKT_CZ_REQUEST_CHAT      = 0x008c,  // C->M  send public chat ("Name : text\0")
    PKT_ZC_NOTIFY_CHAT       = 0x008d,  // M->C  another unit's chat (gid + msg)
    PKT_ZC_NOTIFY_PLAYERCHAT = 0x008e,  // M->C  our own chat echoed (msg)
    PKT_CZ_WHISPER           = 0x0096,  // C->M  send a whisper/PM: len, target[24], message (var)
    PKT_ZC_WHISPER           = 0x0097,  // M->C  incoming whisper/PM: len, nick[24], message (var)
    PKT_ZC_ACK_WHISPER       = 0x0098,  // M->C  whisper result (3B): 0 ok, 1 offline, 2 ignored
    PKT_CZ_EMOTION           = 0x00bf,  // C->M  play an emoticon over my head: type.B (3B)
    PKT_CZ_REMEMBER_WARPPOINT = 0x011d, // C->M  /memo: save a Warp Portal memo point here (2B, no args)
    PKT_ZC_EMOTION           = 0x00c0,  // M->C  an emoticon over a unit: gid@2, type@6 (7B)
    // Right-click-a-player actions (the context menu): trade, add-friend, ignore.
    PKT_CZ_REQ_EXCHANGE_ITEM = 0x00e4,  // C->M  request a trade: targetAccountId (6B)
    PKT_CZ_SETTING_WHISPER_PC = 0x00cf, // C->M  block/unblock a player's whispers: name[24]+type (27B)
    PKT_CZ_ADD_FRIENDS       = 0x0202,  // C->M  send a friend request by name: name[24] (26B)
    // Friend list (#78). Formats verified against OpenKore uaRO (ServerType0).
    PKT_ZC_FRIENDS_LIST      = 0x0201,  // M->C  friend list on login: var, N*{AID.L CID.L name.24}
    PKT_CZ_DELETE_FRIENDS    = 0x0203,  // C->M  remove a friend: AID.L CID.L (10B)
    PKT_ZC_FRIENDS_STATE     = 0x0206,  // M->C  friend logon/logoff: AID.L CID.L state.B (11B; 0=online)
    PKT_ZC_REQ_ADD_FRIENDS   = 0x0207,  // M->C  incoming friend request: AID.L CID.L name.24 (34B)
    PKT_CZ_ACK_REQ_ADD_FRIENDS = 0x0208,// C->M  reply: AID.L CID.L type.L (14B; 1=accept, 0=reject)
    PKT_ZC_ADD_FRIENDS_LIST  = 0x0209,  // M->C  add-friend result: type.W AID.L CID.L name.24 (36B; 0=added)
    PKT_ZC_DELETE_FRIENDS    = 0x020a,  // M->C  a friend was removed: AID.L CID.L (10B)
    // Party (#78 part 2). Formats verified against OpenKore uaRO (ServerType0) + server packet_db.
    PKT_ZC_PARTY_ORGANIZE_RESULT = 0x00fa,  // M->C  create result: fail.B (3B)
    PKT_ZC_PARTY_LIST        = 0x00fb,  // M->C  full member list: len.W name.24 {AID.L name.24 map.16 leader.B offline.B}*
    PKT_ZC_PARTY_INVITE_RESULT = 0x00fd,// M->C  invite result: name.24 type.B (27B)
    PKT_ZC_PARTY_INVITE      = 0x00fe,  // M->C  incoming invite: AID.L name.24 (30B)
    PKT_ZC_PARTY_MEMBER_ADD  = 0x0104,  // M->C  a member added: AID.L role.L x.W y.W online.B partyName.24 charName.24 map.16 (79B)
    PKT_ZC_PARTY_MEMBER_LEAVE = 0x0105, // M->C  a member left: AID.L name.24 result.B (31B)
    PKT_ZC_PARTY_HP          = 0x0106,  // M->C  member HP: AID.L hp.W maxhp.W (10B)
    PKT_ZC_PARTY_LOCATION    = 0x0107,  // M->C  member position: AID.L x.W y.W (10B)
    PKT_ZC_PARTY_CHAT        = 0x0109,  // M->C  party chat: len.W AID.L msg.* (var)
    PKT_CZ_PARTY_CREATE      = 0x00f9,  // C->M  create party: name.24 (26B) -- CZ_MAKE_GROUP (PV7; the
                                        // newer 0x01e8 createparty2 is in a higher packet_ver block the
                                        // server doesn't accept from a PV7 client, so the party never formed)
    PKT_CZ_PARTY_INVITE      = 0x00fc,  // C->M  invite a player by account id: AID.L (6B)
    PKT_CZ_PARTY_INVITE_REPLY = 0x00ff, // C->M  reply to an invite: AID.L flag.L (10B; 1=accept, 0=reject)
    PKT_CZ_PARTY_LEAVE       = 0x0100,  // C->M  leave the party (2B)
    PKT_CZ_PARTY_KICK        = 0x0103,  // C->M  expel a member: AID.L name.24 (30B)
    PKT_CZ_PARTY_CHAT        = 0x0108,  // C->M  party chat: len.W msg.* (var)
    // Chat rooms (#78 part 3, Alt+C).
    PKT_CZ_CREATE_CHATROOM   = 0x00d5,  // C->M  create: len.W limit.W public.B password.8 title.* (var)
    PKT_CZ_CHATROOM_JOIN     = 0x00d9,  // C->M  join: chatId.L password.8 (14B)
    PKT_ZC_CHATROOM_INFO     = 0x00d7,  // M->C  a room exists over a unit: ownerID.L id.L limit.W users.W public.B title.* (var)
    PKT_ZC_CHATROOM_REMOVE   = 0x00d8,  // M->C  a room was dissolved: id.L (6B)
    PKT_ZC_CHATROOM_JOIN_ACK = 0x00da,  // M->C  join result: type.B (3B; 0=ok)
    PKT_ZC_CHATROOM_CREATED  = 0x00d6,  // M->C  your room was created (3B ack) -> you own it
    PKT_ZC_CHATROOM_ENTER    = 0x00db,  // M->C  you entered a room: len.W chatId.L { role.L name.24 }*
    PKT_ZC_CHATROOM_MEMBER_ADD = 0x00dc,// M->C  someone joined the room: users.W name.24 (28B)
    PKT_ZC_CHATROOM_MEMBER_LEAVE = 0x00dd, // M->C  someone left: users.W name.24 flag.B (29B)
    PKT_CZ_CHATROOM_LEAVE    = 0x00e3,  // C->M  leave the current room (2B)
    // Guild (#78 part 4, Alt+G). Formats verified against OpenKore uaRO (ServerType0) + packet_db.
    PKT_ZC_GUILD_NAME        = 0x016c,  // M->C  own guild id/name: guildID.L emblemID.L mode.L x5 name.24 (43B)
    PKT_ZC_GUILD_MEMBERS     = 0x0154,  // M->C  member list (var): len.W { 104B per member }
    PKT_ZC_GUILD_INFO        = 0x01b6,  // M->C  guild info: ID.L lv,conMem,maxMem,avg,exp,expN,tax,t1,t2.L emblem.L name.24 master.24 castles.20 (114B)
    PKT_ZC_GUILD_NOTICE      = 0x016f,  // M->C  notice: subject.60 body.120 (182B)
    PKT_ZC_GUILD_INVITE      = 0x016a,  // M->C  incoming invite: guildID.L name.24 (30B)
    PKT_ZC_GUILD_MEMBER_ONLINE = 0x016d,// M->C  member on/offline: AID.L CID.L online.L (14B)
    PKT_CZ_GUILD_INVITE_REPLY = 0x016b, // C->M  reply: guildID.L flag.L (10B; 1=accept, 0=reject)
    PKT_CZ_GUILD_INVITE_REQ  = 0x0168,  // C->M  invite a player: targetAID.L myAID.L myCID.L (14B)
    PKT_CZ_GUILD_LEAVE       = 0x0159,  // C->M  leave: guildID.L AID.L CID.L reason.40 (54B)
    PKT_CZ_GUILD_CREATE      = 0x0165,  // C->M  create: charID.L name.24 (30B)
    PKT_CZ_REQ_GUILD_MENU    = 0x014f,  // C->M  request a guild page: type.L (6B; 0=info, 1=members)
    PKT_CZ_GUILD_CHANGE_POSITION = 0x0161,  // C->M  change a position: len.W { id.L mode.L rank.L tax.L name.24 }
    PKT_CZ_GUILD_CHANGE_MEMBER_POS = 0x0155, // C->M  change a member's rank: len.W { AID.L CID.L posId.L }
    PKT_ZC_GUILD_POSITIONS   = 0x0160,  // M->C  position config (var): len.W { id.L mode.L ranking.L tax.L }
    PKT_ZC_GUILD_EXPEL_LIST  = 0x0163,  // M->C  expulsion list (var): len.W { name.24 reason.40 }
    PKT_ZC_GUILD_POSITION_NAMES = 0x0166,  // M->C  position names (var): len.W { id.L name.24 }
    PKT_ZC_GUILD_SKILLS      = 0x0162,  // M->C  guild skill tree (var): len.W skillpoint.W { 37B per skill }
    PKT_ZC_GUILD_POSITION_CHANGED = 0x0174,  // M->C  a position changed: posId.L mode.L posId.L tax.L name.24 (44B)
    PKT_ZC_GUILD_MEMBER_POS_CHANGED = 0x0156, // M->C  a member's rank changed: AID.L CID.L posId.L (16B)
    PKT_ZC_GUILD_ALLY_LIST   = 0x014c,  // M->C  ally/enemy list (var): len.W { opposition.L guildId.L name.24 }
    PKT_CZ_GUILD_NOTICE      = 0x016e,  // C->M  set the notice: guildID.L subject.60 body.120 (186B)
    // Mercenary status window (#79). Server-enabled for PV7 (see x64 clif.c PACKETVER_MERC_WINDOW).
    PKT_ZC_MER_INIT          = 0x029b,  // M->C  merc stats window (80B)
    PKT_ZC_MER_PAR_CHANGE    = 0x02a2,  // M->C  a merc stat changed: type.W value.L (8B)
    PKT_ZC_MER_SKILLINFO_LIST = 0x029d, // M->C  merc skill list (var)
    PKT_CZ_MER_COMMAND       = 0x029f,  // C->M  merc command: option.B (5B); option 2 = dismiss
    PKT_ZC_PROPERTY_HOMUN    = 0x022e,  // M->C  homunculus stat window (71B)
    PKT_ZC_HOSKILLINFO_LIST  = 0x0235,  // M->C  homunculus skill tree (var)
    PKT_ZC_CASH_TIME_COUNTER = 0x0298,  // M->C  rental item remaining time: nameid.W seconds.L (8B)
    PKT_ZC_CASH_ITEM_DELETE  = 0x0299,  // M->C  rental item expired -> delete: index.W nameid.W (6B)
    // Player trade (exchange) flow. M->C events + C->M replies.
    PKT_ZC_EXCHANGE_REQUEST  = 0x00e5,  // M->C  someone wants to trade: requester name[24] (26B)
    PKT_CZ_EXCHANGE_ACK      = 0x00e6,  // C->M  accept(3)/reject(4) the request: result (3B)
    PKT_ZC_EXCHANGE_ACK      = 0x00e7,  // M->C  trade start result: type (3B)
    PKT_CZ_EXCHANGE_ADD      = 0x00e8,  // C->M  put an item in: index(2)+amount(4) (8B)
    PKT_ZC_EXCHANGE_ADD      = 0x00e9,  // M->C  the other side put an item in: amount,nameid,refine,cards (19B)
    PKT_ZC_EXCHANGE_ADD_ACK  = 0x00ea,  // M->C  result of OUR add: index(2)+fail(1) (5B)
    PKT_CZ_EXCHANGE_LOCK     = 0x00eb,  // C->M  lock in my side (2B)
    PKT_ZC_EXCHANGE_LOCK     = 0x00ec,  // M->C  a side locked: who (0=you,1=other) (3B)
    PKT_CZ_EXCHANGE_CANCEL   = 0x00ed,  // C->M  cancel the trade (CZ_CANCEL_EXCHANGE_ITEM, 2B)
    PKT_ZC_EXCHANGE_CANCEL   = 0x00ee,  // M->C  trade cancelled (2B)
    PKT_CZ_EXCHANGE_EXEC     = 0x00ef,  // C->M  confirm/commit the trade (CZ_EXEC_EXCHANGE_ITEM, 2B)
    PKT_ZC_EXCHANGE_DONE     = 0x00f0,  // M->C  trade completed: fail (3B)

    // Skill ground units (warp portals, traps, sanctuary, ...) appear/disappear.
    PKT_ZC_SKILL_ENTRY     = 0x011f,  // M->C  a skill unit appeared (gid + cell + unit type), 16B
    PKT_ZC_SKILL_ENTRY2    = 0x01c9,  // M->C  skill unit appeared w/ graffiti string, 97B (first 15B == 0x11f)
    PKT_ZC_MONSTER_INFO    = 0x018c,  // M->C  Sense/Estimation: a mob's class/lv/size/hp/def/race/ele + 9 ele mods, 29B
    PKT_ZC_AUTOSPELL       = 0x01cd,  // M->C  Sage Autospell: up to 7 castable bolt skill ids to pick from, 30B
    PKT_ZC_SET_MAPCELL     = 0x0192,  // M->C  Ice Wall: set/clear a cell's gat type (x,y,type,mapname), 24B
    PKT_ZC_STARSKILL       = 0x020e,  // M->C  Star Gladiator Feel/Hate + TK mission: name(24)+id+level+type, 32B
    PKT_ZC_SKILL_DISAPPEAR = 0x0120,  // M->C  a skill unit was removed (gid), 6B
    PKT_ZC_SKILLINFO_UPDATE = 0x010e, // M->C  one skill changed: id.W lv.W sp.W range.W up.B (11B)
    PKT_ZC_SKILL_FAIL      = 0x0110,  // M->C  a skill use failed: skillId.W btype.L ok.B cause.B (10B)
    PKT_ZC_SKILLINFO_LIST  = 0x010f,  // M->C  the player's learned skill list (37B entries)
    PKT_CZ_UPGRADE_SKILLLEVEL = 0x0112,  // C->M  raise a skill by one level: skillId.W (4B)
    PKT_CZ_USE_SKILL       = 0x0113,  // C->M  cast a skill on a target (lv, id, targetAID), 10B
    PKT_CZ_USE_SKILL_TOGROUND = 0x0116,  // C->M  cast a ground/AoE skill at a cell (lv, id, x, y), 10B

    // Actor (unit) entries — the layouts the server compiles at PACKETVER 7.
    // The unit type is implicit in the id (no objecttype byte on the wire).
    PKT_ZC_NPC_STANDENTRY = 0x0078,  // M->C  NPC/mob standing  (54B)
    PKT_ZC_NPC_NEWENTRY2  = 0x0079,  // M->C  NPC/mob SPAWN at PV7 (53B) -- 0x78 minus the trailing byte
    PKT_ZC_NPC_NEWENTRY   = 0x007c,  // M->C  NPC/mob spawn     (41B; newer PACKETVERs only)
    PKT_ZC_VANISH         = 0x0080,  // M->C  unit left view    (7B)
    PKT_ZC_CLASS_CHANGE   = 0x01b0,  // M->C  a unit's view class changed in place (mob transform / hatch: id(4)+type(1)+class(4), 11B)
    PKT_ZC_ITEMIDENTIFY_LIST = 0x0177,  // M->C  Magnifier: list of unidentified inventory indices (var)
    PKT_ZC_AUTORUN_SKILL  = 0x0147,  // M->C  item-triggered skill: client must auto-cast it (skillid@2, lv@8), 39B
    PKT_ZC_ACK_ITEMIDENTIFY = 0x0179,  // M->C  identify result: index(2)+flag(1) -- flag 0 = success, 5B
    PKT_ZC_EQUIP_ARROW    = 0x013c,  // M->C  which arrow/ammo is equipped: inv index+2 (4B)
    PKT_ZC_MAP_PROPERTY   = 0x0199,  // M->C  map pvp/gvg property: type(2) -- 0 normal, 1 PvP, 3 GvG (4B)
    PKT_ZC_PC_STANDENTRY  = 0x022a,  // M->C  player standing    (58B)
    PKT_ZC_PC_NEWENTRY    = 0x022b,  // M->C  player spawn       (57B)
    PKT_ZC_PC_MOVEENTRY   = 0x022c,  // M->C  player/unit walking (64B)
    // Quest journal (#136), old ZC layout (PACKETVER >= 20080000, our serverType 8 / ver 20).
    PKT_ZC_ALL_QUEST_LIST     = 0x02b1,  // M->C  quest id + state list      (var, id.L state.B)
    PKT_ZC_ALL_QUEST_MISSION  = 0x02b2,  // M->C  quests + objectives + times (var, 104B/quest)
    PKT_ZC_ADD_QUEST          = 0x02b3,  // M->C  one quest added            (107B fixed)
    PKT_ZC_DEL_QUEST          = 0x02b4,  // M->C  one quest removed          (6B, id.L)
    PKT_ZC_UPDATE_MISSION_HUNT = 0x02b5, // M->C  objective kill-count update (var, 12B/objective)
    PKT_ZC_ACTIVE_QUEST       = 0x02b7,  // M->C  quest active/inactive toggle (7B, id.L active.B)
    PKT_ZC_STORE_ENTRY    = 0x0131,  // M->C  a vending shop opened (gid + title[80], 86B)
    PKT_ZC_DISAPPEAR_ENTRY = 0x0132, // M->C  a vending shop closed (gid, 6B)
    PKT_CZ_REQ_BUY_FROMMC = 0x0130,  // C->M  open a vendor's shop  (vendorAID, 6B)
    PKT_ZC_PC_PURCHASE_ITEMLIST_FROMMC = 0x0133, // M->C  vendor's wares (8B hdr + 22B*)
    PKT_CZ_PC_PURCHASE_ITEMLIST_FROMMC = 0x0134, // C->M  buy from a vendor (8B hdr + 4B*)
    PKT_ZC_PC_PURCHASE_RESULT_FROMMC   = 0x0135, // M->C  vending buy result (7B)
    PKT_ZC_DELETEITEM_FROM_MCSTORE     = 0x0137, // M->C  item sold from a shop (6B)
    PKT_ZC_PC_PURCHASE_MYITEMLIST      = 0x0136, // M->C  MY open shop's wares (8B hdr + 22B*, index/amount swapped vs 0x133)
    PKT_ZC_OPENSTORE      = 0x012d,  // M->C  open the vending setup (maxItems, 4B) — Vending skill used
    PKT_CZ_REQ_OPENSTORE  = 0x01b2,  // C->M  open my shop (name[80] + flag + {idx,amt,price}*, var)
    PKT_CZ_REQ_CLOSESTORE = 0x012e,  // C->M  close my shop (2B)
};

// The map-server emits Cyrillic text (names, chat, NPC dialog) in Windows-1251, but we render
// UTF-8, so Russian showed as '?' (S.). Convert CP1251 -> UTF-8. ASCII (< 0x80) passes through,
// so it is safe to run on every server string. Applied inside the text decoders below.
std::string cp1251ToUtf8(const std::string& s);
// Inverse: UTF-8 -> Windows-1251 for OUTGOING text (the server speaks cp1251). Unmappable
// codepoints become '?'. The result is usually SHORTER than the input (2-byte Cyrillic -> 1 byte),
// so callers must size packet length fields from the CONVERTED string, not the original.
std::string utf8ToCp1251(const std::string& s);

// ZC_STORE_ENTRY (0x131): a player opened a vending shop. Fills gid + the shop title.
// Returns the packet length (86) or 0 if short.
usize decodeStoreEntry(const u8* p, usize n, u32& gid, std::string& title);

// ---- decoded structures ---------------------------------------------------
struct CharServer {
    std::string name;   // display name (server[i].name, 20B)
    std::string ip;     // dotted IPv4 ("a.b.c.d")
    u16 port = 0;
    u16 users = 0;
    u16 type = 0;       // maintenance/type flag
    u16 newFlag = 0;    // "new" flag
};

struct AcceptLogin {
    u32 loginId1 = 0;
    u32 accountId = 0;
    u32 loginId2 = 0;
    u8 sex = 0;
    std::vector<CharServer> servers;
};

// A character row from HC_ACCEPT_ENTER (0x6b). Carries the fields the
// char-select screen needs; the rest of mmo_char_tobuf is skipped.
struct CharInfo {
    u32 charId = 0;
    u32 baseExp = 0;
    u32 zeny = 0;
    u32 jobExp = 0;
    u32 jobLevel = 0;
    u16 statusPoint = 0;
    u16 hp = 0, maxHp = 0, sp = 0, maxSp = 0, speed = 0;
    u16 class_ = 0;
    u16 hair = 0;
    u16 weapon = 0;
    u16 baseLevel = 0;
    u16 skillPoint = 0;
    u16 headBottom = 0, shield = 0, headTop = 0, headMid = 0;
    u16 hairColor = 0, clothesColor = 0;
    std::string name;
    u8 str = 0, agi = 0, vit = 0, int_ = 0, dex = 0, luk = 0;
    u16 slot = 0;  // char_num
};

struct ZoneServer {
    u32 charId = 0;
    std::string mapName;  // e.g. "prontera.gat"
    std::string ip;       // dotted IPv4
    u16 port = 0;
};

struct MapAuth {
    u32 tick = 0;
    u16 x = 0, y = 0;  // spawn position in GAT cells
    u8 dir = 0;
};

// ZC_NOTIFY_PLAYERMOVE (0x87): our own walk was accepted, src -> dst cells.
struct MoveData {
    u32 tick = 0;
    u16 fromX = 0, fromY = 0, toX = 0, toY = 0;
};

// ZC_NOTIFY_MOVETAACK / clif_movetoattack (0x139): the server tells US (the player)
// to close on a target before attacking — the player approach is client-driven, the
// server only chases monsters itself. We walk to within `range` cells and re-attack.
struct MoveToAttack {
    u32 gid = 0;                 // the target we are attacking
    u16 x = 0, y = 0;            // target cell (server coords)
    u16 selfX = 0, selfY = 0;    // our cell at send time (server coords)
    u16 range = 0;               // our weapon's attack range, in cells
};

// ZC_NPCACK_MAPMOVE (0x91, same server) / ZC_NPCACK_SERVERMOVE (0x92, other
// server). On 0x92 `newServer` is true and ip/port point at the new map-server.
struct MapChange {
    std::string mapName;  // e.g. "prontera.gat"
    u16 x = 0, y = 0;
    bool newServer = false;
    std::string ip;  // dotted IPv4 (0x92 only)
    u16 port = 0;
};

// Crafting pick-lists (#1): the server opens one of four item-selection menus and the
// client answers with the chosen entry. All four share a [id.W len.W then N entries]
// variable frame; only the stride/fields differ. decodeCraftList normalises them.
enum class CraftKind : u8 { Produce, Arrow, Repair, Refine };
struct CraftItem {
    u16 nameid = 0;  // item id (for the display name + produce/arrow reply)
    u16 index = 0;   // inventory index carried by the packet (repair/refine reply echoes it)
};
struct CraftList {
    CraftKind kind = CraftKind::Produce;
    std::vector<CraftItem> items;
};
// 0x18d produce-mix / 0x1ad arrow-craft / 0x1fc repair / 0x221 refine. Returns the frame
// length consumed, or 0 if the id isn't a craft list / the frame is short.
usize decodeCraftList(const u8* p, usize n, CraftList& out);
// The matching reply: 0x18e producemix (nameid + 3 auto materials) / 0x1ae selectarrow
// (nameid) / 0x1fd repairitem (index) / 0x222 weaponrefine (index). Lengths pinned to
// packet_ver<=20 (10/4/4/6). (S.)
std::vector<u8> buildCraftReply(CraftKind kind, const CraftItem& it);

// One on-map actor (other player, NPC or monster), decoded from a unit-entry
// packet. Whichever fields the packet omits stay 0. `pc` selects the sprite
// composition path (player body+head vs. a single NPC/mob sprite).
struct ActorEntry {
    u32 gid = 0;                  // object / account id (unique key)
    u32 option = 0;              // status option bits (OPTION_RIDING 0x20, etc.)
    u16 opt1 = 0;                // body ailment (1 stone, 2 freeze, 3 stun, 4 sleep) — sprite tint
    u16 opt2 = 0;                // health-state bits (poison/curse/blind) — sprite tint
    u16 class_ = 0;              // job id (PC) or sprite id (NPC/mob)
    u16 level = 0;               // unit level (0 if the packet omits it, e.g. newentry)
    u16 speed = 150;             // walk speed, ms/cell (from the entry packet; 150 = default)
    u16 hair = 0;
    u16 weapon = 0, shield = 0;
    u16 headBottom = 0, headTop = 0, headMid = 0;
    u16 hairColor = 0, clothesColor = 0;
    u32 guildId = 0;            // owning guild (0 = none); keys the emblem cache
    u16 emblemId = 0;           // guild emblem version (0 = no emblem); bumps on emblem change
    u16 x = 0, y = 0;            // current cell
    u16 toX = 0, toY = 0;        // walk destination (== x,y when standing)
    u8 dir = 0;                  // body facing (0..7)
    u8 sex = 0;
    bool pc = false;            // player sprite (0x22a/b/c) vs NPC/mob (0x78/0x7c)
    bool walking = false;       // decoded from a move-entry packet
};

// ZC_GUILD_EMBLEM_IMG (0x0152): a guild's emblem bitmap, requested by guild id. `image`
// is the raw payload — a 24x24 BMP, either raw ("BM"...) or zlib-compressed (0x78...),
// with magenta (0xff00ff) as the transparent key. Cached by guildId; emblemId is the
// version, so a member whose entry carries a newer emblemId triggers a re-request.
struct GuildEmblem {
    u32 guildId = 0;
    u32 emblemId = 0;
    std::vector<u8> image;
};

// ---- framing helper -------------------------------------------------------
// First packet id in the buffer, or 0 if fewer than 2 bytes are present.
u16 peekId(const u8* p, usize n);
inline u16 peekId(const std::vector<u8>& v) { return peekId(v.data(), v.size()); }

// Classify the packet at the front of a receive buffer using the server
// packet_db length table, so the reader consumes exactly one packet and can skip
// ids it does not decode without losing stream sync.
enum class Frame { Need, Unknown, Ready };
struct FrameInfo {
    Frame status = Frame::Need;
    usize length = 0;  // full packet length (Ready); a size hint (Need)
    u16 id = 0;
};
FrameInfo nextPacket(const u8* p, usize n);

// ---- builders (client -> server) ------------------------------------------
std::vector<u8> buildCALogin(u32 version, const std::string& user, const std::string& pass,
                             u8 clientType);
std::vector<u8> buildCHEnter(u32 accountId, u32 loginId1, u32 loginId2, u8 sex, u16 clientType);
std::vector<u8> buildCharSelect(u8 slot);
// CH_DELETE_CHAR (0x68): char_id + 40-byte email. On default-email accounts the
// server accepts "a@a.com" or "" — what we send when there is no email UI.
std::vector<u8> buildCharDelete(u32 charId, const std::string& email);
// CH_MAKE_CHAR (0x67): create a character. Stats must each be 1..9, sum to 30, with the paired
// sums str+int / agi+luk / vit+dex each <= 10 (5/5/5/5/5/5 is the always-valid default).
std::vector<u8> buildMakeChar(const std::string& name, u8 str, u8 agi, u8 vit, u8 int_, u8 dex,
                              u8 luk, u8 slot, u16 hairStyle, u16 hairColor);
std::vector<u8> buildWantToConnection(u32 accountId, u32 charId, u32 loginId1, u32 tick, u8 sex);
std::vector<u8> buildLoadEndAck();
std::vector<u8> buildTickSend(u32 tick);
std::vector<u8> buildWalkRequest(u16 x, u16 y);  // CZ_REQUEST_MOVE (0x85)
std::vector<u8> buildNameRequest(u32 gid);       // CZ_REQNAME (0x94): ask for a unit's name
std::vector<u8> buildGuildEmblemRequest(u32 guildId);  // CZ_REQ_GUILD_EMBLEM_IMG (0x151)
// CZ_REQUEST_ACT (0x89): act on a target. action 0 = single attack, 7 = continuous
// (the server keeps attacking until the target dies or we do something else).
std::vector<u8> buildAttack(u32 targetGid, u8 action);
std::vector<u8> buildNpcContact(u32 gid);          // CZ_CONTACTNPC (0x90): click an NPC
std::vector<u8> buildNpcNext(u32 gid);             // CZ_REQ_NEXT (0xb9): "next" button
std::vector<u8> buildNpcClose(u32 gid);            // CZ_CLOSE_DIALOG (0x146): "close" button
std::vector<u8> buildNpcMenu(u32 gid, u8 choice);  // CZ_CHOOSE_MENU (0xb8): 1-based pick (0xff cancel)
std::vector<u8> buildNpcInputNum(u32 gid, int value);            // CZ_INPUT_EDITDLG (0x143): number box result
std::vector<u8> buildNpcInputStr(u32 gid, const std::string& s); // CZ_INPUT_EDITDLGSTR (0x1d5): string box result
std::vector<u8> buildRestart(u8 type);             // CZ_RESTART (0xb2): 0 = to save point, 1 = resurrect
std::vector<u8> buildRemoveOption();               // CZ_REQ_REMOVEOPTION (0x12a): drop cart/peco/falcon
// CZ_STATUS_CHANGE (0xbb): raise one base stat. statId is SP_STR..SP_LUK (13..18). The
// server raises the stat by exactly 1 per packet (it ignores the amount byte), so a
// Shift "+10" is sent as ten packets; the server clamps to the points actually available.
std::vector<u8> buildStatChange(u16 statId, u8 amount = 1);
// Inventory/equip actions from double-clicking an item, matching roBrowser's Inventory/Equipment.
// CZ_USE_ITEM (0xa7, 8B): consume a usable bag item. index = inventory slot, aid = our account id.
std::vector<u8> buildUseItem(u16 index, u32 accountId);
// CZ_REQ_WEAR_EQUIP (0xa9, 6B): equip a bag item. location = the item's EQP_* slot bitmask.
std::vector<u8> buildWearEquip(u16 index, u16 location);
std::vector<u8> buildUseCard(u16 cardIndex);                      // CZ_REQ_ITEMCOMPOSITION_LIST (0x17a)
std::vector<u8> buildInsertCard(u16 equipIndex, u16 cardIndex);   // CZ_REQ_ITEMCOMPOSITION (0x17c)
// ZC_ITEMCOMPOSITION_LIST (0x17b): each u16 is an inventory index (already +2 on the wire, which is
// exactly how InvItem.index is stored), so `out` values match InvItem.index verbatim — no adjustment.
usize decodeCardTargets(const u8* p, usize n, std::vector<u16>& out);
// CZ_REQ_TAKEOFF_EQUIP (0xab, 4B): unequip the worn item at inventory `index`.
std::vector<u8> buildTakeoffEquip(u16 index);
// CZ_ITEM_THROW (0xa2, 6B): drop `amount` of the bag item at inventory `index` onto the floor.
std::vector<u8> buildDropItem(u16 index, u16 amount);

// CZ_REQUEST_ACT (0x89) action types. Sit/stand reuse buildAttack with target 0
// (the server ignores the target id for these).
enum : u8 { ACT_ATTACK_ONCE = 0, ACT_SIT = 2, ACT_STAND = 3, ACT_ATTACK = 7 };

// NPC merchant shop (client -> server).
std::vector<u8> buildDealSelect(u32 npcId, u8 type);  // CZ_ACK_SELECT_DEALTYPE (0xc5): 0=buy, 1=sell
struct ShopBuyEntry { u16 nameid = 0; u16 amount = 0; };   // one line of a buy order
struct ShopSellEntry { u16 index = 0; u16 amount = 0; };   // one line of a sell order (index = inv slot+2)
std::vector<u8> buildBuyList(const std::vector<ShopBuyEntry>& items);    // CZ_PC_PURCHASE_ITEMLIST (0xc8)
std::vector<u8> buildSellList(const std::vector<ShopSellEntry>& items);  // CZ_PC_SELL_ITEMLIST (0xc9)

// Kafra storage (client -> server). index is the raw wire index we received: inventory uses slot+2
// (server subtracts 2), storage uses slot+1 (server subtracts 1); amount is a 4-byte count.
std::vector<u8> buildStorageStore(u16 invIndex, u32 amount);       // CZ_MOVE_ITEM_FROM_BODY_TO_STORE (0xf3)
std::vector<u8> buildStorageRetrieve(u16 storeIndex, u32 amount);  // CZ_MOVE_ITEM_FROM_STORE_TO_BODY (0xf5)
std::vector<u8> buildStorageClose();                               // CZ_CLOSE_STORE (0xf7)

// Merchant cart moves (client -> server). index = the raw wire index (inventory/cart = slot+2,
// storage = slot+1); amount is a 4-byte count. All 8 bytes: id(2) index(2) amount(4). The cart
// list/add/remove packets reuse decodeInventoryList / decodeStorageAdd / decodeStorageRemove.
std::vector<u8> buildCartAdd(u16 invIndex, u32 amount);       // CZ_MOVE_ITEM_FROM_BODY_TO_CART (0x126)
std::vector<u8> buildCartGet(u16 cartIndex, u32 amount);      // CZ_MOVE_ITEM_FROM_CART_TO_BODY (0x127)
std::vector<u8> buildCartToStore(u16 cartIndex, u32 amount);  // CZ_MOVE_ITEM_FROM_CART_TO_STORE (0x128)
std::vector<u8> buildStoreToCart(u16 storeIndex, u32 amount); // CZ_MOVE_ITEM_FROM_STORE_TO_CART (0x129)

// Public chat. The wire message must be "CharName : text" — the server validates the
// "<name> : " prefix (and a NUL terminator) and force-disconnects if it is wrong, so
// the builder composes it from the player's exact char name plus the typed text.
std::vector<u8> buildGlobalMessage(const std::string& name, const std::string& text);  // CZ_REQUEST_CHAT (0x8c)
std::vector<u8> buildWhisper(const std::string& target, const std::string& text);  // CZ_WHISPER (0x96)
std::vector<u8> buildTradeRequest(u32 targetAccountId);          // CZ_REQ_EXCHANGE_ITEM (0xe4)
std::vector<u8> buildAddFriend(const std::string& name);         // CZ_ADD_FRIENDS (0x202): add by name
std::vector<u8> buildIgnorePlayer(const std::string& name, bool block);  // CZ_SETTING_WHISPER_PC (0xcf)

// --- Player trade (exchange) flow ---
struct TradeAddItem {  // one item the other party placed in the trade (ZC_ADD_EXCHANGE_ITEM 0xe9)
    u32 amount = 0;
    u16 nameid = 0;
    u8 identify = 1;
    u8 refine = 0;
    u16 cards[4] = {0, 0, 0, 0};
};
// Friend list (#78). One friend on the list / in a packet.
struct FriendInfo {
    u32 accountId = 0;
    u32 charId = 0;
    std::string name;
    bool online = false;
};
usize decodeFriendList(const u8* p, usize n, std::vector<FriendInfo>& out);            // 0x201
usize decodeFriendState(const u8* p, usize n, u32& aid, u32& cid, bool& online);        // 0x206
usize decodeFriendRequest(const u8* p, usize n, u32& aid, u32& cid, std::string& name); // 0x207 (incoming)
usize decodeFriendAddResult(const u8* p, usize n, u16& type, u32& aid, u32& cid,
                            std::string& name);                                          // 0x209 (0=added)
usize decodeFriendRemoved(const u8* p, usize n, u32& aid, u32& cid);                    // 0x20a
std::vector<u8> buildFriendReply(u32 aid, u32 cid, bool accept);  // 0x208: 1=accept, 0=reject
std::vector<u8> buildDeleteFriend(u32 aid, u32 cid);             // 0x203: remove a friend

// Party (#78 part 2). One member of the player's party.
struct PartyMember {
    u32 accountId = 0;
    std::string name;
    std::string map;
    bool leader = false;
    bool online = false;
    i32 hp = 0;
    i32 maxHp = 0;
    u16 x = 0, y = 0;
};
usize decodePartyOrganizeResult(const u8* p, usize n, u8& fail);                        // 0xfa
usize decodePartyList(const u8* p, usize n, std::string& partyName,
                      std::vector<PartyMember>& out);                                    // 0xfb (full list)
usize decodePartyInvite(const u8* p, usize n, u32& aid, std::string& name);             // 0xfe (incoming)
usize decodePartyMemberAdd(const u8* p, usize n, PartyMember& m);                       // 0x104
usize decodePartyMemberLeave(const u8* p, usize n, u32& aid, std::string& name, u8& result); // 0x105
usize decodePartyHp(const u8* p, usize n, u32& aid, u16& hp, u16& maxHp);               // 0x106
usize decodePartyLocation(const u8* p, usize n, u32& aid, u16& x, u16& y);              // 0x107
std::vector<u8> buildPartyCreate(const std::string& name);     // 0x1e8: name + share flags
std::vector<u8> buildPartyInvite(u32 aid);                     // 0xfc: invite a player by account id
std::vector<u8> buildPartyInviteReply(u32 aid, bool accept);  // 0xff: 1=accept, 0=reject
std::vector<u8> buildPartyLeave();                            // 0x100
std::vector<u8> buildPartyKick(u32 aid, const std::string& name);  // 0x103

// Quest journal (#136). One kill/collect objective of a quest; `name` is the target mob's
// display name carried inline by the mission/add packets (0x2b2/0x2b3).
struct QuestObjective {
    u32 mobId = 0;
    u16 count = 0;  // current progress
    u16 need = 0;   // required (0x2b2/0x2b3 don't carry it; filled by 0x2b5 hunt updates)
    std::string name;
};
struct QuestInfo {
    u32 id = 0;
    u8 state = 0;       // 0 = inactive, 1 = active, 2 = complete
    u32 timeLimit = 0;  // unix expiry (0 = none)
    std::vector<QuestObjective> objectives;
};
// 0x2b2 ZC_ALL_QUEST_MISSION: the full quest log with objectives + timers (sent on login).
usize decodeQuestMissionList(const u8* p, usize n, std::vector<QuestInfo>& out);
// 0x2b1 ZC_ALL_QUEST_LIST: quest id + state pairs (sent on login, carries the active flag).
usize decodeQuestStateList(const u8* p, usize n, std::vector<std::pair<u32, u8>>& out);
// 0x2b3 ZC_ADD_QUEST: one quest just accepted.
usize decodeQuestAdd(const u8* p, usize n, QuestInfo& out);
// 0x2b4 ZC_DEL_QUEST: id of a quest just removed.
usize decodeQuestDelete(const u8* p, usize n, u32& id);
// 0x2b5 ZC_UPDATE_MISSION_HUNT: (questId, mobId, need, count) per objective touched.
struct QuestHunt { u32 questId = 0; u32 mobId = 0; u16 need = 0; u16 count = 0; };
usize decodeQuestHunt(const u8* p, usize n, std::vector<QuestHunt>& out);
// 0x2b7 ZC_ACTIVE_QUEST: server toggled a quest active/inactive.
usize decodeQuestActive(const u8* p, usize n, u32& id, bool& active);
// Chat rooms (#78 part 3, Alt+C).
std::vector<u8> buildCreateChatRoom(const std::string& title, u16 limit, bool isPublic,
                                    const std::string& password);  // 0xd5
struct ChatRoomInfo {
    u32 ownerId = 0;
    u32 id = 0;
    u16 limit = 0;
    u16 users = 0;
    bool isPublic = true;
    std::string title;
};
usize decodeChatRoomInfo(const u8* p, usize n, ChatRoomInfo& out);     // 0xd7
usize decodeChatRoomRemove(const u8* p, usize n, u32& id);             // 0xd8
usize decodeChatJoinResult(const u8* p, usize n, u8& type);           // 0xda
std::vector<u8> buildJoinChatRoom(u32 id, const std::string& password);  // 0xd9
usize decodeChatRoomEnter(const u8* p, usize n, u32& chatId, std::vector<std::string>& members);  // 0xdb
usize decodeChatRoomMemberAdd(const u8* p, usize n, u16& users, std::string& name);    // 0xdc
usize decodeChatRoomMemberLeave(const u8* p, usize n, u16& users, std::string& name);  // 0xdd
std::vector<u8> buildLeaveChatRoom();  // 0xe3

// Guild (#78 part 4, Alt+G).
struct GuildInfo {
    u32 id = 0;
    u32 level = 0;
    u32 members = 0;
    u32 maxMembers = 0;
    u32 avgLevel = 0;
    u32 exp = 0;
    u32 expNext = 0;
    u32 tax = 0;
    u32 emblemId = 0;
    std::string name;
    std::string master;
    std::string castles;  // territory string ("" = none taken)
};
struct GuildMember {
    u32 accountId = 0;
    u32 charId = 0;
    std::string name;
    u16 level = 0;
    u16 job = 0;
    u32 position = 0;
    bool online = false;
};
usize decodeGuildName(const u8* p, usize n, u32& id, std::string& name);          // 0x16c
usize decodeGuildInfo(const u8* p, usize n, GuildInfo& g);                        // 0x1b6
usize decodeGuildNotice(const u8* p, usize n, std::string& subject, std::string& body);  // 0x16f
usize decodeGuildMembers(const u8* p, usize n, std::vector<GuildMember>& out);    // 0x154
usize decodeGuildInvite(const u8* p, usize n, u32& guildId, std::string& name);   // 0x16a
usize decodeGuildMemberOnline(const u8* p, usize n, u32& aid, u32& cid, bool& online);  // 0x16d
std::vector<u8> buildGuildReply(u32 guildId, bool accept);   // 0x16b: 1=accept, 0=reject
std::vector<u8> buildGuildInvite(u32 targetAid, u32 myAid, u32 myCid);  // 0x168
std::vector<u8> buildGuildLeave(u32 guildId, u32 aid, u32 cid);  // 0x159
std::vector<u8> buildGuildCreate(u32 charId, const std::string& name);  // 0x165
std::vector<u8> buildGuildInfoRequest(u32 page);  // 0x14f: 0=info+emblem, 1=members+positions
std::vector<u8> buildGuildChangePosition(u32 posId, bool canInvite, bool canPunish, u32 ranking,
                                         u32 tax, const std::string& name);  // 0x161 (master only)
std::vector<u8> buildGuildChangeMemberPosition(u32 aid, u32 cid, u32 posId);  // 0x155 (master only)
std::vector<u8> buildGuildEmblem(const std::vector<u8>& bmp);  // 0x153: upload a guild emblem .bmp (master)
struct GuildPosition {
    u32 id = 0;
    std::string name;
    bool canInvite = false;
    bool canPunish = false;
    u32 ranking = 0;  // preserved so editing perms/tax doesn't reset it
    u32 tax = 0;
};
struct GuildExpel {
    std::string name;
    std::string reason;
};
struct GuildAlly {
    bool enemy = false;   // false = alliance, true = antagonist/opposition
    u32 guildId = 0;
    std::string name;
};
usize decodeGuildAllyList(const u8* p, usize n, std::vector<GuildAlly>& out);  // 0x14c
std::vector<u8> buildGuildNotice(u32 guildId, const std::string& subject, const std::string& body);  // 0x16e

// Mercenary status window (#79). 0x29b carries the full stat block.
struct MercInfo {
    u32 gid = 0;
    u16 atk = 0, matk = 0, hit = 0, cri = 0, def = 0, mdef = 0, flee = 0, aspd = 0, level = 0;
    std::string name;
    i32 hp = 0, maxHp = 0, sp = 0, maxSp = 0;
    u32 expireTime = 0;  // unix epoch when the contract ends
    u16 faith = 0;
    u32 summons = 0, kills = 0;
    u16 range = 0;
};
usize decodeMercInfo(const u8* p, usize n, MercInfo& out);             // 0x29b (80B)
usize decodeMercPar(const u8* p, usize n, u16& type, i32& value);     // 0x2a2 (8B)
std::vector<u8> buildMercCommand(u8 option);                          // 0x29f: option 2 = dismiss

// Homunculus status window (#79). ZC_PROPERTY_HOMUN (0x22e, 71B): the alchemist's
// homunculus stat block, sent SELF on map load / summon / stat change.
struct HomunInfo {
    std::string name;
    bool renamed = false, vaporized = false, dead = false;
    u16 level = 0, hunger = 0, intimacy = 0;  // intimacy already /100 by the server (0..10)
    u16 atk = 0, matk = 0, hit = 0, cri = 0, def = 0, mdef = 0, flee = 0, aspd = 0;
    i32 hp = 0, maxHp = 0, sp = 0, maxSp = 0;
    u32 exp = 0, expNext = 0;
    u16 skillPts = 0;
};
usize decodeHomunInfo(const u8* p, usize n, HomunInfo& out);          // 0x22e (71B)

// Homunculus skill tree (ZC_HOSKILLINFO_LIST 0x235, var; 37B per entry).
struct HomSkill {
    u16 id = 0;
    u16 inf = 0;      // skill target type (SKILL_INF_*)
    u16 level = 0;
    u16 sp = 0;
    u16 range = 0;
    std::string name; // internal skill name (ASCII), from the server's skill_db
    bool upgradeable = false;
};
usize decodeHomSkills(const u8* p, usize n, std::vector<HomSkill>& out);  // 0x235
std::vector<u8> buildHomMenu(u8 command);          // 0x22d (5B): 1=feed, 2=delete, 3=rest
std::vector<u8> buildHomName(const std::string& name);  // 0x231 (26B): rename homunculus (one-time)
std::vector<u8> buildPetMenu(u8 command);          // 0x1a1 (3B): 0 info,1 feed,2 perf,3 to egg
std::vector<u8> buildSelectPetEgg(u16 invIndex);   // 0x1a7 (4B): hatch the chosen egg
std::vector<u8> buildPetCatch(u32 mobGid);         // 0x19f (6B): taming target
std::vector<u8> buildPetRename(const std::string& name);  // 0x1a5 (26B)

// Rental items (#79). The server notifies the remaining time (ZC_CASH_TIME_COUNTER 0x298) and, when it
// runs out, deletes the item from the inventory (ZC_CASH_ITEM_DELETE 0x299).
usize decodeRentalTime(const u8* p, usize n, u16& nameid, u32& seconds);  // 0x298 (8B)
usize decodeRentalExpired(const u8* p, usize n, u16& invIndex, u16& nameid);  // 0x299 (6B): index is +2
std::vector<u8> buildHomAttack(u32 targetGid);     // 0x233 (11B): sic the homun on a target
std::vector<u8> buildHomMoveToMaster();            // 0x234 (6B): recall the homun to the master
std::vector<u8> buildHomMoveTo(u16 x, u16 y);      // 0x232 (9B): walk the homun to a cell (AI Move)
usize decodeGuildPositionNames(const u8* p, usize n, std::vector<GuildPosition>& out);  // 0x166
usize decodeGuildPositionInfo(const u8* p, usize n, std::vector<GuildPosition>& out);    // 0x160
usize decodeGuildExpelList(const u8* p, usize n, std::vector<GuildExpel>& out);          // 0x163
usize decodeGuildPositionChanged(const u8* p, usize n, u32& posId, u32& mode, u32& tax,
                                 std::string& name);                                      // 0x174
usize decodeGuildMemberPosChanged(const u8* p, usize n, u32& aid, u32& cid, u32& posId);  // 0x156
usize decodeTradeRequest(const u8* p, usize n, std::string& name);    // 0xe5: who wants to trade (name@2[24])
usize decodeTradeStart(const u8* p, usize n, u8& result);            // 0xe7: start result (type@2)
usize decodeTradeAdd(const u8* p, usize n, TradeAddItem& it);        // 0xe9: the other side added an item
usize decodeTradeAddAck(const u8* p, usize n, u16& index, u8& fail); // 0xea: result of OUR add
usize decodeTradeLock(const u8* p, usize n, u8& who);               // 0xec: a side locked (0=you,1=other)
usize decodeTradeDone(const u8* p, usize n, u8& fail);             // 0xf0: trade completed (fail@2)
std::vector<u8> buildTradeReply(bool accept);          // CZ_ACK_EXCHANGE_ITEM (0xe6): accept(3)/reject(4)
std::vector<u8> buildTradeAdd(u16 index, u32 amount);  // CZ_ADD_EXCHANGE_ITEM (0xe8): index(2)+amount(4)
std::vector<u8> buildTradeLock();     // CZ_CONCLUDE_EXCHANGE_ITEM (0xeb)
std::vector<u8> buildTradeConfirm();  // CZ_EXEC_EXCHANGE_ITEM (0xef) -- the "Trade" commit
std::vector<u8> buildTradeCancel();   // CZ_CANCEL_EXCHANGE_ITEM (0xed)

// ---- decoders (server -> client). return bytes consumed, 0 if incomplete --
usize decodeAcceptLogin(const u8* p, usize n, AcceptLogin& out);
usize decodeRefuseLogin(const u8* p, usize n, u8& code, std::string& banDate);
usize decodeNotifyBan(const u8* p, usize n, u8& code);
usize decodeCharList(const u8* p, usize n, std::vector<CharInfo>& out);
// HC_ACCEPT_MAKECHAR (0x6d): id(2) + one char record -> the freshly created character, so the
// char-select screen can add it in place. HC_REFUSE_MAKECHAR (0x6e): id(2) + reason(1).
usize decodeMakeCharAccept(const u8* p, usize n, CharInfo& out);
usize decodeMakeCharRefuse(const u8* p, usize n, u8& code);
usize decodeRefuseEnter(const u8* p, usize n, u8& code);
usize decodeZoneServer(const u8* p, usize n, ZoneServer& out);
usize decodeDeleteAccept(const u8* p, usize n);            // HC_ACCEPT_DELETECHAR (0x6f, 2B)
usize decodeDeleteRefuse(const u8* p, usize n, u8& reason);  // HC_REFUSE_DELETECHAR (0x70, 3B)
usize decodeMapAuthOk(const u8* p, usize n, MapAuth& out);  // ZC_ACCEPT_ENTER (0x73)
usize decodePlayerMove(const u8* p, usize n, MoveData& out);  // ZC_NOTIFY_PLAYERMOVE (0x87)
// ZC_NOTIFY_MOVE (0x86): another unit started walking. gid + WBUFPOS2 (src/dst) + tick.
usize decodeUnitMove(const u8* p, usize n, u32& gid, MoveData& out);
// ZC_STOPMOVE (0x88, 10B): clif_fixpos — the server snaps `id` to cell (x,y), used to cancel a
// walk and authoritatively correct position (on being hit / knockback). id@2 x@6 y@8.
usize decodeStopMove(const u8* p, usize n, u32& id, u16& x, u16& y);
// ZC_NOTIFY_MOVETOATTACK (0x139, 16B): close on a target before attacking.
usize decodeMoveToAttack(const u8* p, usize n, MoveToAttack& out);
// NPC dialog (var-length): ZC_SAY_DIALOG (0xb4) text / ZC_MENU_LIST (0xb7) ':'-list —
// same layout (len@2, gid@4, string@8). The caller knows which by the packet id.
usize decodeScriptText(const u8* p, usize n, u32& gid, std::string& text);
// ZC_WAIT_DIALOG (0xb5, next) / ZC_CLOSE_DIALOG (0xb6, close): gid@2 only, 6B.
usize decodeScriptGid(const u8* p, usize n, u32& gid);

// ZC_NOTIFY_ACT (0x8a): an attack/damage event. type 0 = normal hit; damage may be
// -1 when hidden. src/dst are gids.
struct ActDamage {
    u32 src = 0, dst = 0;
    u32 amotion = 0;  // src attack-motion ms (sdelay@14) — the attacker's swing timing
    u32 dmotion = 0;  // dst damage-motion ms (ddelay@18) — the victim's flinch/recoil duration
    i32 damage = 0;
    u8 type = 0;
    i32 damage2 = 0;  // left-hand damage (dual-wield / katar 2nd hit; 0 if single)
};
usize decodeAct(const u8* p, usize n, ActDamage& out);

// ZC_NOTIFY_SKILL2 (0x1de, 33B): a skill hit. skillId@2 src@4 dst@8 tick@12 sdelay@16 ddelay@20
// damage@24(L) lv@28 div@30 type@32. PACKETVER 7 sends this (32-bit damage) instead of 0x114.
struct SkillDamage {
    u16 skillId = 0;
    u32 src = 0, dst = 0;        // caster, target AIDs
    u32 sdelay = 0, ddelay = 0;  // caster attack-motion / target damage-motion ms
    i32 damage = 0;
    u16 level = 0, div = 0;
    u8 type = 0;
};
usize decodeSkillDamage(const u8* p, usize n, SkillDamage& out);

// ZC_USE_SKILL (0x11a, 15B): a no-damage skill (buff/heal). skillId@2 heal@4 dstAID@6 srcAID@10 fail@14.
struct SkillNoDamage {
    u16 skillId = 0;
    u16 heal = 0;  // heal amount (AL_HEAL etc.); 0 / level for buffs
    u32 dst = 0;   // target AID (the buffed/healed unit)
    u32 src = 0;   // caster AID
    u8 fail = 0;   // 0 = success
};
usize decodeSkillNoDamage(const u8* p, usize n, SkillNoDamage& out);

// ZC_NOTIFY_GROUNDSKILL (0x117, 18B): a ground/AoE skill placed at a cell. skillId@2 src@4 x@10 y@12.
struct GroundSkill {
    u16 skillId = 0;
    u32 src = 0;      // caster AID
    u16 x = 0, y = 0;  // GAT cell of the effect
};
usize decodeGroundSkill(const u8* p, usize n, GroundSkill& out);
// ZC_ACK_REQNAME (0x95, 30B) / ZC_ACK_REQNAMEALL (0x195, 102B): a unit's name. The
// name is at offset 6 (24 bytes) in both. `extra` gets the 0x195 field @30: a party
// name for players, or for a monster (show_mob_info) the "Lv. N | HP: P%" info string
// the server packs there. If `guild` is non-null and the reply is the long 0x195, it
// gets the guild name @54 (24 bytes). Both empty for the short 0x95 reply.
usize decodeNameAck(const u8* p, usize n, u32& gid, std::string& name, std::string& extra,
                    std::string* guild = nullptr);
usize decodeMapChange(const u8* p, usize n, MapChange& out);  // 0x91 (same) / 0x92 (other server)
// ZC_STATE_CHANGE3 (0x229): a unit's option bits changed (riding, cloaking, ...).
usize decodeStateChange(const u8* p, usize n, u32& gid, u32& option, u16& opt1, u16& opt2);
// ZC_CHANGE_DIRECTION (0x9c, 9B): gid@2, headDir@6, bodyDir@8 -- a unit turned in place.
usize decodeChangeDir(const u8* p, usize n, u32& gid, u8& dir);
// ZC_RECOVERY (0x13d, 6B): type@2 (SP_HP=5 / SP_SP=7), val@4 -- HP/SP restored, float it over self.
usize decodeRecovery(const u8* p, usize n, u16& type, u16& val);
// Player stat ids (SP_*) carried by ZC_PAR_CHANGE (0xb0). SP_SPEED (0) is the walk
// speed in ms per cell — lower is faster (mounts, buffs). The rest feed the status
// window: the six base stats, level/job, status points, zeny and weight. Values match
// the server's _sp enum (map.h).
enum : u16 {
    SP_SPEED = 0, SP_BASEEXP = 1, SP_JOBEXP = 2, SP_HP = 5, SP_MAXHP = 6, SP_SP = 7,
    SP_MAXSP = 8, SP_STATUSPOINT = 9, SP_BASELEVEL = 11, SP_SKILLPOINT = 12,
    SP_STR = 13, SP_AGI = 14, SP_VIT = 15, SP_INT = 16, SP_DEX = 17, SP_LUK = 18,
    SP_ZENY = 20, SP_NEXTBASEEXP = 22, SP_NEXTJOBEXP = 23, SP_WEIGHT = 24,
    SP_MAXWEIGHT = 25, SP_ASPD = 53, SP_JOBLEVEL = 55,
    // Raise-cost ids: the per-stat "points to next" carried by ZC_STATUS_CHANGE (0xbe) after
    // a raise. SP_USTR..SP_ULUK map 1:1 onto SP_STR..SP_LUK (subtract SP_USTR-SP_STR = 19).
    SP_USTR = 32, SP_UAGI = 33, SP_UVIT = 34, SP_UINT = 35, SP_UDEX = 36, SP_ULUK = 37
};
// Look ids (LOOK_*) carried by ZC_SPRITE_CHANGE (0x1d7). The player's OWN weapon
// arrives this way on login (the char-select list sends the item id, and 0 while
// riding) — for a weapon change the packet carries weapon@7 and shield@9.
enum : u8 {
    LOOK_BASE = 0, LOOK_HAIR = 1, LOOK_WEAPON = 2, LOOK_HEAD_BOTTOM = 3, LOOK_HEAD_TOP = 4,
    LOOK_HEAD_MID = 5, LOOK_HAIR_COLOR = 6, LOOK_CLOTHES_COLOR = 7, LOOK_SHIELD = 8, LOOK_SHOES = 9
};
// ZC_SPRITE_CHANGE2 (0x1d7, 11B): id(2) gid(4) type(1) val(2) val2(2).
usize decodeSpriteChange(const u8* p, usize n, u32& gid, u8& type, u16& val, u16& val2);
// ZC_PAR_CHANGE (0xb0, 8B): id(2) type(2) value(4). A single player stat update.
usize decodeParChange(const u8* p, usize n, u16& type, u32& value);
// ZC_STATUS_CHANGE_ACK (0xbc, 6B): id(2) statusID(2) result(1) value(1). The server's reply to a
// CZ_STATUS_CHANGE; on result != 0, `value` is the new base value of stat statusID (SP_STR..LUK).
usize decodeStatusChangeAck(const u8* p, usize n, u16& statusId, u8& ok, u8& value);
// ZC_STATUS_CHANGE (0xbe, 5B): id(2) statusID(2) value(1). A 1-byte stat update; the per-stat
// raise cost (SP_USTR..SP_ULUK) is pushed through here right after a successful raise.
usize decodeStatusChange(const u8* p, usize n, u16& statusId, u8& value);
// Status-effect indices (SI_*) carried by ZC_STATUS_EFFECT (0x196). Riding is the
// one the server pushes to the player's OWN client on login (clif_status_load),
// since the spawn packet that carries OPTION_RIDING is sent area-wide-except-self.
enum : u16 { SI_RIDING = 27, SI_FALCON = 28, SI_NIGHT = 160 };  // SI_NIGHT drives the server's day/night dim
// ZC_STATUS_EFFECT (0x196, 9B): id(2) type(2) aid(4) flag(1). flag 1 = on, 0 = off.
usize decodeStatusEffect(const u8* p, usize n, u32& gid, u16& type, u8& flag);

// ZC_NOTIFY_EFFECT (0x19b, 10B): id(2) aid(4) type(4). type 0 = base level up, 1 = job level up.
usize decodeNotifyEffect(const u8* p, usize n, u32& gid, u32& type);

// The bulk character status (ZC_STATUS 0x00bd, 44B) sent on login: the six base stats
// with their next-point cost, plus the derived combat stats. Base level / job level /
// zeny / weight are NOT in this packet — they arrive as separate ZC_PAR_CHANGE updates.
struct CharStatus {
    u16 statusPoint = 0;
    u8 str = 0, agi = 0, vit = 0, int_ = 0, dex = 0, luk = 0;        // the six base stats
    // Buff/equip bonus per stat (ZC_COUPLESTATUS 0x141); negative for debuffs (Decrease AGI). The
    // stat window shows base+bonus while the raise arrow still works off the base.
    i16 bonusStr = 0, bonusAgi = 0, bonusVit = 0, bonusInt = 0, bonusDex = 0, bonusLuk = 0;
    u8 needStr = 0, needAgi = 0, needVit = 0, needInt = 0, needDex = 0, needLuk = 0;  // up-cost
    u16 atk = 0, atk2 = 0, matkMax = 0, matkMin = 0;
    u16 def = 0, def2 = 0, mdef = 0, mdef2 = 0;
    u16 hit = 0, flee = 0, flee2 = 0, crit = 0;
    u16 aspd = 0;  // attack speed (0xbd @40); roBrowser shows ASPD in the stat window
};
// ZC_STATUS (0x00bd, 44B). Returns bytes consumed (44) or 0 if short.
usize decodeStatus(const u8* p, usize n, CharStatus& out);

// ZC_COUPLESTATUS (0x0141, 14B): type(4) base(4) bonus(4). Sent for SP_STR..SP_LUK whenever a base
// stat or its buff/equip bonus changes; bonus = battle_status - base (negative for debuffs).
struct CoupleStatus {
    u32 type = 0;   // SP_STR..SP_LUK (13..18)
    i32 base = 0;   // allocated base value
    i32 bonus = 0;  // buff/equip delta (may be negative)
};
usize decodeCoupleStatus(const u8* p, usize n, CoupleStatus& out);

// NPC merchant shop (server -> client).
usize decodeDealType(const u8* p, usize n, u32& npcId);  // ZC_SELECT_DEALTYPE (0xc4, 6B): open buy/sell
struct ShopItem {        // an entry of the BUY list (ZC_PC_PURCHASE_ITEMLIST 0xc6, 11B each)
    u32 price = 0;       // discounted price the player pays
    u32 basePrice = 0;   // shop base value (before discount)
    u16 nameid = 0;
    u8 type = 0;
};
struct SellItem {        // an entry of the SELL list (ZC_PC_SELL_ITEMLIST 0xc7, 10B each)
    u16 index = 0;       // inventory index (slot + 2); pass back verbatim to sell
    u32 price = 0;       // overcharge price the player receives
    u32 basePrice = 0;   // pre-overcharge value
};
usize decodeBuyList(const u8* p, usize n, std::vector<ShopItem>& out);   // ZC_PC_PURCHASE_ITEMLIST (0xc6)
usize decodeSellList(const u8* p, usize n, std::vector<SellItem>& out);  // ZC_PC_SELL_ITEMLIST (0xc7)

struct VendItem {        // an entry of a vendor's shop (ZC_..._FROMMC 0x133, 22B each)
    u32 price = 0;       // zeny price per unit
    u16 amount = 0;      // quantity the vendor still has
    u16 index = 0;       // wire index (cart slot + 2); echo back verbatim when buying
    u16 nameid = 0;
    u8 type = 0;
    u8 identify = 1;     // 0 = unidentified (drawn with a grey name)
    u8 refine = 0;
    u16 cards[4] = {0, 0, 0, 0};  // EQUIPSLOTINFO: inserted card ids; card[0]=0x00FF/0x00FE=forged,
                                  // 0x0100=pet egg (then not real cards) -- see clif_addcards.
};
struct VendBuyEntry { u16 amount = 0; u16 index = 0; };  // one line of a vending purchase order
// ZC_PC_PURCHASE_ITEMLIST_FROMMC (0x133): a vendor's wares. Fills vendorAid + the item list.
usize decodeVendingList(const u8* p, usize n, u32& vendorAid, std::vector<VendItem>& out);
// ZC_PC_PURCHASE_RESULT_FROMMC (0x135, 7B): result of a vending buy. result 0 = success.
usize decodeVendingResult(const u8* p, usize n, u16& index, u16& amount, u8& result);
std::vector<u8> buildVendingOpen(u32 vendorAid);  // CZ_REQ_BUY_FROMMC (0x130): open a vendor's shop
std::vector<u8> buildVendingPurchase(u32 vendorAid, const std::vector<VendBuyEntry>& items);  // 0x134

// Opening MY OWN vending shop (Merchant Vending skill). ZC_OPENSTORE (0x12d) is the trigger; the
// client picks cart items + prices and replies with CZ_REQ_OPENSTORE (0x1b2).
struct VendSellEntry { u16 index = 0; u16 amount = 0; u32 price = 0; };  // one shop line (index = cart slot+2)
usize decodeOpenStore(const u8* p, usize n, u16& maxItems);  // ZC_OPENSTORE (0x12d, 4B): max sellable slots
std::vector<u8> buildVendingOpenStore(const std::string& name, const std::vector<VendSellEntry>& items);  // 0x1b2
std::vector<u8> buildVendingCloseStore();  // CZ_REQ_CLOSESTORE (0x12e, 2B): take my shop down
// ZC_PC_PURCHASE_MYITEMLIST (0x136): MY own open shop's wares (sent when it opens). Same 22B record
// as 0x133 but index@4 / amount@6 are SWAPPED (vs amount@4 / index@6). Fills vendorAid + the list.
usize decodeMyVendingList(const u8* p, usize n, u32& vendorAid, std::vector<VendItem>& out);

// Equip-position bitmask (EQP_*) — which slot an item is worn in, as the server's pc.h
// enum and the ZC_EQUIPMENT_ITEMLIST "wearState" field use. 0 = carried, not worn.
enum : u16 {
    EQP_HEAD_LOW = 0x0001, EQP_HAND_R = 0x0002, EQP_GARMENT = 0x0004, EQP_ACC_L = 0x0008,
    EQP_ARMOR = 0x0010, EQP_HAND_L = 0x0020, EQP_SHOES = 0x0040, EQP_ACC_R = 0x0080,
    EQP_HEAD_TOP = 0x0100, EQP_HEAD_MID = 0x0200, EQP_AMMO = 0x8000
};
struct InvItem {         // an inventory slot (ZC_INVENTORY_LIST / ZC_EQUIPMENT_ITEMLIST)
    u16 index = 0;       // inventory index (slot + 2), exactly as the sell list references it
    u16 nameid = 0;
    u16 amount = 0;      // stack count (1 for equippables)
    u16 equipPos = 0;    // EQP_* slot it is currently worn in (0 if not equipped); 20B list only
    u16 equipMask = 0;   // EQP_* slots it CAN go in (20B @6); sent as wearLocation to equip from bag
    u8 type = 0;         // ItemType @ entry+4 (0 heal, 2 usable, 3 etc, 4 weapon, 5 equip, ...)
    u8 refine = 0;       // refine level (20B equip list @11) -> shown as the "+N" name prefix
    u8 identified = 1;   // identify flag (@entry+5); 0 = unidentified -> name drawn grey (S.)
    u16 cards[4] = {0, 0, 0, 0};  // inserted card ids (20B equip-list entry @12) -> RMB info window
};
// Decode a ZC_INVENTORY_LIST (0x1ee, 18B entries) or ZC_EQUIPMENT_ITEMLIST (0xa4, 20B):
// both carry index@0 and nameid@2; the stackable (18B) list also has amount@6. Pass the
// entry size (18 or 20); appends to `out`. Returns the packet's total length (bytes used).
usize decodeInventoryList(const u8* p, usize n, std::vector<InvItem>& out, usize entrySize);
// Live inventory updates so the bag/equip windows refresh without a relog (S. report):
// ZC_DELETE_ITEM (0xaf, 6B): index(2) amount(2) — an item was consumed/dropped.
usize decodeDeleteItem(const u8* p, usize n, u16& index, u16& amount);
// Kafra storage. The two list packets (ZC_STORE_ITEMLIST_NORMAL 0x1f0 18B / _EQUIP 0xa6 20B) reuse
// decodeInventoryList — byte-identical entries (storage index = slot+1). The rest are dedicated:
usize decodeStorageCount(const u8* p, usize n, u16& cur, u16& maxCount);   // ZC_STORE_COUNTINFO (0xf2, 6B)
usize decodeStorageAdd(const u8* p, usize n, InvItem& out);                // ZC_ADD_ITEM_TO_STORE (0xf4, 21B)
usize decodeStorageRemove(const u8* p, usize n, u16& index, u32& amount);  // ZC_DELETE_ITEM_FROM_STORE (0xf6, 8B)
// ZC_REQ_WEAR_EQUIP_ACK (0xaa) / ZC_REQ_TAKEOFF_EQUIP_ACK (0xac), 7B: index(2) location(2) ok(1).
// Identical layout for both; ok != 0 = success. On equip, location is the slot it went to.
usize decodeEquipResult(const u8* p, usize n, u16& index, u16& location, u8& ok);
usize decodeRefineResult(const u8* p, usize n, u16& result, u16& index, u16& refine);

// Little-endian field readers for the small fixed packets handled inline (bounds-checked by caller
// via rx.size() against the packet's documented length).
inline u16 pktU16(const u8* p, usize o) { return static_cast<u16>(p[o] | (p[o + 1] << 8)); }
inline u32 pktU32(const u8* p, usize o) {
    return static_cast<u32>(p[o]) | (static_cast<u32>(p[o + 1]) << 8) |
           (static_cast<u32>(p[o + 2]) << 16) | (static_cast<u32>(p[o + 3]) << 24);
}
// Read a fixed-width NUL-padded name field (cp1251/ASCII) into a trimmed std::string.
std::string pktName(const u8* p, usize n, usize off, usize len);
// ZC_ITEM_ADD (0xa0, 23B): index(2) amount(2) nameid(2) ... equipLoc(2)@19 type(1)@21 fail(1)@22.
usize decodeItemAdd(const u8* p, usize n, InvItem& out, u8& fail);
// An item lying on the map floor (ZC_ITEM_ENTRY 0x9d on view-enter / ZC_ITEM_FALL 0x9e on drop).
struct GroundItem {
    u32 id = 0;       // the floor object's block id (used to pick it up / remove it)
    u16 nameid = 0;   // item view/nameid (-> icon + name)
    u16 x = 0, y = 0;  // GAT cell
    u16 amount = 0;
};
// Decode a floor item. Both 0x9d and 0x9e are 17B with id@2 nameid@6 x@9 y@11, but the amount and
// subX/subY are swapped: 0x9d = amount@13 subX@15 subY@16; 0x9e = subX@13 subY@14 amount@15.
usize decodeGroundItem(const u8* p, usize n, GroundItem& out);
// ZC_ITEM_DISAPPEAR (0xa1, 6B): the floor object id that was picked up / vanished.
usize decodeItemDisappear(const u8* p, usize n, u32& id);
// CZ_ITEM_PICKUP (0x9f, 6B): ask to pick up the floor item with this object id.
std::vector<u8> buildTakeItem(u32 id);
// ZC_PC_PURCHASE_RESULT (0xca) / ZC_PC_SELL_RESULT (0xcb): a 1-byte result code @2.
usize decodeShopResult(const u8* p, usize n, u8& code);

// Public chat. ZC_NOTIFY_CHAT (0x8d): another unit's line, gid@4, message@8 (var).
usize decodeChat(const u8* p, usize n, u32& gid, std::string& msg);
// ZC_WHISPER (0x97, var): len@2, nick@4[24], message@28[len-28]. Incoming PM (Windows-1251).
usize decodeWhisper(const u8* p, usize n, std::string& nick, std::string& msg);
// ZC_ACK_WHISPER (0x98, 3B): result@2 — 0 ok, 1 target offline/not found, 2 ignored by target.
usize decodeWhisperAck(const u8* p, usize n, u8& result);
// ZC_ACK_TOUSESKILL (0x110, 10B): skillId@2, btype@4, ok@8, cause@9. A skill use failed; cause is
// 1 SP / 2 HP / 3 item / 4 delay / 5 zeny / 9 weight (else generic).
usize decodeSkillFail(const u8* p, usize n, u16& skillId, u8& cause);
// ZC_EMOTION (0xc0, 7B): gid@2, type@6 -- an emoticon to show over that unit.
usize decodeEmotion(const u8* p, usize n, u32& gid, u8& type);
std::vector<u8> buildEmotion(u8 type);  // CZ_REQ_EMOTION (0xbf): play an emoticon over my head
std::vector<u8> buildRememberWarp();    // CZ_REMEMBER_WARPPOINT (0x11d): /memo -> save a Warp Portal point
// ZC_USESKILL_ACK (0x13e, 24B): a unit began casting. src@2, skillId@14, casttime(ms)@20.
usize decodeSkillCasting(const u8* p, usize n, u32& src, u16& skillId, u32& castTimeMs, u32* dst = nullptr);
// ZC_USESKILL_CANCEL (0x1b9, 6B): gid@2 -- that unit's cast was interrupted.
usize decodeSkillCastCancel(const u8* p, usize n, u32& gid);
// ZC_NOTIFY_PLAYERCHAT (0x8e): our own line echoed back, message@4, no gid (var).
usize decodePlayerChat(const u8* p, usize n, std::string& msg);

// Skill-unit (ground effect) types carried by ZC_SKILL_ENTRY @14. Only the warp
// portal is rendered for now (the al_warp swirl); the rest are decoded but ignored.
enum : u8 { UNT_WARP_WAITING = 0x80, UNT_WARP_ACTIVE = 0x81 };
struct SkillEntry {  // ZC_SKILL_ENTRY (0x11f, 16B): a skill unit on the ground
    u32 gid = 0;     // the unit's own block id (key for removal via 0x120)
    u32 srcId = 0;   // the caster
    u16 x = 0, y = 0;
    u8 unitId = 0;   // UNT_* type (warp portal = UNT_WARP_WAITING/ACTIVE)
};
usize decodeSkillEntry(const u8* p, usize n, SkillEntry& out);     // ZC_SKILL_ENTRY (0x11f)
usize decodeSkillDisappear(const u8* p, usize n, u32& gid);        // ZC_SKILL_DISAPPEAR (0x120)

struct SkillInfo {       // one learned skill (ZC_SKILLINFO_LIST 0x10f, 37B entry)
    u16 id = 0;
    u16 inf = 0;         // target type (INF_*): ATTACK=1 GROUND=2 SELF=4 SUPPORT=16 TRAP=32; 0 = passive
    u16 level = 0;
    u16 sp = 0;          // SP cost at the current level
    u16 range = 0;
    bool up = false;     // can still be raised (has a spare skill point + below max)
    std::string name;    // internal skill name (e.g. "NV_BASIC"), 24 bytes
};
// ZC_SKILLINFO_LIST (0x10f, var): id@0 inf@2 _@4 lv@6 sp@8 range@10 name@12[24] up@36.
usize decodeSkillList(const u8* p, usize n, std::vector<SkillInfo>& out);
// ZC_GUILD_SKILLINFO (0x162, var): len.W skillpoint.W then the same 37B skill entries as 0x10f.
usize decodeGuildSkills(const u8* p, usize n, u16& skillPoint, std::vector<SkillInfo>& out);
std::vector<u8> buildUseSkill(u16 level, u16 skillId, u32 targetId);  // CZ_USE_SKILL (0x113): cast on a target
std::vector<u8> buildAutoSpellReply(u16 skillId);  // CZ_SELECTAUTOSPELL (0x1ce): Sage Autospell pick, 6B
std::vector<u8> buildSelectWarpPoint(u16 skillId, const std::string& mapName);  // CZ_SELECTWARPPOINT 0x11b
std::vector<u8> buildUseSkillToPos(u16 level, u16 skillId, u16 x, u16 y);  // CZ_USE_SKILL_TOGROUND (0x116): cast at a cell
std::vector<u8> buildSkillUp(u16 skillId);  // CZ_UPGRADE_SKILLLEVEL (0x112): raise a skill by one level
// ZC_SKILLINFO_UPDATE (0x10e, 11B): one skill's new lv/sp/range/up after a skill-up.
usize decodeSkillUpdate(const u8* p, usize n, u16& id, u16& level, u16& sp, u16& range, bool& up);

// Unit-entry packets: 0x22a/0x22b/0x22c (player), 0x78/0x7c (NPC/mob). Branches
// on the packet id, fills `out`, returns the fixed length consumed (0 if short
// or the id is not a unit-entry packet).
usize decodeActorEntry(const u8* p, usize n, ActorEntry& out);
usize decodeGuildEmblem(const u8* p, usize n, GuildEmblem& out);  // ZC_GUILD_EMBLEM_IMG (0x152)
// ZC_NOTIFY_VANISH (0x80): a unit left view. reason: 0 out-of-sight,1 dead,2 quit,3 teleport.
usize decodeVanish(const u8* p, usize n, u32& gid, u8& reason);
// 0x177 ZC_ITEMIDENTIFY_LIST: cmd(2)+len(2)+[inventory index (2)]* -- items awaiting a Magnifier ID.
usize decodeItemIdentifyList(const u8* p, usize n, std::vector<u16>& indices);
// 0x178 CZ_REQ_ITEMIDENTIFY: identify the item at this inventory index (server subtracts 2).
std::vector<u8> buildItemIdentify(u16 index);
// 0x1b0 ZC_CLASS_CHANGE: a unit's view class changed in place (mob transform, e.g. pupa->creamy).
usize decodeClassChange(const u8* p, usize n, u32& gid, u32& class_);

// ---- human-readable error text --------------------------------------------
const char* loginErrorText(u8 code);  // for AC_REFUSE_LOGIN (0x6a)
const char* banErrorText(u8 code);    // for SC_NOTIFY_BAN (0x81)

} // namespace uaro::net
