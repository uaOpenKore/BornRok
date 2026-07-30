#include "net/Protocol.hpp"

#include <cstring>
#include <string>
#include <vector>

#include "microtest.hpp"

using namespace uaro;
using B = std::vector<u8>;

namespace {
void at16(B& v, usize off, u16 x) {
    v[off] = static_cast<u8>(x & 0xff);
    v[off + 1] = static_cast<u8>((x >> 8) & 0xff);
}
void at32(B& v, usize off, u32 x) {
    for (int i = 0; i < 4; ++i) v[off + i] = static_cast<u8>((x >> (i * 8)) & 0xff);
}
void atStr(B& v, usize off, const std::string& s, usize field) {
    for (usize i = 0; i < field; ++i) v[off + i] = i < s.size() ? static_cast<u8>(s[i]) : 0;
}
} // namespace

// AC_ACCEPT_LOGIN (0x69): auth ids + a char-server list, exactly as login.c writes.
TEST_CASE(protocol_accept_login) {
    const u16 len = 47 + 32;  // one char-server
    B b(len, 0);
    at16(b, 0, 0x0069);
    at16(b, 2, len);
    at32(b, 4, 0x11111111);   // login_id1
    at32(b, 8, 2000000);      // account_id
    at32(b, 12, 0x22222222);  // login_id2
    b[46] = 1;                // sex
    // server entry @47
    b[47] = 127; b[48] = 0; b[49] = 0; b[50] = 1;  // ip bytes in order
    at16(b, 47 + 4, 6121);                          // port
    atStr(b, 47 + 6, "uaRO-char", 20);              // name
    at16(b, 47 + 26, 42);                           // users

    net::AcceptLogin a;
    const usize c = net::decodeAcceptLogin(b.data(), b.size(), a);
    CHECK_EQ(c, len);
    CHECK_EQ(a.loginId1, 0x11111111u);
    CHECK_EQ(a.accountId, 2000000u);
    CHECK_EQ(a.loginId2, 0x22222222u);
    CHECK_EQ(a.sex, 1);
    CHECK_EQ(a.servers.size(), 1u);
    CHECK(a.servers[0].ip == "127.0.0.1");
    CHECK_EQ(a.servers[0].port, 6121);
    CHECK(a.servers[0].name == "uaRO-char");
    CHECK_EQ(a.servers[0].users, 42);
}

TEST_CASE(protocol_accept_login_incomplete) {
    B b = {0x69, 0x00, 0x4f, 0x00};  // claims len 0x4f but only 4 bytes present
    net::AcceptLogin a;
    CHECK_EQ(net::decodeAcceptLogin(b.data(), b.size(), a), 0u);
}

// AC_REFUSE_LOGIN (0x6a): fixed 23 bytes, error code at offset 2.
TEST_CASE(protocol_refuse_login) {
    B b(23, 0);
    at16(b, 0, 0x006a);
    b[2] = 1;  // incorrect password
    u8 code = 0;
    std::string ban;
    CHECK_EQ(net::decodeRefuseLogin(b.data(), b.size(), code, ban), 23u);
    CHECK_EQ(code, 1);
    CHECK(std::string(net::loginErrorText(code)) == "Incorrect password");
}

// HC_ACCEPT_ENTER (0x6b): 24-byte header + 106-byte char records (mmo_char_tobuf).
TEST_CASE(protocol_char_list) {
    const usize rec = 106;
    B b(24 + rec, 0);
    at16(b, 0, 0x006b);
    at16(b, 2, static_cast<u16>(24 + rec));
    const usize o = 24;
    at32(b, o + 0, 150001);   // char_id
    at32(b, o + 16, 50);      // job_level
    at16(b, o + 52, 4054);    // class (Lord Knight)
    at16(b, o + 58, 99);      // base_level
    atStr(b, o + 74, "Hero", 24);
    at16(b, o + 104, 2);      // slot

    std::vector<net::CharInfo> chars;
    const usize c = net::decodeCharList(b.data(), b.size(), chars);
    CHECK_EQ(c, b.size());
    CHECK_EQ(chars.size(), 1u);
    CHECK_EQ(chars[0].charId, 150001u);
    CHECK_EQ(chars[0].jobLevel, 50u);
    CHECK_EQ(chars[0].class_, 4054);
    CHECK_EQ(chars[0].baseLevel, 99);
    CHECK(chars[0].name == "Hero");
    CHECK_EQ(chars[0].slot, 2);
}

TEST_CASE(protocol_char_list_empty) {
    B b(24, 0);
    at16(b, 0, 0x006b);
    at16(b, 2, 24);
    std::vector<net::CharInfo> chars;
    CHECK_EQ(net::decodeCharList(b.data(), b.size(), chars), 24u);
    CHECK_EQ(chars.size(), 0u);
}

// HC_NOTIFY_ZONESVR (0x71): char_id, 16B map name, map-server ip + port (28 bytes).
TEST_CASE(protocol_zone_server) {
    B b(28, 0);
    at16(b, 0, 0x0071);
    at32(b, 2, 150001);
    atStr(b, 6, "prontera.gat", 16);
    b[22] = 10; b[23] = 0; b[24] = 0; b[25] = 5;  // ip 10.0.0.5
    at16(b, 26, 5121);
    net::ZoneServer z;
    CHECK_EQ(net::decodeZoneServer(b.data(), b.size(), z), 28u);
    CHECK_EQ(z.charId, 150001u);
    CHECK(z.mapName == "prontera.gat");
    CHECK(z.ip == "10.0.0.5");
    CHECK_EQ(z.port, 5121);
}

// ZC_ACCEPT_ENTER (0x73): start tick + WBUFPOS-packed position (11 bytes).
TEST_CASE(protocol_map_authok) {
    const u16 x = 156, y = 191;
    const u8 dir = 4;
    B b(11, 0);
    at16(b, 0, 0x0073);
    at32(b, 2, 123456);
    b[6] = static_cast<u8>(x >> 2);
    b[7] = static_cast<u8>((x << 6) | ((y >> 4) & 0x3f));
    b[8] = static_cast<u8>((y << 4) | (dir & 0xf));
    net::MapAuth a;
    CHECK_EQ(net::decodeMapAuthOk(b.data(), b.size(), a), 11u);
    CHECK_EQ(a.tick, 123456u);
    CHECK_EQ(a.x, x);
    CHECK_EQ(a.y, y);
    CHECK_EQ(a.dir, dir);
}

// Builders match the byte layout the servers parse.
TEST_CASE(protocol_build_login) {
    const B p = net::buildCALogin(20, "admin", "secret", 14);
    CHECK_EQ(p.size(), 55u);
    CHECK_EQ(net::peekId(p), 0x0064);
    CHECK_EQ(p[2] | (p[3] << 8), 20);            // version (low 16 bits)
    CHECK(std::memcmp(&p[6], "admin", 5) == 0);  // username @6
    CHECK_EQ(p[6 + 5], 0);                        // NUL padded
    CHECK(std::memcmp(&p[30], "secret", 6) == 0);  // password @30
    CHECK_EQ(p[54], 14);                         // clienttype
}

TEST_CASE(protocol_build_wanttoconnection) {
    const B p = net::buildWantToConnection(2000000, 150001, 0x33, 999, 1);
    CHECK_EQ(p.size(), 26u);  // ServerType8 (uaRO) obfuscated map-login (uOK210 sendMapLogin)
    CHECK_EQ(net::peekId(p), 0x009b);
    CHECK_EQ(p[2], 0x39);  // fixed anti-bot padding
    CHECK_EQ(p[3], 0x33);
    CHECK_EQ(p[4] | (p[5] << 8) | (p[6] << 16) | (p[7] << 24), 2000000);  // account_id @4
    CHECK_EQ(p[8], 0x65);
    CHECK_EQ(p[9] | (p[10] << 8) | (p[11] << 16) | (p[12] << 24), 150001);  // char_id @9
    CHECK_EQ(p[13], 0x37);
    CHECK_EQ(p[14], 0x33);
    CHECK_EQ(p[15], 0x36);
    CHECK_EQ(p[16], 0x64);
    CHECK_EQ(p[25], 1);  // sex @25
}

TEST_CASE(protocol_build_ch_enter_and_select) {
    const B e = net::buildCHEnter(2000000, 0x11, 0x22, 1, 0);
    CHECK_EQ(e.size(), 17u);
    CHECK_EQ(net::peekId(e), 0x0065);
    CHECK_EQ(e[16], 1);  // sex @16

    const B s = net::buildCharSelect(3);
    CHECK_EQ(s.size(), 3u);
    CHECK_EQ(net::peekId(s), 0x0066);
    CHECK_EQ(s[2], 3);

    const B la = net::buildLoadEndAck();
    CHECK_EQ(la.size(), 2u);
    CHECK_EQ(net::peekId(la), 0x007d);

    const B tk = net::buildTickSend(777);
    CHECK_EQ(tk.size(), 8u);  // ServerType8 (uaRO) sync (uOK210 sendSync)
    CHECK_EQ(net::peekId(tk), 0x0089);
    CHECK_EQ(tk[4] | (tk[5] << 8) | (tk[6] << 16) | (tk[7] << 24), 777);  // tick @4
}

TEST_CASE(protocol_build_npc_input) {
    // CZ_INPUT_EDITDLG (number box): id, gid.L, value.L — fixed 10B.
    const B n = net::buildNpcInputNum(150001, 42);
    CHECK_EQ(n.size(), 10u);
    CHECK_EQ(net::peekId(n), 0x0143);
    CHECK_EQ(n[2] | (n[3] << 8) | (n[4] << 16) | (n[5] << 24), 150001);  // gid @2
    CHECK_EQ(n[6] | (n[7] << 8) | (n[8] << 16) | (n[9] << 24), 42);      // value @6

    // CZ_INPUT_EDITDLGSTR (string box): id, len.W, gid.L, string + NUL. len = 8 + strlen + 1.
    const B s = net::buildNpcInputStr(150001, "Bob");
    CHECK_EQ(s.size(), 12u);                       // 8 header + 3 chars + 1 NUL
    CHECK_EQ(net::peekId(s), 0x01d5);
    CHECK_EQ(s[2] | (s[3] << 8), 12);              // len @2
    CHECK_EQ(s[4] | (s[5] << 8) | (s[6] << 16) | (s[7] << 24), 150001);  // gid @4
    CHECK_EQ(s[8], 'B');
    CHECK_EQ(s[9], 'o');
    CHECK_EQ(s[10], 'b');
    CHECK_EQ(s[11], 0);                            // NUL terminator
}

TEST_CASE(protocol_skill_up) {
    // CZ_UPGRADE_SKILLLEVEL: id, skillId.W — fixed 4B.
    const B u = net::buildSkillUp(28);  // NV_BASIC=1, AL_HEAL=28 etc.
    CHECK_EQ(u.size(), 4u);
    CHECK_EQ(net::peekId(u), 0x0112);
    CHECK_EQ(u[2] | (u[3] << 8), 28);

    // ZC_SKILLINFO_UPDATE (0x10e, 11B): id skillId.W lv.W sp.W range.W up.B.
    const u8 pkt[11] = {0x0e, 0x01, 28, 0, 5, 0, 40, 0, 9, 0, 1};
    u16 id = 0, lv = 0, sp = 0, rng = 0;
    bool up = false;
    CHECK_EQ(net::decodeSkillUpdate(pkt, sizeof(pkt), id, lv, sp, rng, up), 11u);
    CHECK_EQ(id, 28);
    CHECK_EQ(lv, 5);
    CHECK_EQ(sp, 40);
    CHECK_EQ(rng, 9);
    CHECK_EQ(up, true);
}

// --- map-server actor (unit) entries + stream framing (PACKETVER 7) ---------
namespace {
// Server-side WBUFPOS / WBUFPOS2 packing (clif.c), to build authentic buffers.
void packPos(B& v, usize off, u16 x, u16 y, u8 dir) {
    v[off + 0] = static_cast<u8>(x >> 2);
    v[off + 1] = static_cast<u8>((x << 6) | ((y >> 4) & 0x3f));
    v[off + 2] = static_cast<u8>((y << 4) | (dir & 0xf));
}
void packPos2(B& v, usize off, u16 x0, u16 y0, u16 x1, u16 y1, u8 sx0, u8 sy0) {
    v[off + 0] = static_cast<u8>(x0 >> 2);
    v[off + 1] = static_cast<u8>((x0 << 6) | ((y0 >> 4) & 0x3f));
    v[off + 2] = static_cast<u8>((y0 << 4) | ((x1 >> 6) & 0x0f));
    v[off + 3] = static_cast<u8>((x1 << 2) | ((y1 >> 8) & 0x03));
    v[off + 4] = static_cast<u8>(y1);
    v[off + 5] = static_cast<u8>((sx0 << 4) | (sy0 & 0x0f));
}
} // namespace

