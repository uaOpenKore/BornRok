#include "net/Protocol.hpp"

#include <array>
#include <cstdio>

#include "core/Log.hpp"
#include "core/io/ByteBuffer.hpp"
#include "net/PacketTable.hpp"

namespace uaro::net {

namespace {

// Write a NUL-padded fixed-width string field (RO name/account fields).
void writeFixed(ByteWriter& w, const std::string& s, usize width) {
    for (usize i = 0; i < width; ++i)
        w.u8v(i < s.size() ? static_cast<u8>(s[i]) : 0u);
}

// Format 4 raw IP bytes (network order, as they sit in the packet) as "a.b.c.d".
std::string ipToString(const u8* p) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
    return std::string(buf);
}

// Candidate per-character record sizes for HC_ACCEPT_ENTER, oldest first. Our
// target era is 106 (or 108 with rename); larger sizes cover newer uAthena.
constexpr std::array<usize, 12> kCharRecordSizes = {106, 108, 112, 114, 116, 124,
                                                    128, 132, 136, 140, 144, 147};
} // namespace

u16 peekId(const u8* p, usize n) {
    if (n < 2) return 0;
    return static_cast<u16>(p[0] | (p[1] << 8));
}

// ---------------------------------------------------------------------------
// Builders
// ---------------------------------------------------------------------------
std::vector<u8> buildCALogin(u32 version, const std::string& user, const std::string& pass,
                             u8 clientType) {
    // S 0064 <version>.L <username>.24B <password>.24B <clienttype>.B  (55 bytes)
    ByteWriter w;
    w.u16le(PKT_CA_LOGIN);
    w.u32le(version);
    writeFixed(w, user, 24);
    writeFixed(w, pass, 24);
    w.u8v(clientType);
    return w.data();
}

std::vector<u8> buildCHEnter(u32 accountId, u32 loginId1, u32 loginId2, u8 sex, u16 clientType) {
    // S 0065 <account_id>.L <login_id1>.L <login_id2>.L <clienttype>.W <sex>.B  (17)
    ByteWriter w;
    w.u16le(PKT_CH_ENTER);
    w.u32le(accountId);
    w.u32le(loginId1);
    w.u32le(loginId2);
    w.u16le(clientType);
    w.u8v(sex);
    return w.data();
}

std::vector<u8> buildCharSelect(u8 slot) {
    // S 0066 <slot>.B  (3 bytes)
    ByteWriter w;
    w.u16le(PKT_CH_SELECT_CHAR);
    w.u8v(slot);
    return w.data();
}

std::vector<u8> buildCharDelete(u32 charId, const std::string& email) {
    // S 0068 <char_id>.L <email>.40B  (46 bytes). The char-server checks the email
    // against the account; default-email accounts accept "a@a.com" or "".
    ByteWriter w;
    w.u16le(PKT_CH_DELETE_CHAR);
    w.u32le(charId);
    writeFixed(w, email, 40);
    return w.data();
}

std::vector<u8> buildMakeChar(const std::string& name, u8 str, u8 agi, u8 vit, u8 int_, u8 dex,
                              u8 luk, u8 slot, u16 hairStyle, u16 hairColor) {
    // S 0067 <name>.24B <str><agi><vit><int><dex><luk>.6B <slot>.B <hairColor>.W <hairStyle>.W
    // (37 bytes). The char server validates each stat 1..9, their sum == 30, and the paired
    // sums str+int / agi+luk / vit+dex each <= 10 — so the classic 5/5/5/5/5/5 is always valid.
    ByteWriter w;
    w.u16le(PKT_CH_MAKE_CHAR);
    writeFixed(w, name, 24);
    w.u8v(str);
    w.u8v(agi);
    w.u8v(vit);
    w.u8v(int_);
    w.u8v(dex);
    w.u8v(luk);
    w.u8v(slot);
    w.u16le(hairColor);
    w.u16le(hairStyle);
    return w.data();
}

std::vector<u8> buildWantToConnection(u32 accountId, u32 charId, u32 loginId1, u32 tick, u8 sex) {
    // S 009b — ServerType8 (uaRO) obfuscated map-login (CZ_ENTER), 26 bytes, matching uOK210
    // Send/ServerType8.pm::sendMapLogin EXACTLY. The server picks sd->packet_ver from this
    // packet's opcode+length, and ONLY a ServerType8 client (0x009b, 26B) gets the full
    // view stream (mid-view player spawns); the old standard 0x0072/19B login put us in a
    // degraded mode (no 0x22b on units entering view) -- #95. The 0x39 0x33 / 0x65 /
    // 0x37 0x33 0x36 0x64 bytes are this server's fixed anti-bot padding between fields.
    ByteWriter w;
    w.u16le(0x009b);
    w.u8v(0x39);
    w.u8v(0x33);
    w.u32le(accountId);
    w.u8v(0x65);
    w.u32le(charId);
    w.u8v(0x37);
    w.u8v(0x33);
    w.u8v(0x36);
    w.u8v(0x64);
    w.u32le(loginId1);  // map-login session id
    w.u32le(tick);
    w.u8v(sex);
    return w.data();
}

std::vector<u8> buildLoadEndAck() {
    // S 007d  (2 bytes) — "map fully loaded, spawn me".
    ByteWriter w;
    w.u16le(PKT_CZ_NOTIFY_ACTORINIT);
    return w.data();
}

std::vector<u8> buildWalkRequest(u16 x, u16 y) {
    // S 00a7 — ServerType8 (uaRO) walktoxy (CZ_REQUEST_MOVE), 8 bytes, matching uOK210.
    // sendMove: pack("C*",0xA7,0,0,0) . getCoordString(x,y).
    // uOK210 getCoordString packs 0x44(8b),x(10b),y(10b),0(4b) = 4 bytes and ONLY strips
    // the leading byte for serverType 0; serverType 8 KEEPS it. So coords are 4 bytes with a
    // leading 0x44, not the 3-byte WBUFPOS. Sending 3 bytes makes the server drop the link.
    ByteWriter w;
    w.u16le(0x00a7);
    w.u8v(0x00);
    w.u8v(0x00);
    w.u8v(0x44);
    w.u8v(static_cast<u8>(x >> 2));
    w.u8v(static_cast<u8>((x << 6) | ((y >> 4) & 0x3f)));
    w.u8v(static_cast<u8>(y << 4));
    return w.data();
}

std::vector<u8> buildNameRequest(u32 gid) {
    // S 008c — ServerType8 (uaRO) getcharnamerequest (CZ_REQNAME), 11 bytes, matching uOK210
    // sendGetPlayerInfo: 008c <5 pad bytes> <gid>.L (gid at offset 7).
    ByteWriter w;
    w.u16le(0x008c);
    for (int i = 0; i < 5; ++i) w.u8v(0x00);
    w.u32le(gid);
    return w.data();
}

std::vector<u8> buildAttack(u32 targetGid, u8 action) {
    // S 0190 — ServerType8 (uaRO) actionrequest (CZ_REQUEST_ACT), 19 bytes, matching uOK210
    // sendAction (pack 'v x3 a4 x9 C'): 0190 000000 <target_id>.L <9 pad> <action>.B
    // (target@5, action@18). The standard 0x0089 we sent before is this server's TICKSEND.
    ByteWriter w;
    w.u16le(0x0190);
    w.u8v(0x00);
    w.u8v(0x00);
    w.u8v(0x00);
    w.u32le(targetGid);
    for (int i = 0; i < 9; ++i) w.u8v(0x00);
    w.u8v(action);
    return w.data();
}

std::vector<u8> buildStatChange(u16 statId, u8 amount) {
    // S 00bb <statusID>.W <amount>.B (5B). The server reads only statusID and raises that
    // stat by 1; amount mirrors the original client packet but is ignored server-side, so a
    // "+10" is ten of these. statId is SP_STR..SP_LUK (13..18).
    ByteWriter w;
    w.u16le(PKT_CZ_STATUS_CHANGE);
    w.u16le(statId);
    w.u8v(amount);
    return w.data();
}

std::vector<u8> buildUseItem(u16 index, u32 accountId) {
    // S 009f — ServerType8 (uaRO) item_use (uOK210 sendItemUse), 14 bytes:
    // 9f00 6162 <index>.W 34353261 <accountID>.L (0x61 0x62 / 0x34 0x35 0x32 0x61 = anti-bot pad).
    ByteWriter w;
    w.u8v(0x9f);
    w.u8v(0x00);
    w.u8v(0x61);
    w.u8v(0x62);
    w.u16le(index);
    w.u8v(0x34);
    w.u8v(0x35);
    w.u8v(0x32);
    w.u8v(0x61);
    w.u32le(accountId);
    return w.data();
}

std::vector<u8> buildSelectWarpPoint(u16 skillId, const std::string& mapName) {
    // S 011B — CZ_SELECTWARPPOINT / useskillmap (uOK210 sendWarpTele, pack 'v2 Z16'), 20 bytes:
    // 011B <skillId>.W <mapName>.Z16 ("prontera.gat" / "Random" / "cancel"). Reply to ZC_WARPLIST.
    ByteWriter w;
    w.u16le(0x011b);
    w.u16le(skillId);
    writeFixed(w, mapName, 16);
    return w.data();
}

std::vector<u8> buildCraftReply(CraftKind kind, const CraftItem& it) {
    ByteWriter w;
    switch (kind) {
        case CraftKind::Produce:  // 0x18e producemix (10B): id, nameid, mat1..3 (0 = use recipe)
            w.u16le(0x018e); w.u16le(it.nameid); w.u16le(0); w.u16le(0); w.u16le(0); break;
        case CraftKind::Arrow:    // 0x1ae selectarrow (4B): id, nameid
            w.u16le(0x01ae); w.u16le(it.nameid); break;
        case CraftKind::Repair:   // 0x1fd repairitem (15B per PacketTable): id, inv index, pad.
            w.u16le(0x01fd); w.u16le(it.index);
            for (int i = 0; i < 11; ++i) w.u8v(0); break;  // server only reads index@2
        case CraftKind::Refine:   // 0x222 weaponrefine (6B): id, index, pad
            w.u16le(0x0222); w.u16le(it.index); w.u16le(0); break;
    }
    return w.data();
}

std::vector<u8> buildUseSkill(u16 level, u16 skillId, u32 targetId) {
    // S 0072 — ServerType8 (uaRO) skill_use to target (uOK210 sendSkillUse, pack 'v x4 v x2 v x9'),
    // 25 bytes: 0072 <4 pad> <lv>.W <2 pad> <skillId>.W <9 pad> <targetID>.L.
    ByteWriter w;
    w.u16le(0x0072);
    for (int i = 0; i < 4; ++i) w.u8v(0x00);
    w.u16le(level);
    w.u8v(0x00);
    w.u8v(0x00);
    w.u16le(skillId);
    for (int i = 0; i < 9; ++i) w.u8v(0x00);
    w.u32le(targetId);
    return w.data();
}

std::vector<u8> buildAutoSpellReply(u16 skillId) {
    // CZ_SELECTAUTOSPELL (0x01ce, 6B): the Sage picks a bolt from the ZC_AUTOSPELL menu. Server binds
    // 0x1ce->autospell,pos 2 in packet_db, so skillId is a WORD at offset 2 (+2 pad to length 6).
    ByteWriter w;
    w.u16le(0x01ce);
    w.u16le(skillId);
    w.u16le(0x0000);
    return w.data();
}

std::vector<u8> buildUseSkillToPos(u16 level, u16 skillId, u16 x, u16 y) {
    // S 0113 — ServerType8 (uaRO) skill_use_location (uOK210 sendSkillUseLoc,
    // pack 'C2 x3 v x2 v x1 v x6 v'), 22 bytes: 1301 <3 pad> <lv>.W <2 pad> <skillId>.W <1 pad>
    // <x>.W <6 pad> <y>.W.
    ByteWriter w;
    w.u8v(0x13);
    w.u8v(0x01);
    for (int i = 0; i < 3; ++i) w.u8v(0x00);
    w.u16le(level);
    w.u8v(0x00);
    w.u8v(0x00);
    w.u16le(skillId);
    w.u8v(0x00);
    w.u16le(x);
    for (int i = 0; i < 6; ++i) w.u8v(0x00);
    w.u16le(y);
    return w.data();
}

std::vector<u8> buildSkillUp(u16 skillId) {  // S 0112 <skillId>.W (4B) — CZ_UPGRADE_SKILLLEVEL
    ByteWriter w;
    w.u16le(PKT_CZ_UPGRADE_SKILLLEVEL);
    w.u16le(skillId);
    return w.data();
}

std::vector<u8> buildWearEquip(u16 index, u16 location) {
    // S 00a9 <index>.W <wearLocation>.W (6B). Double-click an equippable bag item; location is
    // the EQP_* slot bitmask the item declares. The server validates it against the item.
    ByteWriter w;
    w.u16le(PKT_CZ_REQ_WEAR_EQUIP);
    w.u16le(index);
    w.u16le(location);
    return w.data();
}

std::vector<u8> buildUseCard(u16 cardIndex) {
    // S 017a <cardIndex>.W (4B). Double-click a card in the bag: ask the server which worn-able
    // items it fits. cardIndex is our InvItem.index (slot+2), echoed verbatim on the wire.
    ByteWriter w;
    w.u16le(PKT_CZ_REQ_ITEMCOMPOSITION_LIST);
    w.u16le(cardIndex);
    return w.data();
}

std::vector<u8> buildInsertCard(u16 equipIndex, u16 cardIndex) {
    // S 017c <cardIndex>.W <equipIndex>.W (6B). CARD FIRST, then equip: the server reads
    // pc_insert_card(sd, RFIFOW(2)-2, RFIFOW(4)-2) whose signature is (sd, idx_CARD, idx_EQUIP).
    // We were sending equip@2/card@4 (swapped) -> the server took the equip as the "card", failed
    // its type!=IT_CARD check, and silently rejected every insert (S.: "карта не вставляется, стак
    // карт не меняется"). Both are InvItem.index (slot+2); server subtracts 2.
    ByteWriter w;
    w.u16le(PKT_CZ_REQ_ITEMCOMPOSITION);
    w.u16le(cardIndex);
    w.u16le(equipIndex);
    return w.data();
}

usize decodeCardTargets(const u8* p, usize n, std::vector<u16>& out) {
    // R 017b <len>.W <inv index>.W* — the equippable inventory items (index = slot+2) this card
    // can be inserted into. len counts the whole packet (4 header + 2*count). Values match
    // InvItem.index verbatim, so we push them as-is; the caller filters equipped items out.
    // rd16 is defined later in this file, so read the little-endian u16s inline here.
    out.clear();
    if (n < 4) return n;
    usize len = static_cast<usize>(p[2] | (p[3] << 8));
    if (len < 4 || len > n) len = n;
    for (usize off = 4; off + 2 <= len; off += 2)
        out.push_back(static_cast<u16>(p[off] | (p[off + 1] << 8)));
    return len;
}

