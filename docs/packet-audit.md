# Аудит пакетов сервер→клиент (uAthena map clif.c vs наш клиент)

Метод: все `ZC` (WFIFOW/WBUFW offset 0, id<0x2000) из `src/map/clif.c` (что сервер МОЖЕТ слать) минус то, что клиент диспатчит (`case net::PKT_ZC_*` + raw hex). PacketTable кадрирует ВСЁ, так что необработанное безопасно пропускается (не краш).

**Клиент диспатчит: 152 id. Сервер шлёт клиенту: 254. Не обрабатывается: 104.**

Сверка имён — по функции-отправителю в clif.c (= роБраузер/uOK-именам эквивалентно).

| id | отправитель (clif_) | что делать |
|---|---|---|
| 0x6a | parse_WantToConnection | skip: login/char-server or request-parse (handled pre-game / not a game ZC) |
| 0x73 | authok | skip: login/char-server or request-parse (handled pre-game / not a game ZC) |
| 0x7b | set007b | review |
| 0x7f | parse_TickSend | skip: login/char-server or request-parse (handled pre-game / not a game ZC) |
| 0x81 | authfail_fd | skip: login/char-server or request-parse (handled pre-game / not a game ZC) |
| 0x9a | GMmessage | skip: GM command ack |
| 0xa3 | inventorylist | review |
| 0xa5 | storagelist | optional: storage list variant |
| 0xb3 | charselectok | skip: login/char-server or request-parse (handled pre-game / not a game ZC) |
| 0xc2 | parse_HowManyConnections | skip: login/char-server or request-parse (handled pre-game / not a game ZC) |
| 0xc3 | changelook | review |
| 0xcd | GM_kickack | skip: GM command ack |
| 0xd1 | wisexin | optional: whisper ignore-list ack |
| 0xd2 | wisall | optional: whisper ignore-list ack |
| 0xd4 | parse_PMIgnoreList | skip: login/char-server or request-parse (handled pre-game / not a game ZC) |
| 0xdf | changechatstatus | optional: chatroom owner/status change |
| 0xe1 | changechatowner | optional: chatroom owner/status change |
| 0xfd | party_inviteack | optional: party option/invite-ack/message (party works) |
| 0x101 | party_option | optional: party option/invite-ack/message (party works) |
| 0x109 | party_message | optional: party option/invite-ack/message (party works) |
| 0x10a | mvp_item | IMPLEMENT: MVP item drop |
| 0x10b | mvp_exp | IMPLEMENT: MVP exp |
| 0x10c | mvp_effect | IMPLEMENT: MVP effect |
| 0x114 | skill_damage | IMPLEMENT: skill_damage (older variant) |
| 0x115 | skill_damage2 | IMPLEMENT: skill_damage2 variant |
| 0x119 | changeoption | review |
| 0x11c | skill_warppoint | IMPLEMENT: skill warp-point picker (Warp Portal) |
| 0x11e | skill_memo | review |
| 0x121 | updatestatus | review |
| 0x123 | cartlist | optional: cart list refresh variant |
| 0x13b | arrow_fail | review |
| 0x144 | viewpoint | IMPLEMENT: viewpoint / minimap markers (quest) |
| 0x14b | GM_silence | skip: GM command ack |
| 0x14e | guild_masterormember | optional: guild detail (guild UI is minimal) |
| 0x15a | guild_leave | optional: guild detail (guild UI is minimal) |
| 0x15c | guild_expulsion | optional: guild detail (guild UI is minimal) |
| 0x15e | guild_broken | optional: guild detail (guild UI is minimal) |
| 0x167 | guild_created | optional: guild detail (guild UI is minimal) |
| 0x169 | guild_inviteack | optional: guild detail (guild UI is minimal) |
| 0x171 | guild_reqalliance | optional: guild detail (guild UI is minimal) |
| 0x173 | guild_allianceack | optional: guild detail (guild UI is minimal) |
| 0x17b | use_card | optional: card insert flow |
| 0x17d | insert_card | optional: card insert flow |
| 0x17f | guild_message | optional: guild detail (guild UI is minimal) |
| 0x181 | guild_oppositionack | optional: guild detail (guild UI is minimal) |
| 0x184 | guild_delalliance | optional: guild detail (guild UI is minimal) |
| 0x185 | guild_oppositionack | optional: guild detail (guild UI is minimal) |
| 0x188 | refine | IMPLEMENT: refine result anim |
| 0x189 | skill_teleportmessage | review |
| 0x18b | GM_kick | skip: GM command ack |
| 0x18c | skill_estimation | review |
| 0x18d | skill_produce_mix_list | IMPLEMENT: produce/brew mix list UI |
| 0x18f | produceeffect | IMPLEMENT: produce effect |
| 0x191 | talkiebox | review |
| 0x192 | set0192 | review |
| 0x194 | solved_charname | review |
| 0x19a | pvpset | skip: fame/rank/PVP-ladder (unused) |
| 0x1ab | changestatus | review |
| 0x1ac | 01ac | review |
| 0x1ad | arrow_create_list | IMPLEMENT: arrow-craft list UI |
| 0x1b3 | cutin | IMPLEMENT: NPC cutin image (dialogue portrait) |
| 0x1c3 | announce | IMPLEMENT: GM broadcast (ss/blue announce) in chat |
| 0x1c9 | getareachar_skillunit | review |
| 0x1cd | autospell | review |
| 0x1cf | devotion | IMPLEMENT: devotion link line (Crusader) — draw on other players |
| 0x1d0 | spiritball | IMPLEMENT: spirit spheres (Monk) count on a unit |
| 0x1d1 | bladestop | IMPLEMENT: Bladestop link (Monk) visual |
| 0x1d2 | combo_delay | IMPLEMENT: combo delay flash |
| 0x1d3 | soundeffect | review |
| 0x1d8 | set0078 | review |
| 0x1d9 | spawn | review |
| 0x1da | set007b | review |
| 0x1e0 | parse_GMReqNoChatCount | skip: login/char-server or request-parse (handled pre-game / not a game ZC) |
| 0x1e1 | spiritball_single | IMPLEMENT: single spirit-sphere update |
| 0x1e2 | parse_ReqMarriage | skip: login/char-server or request-parse (handled pre-game / not a game ZC) |
| 0x1e4 | marriage_process | skip: marriage/adopt feature (unused) |
| 0x1e6 | callpartner | skip: marriage/adopt feature (unused) |
| 0x1e9 | party_member_info | optional: party option/invite-ack/message (party works) |
| 0x1ea | wedding_effect | skip: marriage/adopt feature (unused) |
| 0x1eb | guild_xy | optional: guild detail (guild UI is minimal) |
| 0x1f6 | parse_ReqAdopt | skip: login/char-server or request-parse (handled pre-game / not a game ZC) |
| 0x1f8 | adopt_process | skip: marriage/adopt feature (unused) |
| 0x1fc | item_repair_list | IMPLEMENT: weapon repair list UI |
| 0x1fe | item_repaireffect | IMPLEMENT: repair effect |
| 0x1ff | slide | review |
| 0x205 | divorced | skip: marriage/adopt feature (unused) |
| 0x20e | feel_info | skip: fame/rank/PVP-ladder (unused) |
| 0x210 | parse_PVPInfo | skip: login/char-server or request-parse (handled pre-game / not a game ZC) |
| 0x215 | gospel_info | skip: fame/rank/PVP-ladder (unused) |
| 0x219 | parse_Blacksmith | skip: login/char-server or request-parse (handled pre-game / not a game ZC) |
| 0x21a | parse_Alchemist | skip: login/char-server or request-parse (handled pre-game / not a game ZC) |
| 0x21b | fame_blacksmith | skip: fame/rank/PVP-ladder (unused) |
| 0x21c | fame_alchemist | skip: fame/rank/PVP-ladder (unused) |
| 0x221 | item_refine_list | IMPLEMENT: refine list UI |
| 0x223 | upgrademessage | review |
| 0x224 | fame_taekwon | skip: fame/rank/PVP-ladder (unused) |
| 0x226 | parse_Taekwon | skip: login/char-server or request-parse (handled pre-game / not a game ZC) |
| 0x22f | hom_food | optional: homunculus detail |
| 0x230 | send_homdata | optional: homunculus detail |
| 0x238 | parse_RankingPk | skip: login/char-server or request-parse (handled pre-game / not a game ZC) |
| 0x239 | homskillup | optional: homunculus detail |
| 0x253 | parse_ReqFeel | skip: login/char-server or request-parse (handled pre-game / not a game ZC) |
| 0x28a | changeoption2 | review |
| 0x2b9 | hotkeys_send | review |