TEST_CASE(protocol_actor_pc_idle) {
    B b(58, 0);
    at16(b, 0, 0x022a);
    at32(b, 2, 150000);  // gid
    at32(b, 12, 0x20);   // option: OPTION_RIDING
    at16(b, 16, 4008);   // class (lord knight)
    at16(b, 18, 7);      // hair
    at16(b, 20, 13);     // weapon
    at16(b, 26, 5);      // head_top
    at16(b, 30, 6);      // hair_color
    at16(b, 32, 2);      // clothes_color
    at32(b, 36, 700);    // guild_id
    at16(b, 40, 3);      // emblem_id (version)
    b[49] = 1;           // sex
    packPos(b, 50, 273, 200, 4);

    net::ActorEntry a;
    CHECK_EQ(net::decodeActorEntry(b.data(), b.size(), a), 58u);
    CHECK(a.pc);
    CHECK(!a.walking);
    CHECK_EQ(a.gid, 150000u);
    CHECK_EQ(a.option, 0x20u);
    CHECK_EQ(a.class_, 4008);
    CHECK_EQ(a.hair, 7);
    CHECK_EQ(a.weapon, 13);
    CHECK_EQ(a.headTop, 5);
    CHECK_EQ(a.hairColor, 6);
    CHECK_EQ(a.clothesColor, 2);
    CHECK_EQ(a.guildId, 700u);
    CHECK_EQ(a.emblemId, 3);
    CHECK_EQ(a.sex, 1);
    CHECK_EQ(a.x, 273);
    CHECK_EQ(a.y, 200);
    CHECK_EQ(a.dir, 4);
    CHECK_EQ(a.toX, 273);  // standing: dst == src
    CHECK_EQ(a.toY, 200);
    // short buffer -> not yet
    CHECK_EQ(net::decodeActorEntry(b.data(), 40, a), 0u);

    // spawn variant 0x22b is one byte shorter, same fields 0..54
    B s(57, 0);
    at16(s, 0, 0x022b);
    at32(s, 2, 150001);
    at16(s, 16, 12);
    packPos(s, 50, 50, 60, 0);
    CHECK_EQ(net::decodeActorEntry(s.data(), s.size(), a), 57u);
    CHECK(a.pc);
    CHECK_EQ(a.gid, 150001u);
    CHECK_EQ(a.class_, 12);
    CHECK_EQ(a.x, 50);
    CHECK_EQ(a.y, 60);
}

TEST_CASE(protocol_guild_emblem) {
    // request: S 0151 <guildID>.L  (6B)
    const B req = net::buildGuildEmblemRequest(700);
    CHECK_EQ(req.size(), 6u);
    CHECK_EQ(net::peekId(req), 0x0151);
    const u32 g = req[2] | (req[3] << 8) | (req[4] << 16) | (static_cast<u32>(req[5]) << 24);
    CHECK_EQ(g, 700u);

    // reply: R 0152 <len>.W <guildID>.L <emblemVer>.L <bmp>.?B
    B b(16, 0);
    at16(b, 0, 0x0152);
    at16(b, 2, 16);       // total len = 12 header + 4 payload
    at32(b, 4, 700);      // guildId
    at32(b, 8, 5);        // emblem version
    b[12] = 'B';
    b[13] = 'M';
    b[14] = 0x12;
    b[15] = 0x34;
    net::GuildEmblem e;
    CHECK_EQ(net::decodeGuildEmblem(b.data(), b.size(), e), 16u);
    CHECK_EQ(e.guildId, 700u);
    CHECK_EQ(e.emblemId, 5u);
    CHECK_EQ(e.image.size(), 4u);
    CHECK_EQ(e.image[0], 'B');
    CHECK_EQ(e.image[3], 0x34);
    CHECK_EQ(net::decodeGuildEmblem(b.data(), 10, e), 0u);  // short header
    CHECK_EQ(net::decodeGuildEmblem(b.data(), 14, e), 0u);  // truncated payload (len says 16)
}

// Double-click inventory/equip actions: CZ_USE_ITEM (0xa7), CZ_REQ_WEAR_EQUIP (0xa9),
// CZ_REQ_TAKEOFF_EQUIP (0xab) — the exact byte layouts the server's packet_db expects.
TEST_CASE(protocol_item_equip_actions) {
    const B use = net::buildUseItem(5, 2000000);  // ServerType8 (uaRO) 0x009f item_use, 14B
    CHECK_EQ(use.size(), 14u);
    CHECK_EQ(net::peekId(use), 0x009f);
    CHECK_EQ(use[4] | (use[5] << 8), 5);  // index @4
    CHECK_EQ(use[10] | (use[11] << 8) | (use[12] << 16) | (static_cast<u32>(use[13]) << 24),
             2000000u);  // accountID @10

    const B wear = net::buildWearEquip(7, 0x0100);  // S 00a9 <index>.W <location>.W (6B)
    CHECK_EQ(wear.size(), 6u);
    CHECK_EQ(net::peekId(wear), 0x00a9);
    CHECK_EQ(wear[2] | (wear[3] << 8), 7);
    CHECK_EQ(wear[4] | (wear[5] << 8), 0x0100);

    const B off = net::buildTakeoffEquip(7);  // S 00ab <index>.W (4B)
    CHECK_EQ(off.size(), 4u);
    CHECK_EQ(net::peekId(off), 0x00ab);
    CHECK_EQ(off[2] | (off[3] << 8), 7);

    const B drop = net::buildDropItem(9, 12);  // ServerType8 (uaRO) 0x0116 item_drop, 10B
    CHECK_EQ(drop.size(), 10u);
    CHECK_EQ(net::peekId(drop), 0x0116);
    CHECK_EQ(drop[5] | (drop[6] << 8), 9);   // index @5
    CHECK_EQ(drop[8] | (drop[9] << 8), 12);  // amount @8
}

// The stat-raise reply packets that make the displayed numbers update live: ZC_STATUS_CHANGE_ACK
// (0xbc, new base value) and ZC_STATUS_CHANGE (0xbe, SP_Uxxx raise cost).
TEST_CASE(protocol_status_change_replies) {
    B ack(6, 0);  // R 00bc <statusID>.W <result>.B <value>.B (6B)
    at16(ack, 0, 0x00bc);
    at16(ack, 2, net::SP_STR);
    ack[4] = 1;
    ack[5] = 42;
    u16 sid = 0;
    u8 ok = 0, val = 0;
    CHECK_EQ(net::decodeStatusChangeAck(ack.data(), ack.size(), sid, ok, val), 6u);
    CHECK_EQ(sid, net::SP_STR);
    CHECK_EQ(ok, 1);
    CHECK_EQ(val, 42);
    CHECK_EQ(net::decodeStatusChangeAck(ack.data(), 5, sid, ok, val), 0u);  // short

    B sc(5, 0);  // R 00be <statusID>.W <value>.B (5B) — SP_USTR raise cost
    at16(sc, 0, 0x00be);
    at16(sc, 2, net::SP_USTR);
    sc[4] = 6;
    u16 cid = 0;
    u8 cval = 0;
    CHECK_EQ(net::decodeStatusChange(sc.data(), sc.size(), cid, cval), 5u);
    CHECK_EQ(cid, net::SP_USTR);
    CHECK_EQ(cval, 6);
    CHECK_EQ(net::decodeStatusChange(sc.data(), 4, cid, cval), 0u);  // short
}

TEST_CASE(protocol_actor_pc_walk) {
    B b(64, 0);
    at16(b, 0, 0x022c);
    at32(b, 2, 160000);
    at16(b, 16, 4012);  // class
    at16(b, 18, 3);     // hair
    at16(b, 20, 5);     // weapon
    b[53] = 0;          // sex
    packPos2(b, 54, 100, 110, 105, 112, 8, 8);

    net::ActorEntry a;
    CHECK_EQ(net::decodeActorEntry(b.data(), b.size(), a), 64u);
    CHECK(a.pc);
    CHECK(a.walking);
    CHECK_EQ(a.gid, 160000u);
    CHECK_EQ(a.class_, 4012);
    CHECK_EQ(a.hair, 3);
    CHECK_EQ(a.weapon, 5);
    CHECK_EQ(a.x, 100);
    CHECK_EQ(a.y, 110);
    CHECK_EQ(a.toX, 105);
    CHECK_EQ(a.toY, 112);
}

TEST_CASE(protocol_actor_npc) {
    B b(41, 0);
    at16(b, 0, 0x007c);
    at32(b, 2, 110000000);  // NPC GID (high)
    at16(b, 20, 46);        // class (NPC sprite id @20, not @14)
    packPos(b, 36, 53, 111, 0);

    net::ActorEntry a;
    CHECK_EQ(net::decodeActorEntry(b.data(), b.size(), a), 41u);
    CHECK(!a.pc);
    CHECK_EQ(a.gid, 110000000u);
    CHECK_EQ(a.class_, 46);
    CHECK_EQ(a.x, 53);
    CHECK_EQ(a.y, 111);

    // NPC/mob standing 0x78 (54): class @14
    B s(54, 0);
    at16(s, 0, 0x0078);
    at32(s, 2, 110000001);
    at16(s, 14, 1002);  // mob sprite (Poring)
    at16(s, 52, 45);    // unit level
    packPos(s, 46, 80, 90, 2);
    CHECK_EQ(net::decodeActorEntry(s.data(), s.size(), a), 54u);
    CHECK(!a.pc);
    CHECK_EQ(a.class_, 1002);
    CHECK_EQ(a.x, 80);
    CHECK_EQ(a.y, 90);
    CHECK_EQ(a.dir, 2);
    CHECK_EQ(a.level, 45);
}

TEST_CASE(protocol_vanish) {
    B b(7, 0);
    at16(b, 0, 0x0080);
    at32(b, 2, 150000);
    b[6] = 2;  // logged out
    u32 gid = 0;
    u8 reason = 0;
    CHECK_EQ(net::decodeVanish(b.data(), b.size(), gid, reason), 7u);
    CHECK_EQ(gid, 150000u);
    CHECK_EQ(reason, 2);
    CHECK_EQ(net::decodeVanish(b.data(), 6, gid, reason), 0u);  // short
}

TEST_CASE(protocol_framing) {
    // Fixed-length packet (0x22a = 58).
    B b(58, 0);
    at16(b, 0, 0x022a);
    auto fi = net::nextPacket(b.data(), b.size());
    CHECK(fi.status == net::Frame::Ready);
    CHECK_EQ(fi.length, 58u);
    CHECK_EQ(fi.id, 0x022a);
    fi = net::nextPacket(b.data(), 40);  // incomplete
    CHECK(fi.status == net::Frame::Need);
    CHECK_EQ(fi.length, 58u);  // hints the full size

    // Variable-length packet (0x009a broadcast): u16 total length @2.
    B v(20, 0);
    at16(v, 0, 0x009a);
    at16(v, 2, 20);
    fi = net::nextPacket(v.data(), v.size());
    CHECK(fi.status == net::Frame::Ready);
    CHECK_EQ(fi.length, 20u);
    fi = net::nextPacket(v.data(), 10);  // header present, body short
    CHECK(fi.status == net::Frame::Need);
    CHECK_EQ(fi.length, 20u);
    fi = net::nextPacket(v.data(), 3);  // can't even read the length field
    CHECK(fi.status == net::Frame::Need);

    // Unknown id -> Unknown (caller must not blindly consume).
    B u(8, 0);
    at16(u, 0, 0x0fff);
    fi = net::nextPacket(u.data(), u.size());
    CHECK(fi.status == net::Frame::Unknown);
}

TEST_CASE(protocol_walk_request) {
    const B w = net::buildWalkRequest(150, 200);
    CHECK_EQ(w.size(), 8u);  // ServerType8 (uaRO) walktoxy (uOK210 sendMove): a7 00 00 00 + 0x44 + 3B pos
    CHECK_EQ(net::peekId(w), 0x00a7);
    CHECK_EQ(w[4], 0x44);  // serverType 8 keeps getCoordString's leading byte
    // The server unpacks the cell from offset 5 (after the 0x44 lead byte); round-trip it.
    const u16 x = static_cast<u16>((w[5] << 2) | (w[6] >> 6));
    const u16 y = static_cast<u16>(((w[6] & 0x3f) << 4) | (w[7] >> 4));
    CHECK_EQ(x, 150);
    CHECK_EQ(y, 200);
}