std::vector<u8> buildTakeoffEquip(u16 index) {
    // S 00ab <index>.W (4B). Double-click a worn slot in the equip window to take it off.
    ByteWriter w;
    w.u16le(PKT_CZ_REQ_TAKEOFF_EQUIP);
    w.u16le(index);
    return w.data();
}

std::vector<u8> buildDropItem(u16 index, u16 amount) {
    // S 0116 — ServerType8 (uaRO) item_drop (uOK210 sendDrop), 10 bytes:
    // 1601 353433 <index>.W 61 <amount>.W (0x35 0x34 0x33 / 0x61 = anti-bot pad). The server
    // subtracts 2 from index, matching our InvItem.index (slot + 2).
    ByteWriter w;
    w.u8v(0x16);
    w.u8v(0x01);
    w.u8v(0x35);
    w.u8v(0x34);
    w.u8v(0x33);
    w.u16le(index);
    w.u8v(0x61);
    w.u16le(amount);
    return w.data();
}

std::vector<u8> buildStorageStore(u16 invIndex, u32 amount) {
    // S 0094 — ServerType8 (uaRO) storage_item_add (uOK210 sendStorageAdd, pack 'v x5 v x1 V'),
    // 14 bytes: 0094 <5 pad> <index>.W <1 pad> <amount>.L. index = our InvItem.index (slot+2).
    ByteWriter w;
    w.u16le(0x0094);
    for (int i = 0; i < 5; ++i) w.u8v(0x00);
    w.u16le(invIndex);
    w.u8v(0x00);
    w.u32le(amount);
    return w.data();
}

std::vector<u8> buildStorageRetrieve(u16 storeIndex, u32 amount) {
    // S 00f7 — ServerType8 (uaRO) storage_item_remove (uOK210 sendStorageGet, pack 'v x12 v x2 V'),
    // 22 bytes: 00f7 <12 pad> <index>.W <2 pad> <amount>.L. index = our storage index (slot+1).
    ByteWriter w;
    w.u16le(0x00f7);
    for (int i = 0; i < 12; ++i) w.u8v(0x00);
    w.u16le(storeIndex);
    w.u8v(0x00);
    w.u8v(0x00);
    w.u32le(amount);
    return w.data();
}

std::vector<u8> buildStorageClose() {
    // S 0193 — ServerType8 (uaRO) storage close (uOK210 sendStorageClose), 2 bytes: 9301.
    ByteWriter w;
    w.u16le(0x0193);
    return w.data();
}

// Shared layout for the 8-byte item-move packets (cart in/out + cart<->storage): id index amount.
static std::vector<u8> buildItemMove(u16 pkt, u16 index, u32 amount) {
    ByteWriter w;
    w.u16le(pkt);
    w.u16le(index);
    w.u32le(amount);
    return w.data();
}
std::vector<u8> buildCartAdd(u16 invIndex, u32 amount) {  // inventory -> cart
    return buildItemMove(PKT_CZ_MOVE_ITEM_BODY_TO_CART, invIndex, amount);
}
std::vector<u8> buildCartGet(u16 cartIndex, u32 amount) {  // cart -> inventory
    return buildItemMove(PKT_CZ_MOVE_ITEM_CART_TO_BODY, cartIndex, amount);
}
std::vector<u8> buildCartToStore(u16 cartIndex, u32 amount) {  // cart -> storage
    return buildItemMove(PKT_CZ_MOVE_ITEM_CART_TO_STORE, cartIndex, amount);
}
std::vector<u8> buildStoreToCart(u16 storeIndex, u32 amount) {  // storage -> cart
    return buildItemMove(PKT_CZ_MOVE_ITEM_STORE_TO_CART, storeIndex, amount);
}

std::vector<u8> buildGuildEmblemRequest(u32 guildId) {
    // S 0151 <guildID>.L (6B). The server replies with ZC_GUILD_EMBLEM_IMG (0x152) for ANY
    // guild (clif_parse_GuildRequestEmblem -> guild_search), so a unit's guildId straight
    // from its spawn packet is enough to fetch its emblem.
    ByteWriter w;
    w.u16le(PKT_CZ_REQ_GUILD_EMBLEM_IMG);
    w.u32le(guildId);
    return w.data();
}

std::vector<u8> buildNpcContact(u32 gid) {  // S 0090 <gid>.L <type>.B (7B); type 1 = click
    ByteWriter w;
    w.u16le(PKT_CZ_CONTACTNPC);
    w.u32le(gid);
    w.u8v(0x01);
    return w.data();
}

std::vector<u8> buildNpcNext(u32 gid) {  // S 00b9 <gid>.L (6B): request the next script page
    ByteWriter w;
    w.u16le(PKT_CZ_REQ_NEXT);
    w.u32le(gid);
    return w.data();
}

std::vector<u8> buildNpcClose(u32 gid) {  // S 0146 <gid>.L (6B): close the dialog
    ByteWriter w;
    w.u16le(PKT_CZ_CLOSE_DIALOG);
    w.u32le(gid);
    return w.data();
}

std::vector<u8> buildNpcMenu(u32 gid, u8 choice) {  // S 00b8 <gid>.L <choice>.B (7B), 1-based
    ByteWriter w;
    w.u16le(PKT_CZ_CHOOSE_MENU);
    w.u32le(gid);
    w.u8v(choice);
    return w.data();
}

std::vector<u8> buildNpcInputNum(u32 gid, int value) {  // S 0143 <gid>.L <value>.L (10B)
    ByteWriter w;
    w.u16le(PKT_CZ_INPUT_EDITDLG);
    w.u32le(gid);
    w.u32le(static_cast<u32>(value));
    return w.data();
}

std::vector<u8> buildNpcInputStr(u32 gid, const std::string& s) {
    // S 01d5 <len>.W <gid>.L <string + NUL>  — the server reads len@2, gid@4, the string at @8
    // and force-terminates it, so frame the string with a trailing NUL: len = 8 + strlen + 1.
    ByteWriter w;
    w.u16le(PKT_CZ_INPUT_EDITDLGSTR);
    w.u16le(static_cast<u16>(8 + s.size() + 1));
    w.u32le(gid);
    w.write_string(s);
    w.u8v(0);  // NUL terminator (the server's npc_str is force-null-terminated)
    return w.data();
}

std::vector<u8> buildRestart(u8 type) {  // S 00b2 <type>.B (3B): 0 = save point, 1 = resurrect
    ByteWriter w;
    w.u16le(0x00b2);
    w.u8v(type);
    return w.data();
}

std::vector<u8> buildRemoveOption() {  // S 012a (2B) — server drops cart|riding|falcon at once
    ByteWriter w;
    w.u16le(PKT_CZ_REQ_REMOVEOPTION);
    return w.data();
}

std::vector<u8> buildDealSelect(u32 npcId, u8 type) {  // S 00c5 <npc>.L <type>.B (7B); 0=buy 1=sell
    ByteWriter w;
    w.u16le(PKT_CZ_ACK_SELECT_DEALTYPE);
    w.u32le(npcId);
    w.u8v(type);
    return w.data();
}

std::vector<u8> buildBuyList(const std::vector<ShopBuyEntry>& items) {
    // S 00c8 <len>.W { <amount>.W <nameid>.W }*  — 4-byte header, then 4B per entry.
    ByteWriter w;
    w.u16le(PKT_CZ_PC_PURCHASE_ITEMLIST);
    w.u16le(static_cast<u16>(4 + items.size() * 4));
    for (const ShopBuyEntry& it : items) {
        w.u16le(it.amount);
        w.u16le(it.nameid);
    }
    return w.data();
}

std::vector<u8> buildSellList(const std::vector<ShopSellEntry>& items) {
    // S 00c9 <len>.W { <index>.W <amount>.W }*  — 4-byte header, then 4B per entry.
    ByteWriter w;
    w.u16le(PKT_CZ_PC_SELL_ITEMLIST);
    w.u16le(static_cast<u16>(4 + items.size() * 4));
    for (const ShopSellEntry& it : items) {
        w.u16le(it.index);
        w.u16le(it.amount);
    }
    return w.data();
}

std::vector<u8> buildVendingOpen(u32 vendorAid) {
    // S 0130 <vendorAID>.L  (6B) — CZ_REQ_BUY_FROMMC: ask to browse this vendor's shop.
    ByteWriter w;
    w.u16le(PKT_CZ_REQ_BUY_FROMMC);
    w.u32le(vendorAid);
    return w.data();
}

std::vector<u8> buildVendingPurchase(u32 vendorAid, const std::vector<VendBuyEntry>& items) {
    // S 0134 <len>.W <vendorAID>.L { <amount>.W <index>.W }*  — 8-byte header, 4B per entry.
    // index is echoed verbatim from the 0x133 list (= cart slot + 2); the server subtracts 2.
    ByteWriter w;
    w.u16le(PKT_CZ_PC_PURCHASE_ITEMLIST_FROMMC);
    w.u16le(static_cast<u16>(8 + items.size() * 4));
    w.u32le(vendorAid);
    for (const VendBuyEntry& it : items) {
        w.u16le(it.amount);
        w.u16le(it.index);
    }
    return w.data();
}

std::vector<u8> buildVendingOpenStore(const std::string& name, const std::vector<VendSellEntry>& items) {
    // S 01b2 <len>.W <name>.80B <flag>.B { <index>.W <amount>.W <price>.L }*  — flag 1 = open the shop.
    // index = cart slot + 2 (the server subtracts 2). name is null-padded to 80 bytes.
    ByteWriter w;
    w.u16le(PKT_CZ_REQ_OPENSTORE);
    w.u16le(static_cast<u16>(85 + items.size() * 8));
    for (usize i = 0; i < 80; ++i) w.u8v(i < name.size() ? static_cast<u8>(name[i]) : 0);
    w.u8v(1);  // flag: open
    for (const VendSellEntry& it : items) {
        w.u16le(it.index);
        w.u16le(it.amount);
        w.u32le(it.price);
    }
    return w.data();
}

std::vector<u8> buildVendingCloseStore() {
    ByteWriter w;  // S 012e — take my shop down
    w.u16le(PKT_CZ_REQ_CLOSESTORE);
    return w.data();
}

std::vector<u8> buildGlobalMessage(const std::string& name, const std::string& text) {
    // S 00f3 <len>.W <message>  — message = "Name : text\0". The server validates the
    // "<name> : " prefix and the trailing NUL. len = 4 + strlen(message) + 1. The wire is
    // Windows-1251 (the server matches the prefix against the cp1251 char name and rebroadcasts
    // cp1251), so convert the whole "Name : text" before sizing/sending it.
    const std::string msg = utf8ToCp1251(name + " : " + text);
    ByteWriter w;
    w.u16le(0x00f3);  // ServerType8 (uaRO) public_chat (uOK210 sendChat); same len/body layout
    w.u16le(static_cast<u16>(4 + msg.size() + 1));
    w.write_string(msg);
    w.u8v(0);  // NUL terminator the server requires
    return w.data();
}

std::vector<u8> buildWhisper(const std::string& target, const std::string& text) {
    // S 0096 <len>.W <target>.24B <message>.?B — the server reads len-28 message bytes; we send the
    // message plus a NUL like the stock client, so len = 4 + 24 + strlen(message) + 1. Both the
    // target name and the body travel as Windows-1251; size len from the CONVERTED body.
    const std::string body = utf8ToCp1251(text);
    ByteWriter w;
    w.u16le(PKT_CZ_WHISPER);
    w.u16le(static_cast<u16>(4 + 24 + body.size() + 1));
    writeFixed(w, utf8ToCp1251(target), 24);
    w.write_string(body);
    w.u8v(0);
    return w.data();
}

std::vector<u8> buildTradeRequest(u32 targetAccountId) {
    // S 00e4 <targetAID>.L (6B) — ask to trade with the player at this account id (a PC's gid = AID).
    ByteWriter w;
    w.u16le(PKT_CZ_REQ_EXCHANGE_ITEM);
    w.u32le(targetAccountId);
    return w.data();
}

std::vector<u8> buildAddFriend(const std::string& name) {
    // S 0202 <name>.24B (26B) — send a friend request to the character with this name.
    ByteWriter w;
    w.u16le(PKT_CZ_ADD_FRIENDS);
    writeFixed(w, name, 24);
    return w.data();
}

std::vector<u8> buildFriendReply(u32 aid, u32 cid, bool accept) {
    // S 0208 <reqAID>.L <reqCID>.L <type>.L (14B) -- type 1 = accept, 0 = reject (OpenKore uaRO).
    ByteWriter w;
    w.u16le(PKT_CZ_ACK_REQ_ADD_FRIENDS);
    w.u32le(aid);
    w.u32le(cid);
    w.u32le(accept ? 1u : 0u);
    return w.data();
}

std::vector<u8> buildDeleteFriend(u32 aid, u32 cid) {
    // S 0203 <AID>.L <CID>.L (10B) -- remove the friend with this account/char id.
    ByteWriter w;
    w.u16le(PKT_CZ_DELETE_FRIENDS);
    w.u32le(aid);
    w.u32le(cid);
    return w.data();
}

usize decodeFriendList(const u8* p, usize n, std::vector<FriendInfo>& out) {
    // R 0201 <len>.W { <AID>.L <CID>.L <name>.24B }*  -- the full list on login.
    if (n < 4) return 0;
    const u16 len = static_cast<u16>(p[2] | (p[3] << 8));
    if (len < 4 || n < len) return 0;
    out.clear();
    constexpr usize kRec = 32;  // AID(4) + CID(4) + name(24)
    const usize count = (len - 4) / kRec;
    ByteReader r(p + 4, len - 4);
    for (usize i = 0; i < count; ++i) {
        FriendInfo f;
        f.accountId = r.u32le();
        f.charId = r.u32le();
        f.name = cp1251ToUtf8(r.read_cstring(24));  // char names are cp1251 on the wire
        f.online = true;  // a fresh 0x206 reconciles the real online/offline state right after
        out.push_back(std::move(f));
    }
    return len;
}

usize decodeFriendState(const u8* p, usize n, u32& aid, u32& cid, bool& online) {
    // R 0206 <AID>.L <CID>.L <isNotOnline>.B (11B) -- 0 = online, 1 = offline.
    if (n < 11) return 0;
    ByteReader r(p + 2, 9);
    aid = r.u32le();
    cid = r.u32le();
    online = (r.u8v() == 0);
    return 11;
}

usize decodeFriendRequest(const u8* p, usize n, u32& aid, u32& cid, std::string& name) {
    // R 0207 <reqAID>.L <reqCID>.L <name>.24B (34B) -- someone wants to be friends.
    if (n < 34) return 0;
    ByteReader r(p + 2, 32);
    aid = r.u32le();
    cid = r.u32le();
    name = cp1251ToUtf8(r.read_cstring(24));
    return 34;
}

usize decodeFriendAddResult(const u8* p, usize n, u16& type, u32& aid, u32& cid, std::string& name) {
    // R 0209 <type>.W <AID>.L <CID>.L <name>.24B (36B) -- type 0 = added, non-zero = rejected/failed.
    if (n < 36) return 0;
    ByteReader r(p + 2, 34);
    type = r.u16le();
    aid = r.u32le();
    cid = r.u32le();
    name = cp1251ToUtf8(r.read_cstring(24));
    return 36;
}

usize decodeFriendRemoved(const u8* p, usize n, u32& aid, u32& cid) {
    // R 020a <AID>.L <CID>.L (10B) -- this friend is no longer on the list.
    if (n < 10) return 0;
    ByteReader r(p + 2, 8);
    aid = r.u32le();
    cid = r.u32le();
    return 10;
}

// ---- Party (#78 part 2) ----

usize decodePartyOrganizeResult(const u8* p, usize n, u8& fail) {
    if (n < 3) return 0;  // R 00fa <fail>.B (3B): 0 = ok
    fail = p[2];
    return 3;
}

usize decodePartyList(const u8* p, usize n, std::string& partyName, std::vector<PartyMember>& out) {
    // R 00fb <len>.W <party name>.24B { <AID>.L <char name>.24B <map>.16B <leader>.B <offline>.B }*
    if (n < 4) return 0;
    const u16 len = static_cast<u16>(p[2] | (p[3] << 8));
    if (len < 28 || n < len) return 0;
    ByteReader r(p + 4, len - 4);
    partyName = cp1251ToUtf8(r.read_cstring(24));
    out.clear();
    constexpr usize kRec = 46;  // AID(4) + name(24) + map(16) + leader(1) + offline(1)
    const usize count = (len - 28) / kRec;
    for (usize i = 0; i < count; ++i) {
        PartyMember m;
        m.accountId = r.u32le();
        const std::string raw = r.read_cstring(24);
        // The uaRO server OVER-REPORTS len: only the first N records are real, the rest of the packet is
        // uninitialised garbage (verified on the wire — 6 members but len claims 17). A real record has a
        // non-zero AID and a name that is printable (ASCII / cp1251 high bytes); the garbage records have
        // AID 0 or a name starting with a control byte. Stop at the first such record so we don't render
        // junk names (S.: "не все имена вычитывает корректно в окне группы").
        bool bad = (m.accountId == 0) || raw.empty();
        for (unsigned char c : raw)
            if (c < 0x20) { bad = true; break; }
        if (bad) break;
        m.name = cp1251ToUtf8(raw);
        m.map = r.read_cstring(16);
        m.leader = (r.u8v() == 0);   // 0 = leader
        m.online = (r.u8v() == 0);   // 0 = online (the byte is an "offline" flag)
        out.push_back(std::move(m));
    }
    return len;
}

usize decodePartyInvite(const u8* p, usize n, u32& aid, std::string& name) {
    // R 00fe <AID>.L <name>.24B (30B) -- another party leader invites us.
    if (n < 30) return 0;
    ByteReader r(p + 2, 28);
    aid = r.u32le();
    name = cp1251ToUtf8(r.read_cstring(24));
    return 30;
}

usize decodePartyMemberAdd(const u8* p, usize n, PartyMember& m) {
    // R 0104 <AID>.L <role>.L <x>.W <y>.W <online>.B <partyName>.24B <charName>.24B <map>.16B (79B)
    if (n < 79) return 0;
    ByteReader r(p + 2, 77);
    m.accountId = r.u32le();
    m.leader = (r.u32le() == 0);  // role 0 = leader
    m.x = r.u16le();
    m.y = r.u16le();
    m.online = (r.u8v() == 0);
    r.read_cstring(24);                       // party name (already known from the list)
    m.name = cp1251ToUtf8(r.read_cstring(24));  // the member's character name
    m.map = r.read_cstring(16);
    return 79;
}

usize decodePartyMemberLeave(const u8* p, usize n, u32& aid, std::string& name, u8& result) {
    // R 0105 <AID>.L <name>.24B <result>.B (31B)
    if (n < 31) return 0;
    ByteReader r(p + 2, 29);
    aid = r.u32le();
    name = cp1251ToUtf8(r.read_cstring(24));
    result = r.u8v();
    return 31;
}

usize decodePartyHp(const u8* p, usize n, u32& aid, u16& hp, u16& maxHp) {
    if (n < 10) return 0;  // R 0106 <AID>.L <hp>.W <maxhp>.W (10B)
    ByteReader r(p + 2, 8);
    aid = r.u32le();
    hp = r.u16le();
    maxHp = r.u16le();
    return 10;
}

usize decodePartyLocation(const u8* p, usize n, u32& aid, u16& x, u16& y) {
    if (n < 10) return 0;  // R 0107 <AID>.L <x>.W <y>.W (10B)
    ByteReader r(p + 2, 8);
    aid = r.u32le();
    x = r.u16le();
    y = r.u16le();
    return 10;
}

// ---- Quest journal (#136). Old ZC layout (PACKETVER >= 20080000). Header helpers are inlined
// because rd16/rd32 are defined later in this file. Mob names are cp1251 on the wire.
usize decodeQuestStateList(const u8* p, usize n, std::vector<std::pair<u32, u8>>& out) {
    // R 02b1 <len>.W <count>.L { <quest id>.L <state>.B }*  (5B/quest)
    if (n < 8) return 0;
    const u16 len = static_cast<u16>(p[2] | (p[3] << 8));
    if (len < 8 || n < len) return 0;
    u32 count = static_cast<u32>(p[4] | (p[5] << 8) | (p[6] << 16) | (p[7] << 24));
    const u32 cap = (len - 8u) / 5u;
    if (count > cap) count = cap;
    ByteReader r(p + 8, len - 8);
    out.clear();
    for (u32 i = 0; i < count; ++i) {
        const u32 id = r.u32le();
        out.emplace_back(id, r.u8v());
    }
    return len;
}

usize decodeQuestMissionList(const u8* p, usize n, std::vector<QuestInfo>& out) {
    // R 02b2 <len>.W <count>.L { <id>.L <svrTime>.L <expire>.L <num>.W
    //                            {<mob>.L <count>.W <name>.24B}*3 }*   (104B/quest)
    if (n < 8) return 0;
    const u16 len = static_cast<u16>(p[2] | (p[3] << 8));
    if (len < 8 || n < len) return 0;
    u32 count = static_cast<u32>(p[4] | (p[5] << 8) | (p[6] << 16) | (p[7] << 24));
    const u32 cap = (len - 8u) / 104u;
    if (count > cap) count = cap;
    ByteReader r(p + 8, len - 8);
    out.clear();
    for (u32 i = 0; i < count; ++i) {
        QuestInfo q;
        q.id = r.u32le();
        r.u32le();  // svrTime (remaining); we keep the absolute expiry below
        q.timeLimit = r.u32le();
        const u16 num = r.u16le();
        for (u16 j = 0; j < 3; ++j) {  // the record always carries 3 objective slots
            QuestObjective o;
            o.mobId = r.u32le();
            o.count = r.u16le();
            o.name = cp1251ToUtf8(r.read_cstring(24));
            if (j < num) q.objectives.push_back(std::move(o));
        }
        q.state = 1;  // the mission list is the ACTIVE quests; 0x2b1 refines inactive/complete
        out.push_back(std::move(q));
    }
    return len;
}

usize decodeQuestAdd(const u8* p, usize n, QuestInfo& out) {
    // R 02b3 <id>.L <state>.B <svrTime>.L <expire>.L <num>.W {<mob>.L <count>.W <name>.24B}*3 (107B)
    if (n < 107) return 0;
    ByteReader r(p + 2, 105);
    out = QuestInfo{};
    out.id = r.u32le();
    out.state = r.u8v();
    r.u32le();  // svrTime
    out.timeLimit = r.u32le();
    const u16 num = r.u16le();
    for (u16 j = 0; j < 3; ++j) {
        QuestObjective o;
        o.mobId = r.u32le();
        o.count = r.u16le();
        o.name = cp1251ToUtf8(r.read_cstring(24));
        if (j < num) out.objectives.push_back(std::move(o));
    }
    return 107;
}

usize decodeQuestDelete(const u8* p, usize n, u32& id) {
    if (n < 6) return 0;  // R 02b4 <quest id>.L (6B)
    id = static_cast<u32>(p[2] | (p[3] << 8) | (p[4] << 16) | (p[5] << 24));
    return 6;
}

usize decodeQuestHunt(const u8* p, usize n, std::vector<QuestHunt>& out) {
    // R 02b5 <len>.W <num>.W { <quest id>.L <mob>.L <need>.W <count>.W }*  (12B/objective)
    if (n < 6) return 0;
    const u16 len = static_cast<u16>(p[2] | (p[3] << 8));
    if (len < 6 || n < len) return 0;
    u16 num = static_cast<u16>(p[4] | (p[5] << 8));
    const u16 cap = static_cast<u16>((len - 6) / 12);
    if (num > cap) num = cap;
    ByteReader r(p + 6, len - 6);
    out.clear();
    for (u16 i = 0; i < num; ++i) {
        QuestHunt h;
        h.questId = r.u32le();
        h.mobId = r.u32le();
        h.need = r.u16le();
        h.count = r.u16le();
        out.push_back(h);
    }
    return len;
}

usize decodeQuestActive(const u8* p, usize n, u32& id, bool& active) {
    if (n < 7) return 0;  // R 02b7 <quest id>.L <active>.B (7B)
    id = static_cast<u32>(p[2] | (p[3] << 8) | (p[4] << 16) | (p[5] << 24));
    active = p[6] != 0;
    return 7;
}

std::vector<u8> buildPartyCreate(const std::string& name) {
    // S 00f9 <name>.24B (26B) -- CZ_MAKE_GROUP. PV7 server only binds this opcode to party-create;
    // the newer createparty2 (0x01e8, +share bytes) lives in a higher packet_ver block it ignores.
    ByteWriter w;
    w.u16le(PKT_CZ_PARTY_CREATE);
    writeFixed(w, utf8ToCp1251(name), 24);
    return w.data();
}

std::vector<u8> buildPartyInvite(u32 aid) {
    ByteWriter w;  // S 00fc <AID>.L (6B)
    w.u16le(PKT_CZ_PARTY_INVITE);
    w.u32le(aid);
    return w.data();
}

std::vector<u8> buildPartyInviteReply(u32 aid, bool accept) {
    ByteWriter w;  // S 00ff <AID>.L <flag>.L (10B) -- 1 = accept, 0 = reject
    w.u16le(PKT_CZ_PARTY_INVITE_REPLY);
    w.u32le(aid);
    w.u32le(accept ? 1u : 0u);
    return w.data();
}

std::vector<u8> buildPartyLeave() {
    ByteWriter w;  // S 0100 (2B)
    w.u16le(PKT_CZ_PARTY_LEAVE);
    return w.data();
}

std::vector<u8> buildPartyKick(u32 aid, const std::string& name) {
    ByteWriter w;  // S 0103 <AID>.L <name>.24B (30B)
    w.u16le(PKT_CZ_PARTY_KICK);
    w.u32le(aid);
    writeFixed(w, utf8ToCp1251(name), 24);
    return w.data();
}

std::vector<u8> buildCreateChatRoom(const std::string& title, u16 limit, bool isPublic,
                                    const std::string& password) {
    // S 00d5 <len>.W <limit>.W <public>.B <password>.8B <title>.* -- title fills the rest, no terminator.
    const std::string t = utf8ToCp1251(title);
    ByteWriter w;
    w.u16le(PKT_CZ_CREATE_CHATROOM);
    w.u16le(static_cast<u16>(t.size() + 15));  // total packet length
    w.u16le(limit);
    w.u8v(isPublic ? 1u : 0u);
    writeFixed(w, utf8ToCp1251(password), 8);
    w.write_string(t);
    return w.data();
}

usize decodeChatRoomInfo(const u8* p, usize n, ChatRoomInfo& out) {
    // R 00d7 <len>.W <ownerID>.L <id>.L <limit>.W <users>.W <public>.B <title>.* (var)
    if (n < 4) return 0;
    const u16 len = static_cast<u16>(p[2] | (p[3] << 8));
    if (len < 17 || n < len) return 0;
    ByteReader r(p + 4, len - 4);
    out.ownerId = r.u32le();
    out.id = r.u32le();
    out.limit = r.u16le();
    out.users = r.u16le();
    out.isPublic = (r.u8v() != 0);
    std::string raw = r.read_string(len - 17);  // title fills the rest, not null-terminated
    const auto z = raw.find('\0');
    if (z != std::string::npos) raw.resize(z);
    out.title = cp1251ToUtf8(raw);
    return len;
}

usize decodeChatRoomRemove(const u8* p, usize n, u32& id) {
    if (n < 6) return 0;  // R 00d8 <id>.L (6B)
    ByteReader r(p + 2, 4);
    id = r.u32le();
    return 6;
}

usize decodeChatJoinResult(const u8* p, usize n, u8& type) {
    if (n < 3) return 0;  // R 00da <type>.B (3B): 0 = ok
    type = p[2];
    return 3;
}

std::vector<u8> buildJoinChatRoom(u32 id, const std::string& password) {
    ByteWriter w;  // S 00d9 <id>.L <password>.8B (14B)
    w.u16le(PKT_CZ_CHATROOM_JOIN);
    w.u32le(id);
    writeFixed(w, utf8ToCp1251(password), 8);
    return w.data();
}

usize decodeChatRoomEnter(const u8* p, usize n, u32& chatId, std::vector<std::string>& members) {
    // R 00db <len>.W <chatId>.L { <role>.L <name>.24B }*  -- the room's member list on entry.
    if (n < 8) return 0;
    const u16 len = static_cast<u16>(p[2] | (p[3] << 8));
    if (len < 8 || n < len) return 0;
    members.clear();
    chatId = static_cast<u32>(p[4] | (p[5] << 8) | (p[6] << 16) | (p[7] << 24));
    constexpr usize kRec = 28;  // role(4) + name(24)
    const usize count = (len - 8) / kRec;
    ByteReader r(p + 8, len - 8);
    for (usize i = 0; i < count; ++i) {
        r.u32le();  // role (0 = owner)
        members.push_back(cp1251ToUtf8(r.read_cstring(24)));
    }
    return len;
}

usize decodeChatRoomMemberAdd(const u8* p, usize n, u16& users, std::string& name) {
    if (n < 28) return 0;  // R 00dc <users>.W <name>.24B (28B)
    ByteReader r(p + 2, 26);
    users = r.u16le();
    name = cp1251ToUtf8(r.read_cstring(24));
    return 28;
}

usize decodeChatRoomMemberLeave(const u8* p, usize n, u16& users, std::string& name) {
    if (n < 29) return 0;  // R 00dd <users>.W <name>.24B <flag>.B (29B)
    ByteReader r(p + 2, 27);
    users = r.u16le();
    name = cp1251ToUtf8(r.read_cstring(24));
    return 29;
}

std::vector<u8> buildLeaveChatRoom() {
    ByteWriter w;  // S 00e3 (2B)
    w.u16le(PKT_CZ_CHATROOM_LEAVE);
    return w.data();
}

// ---- Guild (#78 part 4) ----