TEST_CASE(protocol_player_move) {
    B b(12, 0);
    at16(b, 0, 0x0087);
    at32(b, 2, 555);  // tick
    packPos2(b, 6, 100, 110, 105, 112, 8, 8);
    net::MoveData m;
    CHECK_EQ(net::decodePlayerMove(b.data(), b.size(), m), 12u);
    CHECK_EQ(m.tick, 555u);
    CHECK_EQ(m.fromX, 100);
    CHECK_EQ(m.fromY, 110);
    CHECK_EQ(m.toX, 105);
    CHECK_EQ(m.toY, 112);
    CHECK_EQ(net::decodePlayerMove(b.data(), 11, m), 0u);  // short buffer
}

TEST_CASE(protocol_unit_move) {
    B b(16, 0);  // 0x86: another unit walks
    at16(b, 0, 0x0086);
    at32(b, 2, 2000123);  // gid
    packPos2(b, 6, 100, 110, 105, 112, 8, 8);
    at32(b, 12, 777);  // tick
    u32 gid = 0;
    net::MoveData m;
    CHECK_EQ(net::decodeUnitMove(b.data(), b.size(), gid, m), 16u);
    CHECK_EQ(gid, 2000123u);
    CHECK_EQ(m.tick, 777u);
    CHECK_EQ(m.fromX, 100);
    CHECK_EQ(m.fromY, 110);
    CHECK_EQ(m.toX, 105);
    CHECK_EQ(m.toY, 112);
    CHECK_EQ(net::decodeUnitMove(b.data(), 15, gid, m), 0u);  // short buffer
}

TEST_CASE(protocol_stop_move) {
    B b(10, 0);  // 0x88 clif_fixpos: snap a unit to (x,y)
    at16(b, 0, 0x0088);
    at32(b, 2, 2000123);  // unit id
    at16(b, 6, 142);      // x
    at16(b, 8, 88);       // y
    u32 id = 0;
    u16 x = 0, y = 0;
    CHECK_EQ(net::decodeStopMove(b.data(), b.size(), id, x, y), 10u);
    CHECK_EQ(id, 2000123u);
    CHECK_EQ(x, 142);
    CHECK_EQ(y, 88);
    CHECK_EQ(net::decodeStopMove(b.data(), 9, id, x, y), 0u);  // short buffer
}

TEST_CASE(protocol_char_delete) {
    auto blob = net::buildCharDelete(123456, "a@a.com");  // 0x68 + cid + email(40) = 46
    CHECK_EQ(blob.size(), static_cast<usize>(46));
    CHECK_EQ(static_cast<u16>(blob[0] | (blob[1] << 8)), 0x0068);
    const u32 cid = static_cast<u32>(blob[2] | (blob[3] << 8) | (blob[4] << 16) | (blob[5] << 24));
    CHECK_EQ(cid, 123456u);
    CHECK(blob[6] == 'a' && blob[7] == '@');
    CHECK_EQ(blob[6 + 39], 0);  // email field is NUL-padded to 40

    B ok(2, 0);
    at16(ok, 0, 0x006f);
    CHECK_EQ(net::decodeDeleteAccept(ok.data(), ok.size()), 2u);
    CHECK_EQ(net::decodeDeleteAccept(ok.data(), 1), 0u);  // short

    B no(3, 0);
    at16(no, 0, 0x0070);
    no[2] = 0;  // reason: incorrect email
    u8 reason = 9;
    CHECK_EQ(net::decodeDeleteRefuse(no.data(), no.size(), reason), 3u);
    CHECK_EQ(reason, 0);
    CHECK_EQ(net::decodeDeleteRefuse(no.data(), 2, reason), 0u);  // short
}

TEST_CASE(protocol_name_ack) {
    auto req = net::buildNameRequest(2000123);  // ServerType8 (uaRO) 0x008c + 5 pad + gid = 11
    CHECK_EQ(req.size(), static_cast<usize>(11));
    CHECK_EQ(static_cast<u16>(req[0] | (req[1] << 8)), 0x008c);
    const u32 rg = static_cast<u32>(req[7] | (req[8] << 8) | (req[9] << 16) | (req[10] << 24));
    CHECK_EQ(rg, 2000123u);  // gid @7

    B b(30, 0);  // 0x95: gid@2 + name@6[24]
    at16(b, 0, 0x0095);
    at32(b, 2, 150000);
    atStr(b, 6, "Poring", 24);
    u32 gid = 0;
    std::string name, extra;
    CHECK_EQ(net::decodeNameAck(b.data(), b.size(), gid, name, extra), 30u);
    CHECK_EQ(gid, 150000u);
    CHECK(name == "Poring");
    CHECK(extra.empty());  // 0x95 has no extra field
    CHECK_EQ(net::decodeNameAck(b.data(), 29, gid, name, extra), 0u);  // short

    B c(102, 0);  // 0x195: name @6, mob "Lv.|HP%" info (or party name) @30
    at16(c, 0, 0x0195);
    at32(c, 2, 777);
    atStr(c, 6, "Poring", 24);
    atStr(c, 30, "Lv. 3 | HP: 72%", 24);
    CHECK_EQ(net::decodeNameAck(c.data(), c.size(), gid, name, extra), 102u);
    CHECK_EQ(gid, 777u);
    CHECK(name == "Poring");
    CHECK(extra == "Lv. 3 | HP: 72%");
    CHECK_EQ(net::decodeNameAck(c.data(), 101, gid, name, extra), 0u);  // short
}

TEST_CASE(protocol_attack_act) {
    auto a = net::buildAttack(150000, 0x07);  // ServerType8 (uaRO) 0x0190 actionrequest = 19
    CHECK_EQ(a.size(), static_cast<usize>(19));
    CHECK_EQ(static_cast<u16>(a[0] | (a[1] << 8)), 0x0190);
    const u32 tg = static_cast<u32>(a[5] | (a[6] << 8) | (a[7] << 16) | (a[8] << 24));
    CHECK_EQ(tg, 150000u);  // target @5
    CHECK_EQ(a[18], 0x07);  // action @18

    B b(29, 0);  // 0x8a damage
    at16(b, 0, 0x008a);
    at32(b, 2, 111);    // src
    at32(b, 6, 222);    // dst
    at32(b, 14, 432);   // sdelay (amotion)
    at32(b, 18, 288);   // ddelay (dmotion) — drives the victim's flinch duration
    at16(b, 22, 1234);  // damage
    b[26] = 0;          // type
    at16(b, 27, 567);   // damage2 (left-hand hit, dual-wield/katar)
    net::ActDamage ad;
    CHECK_EQ(net::decodeAct(b.data(), b.size(), ad), 29u);
    CHECK_EQ(ad.src, 111u);
    CHECK_EQ(ad.dst, 222u);
    CHECK_EQ(ad.amotion, 432u);
    CHECK_EQ(ad.dmotion, 288u);
    CHECK_EQ(ad.damage, 1234);
    CHECK_EQ(ad.damage2, 567);
    CHECK_EQ(net::decodeAct(b.data(), 28, ad), 0u);  // short
}

TEST_CASE(protocol_map_change) {
    B b(22, 0);  // 0x91 same-server warp
    at16(b, 0, 0x0091);
    atStr(b, 2, "prontera.gat", 16);
    at16(b, 18, 156);
    at16(b, 20, 191);
    net::MapChange mc;
    CHECK_EQ(net::decodeMapChange(b.data(), b.size(), mc), 22u);
    CHECK(mc.mapName == "prontera.gat");
    CHECK_EQ(mc.x, 156);
    CHECK_EQ(mc.y, 191);
    CHECK(!mc.newServer);

    B c(28, 0);  // 0x92 cross-server warp
    at16(c, 0, 0x0092);
    atStr(c, 2, "morocc.gat", 16);
    at16(c, 18, 50);
    at16(c, 20, 60);
    c[22] = 127; c[23] = 0; c[24] = 0; c[25] = 1;
    at16(c, 26, 5121);
    CHECK_EQ(net::decodeMapChange(c.data(), c.size(), mc), 28u);
    CHECK(mc.mapName == "morocc.gat");
    CHECK(mc.newServer);
    CHECK(mc.ip == "127.0.0.1");
    CHECK_EQ(mc.port, 5121);
    CHECK_EQ(net::decodeMapChange(c.data(), 27, mc), 0u);  // short
}

TEST_CASE(protocol_state_change) {
    B b(15, 0);
    at16(b, 0, 0x0229);
    at32(b, 2, 150000);  // gid
    at16(b, 6, 3);       // opt1: OPT1_STUN
    at16(b, 8, 0x011);   // opt2: POISON | BLIND
    at32(b, 10, 0x20);   // option: OPTION_RIDING
    u32 gid = 0, option = 0;
    u16 opt1 = 0, opt2 = 0;
    CHECK_EQ(net::decodeStateChange(b.data(), b.size(), gid, option, opt1, opt2), 15u);
    CHECK_EQ(gid, 150000u);
    CHECK_EQ(option, 0x20u);
    CHECK_EQ(opt1, 3u);
    CHECK_EQ(opt2, 0x011u);
    CHECK_EQ(net::decodeStateChange(b.data(), 14, gid, option, opt1, opt2), 0u);  // short
}

TEST_CASE(protocol_skill_nodamage) {
    B b(15, 0);
    at16(b, 0, 0x011a);
    at16(b, 2, 28);     // skillId (AL_HEAL)
    at16(b, 4, 120);    // heal
    at32(b, 6, 2000);   // dst AID
    at32(b, 10, 3000);  // src AID
    b[14] = 1;          // fail
    net::SkillNoDamage sd;
    CHECK_EQ(net::decodeSkillNoDamage(b.data(), b.size(), sd), 15u);
    CHECK_EQ(sd.skillId, 28u);
    CHECK_EQ(sd.heal, 120u);
    CHECK_EQ(sd.dst, 2000u);
    CHECK_EQ(sd.src, 3000u);
    CHECK_EQ(sd.fail, 1u);
    CHECK_EQ(net::decodeSkillNoDamage(b.data(), 14, sd), 0u);  // short
}

TEST_CASE(protocol_couple_status) {
    B b(14, 0);
    at16(b, 0, 0x0141);
    at32(b, 2, 18);   // type = SP_LUK
    at32(b, 6, 9);    // base
    at32(b, 10, 30);  // bonus (e.g. Gloria +30 LUK)
    net::CoupleStatus cs;
    CHECK_EQ(net::decodeCoupleStatus(b.data(), b.size(), cs), 14u);
    CHECK_EQ(cs.type, 18u);
    CHECK_EQ(cs.base, 9);
    CHECK_EQ(cs.bonus, 30);
    at32(b, 10, 0xFFFFFFF6u);  // -10 (a stat-lowering debuff: Decrease AGI)
    CHECK_EQ(net::decodeCoupleStatus(b.data(), b.size(), cs), 14u);
    CHECK_EQ(cs.bonus, -10);
    CHECK_EQ(net::decodeCoupleStatus(b.data(), 13, cs), 0u);  // short
}

TEST_CASE(protocol_npc_newentry2_spawn) {
    B b(53, 0);
    at16(b, 0, 0x0079);
    at32(b, 2, 5000);    // gid
    at16(b, 6, 150);     // speed
    at16(b, 14, 1002);   // class (Poring)
    at16(b, 16, 7);      // hair
    b[45] = 0;           // sex
    net::ActorEntry e;
    CHECK_EQ(net::decodeActorEntry(b.data(), b.size(), e), 53u);
    CHECK_EQ(e.gid, 5000u);
    CHECK_EQ(e.class_, 1002u);
    CHECK_EQ(e.hair, 7u);
    CHECK(!e.pc);
    CHECK(!e.walking);
    CHECK_EQ(net::decodeActorEntry(b.data(), 52, e), 0u);  // short
}

TEST_CASE(protocol_change_dir) {
    B b(9, 0);
    at16(b, 0, 0x009c);
    at32(b, 2, 3000);  // gid
    b[8] = 4;          // body dir
    u32 gid = 0;
    u8 dir = 0;
    CHECK_EQ(net::decodeChangeDir(b.data(), b.size(), gid, dir), 9u);
    CHECK_EQ(gid, 3000u);
    CHECK_EQ(dir, 4u);
    CHECK_EQ(net::decodeChangeDir(b.data(), 8, gid, dir), 0u);  // short
}