usize decodeGuildName(const u8* p, usize n, u32& id, std::string& name) {
    // R 016c <guildID>.L <emblemID>.L <mode>.L x5 <name>.24B (43B)
    if (n < 43) return 0;
    ByteReader r(p + 2, 41);
    id = r.u32le();
    r.u32le();  // emblemID
    r.u32le();  // mode
    r.skip(5);
    name = cp1251ToUtf8(r.read_cstring(24));
    return 43;
}

usize decodeGuildInfo(const u8* p, usize n, GuildInfo& g) {
    // R 01b6 <ID>.L <lv,conMem,maxMem,avg,exp,expN,tax,t1,t2>.L9 <emblem>.L <name>.24 <master>.24 <castles>.20
    if (n < 114) return 0;
    ByteReader r(p + 2, 112);
    g.id = r.u32le();
    g.level = r.u32le();
    g.members = r.u32le();
    g.maxMembers = r.u32le();
    g.avgLevel = r.u32le();
    g.exp = r.u32le();
    g.expNext = r.u32le();
    g.tax = r.u32le();
    r.u32le();  // tendency left/right
    r.u32le();  // tendency down/up
    g.emblemId = r.u32le();
    g.name = cp1251ToUtf8(r.read_cstring(24));
    g.master = cp1251ToUtf8(r.read_cstring(24));
    g.castles = r.read_cstring(20);  // territory ("" = none taken)
    return 114;
}

usize decodeGuildNotice(const u8* p, usize n, std::string& subject, std::string& body) {
    // R 016f <subject>.60B <notice>.120B (182B)
    if (n < 182) return 0;
    ByteReader r(p + 2, 180);
    subject = cp1251ToUtf8(r.read_cstring(60));
    body = cp1251ToUtf8(r.read_cstring(120));
    return 182;
}

usize decodeGuildMembers(const u8* p, usize n, std::vector<GuildMember>& out) {
    // R 0154 <len>.W { AID.L CID.L hair.W hairColor.W sex.W class.W lv.W exp.L online.L position.L memo.50 name.24 }*
    if (n < 4) return 0;
    const u16 len = static_cast<u16>(p[2] | (p[3] << 8));
    if (len < 4 || n < len) return 0;
    out.clear();
    constexpr usize kRec = 104;
    const usize count = (len - 4) / kRec;
    ByteReader r(p + 4, len - 4);
    for (usize i = 0; i < count; ++i) {
        GuildMember m;
        m.accountId = r.u32le();
        m.charId = r.u32le();
        r.u16le();  // hair
        r.u16le();  // hair color
        r.u16le();  // sex
        m.job = r.u16le();
        m.level = r.u16le();
        r.u32le();  // contribution exp
        m.online = (r.u32le() != 0);
        m.position = r.u32le();
        r.skip(50);  // intro/memo
        m.name = cp1251ToUtf8(r.read_cstring(24));
        out.push_back(std::move(m));
    }
    return len;
}

usize decodeGuildInvite(const u8* p, usize n, u32& guildId, std::string& name) {
    // R 016a <guildID>.L <name>.24B (30B)
    if (n < 30) return 0;
    ByteReader r(p + 2, 28);
    guildId = r.u32le();
    name = cp1251ToUtf8(r.read_cstring(24));
    return 30;
}

usize decodeGuildMemberOnline(const u8* p, usize n, u32& aid, u32& cid, bool& online) {
    // R 016d <AID>.L <CID>.L <online>.L (14B)
    if (n < 14) return 0;
    ByteReader r(p + 2, 12);
    aid = r.u32le();
    cid = r.u32le();
    online = (r.u32le() != 0);
    return 14;
}

static GuildPosition* findOrAddPos(std::vector<GuildPosition>& out, u32 id) {
    for (auto& g : out)
        if (g.id == id) return &g;
    out.push_back({});
    out.back().id = id;
    return &out.back();
}

usize decodeGuildPositionNames(const u8* p, usize n, std::vector<GuildPosition>& out) {
    // R 0166 <len>.W { <id>.L <name>.24B }*  -- merge names into the position list by id.
    if (n < 4) return 0;
    const u16 len = static_cast<u16>(p[2] | (p[3] << 8));
    if (len < 4 || n < len) return 0;
    constexpr usize kRec = 28;
    const usize count = (len - 4) / kRec;
    ByteReader r(p + 4, len - 4);
    for (usize i = 0; i < count; ++i) {
        const u32 id = r.u32le();
        std::string name = cp1251ToUtf8(r.read_cstring(24));
        findOrAddPos(out, id)->name = std::move(name);
    }
    return len;
}

usize decodeGuildPositionInfo(const u8* p, usize n, std::vector<GuildPosition>& out) {
    // R 0160 <len>.W { <id>.L <mode>.L <ranking>.L <tax>.L }*  -- merge perms/tax by id.
    if (n < 4) return 0;
    const u16 len = static_cast<u16>(p[2] | (p[3] << 8));
    if (len < 4 || n < len) return 0;
    constexpr usize kRec = 16;
    const usize count = (len - 4) / kRec;
    ByteReader r(p + 4, len - 4);
    for (usize i = 0; i < count; ++i) {
        const u32 id = r.u32le();
        const u32 mode = r.u32le();
        const u32 ranking = r.u32le();
        const u32 tax = r.u32le();
        GuildPosition* e = findOrAddPos(out, id);
        e->canInvite = (mode & 0x01u) != 0;  // GUILD_PERM_INVITE
        e->canPunish = (mode & 0x10u) != 0;  // GUILD_PERM_PUNISH
        e->ranking = ranking;
        e->tax = tax;
    }
    return len;
}

usize decodeGuildExpelList(const u8* p, usize n, std::vector<GuildExpel>& out) {
    // R 0163 <len>.W { <name>.24B <reason>.40B }*  (this era; newer adds an account-name field).
    if (n < 4) return 0;
    const u16 len = static_cast<u16>(p[2] | (p[3] << 8));
    if (len < 4 || n < len) return 0;
    constexpr usize kRec = 64;
    const usize count = (len - 4) / kRec;
    out.clear();
    ByteReader r(p + 4, len - 4);
    for (usize i = 0; i < count; ++i) {
        GuildExpel e;
        e.name = cp1251ToUtf8(r.read_cstring(24));
        e.reason = cp1251ToUtf8(r.read_cstring(40));
        out.push_back(std::move(e));
    }
    return len;
}

usize decodeGuildPositionChanged(const u8* p, usize n, u32& posId, u32& mode, u32& tax,
                                 std::string& name) {
    // R 0174 <posId>.L <mode>.L <posId>.L <tax>.L <name>.24B (44B) -- a position's perms/name changed.
    if (n < 44) return 0;
    ByteReader r(p + 4, 40);  // skip id(2) + len(2)
    posId = r.u32le();
    mode = r.u32le();
    r.u32le();  // posId repeated
    tax = r.u32le();
    name = cp1251ToUtf8(r.read_cstring(24));
    return 44;
}

usize decodeGuildMemberPosChanged(const u8* p, usize n, u32& aid, u32& cid, u32& posId) {
    // R 0156 <AID>.L <CID>.L <posId>.L (16B) -- a member's rank changed.
    if (n < 16) return 0;
    ByteReader r(p + 4, 12);
    aid = r.u32le();
    cid = r.u32le();
    posId = r.u32le();
    return 16;
}

usize decodeGuildAllyList(const u8* p, usize n, std::vector<GuildAlly>& out) {
    // R 014c <len>.W { <opposition>.L <guildID>.L <name>.24B }*  (32B/rec)
    // opposition: 0 = alliance, 1 = antagonist/enemy.
    if (n < 4) return 0;
    const u16 len = static_cast<u16>(p[2] | (p[3] << 8));
    if (len < 4 || n < len) return 0;
    constexpr usize kRec = 32;
    const usize count = (len - 4) / kRec;
    out.clear();
    ByteReader r(p + 4, len - 4);
    for (usize i = 0; i < count; ++i) {
        GuildAlly a;
        a.enemy = (r.u32le() != 0);
        a.guildId = r.u32le();
        a.name = cp1251ToUtf8(r.read_cstring(24));
        out.push_back(std::move(a));
    }
    return len;
}

usize decodeMercInfo(const u8* p, usize n, MercInfo& out) {
    // R 029b <gid>.L <atk>.W <matk>.W <hit>.W <cri>.W <def>.W <mdef>.W <flee>.W <aspd>.W
    //         <name>.24B <lv>.W <hp>.L <maxhp>.L <sp>.L <maxsp>.L <expire>.L <faith>.W
    //         <summons>.L <kills>.L <range>.W  (80B)
    if (n < 80) return 0;
    ByteReader r(p + 2, 78);
    out.gid = r.u32le();
    out.atk = r.u16le();
    out.matk = r.u16le();
    out.hit = r.u16le();
    out.cri = r.u16le();
    out.def = r.u16le();
    out.mdef = r.u16le();
    out.flee = r.u16le();
    out.aspd = r.u16le();
    out.name = cp1251ToUtf8(r.read_cstring(24));
    out.level = r.u16le();
    out.hp = static_cast<i32>(r.u32le());
    out.maxHp = static_cast<i32>(r.u32le());
    out.sp = static_cast<i32>(r.u32le());
    out.maxSp = static_cast<i32>(r.u32le());
    out.expireTime = r.u32le();
    out.faith = r.u16le();
    out.summons = r.u32le();
    out.kills = r.u32le();
    out.range = r.u16le();
    return 80;
}

usize decodeMercPar(const u8* p, usize n, u16& type, i32& value) {
    if (n < 8) return 0;  // R 02a2 <type>.W <value>.L
    ByteReader r(p + 2, 6);
    type = r.u16le();
    value = static_cast<i32>(r.u32le());
    return 8;
}

std::vector<u8> buildMercCommand(u8 option) {
    ByteWriter w;  // S 029f <option>.B (5B): option 2 = dismiss the mercenary
    w.u16le(PKT_CZ_MER_COMMAND);
    w.u8v(option);
    w.u8v(0);
    w.u8v(0);
    return w.data();
}

usize decodeHomunInfo(const u8* p, usize n, HomunInfo& out) {
    // R 022e <name>.24B <flags>.B <lv>.W <hunger>.W <intimacy>.W <equip>.W <atk>.W <matk>.W
    //         <hit>.W <cri>.W <def>.W <mdef>.W <flee>.W <aspd>.W <hp>.W <maxhp>.W <sp>.W
    //         <maxsp>.W <exp>.L <expnext>.L <skillpts>.W <attackable>.W  (71B)
    if (n < 71) return 0;
    ByteReader r(p + 2, 69);
    out.name = cp1251ToUtf8(r.read_cstring(24));
    const u8 flags = r.u8v();
    out.renamed   = (flags & 0x1) != 0;
    out.vaporized = (flags & 0x2) != 0;
    out.dead      = (flags & 0x4) != 0;
    out.level    = r.u16le();
    out.hunger   = r.u16le();
    out.intimacy = r.u16le();
    r.u16le();  // equip id (unused)
    out.atk  = r.u16le();
    out.matk = r.u16le();
    out.hit  = r.u16le();
    out.cri  = r.u16le();
    out.def  = r.u16le();
    out.mdef = r.u16le();
    out.flee = r.u16le();
    out.aspd = r.u16le();
    out.hp    = r.u16le();
    out.maxHp = r.u16le();
    out.sp    = r.u16le();
    out.maxSp = r.u16le();
    out.exp     = r.u32le();
    out.expNext = r.u32le();
    out.skillPts = r.u16le();
    return 71;
}

usize decodeHomSkills(const u8* p, usize n, std::vector<HomSkill>& out) {
    // R 0235 <len>.W { <id>.W <inf>.W <unused>.W <lv>.W <sp>.W <range>.W <name>.24B <up>.B }*
    out.clear();
    if (n < 4) return 0;
    const usize len = static_cast<usize>(p[2] | (p[3] << 8));
    const usize total = (len <= n) ? len : n;  // trust the smaller of framed vs declared
    usize o = 4;
    while (o + 37 <= total) {
        ByteReader r(p + o, 37);
        HomSkill s;
        s.id    = r.u16le();
        s.inf   = r.u16le();
        r.u16le();  // unused word
        s.level = r.u16le();
        s.sp    = r.u16le();
        s.range = r.u16le();
        s.name  = r.read_cstring(24);  // ASCII internal skill id from skill_db
        s.upgradeable = r.u8v() != 0;
        if (s.id != 0) out.push_back(s);
        o += 37;
    }
    return total;
}

usize decodeRentalTime(const u8* p, usize n, u16& nameid, u32& seconds) {
    if (n < 8) return 0;  // R 0298 <nameid>.W <seconds>.L
    ByteReader r(p + 2, 6);
    nameid = r.u16le();
    seconds = r.u32le();
    return 8;
}

usize decodeRentalExpired(const u8* p, usize n, u16& invIndex, u16& nameid) {
    if (n < 6) return 0;  // R 0299 <index>.W <nameid>.W ; index is already slot+2 == our InvItem.index
    ByteReader r(p + 2, 4);
    invIndex = r.u16le();
    nameid = r.u16le();
    return 6;
}

std::vector<u8> buildHomMenu(u8 command) {
    ByteWriter w;  // S 022d <pad>.W <command>.B (5B): merc_menu 1=feed, 2=delete, 3=rest(vaporize)
    w.u16le(0x022d);
    w.u16le(0);
    w.u8v(command);
    return w.data();
}

std::vector<u8> buildHomName(const std::string& name) {
    ByteWriter w;  // S 0231 <name>.24B (26B): CZ_RENAME_MER -> merc_hom_change_name (one-time). cp1251.
    w.u16le(0x0231);
    writeFixed(w, utf8ToCp1251(name), 24);
    return w.data();
}

std::vector<u8> buildPetMenu(u8 command) {
    ByteWriter w;  // S 01a1 <command>.B (3B): 0=info, 1=feed, 2=performance, 3=return to egg
    w.u16le(0x01a1);
    w.u8v(command);
    return w.data();
}

std::vector<u8> buildSelectPetEgg(u16 invIndex) {
    ByteWriter w;  // S 01a7 <index>.W (4B): hatch the egg at this inventory index
    w.u16le(0x01a7);
    w.u16le(invIndex);
    return w.data();
}

std::vector<u8> buildPetCatch(u32 mobGid) {
    ByteWriter w;  // S 019f <mob gid>.L (6B): the taming target picked by the player
    w.u16le(0x019f);
    w.u32le(mobGid);
    return w.data();
}

std::vector<u8> buildPetRename(const std::string& name) {
    ByteWriter w;  // S 01a5 <name>.24B (26B), NUL-padded
    w.u16le(0x01a5);
    for (usize i = 0; i < 24; ++i) w.u8v(i < name.size() ? static_cast<u8>(name[i]) : 0);
    return w.data();
}

std::vector<u8> buildHomAttack(u32 targetGid) {
    ByteWriter w;  // S 0233 <id>.L <target>.L <action>.B (11B); server uses only <target>
    w.u16le(0x0233);
    w.u32le(0);
    w.u32le(targetGid);
    w.u8v(0);
    return w.data();
}

std::vector<u8> buildHomMoveToMaster() {
    ByteWriter w;  // S 0234 <id>.L (6B); server recalls the homun to the master, ignores <id>
    w.u16le(0x0234);
    w.u32le(0);
    return w.data();
}

std::vector<u8> buildHomMoveTo(u16 x, u16 y) {
    ByteWriter w;  // S 0232 <id>.L <dest>.3B (9B); server (clif_parse_HomMoveTo) reads dest at off 6
    w.u16le(0x0232);
    w.u32le(0);  // <id> ignored; server acts on sd->hd
    w.u8v(static_cast<u8>(x >> 2));                         // standard 3-byte cell pack (as 0x85/0xa7)
    w.u8v(static_cast<u8>((x << 6) | ((y >> 4) & 0x3f)));
    w.u8v(static_cast<u8>(y << 4));
    return w.data();
}

std::vector<u8> buildGuildNotice(u32 guildId, const std::string& subject, const std::string& body) {
    ByteWriter w;  // S 016e <guildID>.L <subject>.60B <body>.120B (186B)
    w.u16le(PKT_CZ_GUILD_NOTICE);
    w.u32le(guildId);
    writeFixed(w, utf8ToCp1251(subject), 60);
    writeFixed(w, utf8ToCp1251(body), 120);
    return w.data();
}

std::vector<u8> buildGuildReply(u32 guildId, bool accept) {
    ByteWriter w;  // S 016b <guildID>.L <flag>.L (10B) -- 1 = accept, 0 = reject
    w.u16le(PKT_CZ_GUILD_INVITE_REPLY);
    w.u32le(guildId);
    w.u32le(accept ? 1u : 0u);
    return w.data();
}

std::vector<u8> buildGuildInvite(u32 targetAid, u32 myAid, u32 myCid) {
    ByteWriter w;  // S 0168 <targetAID>.L <myAID>.L <myCID>.L (14B)
    w.u16le(PKT_CZ_GUILD_INVITE_REQ);
    w.u32le(targetAid);
    w.u32le(myAid);
    w.u32le(myCid);
    return w.data();
}

std::vector<u8> buildGuildLeave(u32 guildId, u32 aid, u32 cid) {
    ByteWriter w;  // S 0159 <guildID>.L <AID>.L <CID>.L <reason>.40B (54B)
    w.u16le(PKT_CZ_GUILD_LEAVE);
    w.u32le(guildId);
    w.u32le(aid);
    w.u32le(cid);
    writeFixed(w, "", 40);
    return w.data();
}

std::vector<u8> buildGuildCreate(u32 charId, const std::string& name) {
    ByteWriter w;  // S 0165 <charID>.L <name>.24B (30B)
    w.u16le(PKT_CZ_GUILD_CREATE);
    w.u32le(charId);
    writeFixed(w, utf8ToCp1251(name), 24);
    return w.data();
}

std::vector<u8> buildGuildInfoRequest(u32 page) {
    ByteWriter w;  // S 014f <page>.L (6B) -- ask the server to (re)send a guild page
    w.u16le(PKT_CZ_REQ_GUILD_MENU);
    w.u32le(page);
    return w.data();
}

std::vector<u8> buildGuildChangePosition(u32 posId, bool canInvite, bool canPunish, u32 ranking,
                                         u32 tax, const std::string& name) {
    // S 0161 <len>.W { <id>.L <mode>.L <ranking>.L <tax>.L <name>.24B } -- mode: invite 0x1, punish 0x10.
    ByteWriter w;
    w.u16le(PKT_CZ_GUILD_CHANGE_POSITION);
    w.u16le(44);  // 4 header + one 40-byte entry
    w.u32le(posId);
    w.u32le((canInvite ? 0x01u : 0u) | (canPunish ? 0x10u : 0u));
    w.u32le(ranking);
    w.u32le(tax);
    writeFixed(w, utf8ToCp1251(name), 24);
    return w.data();
}

std::vector<u8> buildGuildEmblem(const std::vector<u8>& bmp) {
    // S 0153 <len>.W <bitmap>.* -- upload a new guild emblem (the raw .bmp file). len = 4 + bytes.
    ByteWriter w;
    w.u16le(0x0153);
    w.u16le(static_cast<u16>(4 + bmp.size()));
    w.write_bytes(bmp.data(), bmp.size());
    return w.data();
}

std::vector<u8> buildGuildChangeMemberPosition(u32 aid, u32 cid, u32 posId) {
    // S 0155 <len>.W { <AID>.L <CID>.L <posId>.L } -- assign a member to a position.
    ByteWriter w;
    w.u16le(PKT_CZ_GUILD_CHANGE_MEMBER_POS);
    w.u16le(16);  // 4 header + one 12-byte entry
    w.u32le(aid);
    w.u32le(cid);
    w.u32le(posId);
    return w.data();
}

std::vector<u8> buildIgnorePlayer(const std::string& name, bool block) {
    // S 00cf <name>.24B <type>.B (27B) — type 0 = block this player's whispers, 1 = unblock.
    ByteWriter w;
    w.u16le(PKT_CZ_SETTING_WHISPER_PC);
    writeFixed(w, name, 24);
    w.u8v(block ? 0u : 1u);
    return w.data();
}

std::vector<u8> buildTradeReply(bool accept) {
    ByteWriter w;  // S 00e6 <result>.B (3B) — 3 = accept, 4 = reject
    w.u16le(PKT_CZ_EXCHANGE_ACK);
    w.u8v(accept ? 3u : 4u);
    return w.data();
}

std::vector<u8> buildTradeAdd(u16 index, u32 amount) {
    ByteWriter w;  // S 00e8 <index>.W <amount>.L (8B) — index = inventory slot+2; index 0 = add zeny
    w.u16le(PKT_CZ_EXCHANGE_ADD);
    w.u16le(index);
    w.u32le(amount);
    return w.data();
}

std::vector<u8> buildTradeLock() {
    ByteWriter w;  // S 00eb (2B) — lock in my side
    w.u16le(PKT_CZ_EXCHANGE_LOCK);
    return w.data();
}

std::vector<u8> buildTradeConfirm() {
    ByteWriter w;  // S 00ef (2B) — confirm/commit the trade (uOK210 sendDealTrade, CZ_EXEC_EXCHANGE_ITEM)
    w.u16le(PKT_CZ_EXCHANGE_EXEC);
    return w.data();
}

std::vector<u8> buildTradeCancel() {
    ByteWriter w;  // S 00ed (2B) — cancel the trade (uOK210 sendCurrentDealCancel, CZ_CANCEL_EXCHANGE_ITEM)
    w.u16le(PKT_CZ_EXCHANGE_CANCEL);
    return w.data();
}

usize decodeWhisperAck(const u8* p, usize n, u8& result) {
    // 0x98: id(2) result(1) — 0 ok, 1 target offline/not found, 2 ignored by the target.
    if (n < 3) return 0;
    result = p[2];
    return 3;
}

std::vector<u8> buildTickSend(u32 tick) {
    // S 0089 — ServerType8 (uaRO) sync/keepalive (CZ_REQUEST_TIME), 8 bytes, matching uOK210
    // sendSync: 0089 0000 <tick>.L. The server keeps the session's VIEW fresh off this; the
    // standard 0x007e we sent before is not the ticksend on this server -> it never refreshed
    // our view, so units entering range arrived as plain moves, never spawns (#95).
    ByteWriter w;
    w.u16le(0x0089);
    w.u8v(0x00);
    w.u8v(0x00);
    w.u32le(tick);
    return w.data();
}

// ---------------------------------------------------------------------------
// Decoders
// ---------------------------------------------------------------------------
usize decodeAcceptLogin(const u8* p, usize n, AcceptLogin& out) {
    if (n < 4) return 0;
    const usize len = static_cast<usize>(p[2] | (p[3] << 8));
    if (len < 47 || n < len) return 0;

    // login.c: id@0, len@2, login_id1@4, account_id@8, login_id2@12, (unused ip)@16,
    // (unused 24B name)@20, sex@46; the char-server list starts at offset 47.
    auto rd32 = [p](usize o) {
        return static_cast<u32>(p[o] | (p[o + 1] << 8) | (p[o + 2] << 16) | (p[o + 3] << 24));
    };
    out.loginId1 = rd32(4);
    out.accountId = rd32(8);
    out.loginId2 = rd32(12);
    out.sex = p[46];

    out.servers.clear();
    const usize count = (len - 47) / 32;
    for (usize i = 0; i < count; ++i) {
        const usize base = 47 + i * 32;
        CharServer s;
        s.ip = ipToString(p + base + 0);
        s.port = static_cast<u16>(p[base + 4] | (p[base + 5] << 8));
        ByteReader nr(p + base + 6, 20);
        s.name = nr.read_cstring(20);
        s.users = static_cast<u16>(p[base + 26] | (p[base + 27] << 8));
        s.type = static_cast<u16>(p[base + 28] | (p[base + 29] << 8));
        s.newFlag = static_cast<u16>(p[base + 30] | (p[base + 31] << 8));
        out.servers.push_back(std::move(s));
    }
    return len;
}

usize decodeRefuseLogin(const u8* p, usize n, u8& code, std::string& banDate) {
    if (n < 23) return 0;  // 0x6a is a fixed 23-byte packet
    code = p[2];
    ByteReader r(p + 3, 20);
    banDate = r.read_cstring(20);
    return 23;
}

usize decodeNotifyBan(const u8* p, usize n, u8& code) {
    if (n < 3) return 0;
    code = p[2];
    return 3;
}

// One character record, shared by the char list (0x6b) and the make-char accept (0x6d):
// both carry the same mmo_char_tobuf block, just framed differently.
static void decodeOneChar(const u8* b, usize rec, CharInfo& c) {
    ByteReader r(b, rec);
    c.charId = r.u32le();
    c.baseExp = r.u32le();
    c.zeny = r.u32le();
    c.jobExp = r.u32le();
    c.jobLevel = r.u32le();
    r.skip(4 * 3);  // opt1, opt2, option
    r.skip(4 * 2);  // karma, manner
    c.statusPoint = r.u16le();
    c.hp = r.u16le();
    c.maxHp = r.u16le();
    c.sp = r.u16le();
    c.maxSp = r.u16le();
    c.speed = r.u16le();
    c.class_ = r.u16le();
    c.hair = r.u16le();
    c.weapon = r.u16le();
    c.baseLevel = r.u16le();
    c.skillPoint = r.u16le();
    c.headBottom = r.u16le();
    c.shield = r.u16le();
    c.headTop = r.u16le();
    c.headMid = r.u16le();
    c.hairColor = r.u16le();
    c.clothesColor = r.u16le();
    c.name = cp1251ToUtf8(r.read_cstring(24));  // char name is Windows-1251 on the wire
    c.str = r.u8v();
    c.agi = r.u8v();
    c.vit = r.u8v();
    c.int_ = r.u8v();
    c.dex = r.u8v();
    c.luk = r.u8v();
    c.slot = r.u16le();
}

usize decodeCharList(const u8* p, usize n, std::vector<CharInfo>& out) {
    if (n < 4) return 0;
    const usize len = static_cast<usize>(p[2] | (p[3] << 8));
    if (len < 24 || n < len) return 0;

    out.clear();
    const usize body = len - 24;  // 24B header (id + len + 20 unknown)
    if (body == 0) return len;     // no characters

    usize rec = 0;
    for (usize cand : kCharRecordSizes) {
        if (body % cand == 0) { rec = cand; break; }
    }
    if (rec == 0) {
        log::warn("net: char list body {} matches no known record size", body);
        return len;  // consume to stay in sync, but yield no chars
    }

    const usize count = body / rec;
    for (usize i = 0; i < count; ++i) {
        CharInfo c;
        decodeOneChar(p + 24 + i * rec, rec, c);
        out.push_back(std::move(c));
    }
    return len;
}

// HC_ACCEPT_MAKECHAR (0x6d): id(2) + ONE char record (same block as a list entry). The new
// character was created; decode it so the char-select screen can add it without a relog.
usize decodeMakeCharAccept(const u8* p, usize n, CharInfo& out) {
    // id(2) + one record. decodeOneChar reads the first 106 bytes of the record (the fields the
    // client uses); the framing tells us the exact length (0x6d is fixed-size per PACKETVER), so
    // accept any payload long enough to hold a record rather than pinning one size.
    if (n < 2 + 106) return 0;
    decodeOneChar(p + 2, n - 2, out);
    return n;
}

// HC_REFUSE_MAKECHAR (0x6e): id(2) + reason(1). 0x00 name exists, 0x01 underaged, 0x02 denied.
usize decodeMakeCharRefuse(const u8* p, usize n, u8& code) {
    if (n < 3) return 0;
    code = p[2];
    return 3;
}

usize decodeRefuseEnter(const u8* p, usize n, u8& code) {
    if (n < 3) return 0;
    code = p[2];
    return 3;
}

usize decodeZoneServer(const u8* p, usize n, ZoneServer& out) {
    if (n < 28) return 0;  // 0x71 is a fixed 28-byte packet
    ByteReader r(p, 28);
    r.skip(2);
    out.charId = r.u32le();
    out.mapName = r.read_cstring(16);
    out.ip = ipToString(p + 22);
    out.port = static_cast<u16>(p[26] | (p[27] << 8));
    return 28;
}

usize decodeDeleteAccept(const u8* p, usize n) {
    (void)p;
    if (n < 2) return 0;  // 0x6f: id only, no payload
    return 2;
}

usize decodeDeleteRefuse(const u8* p, usize n, u8& reason) {
    if (n < 3) return 0;  // 0x70: id + reason byte (0 = incorrect email)
    reason = p[2];
    return 3;
}

usize decodeMapAuthOk(const u8* p, usize n, MapAuth& out) {
    if (n < 11) return 0;  // 0x73 is a fixed 11-byte packet
    out.tick = static_cast<u32>(p[2] | (p[3] << 8) | (p[4] << 16) | (p[5] << 24));
    // 3-byte packed position at offset 6 (WBUFPOS: x>>2 / (x<<6|y>>4) / (y<<4|dir)).
    const u8 b0 = p[6], b1 = p[7], b2 = p[8];
    out.x = static_cast<u16>((b0 << 2) | ((b1 >> 6) & 0x03));
    out.y = static_cast<u16>(((b1 & 0x3f) << 4) | ((b2 >> 4) & 0x0f));
    out.dir = static_cast<u8>(b2 & 0x0f);
    return 11;
}