TEST_CASE(protocol_recovery) {
    B b(6, 0);
    at16(b, 0, 0x013d);
    at16(b, 2, 5);     // type = SP_HP
    at16(b, 4, 250);   // val
    u16 type = 0, val = 0;
    CHECK_EQ(net::decodeRecovery(b.data(), b.size(), type, val), 6u);
    CHECK_EQ(type, 5u);
    CHECK_EQ(val, 250u);
    CHECK_EQ(net::decodeRecovery(b.data(), 5, type, val), 0u);  // short
}

TEST_CASE(protocol_whisper) {
    B b(28, 0);
    at16(b, 0, 0x0097);
    b[4] = 'B'; b[5] = 'o'; b[6] = 'b';            // nick at offset 4
    b.push_back('h'); b.push_back('i'); b.push_back(0);  // message "hi" at offset 28
    at16(b, 2, static_cast<u16>(b.size()));        // len = total packet length
    std::string nick, msg;
    CHECK_EQ(net::decodeWhisper(b.data(), b.size(), nick, msg), static_cast<usize>(b.size()));
    CHECK(nick == "Bob");
    CHECK(msg == "hi");
    CHECK_EQ(net::decodeWhisper(b.data(), 27, nick, msg), 0u);  // short
}

TEST_CASE(protocol_player_actions) {
    // CZ_WHISPER (0x96): id(2) len(2) target[24] message + NUL. len = 4 + 24 + strlen + 1.
    const B w = net::buildWhisper("Alice", "hello");
    CHECK_EQ(net::peekId(w), 0x0096);
    CHECK_EQ(w.size(), static_cast<usize>(4 + 24 + 5 + 1));
    CHECK_EQ(static_cast<u16>(w[2] | (w[3] << 8)), static_cast<u16>(w.size()));  // len field
    CHECK(w[4] == 'A' && w[8] == 'e' && w[9] == 0);  // target @4, NUL-padded
    CHECK(w[28] == 'h' && w[32] == 'o');             // message @28
    CHECK_EQ(w.back(), 0);                            // trailing NUL

    // CZ_REQ_EXCHANGE_ITEM (0xe4): id(2) targetAID(4) = 6B.
    const B tr = net::buildTradeRequest(0x0a0b0c0d);
    CHECK_EQ(net::peekId(tr), 0x00e4);
    CHECK_EQ(tr.size(), 6u);
    CHECK_EQ(static_cast<u32>(tr[2] | (tr[3] << 8) | (tr[4] << 16) | (tr[5] << 24)), 0x0a0b0c0du);

    // CZ_ADD_FRIENDS (0x202): id(2) name[24] = 26B.
    const B af = net::buildAddFriend("Bob");
    CHECK_EQ(net::peekId(af), 0x0202);
    CHECK_EQ(af.size(), 26u);
    CHECK(af[2] == 'B' && af[4] == 'b' && af[5] == 0);  // name @2, NUL-padded

    // CZ_SETTING_WHISPER_PC (0xcf): id(2) name[24] type(1) = 27B. block=0, unblock=1.
    const B ig = net::buildIgnorePlayer("Carol", true);
    CHECK_EQ(net::peekId(ig), 0x00cf);
    CHECK_EQ(ig.size(), 27u);
    CHECK(ig[2] == 'C' && ig[6] == 'l' && ig[7] == 0);  // name @2
    CHECK_EQ(ig[26], 0);                                 // type 0 = block
    CHECK_EQ(net::buildIgnorePlayer("Carol", false)[26], 1);  // type 1 = unblock

    // ZC_ACK_WHISPER (0x98, 3B): result byte.
    B ack(3, 0);
    at16(ack, 0, 0x0098);
    ack[2] = 1;  // target offline
    u8 res = 0;
    CHECK_EQ(net::decodeWhisperAck(ack.data(), ack.size(), res), 3u);
    CHECK_EQ(res, 1);
    CHECK_EQ(net::decodeWhisperAck(ack.data(), 2, res), 0u);  // short packet
}

TEST_CASE(protocol_cp1251_roundtrip) {
    // Russian text is Windows-1251 on the wire but UTF-8 in the client. Incoming uses cp1251ToUtf8,
    // outgoing uses utf8ToCp1251 (S.: "кириллица в чате не работает"). "Привет" + Ё + ё (the last
    // two exercise the special 0x80..0xBF table slots, not the contiguous А..я block).
    const std::string cp = "\xCF\xF0\xE8\xE2\xE5\xF2\xA8\xB8";  // Привет Ё ё in cp1251 (8 bytes, no NUL)
    const std::string utf = net::cp1251ToUtf8(cp);
    CHECK_EQ(utf.size(), 16u);                              // 8 Cyrillic letters * 2 UTF-8 bytes
    CHECK(net::utf8ToCp1251(utf) == cp);                    // exact round trip back to cp1251
    CHECK(net::utf8ToCp1251("abc 123") == "abc 123");       // ASCII untouched
    CHECK(net::cp1251ToUtf8("abc 123") == "abc 123");
    CHECK(net::utf8ToCp1251(net::cp1251ToUtf8("Hi " + cp)) == "Hi " + cp);  // mixed
    CHECK(net::utf8ToCp1251("\xE4\xB8\xAD") == "?");        // U+4E2D (outside cp1251) -> '?'

    // The builders now encode their text to cp1251 and size the length field from the SHORTER
    // converted form (16 UTF-8 bytes -> 8 cp1251 bytes).
    const B w = net::buildWhisper("Bob", utf);
    CHECK_EQ(net::peekId(w), 0x0096);
    CHECK_EQ(w.size(), static_cast<usize>(4 + 24 + 8 + 1));
    CHECK(static_cast<unsigned char>(w[28]) == 0xCFu);      // first cp1251 body byte (П)
    CHECK_EQ(w.back(), 0);
    const B g = net::buildGlobalMessage("Bob", utf);        // "Bob : " (6 ASCII) + 8 cp1251 + NUL
    CHECK_EQ(net::peekId(g), 0x00f3);  // ServerType8 (uaRO) public_chat
    CHECK_EQ(g.size(), static_cast<usize>(4 + 6 + 8 + 1));
    CHECK(static_cast<unsigned char>(g[10]) == 0xCFu);      // body starts after "Bob : "
}

TEST_CASE(protocol_trade) {
    // ZC_EXCHANGE_REQUEST (0xe5, 26B): requester name@2.
    B rq(26, 0);
    at16(rq, 0, 0x00e5);
    rq[2] = 'A'; rq[3] = 'n'; rq[4] = 'n';
    std::string nm;
    CHECK_EQ(net::decodeTradeRequest(rq.data(), rq.size(), nm), 26u);
    CHECK(nm == "Ann");

    // ZC_ACK_EXCHANGE_ITEM (0xe7, 3B): start result.
    B st(3, 0);
    at16(st, 0, 0x00e7);
    st[2] = 2;  // started
    u8 res = 0;
    CHECK_EQ(net::decodeTradeStart(st.data(), st.size(), res), 3u);
    CHECK_EQ(res, 2);

    // ZC_ADD_EXCHANGE_ITEM (0xe9, 19B): amount@2, nameid@6, identify@8, refine@10, cards@11.
    B ad(19, 0);
    at16(ad, 0, 0x00e9);
    at32(ad, 2, 5);      // amount
    at16(ad, 6, 501);    // nameid (Red Potion)
    ad[8] = 1;           // identify
    ad[10] = 7;          // refine
    at16(ad, 11, 4035);  // card[0]
    net::TradeAddItem ti;
    CHECK_EQ(net::decodeTradeAdd(ad.data(), ad.size(), ti), 19u);
    CHECK_EQ(ti.amount, 5u);
    CHECK_EQ(ti.nameid, 501);
    CHECK_EQ(ti.refine, 7);
    CHECK_EQ(ti.cards[0], 4035);

    // ZC_ACK_ADD_EXCHANGE_ITEM (0xea, 5B): index@2, fail@4.
    B aa(5, 0);
    at16(aa, 0, 0x00ea);
    at16(aa, 2, 9);
    aa[4] = 1;
    u16 idx = 0;
    u8 fail = 0;
    CHECK_EQ(net::decodeTradeAddAck(aa.data(), aa.size(), idx, fail), 5u);
    CHECK_EQ(idx, 9);
    CHECK_EQ(fail, 1);

    // ZC_EXEC_EXCHANGE_ITEM (0xf0, 3B): fail.
    B dn(3, 0);
    at16(dn, 0, 0x00f0);
    dn[2] = 0;
    u8 df = 9;
    CHECK_EQ(net::decodeTradeDone(dn.data(), dn.size(), df), 3u);
    CHECK_EQ(df, 0);

    // Builders (C->M).
    const B re = net::buildTradeReply(true);
    CHECK_EQ(net::peekId(re), 0x00e6);
    CHECK_EQ(re.size(), 3u);
    CHECK_EQ(re[2], 3);
    CHECK_EQ(net::buildTradeReply(false)[2], 4);
    const B tad = net::buildTradeAdd(9, 5);
    CHECK_EQ(net::peekId(tad), 0x00e8);
    CHECK_EQ(tad.size(), 8u);
    CHECK_EQ(static_cast<u16>(tad[2] | (tad[3] << 8)), 9);
    CHECK_EQ(static_cast<u32>(tad[4] | (tad[5] << 8) | (tad[6] << 16) | (tad[7] << 24)), 5u);
    // Per the server packet_db.txt: 0xeb tradeok (lock), 0xed tradecancel, 0xef tradecommit.
    // EXEC and CANCEL were previously swapped, so "Trade" sent cancel (the deal aborted). (S.)
    CHECK_EQ(net::peekId(net::buildTradeLock()), 0x00eb);
    CHECK_EQ(net::peekId(net::buildTradeConfirm()), 0x00ef);  // commit, NOT 0xed
    CHECK_EQ(net::peekId(net::buildTradeCancel()), 0x00ed);   // cancel, NOT 0xef
}

TEST_CASE(protocol_skill_casting) {
    // ZC_USESKILL_ACK (0x13e, 24B): src@2, skillId@14, casttime(ms)@20.
    B b(24, 0);
    at16(b, 0, 0x013e);
    at32(b, 2, 150001);   // caster gid
    at16(b, 14, 19);      // skillId (Fire Bolt)
    at32(b, 20, 1800);    // casttime ms
    u32 src = 0, ct = 0;
    u16 sid = 0;
    CHECK_EQ(net::decodeSkillCasting(b.data(), b.size(), src, sid, ct), 24u);
    CHECK_EQ(src, 150001u);
    CHECK_EQ(sid, 19);
    CHECK_EQ(ct, 1800u);
    CHECK_EQ(net::decodeSkillCasting(b.data(), 23, src, sid, ct), 0u);  // short

    // ZC_USESKILL_CANCEL (0x1b9, 6B): caster gid@2.
    B c(6, 0);
    at16(c, 0, 0x01b9);
    at32(c, 2, 150001);
    u32 gid = 0;
    CHECK_EQ(net::decodeSkillCastCancel(c.data(), c.size(), gid), 6u);
    CHECK_EQ(gid, 150001u);
    CHECK_EQ(net::decodeSkillCastCancel(c.data(), 5, gid), 0u);  // short
}

TEST_CASE(protocol_skill_fail) {
    B b(10, 0);
    at16(b, 0, 0x0110);
    at16(b, 2, 5);     // skillId
    b[9] = 1;          // cause = SP insufficient
    u16 skillId = 0;
    u8 cause = 0;
    CHECK_EQ(net::decodeSkillFail(b.data(), b.size(), skillId, cause), 10u);
    CHECK_EQ(skillId, 5u);
    CHECK_EQ(cause, 1u);
    CHECK_EQ(net::decodeSkillFail(b.data(), 9, skillId, cause), 0u);  // short
}

TEST_CASE(protocol_emotion) {
    B b(7, 0);
    at16(b, 0, 0x00c0);
    at32(b, 2, 4000);  // gid
    b[6] = 3;          // emote type
    u32 gid = 0;
    u8 type = 0;
    CHECK_EQ(net::decodeEmotion(b.data(), b.size(), gid, type), 7u);
    CHECK_EQ(gid, 4000u);
    CHECK_EQ(type, 3u);
    CHECK_EQ(net::decodeEmotion(b.data(), 6, gid, type), 0u);  // short
}

TEST_CASE(protocol_ground_skill) {
    B b(18, 0);
    at16(b, 0, 0x0117);
    at16(b, 2, 89);    // skillId (WZ_STORMGUST)
    at32(b, 4, 3000);  // src
    at16(b, 10, 150);  // x
    at16(b, 12, 200);  // y
    net::GroundSkill gs;
    CHECK_EQ(net::decodeGroundSkill(b.data(), b.size(), gs), 18u);
    CHECK_EQ(gs.skillId, 89u);
    CHECK_EQ(gs.src, 3000u);
    CHECK_EQ(gs.x, 150u);
    CHECK_EQ(gs.y, 200u);
    CHECK_EQ(net::decodeGroundSkill(b.data(), 17, gs), 0u);  // short
}

TEST_CASE(protocol_status_effect) {
    B b(9, 0);
    at16(b, 0, 0x0196);
    at16(b, 2, static_cast<u16>(net::SI_RIDING));  // type = 27 (riding)
    at32(b, 4, 150000);                            // aid (own account on login)
    b[8] = 1;                                      // flag: riding on
    u32 gid = 0;
    u16 type = 0;
    u8 flag = 0;
    CHECK_EQ(net::decodeStatusEffect(b.data(), b.size(), gid, type, flag), 9u);
    CHECK_EQ(gid, 150000u);
    CHECK_EQ(type, static_cast<u16>(net::SI_RIDING));
    CHECK_EQ(flag, 1);
    CHECK_EQ(net::decodeStatusEffect(b.data(), 8, gid, type, flag), 0u);  // short -> 0
}

TEST_CASE(protocol_notify_effect) {
    // ZC_NOTIFY_EFFECT (0x19b, 10B): id(2) aid(4) type(4). type 0 = base lvl up, 1 = job lvl up.
    B b(10, 0);
    at16(b, 0, 0x019b);
    at32(b, 2, 150000);  // aid
    at32(b, 6, 1);       // type = job level up
    u32 gid = 0, type = 0;
    CHECK_EQ(net::decodeNotifyEffect(b.data(), b.size(), gid, type), 10u);
    CHECK_EQ(gid, 150000u);
    CHECK_EQ(type, 1u);
    CHECK_EQ(net::decodeNotifyEffect(b.data(), 9, gid, type), 0u);  // short -> 0
}

TEST_CASE(protocol_par_change) {
    B b(8, 0);
    at16(b, 0, 0x00b0);
    at16(b, 2, static_cast<u16>(net::SP_MAXHP));  // type = 6
    at32(b, 4, 1234);                             // value
    u16 type = 0;
    u32 value = 0;
    CHECK_EQ(net::decodeParChange(b.data(), b.size(), type, value), 8u);
    CHECK_EQ(type, static_cast<u16>(net::SP_MAXHP));
    CHECK_EQ(value, 1234u);
    CHECK_EQ(net::decodeParChange(b.data(), 7, type, value), 0u);  // short -> 0
}

TEST_CASE(protocol_sprite_change) {
    B b(11, 0);
    at16(b, 0, 0x01d7);
    at32(b, 2, 2000123);  // gid
    b[6] = static_cast<u8>(net::LOOK_WEAPON);
    at16(b, 7, 1);  // weapon look (dagger)
    at16(b, 9, 0);  // shield look
    u32 gid = 0;
    u8 type = 0;
    u16 val = 0, val2 = 0;
    CHECK_EQ(net::decodeSpriteChange(b.data(), b.size(), gid, type, val, val2), 11u);
    CHECK_EQ(gid, 2000123u);
    CHECK_EQ(type, static_cast<u8>(net::LOOK_WEAPON));
    CHECK_EQ(val, 1);
    CHECK_EQ(net::decodeSpriteChange(b.data(), 10, gid, type, val, val2), 0u);  // short -> 0
}

TEST_CASE(protocol_movetoattack) {
    B b(16, 0);
    at16(b, 0, 0x0139);
    at32(b, 2, 2000123);  // target gid
    at16(b, 6, 150);      // target x
    at16(b, 8, 160);      // target y
    at16(b, 10, 145);     // self x
    at16(b, 12, 160);     // self y
    at16(b, 14, 2);       // attack range (cells)
    net::MoveToAttack m;
    CHECK_EQ(net::decodeMoveToAttack(b.data(), b.size(), m), 16u);
    CHECK_EQ(m.gid, 2000123u);
    CHECK_EQ(m.x, 150);
    CHECK_EQ(m.y, 160);
    CHECK_EQ(m.selfX, 145);
    CHECK_EQ(m.selfY, 160);
    CHECK_EQ(m.range, 2);
    CHECK_EQ(net::decodeMoveToAttack(b.data(), 15, m), 0u);  // short -> 0
}

TEST_CASE(protocol_npc_dialog) {
    auto rd32at = [](const B& v, usize o) {
        return static_cast<u32>(v[o]) | (static_cast<u32>(v[o + 1]) << 8) |
               (static_cast<u32>(v[o + 2]) << 16) | (static_cast<u32>(v[o + 3]) << 24);
    };
    const B contact = net::buildNpcContact(2000123);
    CHECK_EQ(contact.size(), 7u);
    CHECK_EQ(net::peekId(contact), 0x0090);
    CHECK_EQ(rd32at(contact, 2), 2000123u);
    CHECK_EQ(contact[6], 0x01);  // type = click

    const B next = net::buildNpcNext(2000123);
    CHECK_EQ(next.size(), 6u);
    CHECK_EQ(net::peekId(next), 0x00b9);
    CHECK_EQ(rd32at(next, 2), 2000123u);

    const B close = net::buildNpcClose(2000123);
    CHECK_EQ(close.size(), 6u);
    CHECK_EQ(net::peekId(close), 0x0146);

    const B menu = net::buildNpcMenu(2000123, 3);
    CHECK_EQ(menu.size(), 7u);
    CHECK_EQ(net::peekId(menu), 0x00b8);
    CHECK_EQ(menu[6], 3);  // 1-based choice

    const B restart = net::buildRestart(0);
    CHECK_EQ(restart.size(), 3u);
    CHECK_EQ(net::peekId(restart), 0x00b2);
    CHECK_EQ(restart[2], 0);  // 0 = return to save point

    {  // ZC_SAY_DIALOG (0xb4): id(2) len(2) gid(4) "Hi"(2) NUL(1) = 11 bytes
        B b(11, 0);
        at16(b, 0, 0x00b4);
        at16(b, 2, 11);
        at32(b, 4, 2000123);
        b[8] = 'H';
        b[9] = 'i';
        u32 gid = 0;
        std::string text;
        CHECK_EQ(net::decodeScriptText(b.data(), b.size(), gid, text), 11u);
        CHECK_EQ(gid, 2000123u);
        CHECK(text == "Hi");
        CHECK_EQ(net::decodeScriptText(b.data(), 7, gid, text), 0u);  // short header -> 0
    }
    {  // ZC_CLOSE_DIALOG (0xb6): gid only, 6 bytes
        B b(6, 0);
        at16(b, 0, 0x00b6);
        at32(b, 2, 2000123);
        u32 gid = 0;
        CHECK_EQ(net::decodeScriptGid(b.data(), b.size(), gid), 6u);
        CHECK_EQ(gid, 2000123u);
    }
}

TEST_CASE(protocol_sit_stand) {
    // Sit/stand reuse the action packet (ServerType8 0x0190): the target is ignored,
    // action 2 = sit, 3 = stand (7 = continuous attack); action byte is at offset 18.
    const B sit = net::buildAttack(0, net::ACT_SIT);
    CHECK_EQ(sit.size(), 19u);
    CHECK_EQ(net::peekId(sit), 0x0190);
    CHECK_EQ(sit[18], 2);
    const B stand = net::buildAttack(0, net::ACT_STAND);
    CHECK_EQ(stand[18], 3);
    CHECK_EQ(static_cast<int>(net::ACT_ATTACK), 7);
}

TEST_CASE(protocol_chat_build) {
    // CZ_REQUEST_CHAT (0x8c): wire "Name : text\0"; len = 4 + msg + 1 (NUL).
    const B b = net::buildGlobalMessage("Hero", "hi all");
    const std::string wire = "Hero : hi all";
    CHECK_EQ(net::peekId(b), 0x00f3);  // ServerType8 (uaRO) public_chat
    CHECK_EQ(b.size(), 4u + wire.size() + 1u);
    CHECK_EQ(static_cast<usize>(b[2] | (b[3] << 8)), b.size());  // len field == total bytes
    std::string got;
    for (usize i = 4; i < b.size() && b[i]; ++i) got += static_cast<char>(b[i]);
    CHECK(got == wire);
    CHECK_EQ(b.back(), 0);  // NUL terminated
}

TEST_CASE(protocol_cp1251_to_utf8) {
    CHECK(net::cp1251ToUtf8("abc") == "abc");  // ASCII passes through
    // "Привет" in CP1251 -> UTF-8
    CHECK(net::cp1251ToUtf8("\xCF\xF0\xE8\xE2\xE5\xF2") ==
          "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82");
    CHECK(net::cp1251ToUtf8("\xC0") == "\xD0\x90");  // А (U+0410)
    CHECK(net::cp1251ToUtf8("\xFF") == "\xD1\x8F");  // я (U+044F)
    CHECK(net::cp1251ToUtf8("\xA8") == "\xD0\x81");  // Ё (U+0401)
    CHECK(net::cp1251ToUtf8("\xB8") == "\xD1\x91");  // ё (U+0451)
}

TEST_CASE(protocol_chat_decode) {
    {  // ZC_NOTIFY_CHAT (0x8d): id(2) len(2) gid(4) "Hi"(2) NUL(1) = 11
        B b(11, 0);
        at16(b, 0, 0x008d);
        at16(b, 2, 11);
        at32(b, 4, 2000123);
        b[8] = 'H';
        b[9] = 'i';
        u32 gid = 0;
        std::string msg;
        CHECK_EQ(net::decodeChat(b.data(), b.size(), gid, msg), 11u);
        CHECK_EQ(gid, 2000123u);
        CHECK(msg == "Hi");
        CHECK_EQ(net::decodeChat(b.data(), 7, gid, msg), 0u);  // short -> 0
    }
    {  // ZC_NOTIFY_PLAYERCHAT (0x8e): id(2) len(2) "Yo"(2) NUL(1) = 7
        B b(7, 0);
        at16(b, 0, 0x008e);
        at16(b, 2, 7);
        b[4] = 'Y';
        b[5] = 'o';
        std::string msg;
        CHECK_EQ(net::decodePlayerChat(b.data(), b.size(), msg), 7u);
        CHECK(msg == "Yo");
    }
}

TEST_CASE(protocol_shop_deal_select) {
    const B b = net::buildDealSelect(2000123, 1);  // 1 = sell
    CHECK_EQ(b.size(), 7u);
    CHECK_EQ(net::peekId(b), 0x00c5);
    CHECK_EQ(b[6], 1);
    B c(6, 0);  // ZC_SELECT_DEALTYPE (0xc4): id(2) npc(4)
    at16(c, 0, 0x00c4);
    at32(c, 2, 2000123);
    u32 npc = 0;
    CHECK_EQ(net::decodeDealType(c.data(), c.size(), npc), 6u);
    CHECK_EQ(npc, 2000123u);
}

TEST_CASE(protocol_shop_buy_list) {
    // ZC_PC_PURCHASE_ITEMLIST (0xc6): 4-byte header + one 11B entry = 15.
    B b(15, 0);
    at16(b, 0, 0x00c6);
    at16(b, 2, 15);
    at32(b, 4, 100);   // base price
    at32(b, 8, 90);    // discounted price
    b[12] = 3;         // type
    at16(b, 13, 501);  // nameid (Red Potion)
    std::vector<net::ShopItem> items;
    CHECK_EQ(net::decodeBuyList(b.data(), b.size(), items), 15u);
    CHECK_EQ(items.size(), 1u);
    CHECK_EQ(items[0].basePrice, 100u);
    CHECK_EQ(items[0].price, 90u);
    CHECK_EQ(items[0].type, 3);
    CHECK_EQ(items[0].nameid, 501);
    // Build the buy order back: CZ_PC_PURCHASE_ITEMLIST (0xc8) { amount(2) nameid(2) }.
    std::vector<net::ShopBuyEntry> order{{501, 2}};
    const B ob = net::buildBuyList(order);
    CHECK_EQ(net::peekId(ob), 0x00c8);
    CHECK_EQ(ob.size(), 8u);  // 4 header + 1*4
    CHECK_EQ(static_cast<usize>(ob[2] | (ob[3] << 8)), 8u);
    CHECK_EQ(static_cast<u16>(ob[4] | (ob[5] << 8)), 2);    // amount
    CHECK_EQ(static_cast<u16>(ob[6] | (ob[7] << 8)), 501);  // nameid
}