// ---------------------------------------------------------------------------
// Stream framing + actor (unit) entries  (map-server, PACKETVER 7 layouts)
// ---------------------------------------------------------------------------
namespace {
inline u16 rd16(const u8* p, usize o) { return static_cast<u16>(p[o] | (p[o + 1] << 8)); }
inline u32 rd32(const u8* p, usize o) {
    return static_cast<u32>(p[o] | (p[o + 1] << 8) | (p[o + 2] << 16) | (p[o + 3] << 24));
}
// WBUFPOS: 3 bytes -> x,y,dir (see clif.c WBUFPOS).
void unpackPos(const u8* p, u16& x, u16& y, u8& dir) {
    const u8 b0 = p[0], b1 = p[1], b2 = p[2];
    x = static_cast<u16>((b0 << 2) | ((b1 >> 6) & 0x03));
    y = static_cast<u16>(((b1 & 0x3f) << 4) | ((b2 >> 4) & 0x0f));
    dir = static_cast<u8>(b2 & 0x0f);
}
// WBUFPOS2: 6 bytes -> src (x0,y0) + dst (x1,y1); dir is not encoded.
void unpackPos2(const u8* p, u16& x0, u16& y0, u16& x1, u16& y1) {
    const u8 b0 = p[0], b1 = p[1], b2 = p[2], b3 = p[3], b4 = p[4];
    x0 = static_cast<u16>((b0 << 2) | ((b1 >> 6) & 0x03));
    y0 = static_cast<u16>(((b1 & 0x3f) << 4) | ((b2 >> 4) & 0x0f));
    x1 = static_cast<u16>(((b2 & 0x0f) << 6) | ((b3 >> 2) & 0x3f));
    y1 = static_cast<u16>(((b3 & 0x03) << 8) | b4);
}
} // namespace

FrameInfo nextPacket(const u8* p, usize n) {
    FrameInfo fi;
    if (n < 2) return fi;  // Need more
    fi.id = peekId(p, n);
    const int L = packetDbLength(fi.id);
    if (L > 0) {  // fixed length
        fi.length = static_cast<usize>(L);
        if (n >= fi.length) fi.status = Frame::Ready;
        return fi;
    }
    if (L < 0) {  // variable: u16 total length at offset 2
        if (n < 4) { fi.length = 4; return fi; }
        const usize total = rd16(p, 2);
        if (total < 4) { fi.status = Frame::Unknown; return fi; }  // corrupt header
        fi.length = total;
        if (n >= total) fi.status = Frame::Ready;
        return fi;
    }
    fi.status = Frame::Unknown;  // id not in the table
    return fi;
}

usize decodeActorEntry(const u8* p, usize n, ActorEntry& out) {
    if (n < 2) return 0;
    out = ActorEntry{};
    switch (peekId(p, n)) {
        case PKT_ZC_PC_STANDENTRY:   // 0x22a (58)
        case PKT_ZC_PC_NEWENTRY: {   // 0x22b (57) — fields 0..54 identical
            const usize len = (rd16(p, 0) == PKT_ZC_PC_STANDENTRY) ? 58 : 57;
            if (n < len) return 0;
            out.pc = true;
            out.gid = rd32(p, 2);
            out.speed = rd16(p, 6);  // ms/cell, right after the GID (mount/buff pace)
            out.opt1 = rd16(p, 8);   // body ailment (stone/freeze/stun/sleep) — sprite tint
            out.opt2 = rd16(p, 10);  // health-state bits (poison/curse) — sprite tint
            out.option = rd32(p, 12);
            out.class_ = rd16(p, 16);
            out.hair = rd16(p, 18);
            out.weapon = rd16(p, 20);
            out.shield = rd16(p, 22);
            out.headBottom = rd16(p, 24);
            out.headTop = rd16(p, 26);
            out.headMid = rd16(p, 28);
            out.hairColor = rd16(p, 30);
            out.clothesColor = rd16(p, 32);
            out.guildId = rd32(p, 36);
            out.emblemId = rd16(p, 40);
            out.sex = p[49];
            unpackPos(p + 50, out.x, out.y, out.dir);
            out.toX = out.x;
            out.toY = out.y;
            if (len == 58) out.level = rd16(p, 56);  // standentry carries level; newentry omits it
            return len;
        }
        case PKT_ZC_PC_MOVEENTRY: {  // 0x22c (64) — walking
            if (n < 64) return 0;
            out.pc = true;
            out.walking = true;
            out.gid = rd32(p, 2);
            out.speed = rd16(p, 6);  // ms/cell
            out.opt1 = rd16(p, 8);   // body ailment — sprite tint
            out.opt2 = rd16(p, 10);  // health-state bits — sprite tint
            out.option = rd32(p, 12);
            out.class_ = rd16(p, 16);
            out.hair = rd16(p, 18);
            out.weapon = rd16(p, 20);
            out.shield = rd16(p, 22);
            out.headBottom = rd16(p, 24);
            out.headTop = rd16(p, 30);
            out.headMid = rd16(p, 32);
            out.hairColor = rd16(p, 34);
            out.clothesColor = rd16(p, 36);
            out.guildId = rd32(p, 40);
            out.emblemId = rd16(p, 44);
            out.sex = p[53];
            unpackPos2(p + 54, out.x, out.y, out.toX, out.toY);
            out.level = rd16(p, 62);
            return 64;
        }
        case PKT_ZC_NPC_STANDENTRY: {  // 0x78 (54) — NPC/mob standing
            if (n < 54) return 0;
            out.gid = rd32(p, 2);
            out.speed = rd16(p, 6);  // ms/cell
            out.class_ = rd16(p, 14);
            out.hair = rd16(p, 16);
            out.headBottom = rd16(p, 20);
            out.shield = rd16(p, 22);
            out.headTop = rd16(p, 24);
            out.headMid = rd16(p, 26);
            out.hairColor = rd16(p, 28);
            out.clothesColor = rd16(p, 30);
            // A guild flag NPC (class FLAG_CLASS 722) carries its owning guild here: server clif_set0078
            // fills guild_id@34 + emblem_id@38 for EVERY unit (0 for ordinary mobs/NPCs), and additionally
            // duplicates them at @26/@22 for flags. Read the standard fields so the flag knows its guild ->
            // the client requests + renders that guild's emblem on the banner (verified in a live capture:
            // guildId 25/35, emblemVer 3/253). Harmless for non-guild units (both 0).
            out.guildId = rd32(p, 34);
            out.emblemId = rd16(p, 38);
            out.sex = p[45];
            unpackPos(p + 46, out.x, out.y, out.dir);
            out.toX = out.x;
            out.toY = out.y;
            out.level = rd16(p, 52);  // NPC/mob standentry carries the unit level
            return 54;
        }
        case PKT_ZC_NPC_NEWENTRY2: {  // 0x79 (53) — NPC/mob SPAWN (PV7): clif_spawn sends the 0x78
            if (n < 53) return 0;     // standentry truncated by its trailing byte, so the same fields 0..52
            out.gid = rd32(p, 2);
            out.speed = rd16(p, 6);
            out.class_ = rd16(p, 14);
            out.hair = rd16(p, 16);
            out.headBottom = rd16(p, 20);
            out.shield = rd16(p, 22);
            out.headTop = rd16(p, 24);
            out.headMid = rd16(p, 26);
            out.hairColor = rd16(p, 28);
            out.clothesColor = rd16(p, 30);
            // Same guild_id@34 + emblem_id@38 as the 0x78 standentry (both fit inside the 53B packet):
            // a guild flag (class 722) entering view via clif_spawn (0x79, when the player walks it into
            // range) MUST carry its owning guild too, or the flag renders with no emblem while flags seen
            // through the initial 0x78 area-scan show theirs -> "some castle flags have no emblem" (S.).
            out.guildId = rd32(p, 34);
            out.emblemId = rd16(p, 38);
            out.sex = p[45];
            unpackPos(p + 46, out.x, out.y, out.dir);  // pos fits in bytes 46..48, within the 53B packet
            out.toX = out.x;
            out.toY = out.y;
            // Only the level's HIGH byte (the 54th) is dropped; its low byte at 52 survives, so a
            // mob level <=255 (all of them) reads correctly -> the auto-attack filter shows "Lv." for
            // freshly-spawned mobs too, not just ones that stood still and sent a 0x78 (S.).
            out.level = p[52];
            return 53;
        }
        case PKT_ZC_NPC_NEWENTRY: {  // 0x7c (41) — NPC/mob spawn
            if (n < 41) return 0;
            out.gid = rd32(p, 2);
            out.hair = rd16(p, 14);
            out.headBottom = rd16(p, 18);
            out.class_ = rd16(p, 20);
            unpackPos(p + 36, out.x, out.y, out.dir);
            out.toX = out.x;
            out.toY = out.y;
            return 41;
        }
        default:
            return 0;
    }
}

usize decodeGuildEmblem(const u8* p, usize n, GuildEmblem& out) {
    // R 0152 <len>.W <guildID>.L <emblemVer>.L <bitmap>.?B  (bitmap = len - 12 bytes; the
    // payload is a 24x24 BMP, raw or zlib-deflated, magenta = transparent).
    if (n < 12) return 0;
    const usize len = rd16(p, 2);
    if (len < 12 || n < len) return 0;
    out = GuildEmblem{};
    out.guildId = rd32(p, 4);
    out.emblemId = rd32(p, 8);
    out.image.assign(p + 12, p + len);
    return len;
}

usize decodeVanish(const u8* p, usize n, u32& gid, u8& reason) {
    if (n < 7) return 0;
    gid = rd32(p, 2);
    reason = p[6];
    return 7;
}

usize decodeClassChange(const u8* p, usize n, u32& gid, u32& class_) {
    if (n < 11) return 0;  // 0x1b0 = cmd(2) + id(4) + type(1) + class(4)
    gid = rd32(p, 2);
    class_ = rd32(p, 7);
    return 11;
}

usize decodeItemIdentifyList(const u8* p, usize n, std::vector<u16>& indices) {
    if (n < 4) return 0;  // 0x177: cmd(2) + len(2) + [index(2)]*
    const usize len = rd16(p, 2);
    if (len < 4 || len > n) return 0;
    indices.clear();
    for (usize o = 4; o + 2 <= len; o += 2) indices.push_back(rd16(p, o));
    return len;
}

std::vector<u8> buildItemIdentify(u16 index) {
    ByteWriter w;  // S 0178 <index>.W (4B): identify the item at this inventory index (server does -2)
    w.u16le(0x0178);
    w.u16le(index);
    return w.data();
}

usize decodePlayerMove(const u8* p, usize n, MoveData& out) {
    if (n < 12) return 0;  // 0x87 = id(2) + tick(4) + WBUFPOS2(6)
    out.tick = rd32(p, 2);
    unpackPos2(p + 6, out.fromX, out.fromY, out.toX, out.toY);
    return 12;
}

usize decodeUnitMove(const u8* p, usize n, u32& gid, MoveData& out) {
    if (n < 16) return 0;  // 0x86 = id(2) + gid(4) + WBUFPOS2(6) + tick(4)
    gid = rd32(p, 2);
    unpackPos2(p + 6, out.fromX, out.fromY, out.toX, out.toY);
    out.tick = rd32(p, 12);
    return 16;
}

usize decodeStopMove(const u8* p, usize n, u32& id, u16& x, u16& y) {
    if (n < 10) return 0;  // 0x88: id(4)@2 x(2)@6 y(2)@8 (clif_fixpos)
    id = rd32(p, 2);
    x = rd16(p, 6);
    y = rd16(p, 8);
    return 10;
}

usize decodeMoveToAttack(const u8* p, usize n, MoveToAttack& out) {
    if (n < 16) return 0;  // 0x139: id(2) gid(4) tx(2) ty(2) sx(2) sy(2) range(2)
    out.gid = rd32(p, 2);
    out.x = rd16(p, 6);
    out.y = rd16(p, 8);
    out.selfX = rd16(p, 10);
    out.selfY = rd16(p, 12);
    out.range = rd16(p, 14);
    return 16;
}

std::string cp1251ToUtf8(const std::string& s) {
    // CP1251 0x80..0xBF -> Unicode (the 0xC0..0xFF range is the contiguous А..я block below).
    static const unsigned short hi[64] = {
        0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021, 0x20AC, 0x2030, 0x0409,
        0x2039, 0x040A, 0x040C, 0x040B, 0x040F, 0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
        0x2013, 0x2014, 0x0000, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F, 0x00A0,
        0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7, 0x0401, 0x00A9, 0x0404, 0x00AB,
        0x00AC, 0x00AD, 0x00AE, 0x0407, 0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6,
        0x00B7, 0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457};
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        unsigned cp = (c < 0x80) ? c : (c < 0xC0) ? hi[c - 0x80] : 0x0410u + (c - 0xC0);
        if (cp == 0) continue;  // undefined 0x98 slot
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return out;
}

std::string utf8ToCp1251(const std::string& s) {
    // Inverse of cp1251ToUtf8: map each decoded UTF-8 codepoint back to its Windows-1251 byte so
    // outgoing Russian chat reaches the server in the encoding it expects (S.: "кириллица в чате").
    // 0xC0..0xFF is the contiguous А..я block; 0x80..0xBF reuses the same special table as the
    // forward map (Ё/ё, №, typographic punctuation). ASCII passes through; anything else -> '?'.
    static const unsigned short hi[64] = {
        0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021, 0x20AC, 0x2030, 0x0409,
        0x2039, 0x040A, 0x040C, 0x040B, 0x040F, 0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
        0x2013, 0x2014, 0x0000, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F, 0x00A0,
        0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7, 0x0401, 0x00A9, 0x0404, 0x00AB,
        0x00AC, 0x00AD, 0x00AE, 0x0407, 0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6,
        0x00B7, 0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457};
    std::string out;
    out.reserve(s.size());
    usize i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        unsigned cp;
        usize len;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1Fu; len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0Fu; len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07u; len = 4; }
        else { ++i; continue; }            // stray continuation / invalid lead -> skip the byte
        if (i + len > s.size()) break;      // truncated multibyte tail
        for (usize k = 1; k < len; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3Fu);
        i += len;
        if (cp < 0x80) { out += static_cast<char>(cp); continue; }
        if (cp >= 0x0410 && cp <= 0x044F) { out += static_cast<char>(0xC0 + (cp - 0x0410)); continue; }
        unsigned char b = 0;
        for (int k = 0; k < 64; ++k) if (hi[k] == cp) { b = static_cast<unsigned char>(0x80 + k); break; }
        out += static_cast<char>(b ? b : '?');  // outside cp1251 (emoji, Latin-ext, ...) -> '?'
    }
    return out;
}

usize decodeScriptText(const u8* p, usize n, u32& gid, std::string& text) {
    if (n < 8) return 0;  // 0xb4/0xb7: id(2) len(2) gid(4) string[len-8] (NUL-terminated)
    const usize len = rd16(p, 2);
    if (len < 8 || n < len) return 0;
    gid = rd32(p, 4);
    text.clear();
    for (usize i = 8; i < len && p[i]; ++i) text += static_cast<char>(p[i]);
    text = cp1251ToUtf8(text);  // NPC dialog is Windows-1251 on the wire
    return len;
}

usize decodeScriptGid(const u8* p, usize n, u32& gid) {
    if (n < 6) return 0;  // 0xb5 (next) / 0xb6 (close): id(2) gid(4)
    gid = rd32(p, 2);
    return 6;
}