TEST_CASE(protocol_shop_sell_list) {
    // ZC_PC_SELL_ITEMLIST (0xc7): 4-byte header + one 10B entry = 14.
    B b(14, 0);
    at16(b, 0, 0x00c7);
    at16(b, 2, 14);
    at16(b, 4, 7);    // inventory index (slot + 2)
    at32(b, 6, 50);   // sell price
    at32(b, 10, 60);  // overcharge price
    std::vector<net::SellItem> items;
    CHECK_EQ(net::decodeSellList(b.data(), b.size(), items), 14u);
    CHECK_EQ(items.size(), 1u);
    CHECK_EQ(items[0].index, 7);
    CHECK_EQ(items[0].basePrice, 50u);
    CHECK_EQ(items[0].price, 60u);
    // CZ_PC_SELL_ITEMLIST (0xc9) { index(2) amount(2) }.
    std::vector<net::ShopSellEntry> order{{7, 1}};
    const B ob = net::buildSellList(order);
    CHECK_EQ(net::peekId(ob), 0x00c9);
    CHECK_EQ(ob.size(), 8u);
    CHECK_EQ(static_cast<u16>(ob[4] | (ob[5] << 8)), 7);  // index
    CHECK_EQ(static_cast<u16>(ob[6] | (ob[7] << 8)), 1);  // amount
}

TEST_CASE(protocol_vending) {
    // ZC_PC_PURCHASE_ITEMLIST_FROMMC (0x133): 8B header + one 22B entry = 30.
    B b(30, 0);
    at16(b, 0, 0x0133);
    at16(b, 2, 30);
    at32(b, 4, 2000123);  // vendor AID
    at32(b, 8, 12500);    // price per unit
    at16(b, 12, 7);       // amount the vendor still has
    at16(b, 14, 9);       // wire index (cart slot 7 + 2)
    b[16] = 4;            // type (weapon)
    at16(b, 17, 1201);    // nameid (Knife)
    b[19] = 1;            // identify
    b[20] = 0;            // attribute (damaged)
    b[21] = 5;            // refine
    at16(b, 22, 4035);    // card[0] (Hydra Card) — entry offset o+14
    at16(b, 24, 4035);    // card[1]
    at16(b, 26, 0);       // card[2] empty slot
    at16(b, 28, 0);       // card[3] empty slot
    u32 aid = 0;
    std::vector<net::VendItem> items;
    CHECK_EQ(net::decodeVendingList(b.data(), b.size(), aid, items), 30u);
    CHECK_EQ(aid, 2000123u);
    CHECK_EQ(items.size(), 1u);
    CHECK_EQ(items[0].price, 12500u);
    CHECK_EQ(items[0].amount, 7);
    CHECK_EQ(items[0].index, 9);
    CHECK_EQ(items[0].type, 4);
    CHECK_EQ(items[0].nameid, 1201);
    CHECK_EQ(items[0].identify, 1);
    CHECK_EQ(items[0].refine, 5);
    CHECK_EQ(items[0].cards[0], 4035);  // EQUIPSLOTINFO decoded (two Hydra Cards + two empty slots)
    CHECK_EQ(items[0].cards[1], 4035);
    CHECK_EQ(items[0].cards[2], 0);
    CHECK_EQ(items[0].cards[3], 0);
    // CZ_REQ_BUY_FROMMC (0x130): open the shop — id(2) + AID(4) = 6B.
    const B vo = net::buildVendingOpen(2000123);
    CHECK_EQ(net::peekId(vo), 0x0130);
    CHECK_EQ(vo.size(), 6u);
    CHECK_EQ(static_cast<u32>(vo[2] | (vo[3] << 8) | (vo[4] << 16) | (vo[5] << 24)), 2000123u);
    // CZ_PC_PURCHASE_ITEMLIST_FROMMC (0x134): 8B header + { amount(2) index(2) } — echo index verbatim.
    std::vector<net::VendBuyEntry> order{{3, 9}};  // buy 3 of wire-index 9
    const B pb = net::buildVendingPurchase(2000123, order);
    CHECK_EQ(net::peekId(pb), 0x0134);
    CHECK_EQ(pb.size(), 12u);  // 8 header + 1*4
    CHECK_EQ(static_cast<usize>(pb[2] | (pb[3] << 8)), 12u);
    CHECK_EQ(static_cast<u32>(pb[4] | (pb[5] << 8) | (pb[6] << 16) | (pb[7] << 24)), 2000123u);
    CHECK_EQ(static_cast<u16>(pb[8] | (pb[9] << 8)), 3);    // amount
    CHECK_EQ(static_cast<u16>(pb[10] | (pb[11] << 8)), 9);  // index (verbatim)
    // ZC_PC_PURCHASE_RESULT_FROMMC (0x135, 7B): index(2) amount(2) result(1).
    B rb(7, 0);
    at16(rb, 0, 0x0135);
    at16(rb, 2, 9);
    at16(rb, 4, 3);
    rb[6] = 0;  // success
    u16 ri = 0, ra = 0;
    u8 rr = 9;
    CHECK_EQ(net::decodeVendingResult(rb.data(), rb.size(), ri, ra, rr), 7u);
    CHECK_EQ(ri, 9);
    CHECK_EQ(ra, 3);
    CHECK_EQ(rr, 0);
}

TEST_CASE(protocol_openstore) {
    // ZC_OPENSTORE (0x12d, 4B): id + maxItems (Vending skill used -> open the setup).
    B b(4, 0);
    at16(b, 0, 0x012d);
    at16(b, 2, 10);
    u16 maxItems = 0;
    CHECK_EQ(net::decodeOpenStore(b.data(), b.size(), maxItems), 4u);
    CHECK_EQ(maxItems, 10);
    // CZ_REQ_OPENSTORE (0x1b2): id(2) len(2) name[80] flag(1) { index(2) amount(2) price(4) }.
    std::vector<net::VendSellEntry> wares{{5, 3, 1000}};  // cart wire-index 5, qty 3, 1000z each
    const B ob = net::buildVendingOpenStore("MyShop", wares);
    CHECK_EQ(net::peekId(ob), 0x01b2);
    CHECK_EQ(ob.size(), 93u);  // 85 (name+flag header) + 1*8
    CHECK_EQ(static_cast<usize>(ob[2] | (ob[3] << 8)), 93u);
    CHECK_EQ(ob[4], 'M');   // name starts at offset 4
    CHECK_EQ(ob[5], 'y');
    CHECK_EQ(ob[84], 1);    // flag = open
    CHECK_EQ(static_cast<u16>(ob[85] | (ob[86] << 8)), 5);  // index
    CHECK_EQ(static_cast<u16>(ob[87] | (ob[88] << 8)), 3);  // amount
    CHECK_EQ(static_cast<u32>(ob[89] | (ob[90] << 8) | (ob[91] << 16) | (ob[92] << 24)), 1000u);  // price
    // CZ_REQ_CLOSESTORE (0x12e, 2B).
    const B cb = net::buildVendingCloseStore();
    CHECK_EQ(net::peekId(cb), 0x012e);
    CHECK_EQ(cb.size(), 2u);
}

TEST_CASE(protocol_use_skill) {
    // ServerType8 (uaRO) skill_use 0x0072, 25B: lv@6, skillId@10, targetAID@21.
    const B b = net::buildUseSkill(5, 41, 2000123);  // e.g. AL_HEAL lv5 cast on AID 2000123
    CHECK_EQ(net::peekId(b), 0x0072);
    CHECK_EQ(b.size(), 25u);
    CHECK_EQ(static_cast<u16>(b[6] | (b[7] << 8)), 5);    // level @6
    CHECK_EQ(static_cast<u16>(b[10] | (b[11] << 8)), 41);  // skill id @10
    CHECK_EQ(static_cast<u32>(b[21] | (b[22] << 8) | (b[23] << 16) | (b[24] << 24)),
             2000123u);  // target @21
}

TEST_CASE(protocol_my_vending) {
    // ZC_PC_PURCHASE_MYITEMLIST (0x136): 8B header + one 22B entry = 30. index@4 / amount@6 (swapped).
    B b(30, 0);
    at16(b, 0, 0x0136);
    at16(b, 2, 30);
    at32(b, 4, 2000123);  // vendor AID (self)
    at32(b, 8, 5000);     // price
    at16(b, 12, 9);       // index (cart slot 7 + 2) @rel 4
    at16(b, 14, 12);      // amount remaining        @rel 6
    b[16] = 4;            // type
    at16(b, 17, 1201);    // nameid
    b[19] = 1;            // identify
    b[21] = 7;            // refine
    u32 aid = 0;
    std::vector<net::VendItem> items;
    CHECK_EQ(net::decodeMyVendingList(b.data(), b.size(), aid, items), 30u);
    CHECK_EQ(aid, 2000123u);
    CHECK_EQ(items.size(), 1u);
    CHECK_EQ(items[0].price, 5000u);
    CHECK_EQ(items[0].index, 9);    // not amount -- offsets are swapped vs 0x133
    CHECK_EQ(items[0].amount, 12);
    CHECK_EQ(items[0].nameid, 1201);
    CHECK_EQ(items[0].refine, 7);
}

TEST_CASE(protocol_inventory_list) {
    // ZC_INVENTORY_LIST (0x1ee): 4-byte header + two 18B stackable entries = 40.
    B b(40, 0);
    at16(b, 0, 0x01ee);
    at16(b, 2, 40);
    at16(b, 4, 5);       // entry 0: index
    at16(b, 6, 501);     //          nameid (Red Potion)
    b[8] = 0;            //          type @ +4 (HEALING)
    at16(b, 10, 3);      //          amount @ +6
    at16(b, 22, 8);      // entry 1: index
    at16(b, 24, 1201);   //          nameid (Knife)
    b[26] = 3;           //          type @ +4 (ETC, for the test)
    at16(b, 28, 1);      //          amount
    std::vector<net::InvItem> inv;
    CHECK_EQ(net::decodeInventoryList(b.data(), b.size(), inv, 18), 40u);
    CHECK_EQ(inv.size(), 2u);
    CHECK_EQ(inv[0].index, 5);
    CHECK_EQ(inv[0].nameid, 501);
    CHECK_EQ(inv[0].amount, 3);
    CHECK_EQ(inv[0].type, 0);
    CHECK_EQ(inv[1].index, 8);
    CHECK_EQ(inv[1].nameid, 1201);
    CHECK_EQ(inv[1].type, 3);

    // ZC_EQUIPMENT_ITEMLIST (0xa4, 20B entries): index@0, nameid@2, amount reported as 1.
    B e(24, 0);
    at16(e, 0, 0x00a4);
    at16(e, 2, 24);
    at16(e, 4, 10);    // index
    at16(e, 6, 1101);  // nameid
    e[8] = 4;          // type @ +4 (WEAPON)
    at16(e, 10, net::EQP_HAND_R | net::EQP_HAND_L);  // equippable-slots mask (entry+6)
    at16(e, 12, net::EQP_HAND_R);  // worn position (entry+8) = weapon hand
    e[15] = 7;                     // refiningLevel @ entry+11 (isDamaged@10, refine@11, card@12)
    std::vector<net::InvItem> eq;
    CHECK_EQ(net::decodeInventoryList(e.data(), e.size(), eq, 20), 24u);
    CHECK_EQ(eq.size(), 1u);
    CHECK_EQ(eq[0].index, 10);
    CHECK_EQ(eq[0].nameid, 1101);
    CHECK_EQ(eq[0].amount, 1);
    CHECK_EQ(eq[0].type, 4);
    CHECK_EQ(eq[0].equipMask, net::EQP_HAND_R | net::EQP_HAND_L);  // @6 = slots it can go in
    CHECK_EQ(eq[0].equipPos, net::EQP_HAND_R);  // @8 = the slot it is worn in
    CHECK_EQ(eq[0].refine, 7);                  // @11 = refine level -> "+7" name prefix
}