usize decodeAct(const u8* p, usize n, ActDamage& out) {
    if (n < 29) return 0;  // 0x8a: src@2 dst@6 tick@10 sdelay@14 ddelay@18 dmg@22 div@24 type@26
    out.src = rd32(p, 2);
    out.dst = rd32(p, 6);
    out.amotion = rd32(p, 14);  // sdelay: attacker's attack-motion time
    out.dmotion = rd32(p, 18);  // ddelay: victim's damage-motion (flinch) time
    out.damage = static_cast<i16>(rd16(p, 22));  // signed: -1 = hidden
    out.type = p[26];
    out.damage2 = static_cast<i16>(rd16(p, 27));  // left-hand hit (dual-wield/katar 2nd damage)
    return 29;
}

usize decodeSkillDamage(const u8* p, usize n, SkillDamage& out) {
    if (n < 33) return 0;  // 0x1de: skillId@2 src@4 dst@8 tick@12 sdelay@16 ddelay@20 dmg@24(L) lv@28 div@30 type@32
    out.skillId = rd16(p, 2);
    out.src = rd32(p, 4);
    out.dst = rd32(p, 8);
    out.sdelay = rd32(p, 16);  // caster attack-motion ms
    out.ddelay = rd32(p, 20);  // target damage-motion (flinch) ms
    out.damage = static_cast<i32>(rd32(p, 24));  // signed: -1 = hidden
    out.level = rd16(p, 28);
    out.div = rd16(p, 30);
    out.type = p[32];
    return 33;
}

usize decodeSkillNoDamage(const u8* p, usize n, SkillNoDamage& out) {
    if (n < 15) return 0;  // 0x11a: skillId@2 heal@4 dst@6 src@10 fail@14
    out.skillId = rd16(p, 2);
    out.heal = rd16(p, 4);
    out.dst = rd32(p, 6);
    out.src = rd32(p, 10);
    out.fail = p[14];
    return 15;
}

usize decodeGroundSkill(const u8* p, usize n, GroundSkill& out) {
    if (n < 18) return 0;  // 0x117: skillId@2 src@4 val@8 x@10 y@12 tick@16
    out.skillId = rd16(p, 2);
    out.src = rd32(p, 4);
    out.x = rd16(p, 10);
    out.y = rd16(p, 12);
    return 18;
}

usize decodeNameAck(const u8* p, usize n, u32& gid, std::string& name, std::string& extra,
                    std::string* guild) {
    const u16 id = peekId(p, n);
    const usize len = (id == PKT_ZC_ACK_REQNAMEALL) ? 102 : 30;  // 0x195 carries party/guild
    if (n < len) return 0;
    gid = rd32(p, 2);
    name.clear();
    for (usize i = 0; i < 24 && p[6 + i]; ++i) name += static_cast<char>(p[6 + i]);  // name @6
    extra.clear();
    if (guild) guild->clear();
    if (id == PKT_ZC_ACK_REQNAMEALL) {  // 0x195 @30: party name (or mob "Lv.|HP%" info)
        for (usize i = 0; i < 24 && p[30 + i]; ++i) extra += static_cast<char>(p[30 + i]);
        if (guild)  // 0x195 @54: guild name (24 bytes)
            for (usize i = 0; i < 24 && p[54 + i]; ++i) *guild += static_cast<char>(p[54 + i]);
    }
    name = cp1251ToUtf8(name);  // unit/player/guild/party names are Windows-1251 on the wire
    extra = cp1251ToUtf8(extra);
    if (guild) *guild = cp1251ToUtf8(*guild);
    return len;
}

usize decodeStoreEntry(const u8* p, usize n, u32& gid, std::string& title) {
    if (n < 86) return 0;  // 0x131: id(2) gid(4) title[80]
    gid = rd32(p, 2);
    title.clear();
    for (usize i = 0; i < 80 && p[6 + i]; ++i) title += static_cast<char>(p[6 + i]);
    title = cp1251ToUtf8(title);  // shop title is Windows-1251 on the wire
    return 86;
}

usize decodeStateChange(const u8* p, usize n, u32& gid, u32& option, u16& opt1, u16& opt2) {
    if (n < 15) return 0;  // 0x229: id(2) gid(4) opt1(2) opt2(2) option(4) pk(1)
    gid = rd32(p, 2);
    opt1 = rd16(p, 6);   // body state: 1 stone, 2 freeze, 3 stun, 4 sleep (OPT1_*)
    opt2 = rd16(p, 8);   // health-state bits: poison/curse/silence/signum/blind (OPT2_*)
    option = rd32(p, 10);
    return 15;
}

usize decodeChangeDir(const u8* p, usize n, u32& gid, u8& dir) {
    if (n < 9) return 0;  // 0x9c: id(2) gid(4) headDir(2) bodyDir(1)
    gid = rd32(p, 2);
    dir = p[8];           // body direction (unit_getdir)
    return 9;
}

usize decodeRecovery(const u8* p, usize n, u16& type, u16& val) {
    if (n < 6) return 0;  // 0x13d: id(2) type(2) val(2)
    type = rd16(p, 2);    // SP_HP=5 / SP_SP=7
    val = rd16(p, 4);
    return 6;
}

usize decodeSpriteChange(const u8* p, usize n, u32& gid, u8& type, u16& val, u16& val2) {
    if (n < 11) return 0;  // 0x1d7: id(2) gid(4) type(1) val(2) val2(2)
    gid = rd32(p, 2);
    type = p[6];
    val = rd16(p, 7);
    val2 = rd16(p, 9);
    return 11;
}

usize decodeParChange(const u8* p, usize n, u16& type, u32& value) {
    if (n < 8) return 0;  // 0xb0: id(2) type(2) value(4)
    type = rd16(p, 2);
    value = rd32(p, 4);
    return 8;
}

usize decodeCraftList(const u8* p, usize n, CraftList& out) {
    if (n < 4) return 0;
    const u16 id = rd16(p, 0);
    const u16 len = rd16(p, 2);
    if (len < 4 || n < len) return 0;
    usize stride = 0;
    switch (id) {
        case 0x18d: out.kind = CraftKind::Produce; stride = 8;  break;  // nameid.W 0012.W charId.L
        case 0x1ad: out.kind = CraftKind::Arrow;   stride = 2;  break;  // nameid.W
        case 0x1fc: out.kind = CraftKind::Repair;  stride = 13; break;  // idx.W nameid.W ...
        case 0x221: out.kind = CraftKind::Refine;  stride = 13; break;  // idx(+2).W nameid.W ...
        default: return 0;
    }
    out.items.clear();
    for (usize off = 4; off + stride <= len; off += stride) {
        CraftItem it{};
        if (out.kind == CraftKind::Repair || out.kind == CraftKind::Refine) {
            it.index = rd16(p, off);       // packet index (repair = inv slot, refine = slot+2)
            it.nameid = rd16(p, off + 2);
        } else {                            // produce / arrow: just the item id
            it.nameid = rd16(p, off);
        }
        out.items.push_back(it);
    }
    return len;
}

usize decodeStatusChangeAck(const u8* p, usize n, u16& statusId, u8& ok, u8& value) {
    if (n < 6) return 0;  // 0xbc: id(2) statusID(2) result(1) value(1)
    statusId = rd16(p, 2);
    ok = p[4];
    value = p[5];
    return 6;
}

usize decodeStatusChange(const u8* p, usize n, u16& statusId, u8& value) {
    if (n < 5) return 0;  // 0xbe: id(2) statusID(2) value(1)
    statusId = rd16(p, 2);
    value = p[4];
    return 5;
}

usize decodeStatus(const u8* p, usize n, CharStatus& out) {
    if (n < 44) return 0;  // 0xbd, fixed 44B (clif_initialstatus layout)
    out.statusPoint = rd16(p, 2);
    out.str = p[4];  out.needStr = p[5];
    out.agi = p[6];  out.needAgi = p[7];
    out.vit = p[8];  out.needVit = p[9];
    out.int_ = p[10]; out.needInt = p[11];
    out.dex = p[12]; out.needDex = p[13];
    out.luk = p[14]; out.needLuk = p[15];
    out.atk = rd16(p, 16);  out.atk2 = rd16(p, 18);
    out.matkMax = rd16(p, 20); out.matkMin = rd16(p, 22);
    out.def = rd16(p, 24);  out.def2 = rd16(p, 26);
    out.mdef = rd16(p, 28); out.mdef2 = rd16(p, 30);
    out.hit = rd16(p, 32);  out.flee = rd16(p, 34);
    out.flee2 = rd16(p, 36); out.crit = rd16(p, 38);
    // @40/@42 are karma/manner on THIS server's 0xbd (NOT aspd) — leave out.aspd untouched so the
    // value set from ZC_PAR_CHANGE SP_ASPD (amotion -> 200 - amotion/10) survives a status refresh.
    return 44;
}

usize decodeCoupleStatus(const u8* p, usize n, CoupleStatus& out) {
    if (n < 14) return 0;  // 0x141: type(4) base(4) bonus(4)
    out.type = rd32(p, 2);
    out.base = static_cast<i32>(rd32(p, 6));
    out.bonus = static_cast<i32>(rd32(p, 10));  // signed: negative for stat-lowering debuffs
    return 14;
}

usize decodeStatusEffect(const u8* p, usize n, u32& gid, u16& type, u8& flag) {
    if (n < 9) return 0;  // 0x196: id(2) type(2) aid(4) flag(1)
    type = rd16(p, 2);
    gid = rd32(p, 4);
    flag = p[8];
    return 9;
}

usize decodeNotifyEffect(const u8* p, usize n, u32& gid, u32& type) {
    if (n < 10) return 0;  // 0x19b: id(2) aid(4) type(4)
    gid = rd32(p, 2);
    type = rd32(p, 6);
    return 10;
}

usize decodeDealType(const u8* p, usize n, u32& npcId) {
    if (n < 6) return 0;  // 0xc4: id(2) npc(4)
    npcId = rd32(p, 2);
    return 6;
}

usize decodeBuyList(const u8* p, usize n, std::vector<ShopItem>& out) {
    if (n < 4) return 0;  // 0xc6: id(2) len(2) { base(4) discount(4) type(1) nameid(2) }*
    const usize len = rd16(p, 2);
    if (len < 4 || n < len) return 0;
    out.clear();
    for (usize o = 4; o + 11 <= len; o += 11) {
        ShopItem it;
        it.basePrice = rd32(p, o);
        it.price = rd32(p, o + 4);
        it.type = p[o + 8];
        it.nameid = rd16(p, o + 9);
        out.push_back(it);
    }
    return len;
}

usize decodeSellList(const u8* p, usize n, std::vector<SellItem>& out) {
    if (n < 4) return 0;  // 0xc7: id(2) len(2) { index(2) sell(4) overcharge(4) }*
    const usize len = rd16(p, 2);
    if (len < 4 || n < len) return 0;
    out.clear();
    for (usize o = 4; o + 10 <= len; o += 10) {
        SellItem it;
        it.index = rd16(p, o);
        it.basePrice = rd32(p, o + 2);
        it.price = rd32(p, o + 6);
        out.push_back(it);
    }
    return len;
}

usize decodeVendingList(const u8* p, usize n, u32& vendorAid, std::vector<VendItem>& out) {
    // 0x133: id(2) len(2) vendorAID(4) { price(4) amount(2) index(2) type(1) nameid(2)
    //         identify(1) attribute(1) refine(1) cards(8) }*  — 8B header, 22B per entry.
    if (n < 8) return 0;
    const usize len = rd16(p, 2);
    if (len < 8 || n < len) return 0;
    vendorAid = rd32(p, 4);
    out.clear();
    for (usize o = 8; o + 22 <= len; o += 22) {
        VendItem it;
        it.price = rd32(p, o);
        it.amount = rd16(p, o + 4);
        it.index = rd16(p, o + 6);  // cart slot + 2; echoed back verbatim on purchase
        it.type = p[o + 8];
        it.nameid = rd16(p, o + 9);
        it.identify = p[o + 11];
        it.refine = p[o + 13];
        for (int k = 0; k < 4; ++k) it.cards[k] = rd16(p, o + 14 + 2 * k);  // EQUIPSLOTINFO (8B)
        out.push_back(it);
    }
    return len;
}

usize decodeVendingResult(const u8* p, usize n, u16& index, u16& amount, u8& result) {
    // 0x135: id(2) index(2) amount(2) result(1)  — 7B. result 0 = success, else an error code.
    if (n < 7) return 0;
    index = rd16(p, 2);
    amount = rd16(p, 4);
    result = p[6];
    return 7;
}

usize decodeOpenStore(const u8* p, usize n, u16& maxItems) {
    // 0x12d: id(2) maxItems(2) — the Vending skill was used; open the setup for up to maxItems wares.
    if (n < 4) return 0;
    maxItems = rd16(p, 2);
    return 4;
}

usize decodeMyVendingList(const u8* p, usize n, u32& vendorAid, std::vector<VendItem>& out) {
    // 0x136: id(2) len(2) vendorAID(4) { price(4) index(2) amount(2) type(1) nameid(2) identify(1)
    //  attribute(1) refine(1) cards(8) }* — 8B header, 22B/entry. index@4 / amount@6 SWAPPED vs 0x133.
    if (n < 8) return 0;
    const usize len = rd16(p, 2);
    if (len < 8 || n < len) return 0;
    vendorAid = rd32(p, 4);
    out.clear();
    for (usize o = 8; o + 22 <= len; o += 22) {
        VendItem it;
        it.price = rd32(p, o);
        it.index = rd16(p, o + 4);   // cart slot + 2
        it.amount = rd16(p, o + 6);  // remaining quantity
        it.type = p[o + 8];
        it.nameid = rd16(p, o + 9);
        it.identify = p[o + 11];
        it.refine = p[o + 13];
        out.push_back(it);
    }
    return len;
}

usize decodeInventoryList(const u8* p, usize n, std::vector<InvItem>& out, usize entrySize) {
    // 0x1ee (18B) / 0xa4 (20B): id(2) len(2) { index(2) nameid(2) ... }*. The stackable
    // (18B) entry carries amount at +6; the equippable (20B) entry has the equip mask
    // there, so report a count of 1 for it.
    if (n < 4 || entrySize < 8) return 0;
    const usize len = rd16(p, 2);
    if (len < 4 || n < len) return 0;
    for (usize o = 4; o + entrySize <= len; o += entrySize) {
        InvItem it;
        it.index = rd16(p, o);
        it.nameid = rd16(p, o + 2);
        it.type = p[o + 4];  // ItemType byte (both 18B and 20B entries carry it here)
        it.identified = p[o + 5];  // identify flag @entry+5 (0 = unidentified)
        it.amount = (entrySize == 18) ? rd16(p, o + 6) : 1;
        // The equippable (20B) entry carries the equippable-slots mask @6 and the
        // currently-worn position @8 (clif_item_sub: WBUFW n+6 = i->equip). 0 = carried.
        if (entrySize >= 20) {
            it.equipMask = rd16(p, o + 6);  // slots it can go in -> wearLocation for equip-from-bag
            it.equipPos = rd16(p, o + 8);   // slot it is currently worn in (0 = carried)
            it.refine = p[o + 11];          // isDamaged@10, refiningLevel@11, card[4]@12 (clif_item_sub)
            for (int k = 0; k < 4; ++k) it.cards[k] = rd16(p, o + 12 + 2 * k);  // inserted cards (RMB info)
        }
        out.push_back(it);
    }
    return len;
}