TEST_CASE(protocol_ground_item) {
    // ZC_ITEM_ENTRY (0x9d, 17B): id@2 nameid@6 ident@8 x@9 y@11 amount@13 subX@15 subY@16
    B d(17, 0);
    at16(d, 0, 0x009d);
    at32(d, 2, 555);   // floor object id
    at16(d, 6, 909);   // nameid
    at16(d, 9, 100);   // x
    at16(d, 11, 200);  // y
    at16(d, 13, 7);    // amount (0x9d position)
    net::GroundItem gi;
    CHECK_EQ(net::decodeGroundItem(d.data(), d.size(), gi), 17u);
    CHECK_EQ(gi.id, 555u);
    CHECK_EQ(gi.nameid, 909);
    CHECK_EQ(gi.x, 100);
    CHECK_EQ(gi.y, 200);
    CHECK_EQ(gi.amount, 7);
    CHECK_EQ(net::decodeGroundItem(d.data(), 16, gi), 0u);  // short

    // ZC_ITEM_FALL (0x9e): amount@15, subX@13 subY@14 (swapped vs 0x9d)
    B e(17, 0);
    at16(e, 0, 0x009e);
    at32(e, 2, 556);
    at16(e, 6, 910);
    at16(e, 9, 101);
    at16(e, 11, 201);
    at16(e, 15, 3);  // amount (0x9e position)
    net::GroundItem gf;
    CHECK_EQ(net::decodeGroundItem(e.data(), e.size(), gf), 17u);
    CHECK_EQ(gf.id, 556u);
    CHECK_EQ(gf.amount, 3);

    // ZC_ITEM_DISAPPEAR (0xa1, 6B)
    B z(6, 0);
    at16(z, 0, 0x00a1);
    at32(z, 2, 555);
    u32 gone = 0;
    CHECK_EQ(net::decodeItemDisappear(z.data(), z.size(), gone), 6u);
    CHECK_EQ(gone, 555u);

    // ServerType8 (uaRO) item_take 0x00f5, 8B: id@4
    const B tk = net::buildTakeItem(555);
    CHECK_EQ(tk.size(), 8u);
    CHECK_EQ(tk[0] | (tk[1] << 8), 0x00f5);
    CHECK_EQ(tk[4] | (tk[5] << 8) | (tk[6] << 16) | (tk[7] << 24), 555);
}

// Live inventory updates: ZC_DELETE_ITEM (0xaf), ZC_EQUIP_ACK (0xaa) / ZC_TAKEOFF_ACK (0xac),
// ZC_ITEM_ADD (0xa0) — so the bag/equip windows refresh without a relog.
TEST_CASE(protocol_inventory_updates) {
    B d(6, 0);  // 0xaf: index(2) amount(2)
    at16(d, 0, 0x00af);
    at16(d, 2, 5);
    at16(d, 4, 2);
    u16 di = 0, da = 0;
    CHECK_EQ(net::decodeDeleteItem(d.data(), d.size(), di, da), 6u);
    CHECK_EQ(di, 5);
    CHECK_EQ(da, 2);
    CHECK_EQ(net::decodeDeleteItem(d.data(), 5, di, da), 0u);

    B a(7, 0);  // 0xaa/0xac: index(2) location(2) ok(1)
    at16(a, 0, 0x00aa);
    at16(a, 2, 5);
    at16(a, 4, net::EQP_HEAD_TOP);
    a[6] = 1;
    u16 ai = 0, al = 0;
    u8 aok = 0;
    CHECK_EQ(net::decodeEquipResult(a.data(), a.size(), ai, al, aok), 7u);
    CHECK_EQ(ai, 5);
    CHECK_EQ(al, net::EQP_HEAD_TOP);
    CHECK_EQ(aok, 1);
    CHECK_EQ(net::decodeEquipResult(a.data(), 6, ai, al, aok), 0u);

    B g(23, 0);  // 0xa0: index@2 amount@4 nameid@6 equipLoc@19 type@21 fail@22
    at16(g, 0, 0x00a0);
    at16(g, 2, 9);
    at16(g, 4, 3);
    at16(g, 6, 501);
    at16(g, 19, net::EQP_HAND_R);
    g[21] = 4;
    g[22] = 0;
    net::InvItem ni;
    u8 fail = 1;
    CHECK_EQ(net::decodeItemAdd(g.data(), g.size(), ni, fail), 23u);
    CHECK_EQ(ni.index, 9);
    CHECK_EQ(ni.amount, 3);
    CHECK_EQ(ni.nameid, 501);
    CHECK_EQ(ni.equipMask, net::EQP_HAND_R);
    CHECK_EQ(ni.type, 4);
    CHECK_EQ(ni.equipPos, 0);
    CHECK_EQ(fail, 0);
    CHECK_EQ(net::decodeItemAdd(g.data(), 22, ni, fail), 0u);
}

TEST_CASE(protocol_shop_result) {
    B b(3, 0);  // ZC_PC_PURCHASE_RESULT (0xca) / SELL_RESULT (0xcb): id(2) code(1)
    at16(b, 0, 0x00ca);
    b[2] = 1;  // e.g. not enough zeny
    u8 code = 0;
    CHECK_EQ(net::decodeShopResult(b.data(), b.size(), code), 3u);
    CHECK_EQ(code, 1);
    CHECK_EQ(net::decodeShopResult(b.data(), 2, code), 0u);  // short -> 0
}

TEST_CASE(protocol_skill_unit) {
    // ZC_SKILL_ENTRY (0x11f, 16B): a warp portal on the ground.
    B b(16, 0);
    at16(b, 0, 0x011f);
    at32(b, 2, 2000150);  // unit gid
    at32(b, 6, 2000148);  // caster
    at16(b, 10, 100);     // x
    at16(b, 12, 120);     // y
    b[14] = net::UNT_WARP_ACTIVE;
    net::SkillEntry e;
    CHECK_EQ(net::decodeSkillEntry(b.data(), b.size(), e), 16u);
    CHECK_EQ(e.gid, 2000150u);
    CHECK_EQ(e.srcId, 2000148u);
    CHECK_EQ(e.x, 100);
    CHECK_EQ(e.y, 120);
    CHECK_EQ(e.unitId, net::UNT_WARP_ACTIVE);
    CHECK_EQ(net::decodeSkillEntry(b.data(), 15, e), 0u);  // short -> 0

    B d(6, 0);  // ZC_SKILL_DISAPPEAR (0x120, 6B)
    at16(d, 0, 0x0120);
    at32(d, 2, 2000150);
    u32 gid = 0;
    CHECK_EQ(net::decodeSkillDisappear(d.data(), d.size(), gid), 6u);
    CHECK_EQ(gid, 2000150u);
}

// ZC_STATUS (0x00bd): bulk character status, 44 bytes (clif_initialstatus layout).
TEST_CASE(protocol_status) {
    B b(44, 0);
    at16(b, 0, 0x00bd);
    at16(b, 2, 48);         // status point
    b[4] = 10;  b[5] = 2;   // str / needStr
    b[6] = 11;  b[7] = 3;   // agi
    b[8] = 12;  b[9] = 4;   // vit
    b[10] = 13; b[11] = 5;  // int
    b[12] = 14; b[13] = 6;  // dex
    b[14] = 15; b[15] = 7;  // luk
    at16(b, 16, 120);       // atk
    at16(b, 18, 8);         // atk2
    at16(b, 20, 30);        // matkMax
    at16(b, 22, 25);        // matkMin
    at16(b, 24, 40);        // def
    at16(b, 26, 5);         // def2
    at16(b, 28, 6);         // mdef
    at16(b, 30, 2);         // mdef2
    at16(b, 32, 88);        // hit
    at16(b, 34, 77);        // flee
    at16(b, 36, 3);         // flee2 (perfect dodge)
    at16(b, 38, 12);        // crit
    net::CharStatus s;
    CHECK_EQ(net::decodeStatus(b.data(), b.size(), s), 44u);
    CHECK_EQ(s.statusPoint, 48);
    CHECK_EQ(s.str, 10);  CHECK_EQ(s.needStr, 2);
    CHECK_EQ(s.luk, 15);  CHECK_EQ(s.needLuk, 7);
    CHECK_EQ(s.atk, 120); CHECK_EQ(s.matkMax, 30);
    CHECK_EQ(s.def, 40);  CHECK_EQ(s.hit, 88);
    CHECK_EQ(s.flee, 77); CHECK_EQ(s.crit, 12);
    B sh(43, 0);
    CHECK_EQ(net::decodeStatus(sh.data(), sh.size(), s), 0u);  // short -> 0
}

// ZC_SKILLINFO_LIST (0x10f): the learned-skill list, 37-byte entries.
TEST_CASE(protocol_skill_list) {
    const usize total = 4 + 37 * 2;
    B b(total, 0);
    at16(b, 0, 0x010f);
    at16(b, 2, total);
    at16(b, 4 + 0, 28);          // id (NV_BASIC)
    at16(b, 4 + 6, 9);           // level
    at16(b, 4 + 8, 0);           // sp
    atStr(b, 4 + 12, "NV_BASIC", 24);
    b[4 + 36] = 0;               // not upgradable
    at16(b, 41 + 0, 1);          // id (SM_SWORD)
    at16(b, 41 + 6, 10);         // level
    at16(b, 41 + 8, 5);          // sp
    atStr(b, 41 + 12, "SM_SWORD", 24);
    b[41 + 36] = 1;              // upgradable
    std::vector<net::SkillInfo> sk;
    CHECK_EQ(net::decodeSkillList(b.data(), b.size(), sk), total);
    CHECK_EQ(sk.size(), 2u);
    CHECK_EQ(sk[0].id, 28);
    CHECK_EQ(sk[0].level, 9);
    CHECK(sk[0].name == "NV_BASIC");
    CHECK(!sk[0].up);
    CHECK_EQ(sk[1].id, 1);
    CHECK_EQ(sk[1].level, 10);
    CHECK_EQ(sk[1].sp, 5);
    CHECK(sk[1].up);
}

TEST_CASE(protocol_stat_change) {
    // CZ_STATUS_CHANGE (0xbb): id(2) statusID(2) amount(1) = 5 bytes.
    const B s = net::buildStatChange(net::SP_VIT);  // default amount = 1
    CHECK_EQ(s.size(), 5u);
    CHECK_EQ(net::peekId(s), 0x00bb);
    CHECK_EQ(static_cast<u16>(s[2] | (s[3] << 8)), static_cast<u16>(net::SP_VIT));  // statusID @2
    CHECK_EQ(s[4], 1);  // amount @4
    const B s10 = net::buildStatChange(net::SP_STR, 10);
    CHECK_EQ(s10.size(), 5u);
    CHECK_EQ(static_cast<u16>(s10[2] | (s10[3] << 8)), static_cast<u16>(net::SP_STR));
    CHECK_EQ(s10[4], 10);
}

TEST_CASE(protocol_make_char) {
    // CH_MAKE_CHAR (0x67) request: id(2) name(24) str/agi/vit/int/dex/luk(6) slot(1)
    // hairColor(2) hairStyle(2) = 37 bytes.
    const B m = net::buildMakeChar("Hero", 5, 5, 5, 5, 5, 5, 2, /*hairStyle=*/7, /*hairColor=*/3);
    CHECK_EQ(m.size(), 37u);
    CHECK_EQ(net::peekId(m), 0x0067);
    CHECK(std::string(reinterpret_cast<const char*>(&m[2])) == "Hero");  // name @2
    CHECK_EQ(m[26], 5);   // str @26 (name end)
    CHECK_EQ(m[31], 5);   // luk @31
    CHECK_EQ(m[32], 2);   // slot @32
    CHECK_EQ(static_cast<u16>(m[33] | (m[34] << 8)), 3);  // hairColor @33
    CHECK_EQ(static_cast<u16>(m[35] | (m[36] << 8)), 7);  // hairStyle @35

    // HC_REFUSE_MAKECHAR (0x6e): id(2) + reason(1).
    B ref(3, 0);
    at16(ref, 0, 0x006e);
    ref[2] = 0x00;  // name already exists
    u8 code = 0xff;
    CHECK_EQ(net::decodeMakeCharRefuse(ref.data(), ref.size(), code), 3u);
    CHECK_EQ(code, 0x00);
    CHECK_EQ(net::decodeMakeCharRefuse(ref.data(), 2, code), 0u);  // short -> 0

    // HC_ACCEPT_MAKECHAR (0x6d): id(2) + one 108B record (0x6d is 110 in the packet table).
    B acc(110, 0);
    at16(acc, 0, 0x006d);
    at32(acc, 2 + 0, 150007);          // char_id @ record+0
    at16(acc, 2 + 52, 1);              // class @ record+52 (Swordman)
    atStr(acc, 2 + 74, "Newbie", 24);  // name @ record+74
    at16(acc, 2 + 104, 4);             // slot @ record+104
    net::CharInfo c;
    CHECK_EQ(net::decodeMakeCharAccept(acc.data(), acc.size(), c), 110u);
    CHECK_EQ(c.charId, 150007u);
    CHECK_EQ(c.class_, 1);
    CHECK(c.name == "Newbie");
    CHECK_EQ(c.slot, 4);
    CHECK_EQ(net::decodeMakeCharAccept(acc.data(), 100, c), 0u);  // too short -> 0

    // The create form distributes the six stats individually, so a non-uniform but
    // server-valid spread (sum 30, each 1..9, paired sums str+int/agi+luk/vit+dex <= 10)
    // must reach the wire byte-for-byte at the offsets make_new_char_sql reads (dat[24..29]
    // = packet bytes 26..31). 9/1/9/1/9/1: sum 30, pairs 9+1, 1+1, 9+9... -> use 6/4/6/4/6/4.
    const B s = net::buildMakeChar("Spread", 6, 4, 6, 4, 6, 4, 7, /*hairStyle=*/3, /*hairColor=*/2);
    CHECK_EQ(s.size(), 37u);
    CHECK_EQ(s[26], 6);  // str
    CHECK_EQ(s[27], 4);  // agi
    CHECK_EQ(s[28], 6);  // vit
    CHECK_EQ(s[29], 4);  // int
    CHECK_EQ(s[30], 6);  // dex
    CHECK_EQ(s[31], 4);  // luk
    CHECK_EQ(s[32], 7);  // slot
    CHECK_EQ(net::kMaxChars, 9);  // matches the char-server's MAX_CHARS (3 pages of 3)
}