usize decodeDeleteItem(const u8* p, usize n, u16& index, u16& amount) {
    if (n < 6) return 0;  // 0xaf: id(2) index(2) amount(2)
    index = rd16(p, 2);
    amount = rd16(p, 4);
    return 6;
}

usize decodeEquipResult(const u8* p, usize n, u16& index, u16& location, u8& ok) {
    if (n < 7) return 0;  // 0xaa / 0xac: id(2) index(2) location(2) ok(1)
    index = rd16(p, 2);
    location = rd16(p, 4);
    ok = p[6];
    return 7;
}

usize decodeRefineResult(const u8* p, usize n, u16& result, u16& index, u16& refine) {
    if (n < 8) return 0;  // 0x188 ZC_ACK_ITEMREFINING: id(2) result(2) index(2) refine(2)
    result = rd16(p, 2);   // 0 = success, 1 = fail, 2 = downgrade
    index = rd16(p, 4);    // inventory index (slot+2, same key as inventory_)
    refine = rd16(p, 6);   // the item's new refine (upgrade) level
    return 8;
}

usize decodeItemAdd(const u8* p, usize n, InvItem& out, u8& fail) {
    if (n < 23) return 0;  // 0xa0: index(2) amount(2) nameid(2) ...8 equipLoc(2)@19 type@21 fail@22
    out.index = rd16(p, 2);
    out.amount = rd16(p, 4);
    out.nameid = rd16(p, 6);
    out.identified = p[8];  // identify flag @8
    out.equipMask = rd16(p, 19);  // equippable slots (so a picked-up equip can be worn from the bag)
    out.type = p[21];
    out.equipPos = 0;  // freshly added -> carried, not worn
    fail = p[22];
    return 23;
}

usize decodeStorageCount(const u8* p, usize n, u16& cur, u16& maxCount) {
    if (n < 6) return 0;  // 0xf2: id(2) cur(2) max(2)
    cur = rd16(p, 2);
    maxCount = rd16(p, 4);
    return 6;
}

usize decodeStorageAdd(const u8* p, usize n, InvItem& out) {
    // 0xf4 (21B): id(2) index(2) amount(4) nameid(2)@8 identify@10 attribute@11 refine@12 cards@13.
    // Note amount is a 4-byte field here (the inventory 0xa0 used 2). type isn't carried; the
    // caller keeps the type from the initial storage list (or the inventory item it just stored).
    if (n < 21) return 0;
    out.index = rd16(p, 2);
    out.amount = static_cast<u16>(rd32(p, 4));  // a single stack fits u16 (caps well below 65535)
    out.nameid = rd16(p, 8);
    out.identified = p[10];  // identify flag @10
    out.refine = p[12];
    out.equipPos = 0;  // in storage, never "worn"
    return 21;
}

usize decodeStorageRemove(const u8* p, usize n, u16& index, u32& amount) {
    if (n < 8) return 0;  // 0xf6: id(2) index(2) amount(4)
    index = rd16(p, 2);
    amount = rd32(p, 4);
    return 8;
}

usize decodeGroundItem(const u8* p, usize n, GroundItem& out) {
    if (n < 17) return 0;  // 0x9d/0x9e: id(4)@2 nameid(2)@6 ident@8 x(2)@9 y(2)@11 ...
    const u16 pid = rd16(p, 0);
    out.id = rd32(p, 2);
    out.nameid = rd16(p, 6);
    out.x = rd16(p, 9);
    out.y = rd16(p, 11);
    // The on-enter (0x9d) and just-dropped (0x9e) packets swap amount vs subX/subY:
    // 0x9d = amount@13, subX@15, subY@16; 0x9e = subX@13, subY@14, amount@15.
    out.amount = (pid == PKT_ZC_ITEM_ENTRY) ? rd16(p, 13) : rd16(p, 15);
    return 17;
}

usize decodeItemDisappear(const u8* p, usize n, u32& id) {
    if (n < 6) return 0;  // 0xa1: id(4)@2
    id = rd32(p, 2);
    return 6;
}

std::vector<u8> buildTakeItem(u32 id) {
    // S 00f5 — ServerType8 (uaRO) item_take (uOK210 sendTake, pack 'v x2'), 8 bytes:
    // 00f5 <2 pad> <id>.L. (On this server 0x009f is item_use, not pickup -- that swap was #95.)
    ByteWriter w;
    w.u16le(0x00f5);
    w.u8v(0x00);
    w.u8v(0x00);
    w.u32le(id);
    return w.data();
}

usize decodeShopResult(const u8* p, usize n, u8& code) {
    if (n < 3) return 0;  // 0xca (buy) / 0xcb (sell): id(2) result(1)
    code = p[2];
    return 3;
}

usize decodeChat(const u8* p, usize n, u32& gid, std::string& msg) {
    if (n < 8) return 0;  // 0x8d: id(2) len(2) gid(4) message[len-8] (NUL-terminated)
    const usize len = rd16(p, 2);
    if (len < 8 || n < len) return 0;
    gid = rd32(p, 4);
    msg.clear();
    for (usize i = 8; i < len && p[i]; ++i) msg += static_cast<char>(p[i]);
    msg = cp1251ToUtf8(msg);  // chat is Windows-1251 on the wire
    return len;
}

usize decodeWhisper(const u8* p, usize n, std::string& nick, std::string& msg) {
    if (n < 28) return 0;  // 0x97: id(2) len(2) nick(24) message[len-28]
    const usize len = rd16(p, 2);
    if (len < 28 || n < len) return 0;
    nick.clear();
    for (usize i = 0; i < 24 && p[4 + i]; ++i) nick += static_cast<char>(p[4 + i]);
    nick = cp1251ToUtf8(nick);
    msg.clear();
    for (usize i = 28; i < len && p[i]; ++i) msg += static_cast<char>(p[i]);
    msg = cp1251ToUtf8(msg);
    return len;
}

usize decodeSkillFail(const u8* p, usize n, u16& skillId, u8& cause) {
    if (n < 10) return 0;  // 0x110: id(2) skillId(2) btype(4) ok(1) cause(1)
    skillId = rd16(p, 2);
    cause = p[9];
    return 10;
}

usize decodeEmotion(const u8* p, usize n, u32& gid, u8& type) {
    if (n < 7) return 0;  // 0xc0: id(2) gid(4) type(1)
    gid = rd32(p, 2);
    type = p[6];
    return 7;
}

std::vector<u8> buildEmotion(u8 type) {
    ByteWriter w;  // S 00bf <type>.B (3B)
    w.u16le(PKT_CZ_EMOTION);
    w.u8v(type);
    return w.data();
}

std::vector<u8> buildRememberWarp() {
    ByteWriter w;  // S 011d (2B, opcode only) -> server pc_memo saves the current cell as a warp point
    w.u16le(PKT_CZ_REMEMBER_WARPPOINT);
    return w.data();
}

usize decodeSkillCasting(const u8* p, usize n, u32& src, u16& skillId, u32& castTimeMs, u32* dst) {
    // 0x13e: id(2) src(4) dst(4) x(2) y(2) skillId(2) element(4) casttime(4) — 24B.
    if (n < 24) return 0;
    src = rd32(p, 2);
    if (dst) *dst = rd32(p, 6);  // target AID (0 for ground/self casts)
    skillId = rd16(p, 14);
    castTimeMs = rd32(p, 20);
    return 24;
}

usize decodeSkillCastCancel(const u8* p, usize n, u32& gid) {
    if (n < 6) return 0;  // 0x1b9: id(2) gid(4)
    gid = rd32(p, 2);
    return 6;
}

usize decodeTradeRequest(const u8* p, usize n, std::string& name) {
    if (n < 26) return 0;  // 0xe5: id(2) name(24) — a player asked to trade with us
    name.clear();
    for (usize i = 0; i < 24 && p[2 + i]; ++i) name += static_cast<char>(p[2 + i]);
    name = cp1251ToUtf8(name);
    return 26;
}

usize decodeTradeStart(const u8* p, usize n, u8& result) {
    if (n < 3) return 0;  // 0xe7: id(2) type(1) — 0 too far, 1 no char, 2 ok/started, 3 busy, 4 cancel
    result = p[2];
    return 3;
}

usize decodeTradeAdd(const u8* p, usize n, TradeAddItem& it) {
    // 0xe9: id(2) amount(4) nameid(2) identify(1) attribute(1) refine(1) cards(8) — 19B.
    if (n < 19) return 0;
    it.amount = rd32(p, 2);
    it.nameid = rd16(p, 6);
    it.identify = p[8];
    it.refine = p[10];
    for (int k = 0; k < 4; ++k) it.cards[k] = rd16(p, 11 + 2 * k);
    return 19;
}

usize decodeTradeAddAck(const u8* p, usize n, u16& index, u8& fail) {
    if (n < 5) return 0;  // 0xea: id(2) index(2) fail(1) — 0 ok, 1 overweight, 2 cancelled
    index = rd16(p, 2);
    fail = p[4];
    return 5;
}

usize decodeTradeLock(const u8* p, usize n, u8& who) {
    if (n < 3) return 0;  // 0xec: id(2) who(1) — 0 = you locked, 1 = the other party locked
    who = p[2];
    return 3;
}

usize decodeTradeDone(const u8* p, usize n, u8& fail) {
    if (n < 3) return 0;  // 0xf0: id(2) fail(1) — 0 = success
    fail = p[2];
    return 3;
}

usize decodePlayerChat(const u8* p, usize n, std::string& msg) {
    if (n < 4) return 0;  // 0x8e: id(2) len(2) message[len-4] (NUL-terminated)
    const usize len = rd16(p, 2);
    if (len < 4 || n < len) return 0;
    msg.clear();
    for (usize i = 4; i < len && p[i]; ++i) msg += static_cast<char>(p[i]);
    msg = cp1251ToUtf8(msg);  // chat is Windows-1251 on the wire
    return len;
}

usize decodeSkillEntry(const u8* p, usize n, SkillEntry& out) {
    if (n < 16) return 0;  // 0x11f: id(2) gid(4) src(4) x(2) y(2) unitId(1) flag(1)
    out.gid = rd32(p, 2);
    out.srcId = rd32(p, 6);
    out.x = rd16(p, 10);
    out.y = rd16(p, 12);
    out.unitId = p[14];
    return 16;
}

usize decodeSkillDisappear(const u8* p, usize n, u32& gid) {
    if (n < 6) return 0;  // 0x120: id(2) gid(4)
    gid = rd32(p, 2);
    return 6;
}

usize decodeSkillList(const u8* p, usize n, std::vector<SkillInfo>& out) {
    if (n < 4) return 0;  // 0x10f: id(2) len(2) { 37B }*
    const usize len = rd16(p, 2);
    if (len < 4 || n < len) return 0;
    for (usize o = 4; o + 37 <= len; o += 37) {
        SkillInfo s;
        s.id = rd16(p, o);
        s.inf = rd16(p, o + 2);  // target type (INF_*): low 2 bytes hold ATTACK/GROUND/SELF/SUPPORT/TRAP
        s.level = rd16(p, o + 6);
        s.sp = rd16(p, o + 8);
        s.range = rd16(p, o + 10);
        for (usize i = 0; i < 24 && p[o + 12 + i]; ++i) s.name += static_cast<char>(p[o + 12 + i]);
        s.name = cp1251ToUtf8(s.name);  // skill name (Windows-1251 if localized)
        s.up = p[o + 36] != 0;
        out.push_back(std::move(s));
    }
    return len;
}

usize decodeGuildSkills(const u8* p, usize n, u16& skillPoint, std::vector<SkillInfo>& out) {
    if (n < 6) return 0;  // 0x162: id(2) len(2) skillpoint(2) { 37B }*
    const usize len = rd16(p, 2);
    if (len < 6 || n < len) return 0;
    skillPoint = rd16(p, 4);
    out.clear();
    for (usize o = 6; o + 37 <= len; o += 37) {
        SkillInfo s;
        s.id = rd16(p, o);
        s.inf = rd16(p, o + 2);
        s.level = rd16(p, o + 6);
        s.sp = rd16(p, o + 8);
        s.range = rd16(p, o + 10);
        for (usize i = 0; i < 24 && p[o + 12 + i]; ++i) s.name += static_cast<char>(p[o + 12 + i]);
        s.name = cp1251ToUtf8(s.name);
        s.up = p[o + 36] != 0;
        out.push_back(std::move(s));
    }
    return len;
}

usize decodeSkillUpdate(const u8* p, usize n, u16& id, u16& level, u16& sp, u16& range, bool& up) {
    if (n < 11) return 0;  // 0x10e: id(2) skillId(2) lv(2) sp(2) range(2) up(1)
    id = rd16(p, 2);
    level = rd16(p, 4);
    sp = rd16(p, 6);
    range = rd16(p, 8);
    up = p[10] != 0;
    return 11;
}

usize decodeMapChange(const u8* p, usize n, MapChange& out) {
    const u16 id = peekId(p, n);
    const usize len = (id == PKT_ZC_CHANGE_MAPSVR) ? 28 : 22;
    if (n < len) return 0;
    out.mapName.clear();
    for (usize i = 0; i < 16 && p[2 + i]; ++i) out.mapName += static_cast<char>(p[2 + i]);
    out.x = rd16(p, 18);
    out.y = rd16(p, 20);
    if (id == PKT_ZC_CHANGE_MAPSVR) {
        out.newServer = true;
        out.ip = ipToString(p + 22);
        out.port = rd16(p, 26);
    }
    return len;
}

// ---------------------------------------------------------------------------
// Error text
// ---------------------------------------------------------------------------
const char* loginErrorText(u8 code) {
    switch (code) {
        case 0:  return "Unregistered ID";
        case 1:  return "Incorrect password";
        case 2:  return "This ID is expired";
        case 3:  return "Rejected from server";
        case 4:  return "You have been blocked by the GM team";
        case 5:  return "Your game's EXE file is not the latest version";
        case 6:  return "You are prohibited to log in until the given date";
        case 7:  return "Server is jammed due to over-population";
        case 8:  return "No more accounts may be connected from this company";
        case 9:  return "Banned by the DBA";
        case 10: return "Email address not confirmed";
        default: return "Login failed (unknown error)";
    }
}

const char* banErrorText(u8 code) {
    switch (code) {
        case 0:  return "Server closed";
        case 1:  return "Server closed";
        case 2:  return "Someone has already logged in with this ID";
        case 3:  return "Speed-hack detected";
        case 8:  return "Server is jammed due to over-population";
        default: return "Disconnected by server";
    }
}

} // namespace uaro::net