TEST_CASE(protocol_storage) {
    // ZC_STORE_COUNTINFO (0xf2, 6B): id(2) cur(2) max(2).
    B cnt(6, 0);
    at16(cnt, 0, 0x00f2);
    at16(cnt, 2, 45);
    at16(cnt, 4, 300);
    u16 cur = 0, mx = 0;
    CHECK_EQ(net::decodeStorageCount(cnt.data(), cnt.size(), cur, mx), 6u);
    CHECK_EQ(cur, 45);
    CHECK_EQ(mx, 300);
    CHECK_EQ(net::decodeStorageCount(cnt.data(), 5, cur, mx), 0u);  // short -> 0

    // ZC_ADD_ITEM_TO_STORE (0xf4, 21B): id(2) index(2) amount(4) nameid(2)@8 ident@10 attr@11 refine@12.
    B add(21, 0);
    at16(add, 0, 0x00f4);
    at16(add, 2, 7);      // storage index (slot+1)
    at32(add, 4, 120);    // amount is a 4-byte field here
    at16(add, 8, 501);    // nameid (Red Potion)
    add[12] = 5;          // refine
    add[10] = 0;          // unidentified
    net::InvItem it;
    CHECK_EQ(net::decodeStorageAdd(add.data(), add.size(), it), 21u);
    CHECK_EQ(it.index, 7);
    CHECK_EQ(it.amount, 120);
    CHECK_EQ(it.nameid, 501);
    CHECK_EQ(it.refine, 5);
    CHECK_EQ(it.identified, 0);  // identify flag @10 decoded
    CHECK_EQ(net::decodeStorageAdd(add.data(), 20, it), 0u);  // short -> 0

    // ZC_DELETE_ITEM_FROM_STORE (0xf6, 8B): id(2) index(2) amount(4).
    B del(8, 0);
    at16(del, 0, 0x00f6);
    at16(del, 2, 7);
    at32(del, 4, 30);
    u16 di = 0;
    u32 da = 0;
    CHECK_EQ(net::decodeStorageRemove(del.data(), del.size(), di, da), 8u);
    CHECK_EQ(di, 7);
    CHECK_EQ(da, 30u);

    // Builders: CZ_MOVE_ITEM_FROM_BODY_TO_STORE (0xf3) / _STORE_TO_BODY (0xf5) — id(2) index(2) amount(4).
    const B store = net::buildStorageStore(9, 50);  // ServerType8 (uaRO) 0x0094 storage_add, 14B
    CHECK_EQ(store.size(), 14u);
    CHECK_EQ(net::peekId(store), 0x0094);
    CHECK_EQ(static_cast<u16>(store[7] | (store[8] << 8)), 9);  // index @7
    CHECK_EQ(store[10], 50);  // amount low byte @10 (50 < 256)
    const B take = net::buildStorageRetrieve(7, 50);  // ServerType8 0x00f7 storage_get, 22B
    CHECK_EQ(take.size(), 22u);
    CHECK_EQ(net::peekId(take), 0x00f7);
    const B close = net::buildStorageClose();  // ServerType8 0x0193 storage close, 2B
    CHECK_EQ(close.size(), 2u);
    CHECK_EQ(net::peekId(close), 0x0193);

    // The storage list packets (0x1f0 18B / 0xa6 20B) reuse decodeInventoryList. One stackable
    // entry: id(2) len(2) { index@4 nameid@6 type@8 ident@9 amount@10 ... } -> len = 4 + 18 = 22.
    B lst(22, 0);
    at16(lst, 0, 0x01f0);
    at16(lst, 2, 22);
    at16(lst, 4, 3);      // index (slot+1)
    at16(lst, 6, 909);    // nameid (Jellopy)
    lst[8] = 3;           // type = IT_ETC
    lst[9] = 1;           // identified @entry+5
    at16(lst, 10, 14);    // amount
    std::vector<net::InvItem> sl;
    CHECK_EQ(net::decodeInventoryList(lst.data(), lst.size(), sl, 18), 22u);
    CHECK_EQ(sl.size(), 1u);
    CHECK_EQ(sl[0].index, 3);
    CHECK_EQ(sl[0].nameid, 909);
    CHECK_EQ(sl[0].amount, 14);
    CHECK_EQ(sl[0].identified, 1);
}

TEST_CASE(protocol_homun) {
    // ZC_PROPERTY_HOMUN (0x22e, 71B): name@2(24) flags@26 lv@27 hunger@29 intimacy@31
    // equip@33 atk@35 matk@37 hit@39 cri@41 def@43 mdef@45 flee@47 aspd@49 hp@51 maxhp@53
    // sp@55 maxsp@57 exp@59(L) expnext@63(L) skillpts@67 attackable@69.
    B h(71, 0);
    at16(h, 0, 0x022e);
    atStr(h, 2, "Filir", 24);
    h[26] = 0x01;       // renamed, not vaporized, not dead
    at16(h, 27, 42);    // level
    at16(h, 29, 88);    // hunger
    at16(h, 31, 750);   // intimacy (already /100 on the wire)
    at16(h, 35, 310);   // atk
    at16(h, 43, 60);    // def
    at16(h, 51, 1450);  // hp
    at16(h, 53, 1600);  // maxhp
    at16(h, 55, 210);   // sp
    at16(h, 57, 240);   // maxsp
    at32(h, 59, 123456);  // exp
    at16(h, 67, 3);     // skill points
    net::HomunInfo hi;
    CHECK_EQ(net::decodeHomunInfo(h.data(), h.size(), hi), 71u);
    CHECK_EQ(hi.name, std::string("Filir"));
    CHECK(hi.renamed);
    CHECK(!hi.vaporized);
    CHECK_EQ(hi.level, 42);
    CHECK_EQ(hi.hunger, 88);
    CHECK_EQ(hi.intimacy, 750);
    CHECK_EQ(hi.atk, 310);
    CHECK_EQ(hi.def, 60);
    CHECK_EQ(hi.hp, 1450);
    CHECK_EQ(hi.maxHp, 1600);
    CHECK_EQ(hi.sp, 210);
    CHECK_EQ(hi.maxSp, 240);
    CHECK_EQ(hi.exp, 123456u);
    CHECK_EQ(hi.skillPts, 3);
    CHECK_EQ(net::decodeHomunInfo(h.data(), 70, hi), 0u);  // short buffer rejected
}

TEST_CASE(protocol_homun_skills) {
    // ZC_HOSKILLINFO_LIST (0x235, var): len@2, then 37B entries { id@0 inf@2 unused@4
    // lv@6 sp@8 range@10 name@12(24) upgradeable@36 }.
    const u16 len = 4 + 37 * 2;
    B s(len, 0);
    at16(s, 0, 0x0235);
    at16(s, 2, len);
    // entry 0 @4
    at16(s, 4, 8001);   // id
    at16(s, 4 + 2, 16); // inf
    at16(s, 4 + 6, 3);  // level
    at16(s, 4 + 8, 12); // sp
    at16(s, 4 + 10, 9); // range
    atStr(s, 4 + 12, "MH_SUMMON_LEGION", 24);
    s[4 + 36] = 1;      // upgradeable
    // entry 1 @41
    at16(s, 41, 8002);
    at16(s, 41 + 6, 1);
    atStr(s, 41 + 12, "MH_NEEDLE_OF_PARALYZE", 24);
    std::vector<net::HomSkill> sk;
    CHECK_EQ(net::decodeHomSkills(s.data(), s.size(), sk), static_cast<usize>(len));
    CHECK_EQ(sk.size(), 2u);
    CHECK_EQ(sk[0].id, 8001);
    CHECK_EQ(sk[0].level, 3);
    CHECK_EQ(sk[0].sp, 12);
    CHECK_EQ(sk[0].name, std::string("MH_SUMMON_LEGION"));
    CHECK(sk[0].upgradeable);
    CHECK_EQ(sk[1].id, 8002);
    CHECK(!sk[1].upgradeable);

    // Control command builders.
    const B feed = net::buildHomMenu(1);
    CHECK_EQ(feed.size(), 5u);
    CHECK_EQ(net::peekId(feed), 0x022d);
    CHECK_EQ(feed[4], 1);
    const B atk = net::buildHomAttack(0x11223344);
    CHECK_EQ(atk.size(), 11u);
    CHECK_EQ(net::peekId(atk), 0x0233);
    CHECK_EQ(static_cast<u32>(atk[6] | (atk[7] << 8) | (atk[8] << 16) | (atk[9] << 24)), 0x11223344u);
    const B rec = net::buildHomMoveToMaster();
    CHECK_EQ(rec.size(), 6u);
    CHECK_EQ(net::peekId(rec), 0x0234);
}

TEST_CASE(protocol_cart) {
    // Merchant cart moves: CZ 0x126/0x127/0x128/0x129, all 8B: id(2) index(2) amount(4).
    const B add = net::buildCartAdd(9, 50);  // inventory wire index (slot+2) -> cart
    CHECK_EQ(add.size(), 8u);
    CHECK_EQ(net::peekId(add), 0x0126);
    CHECK_EQ(static_cast<u16>(add[2] | (add[3] << 8)), 9);
    CHECK_EQ(add[4], 50);  // amount low byte (50 < 256)
    CHECK_EQ(net::peekId(net::buildCartGet(7, 1)), 0x0127);
    CHECK_EQ(net::peekId(net::buildCartToStore(7, 1)), 0x0128);
    const B s2c = net::buildStoreToCart(3, 1);
    CHECK_EQ(s2c.size(), 8u);
    CHECK_EQ(net::peekId(s2c), 0x0129);
    // The cart list/add/remove packets reuse the storage/inventory decoders (covered by
    // protocol_storage): 0x1ef/0x122 -> decodeInventoryList, 0x124 -> decodeStorageAdd,
    // 0x125 -> decodeStorageRemove.
}

TEST_CASE(protocol_rental_time) {  // 0x298: nameid.W seconds.L (8B)
    B b(8, 0);
    at16(b, 0, 0x0298);
    at16(b, 2, 501);
    at32(b, 4, 3600);
    u16 nid = 0;
    u32 secs = 0;
    CHECK_EQ(net::decodeRentalTime(b.data(), b.size(), nid, secs), 8u);
    CHECK_EQ(nid, 501);
    CHECK_EQ(secs, 3600u);
}

TEST_CASE(protocol_rental_expired) {  // 0x299: index.W nameid.W (6B); index == InvItem.index (slot+2)
    B b(6, 0);
    at16(b, 0, 0x0299);
    at16(b, 2, 7);
    at16(b, 4, 909);
    u16 idx = 0, nid = 0;
    CHECK_EQ(net::decodeRentalExpired(b.data(), b.size(), idx, nid), 6u);
    CHECK_EQ(idx, 7);
    CHECK_EQ(nid, 909);
}
