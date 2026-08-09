# Data-driven skill→effect table (`skilleffectinfolist.lub`)

The classic 1st/2nd-job skill visuals are **hardcoded in `uaRO.exe`** (see the per-class dossiers). The remaining **modern skills** (Ninja/Kagerou/Oboro/Rebellion/Eclage) are **data-driven** from `gro.zip` → `data/lua files/skilleffectinfo/skilleffectinfolist.lub` (Lua 5.0 bytecode), decoded by executing it in a patched Lua 5.0 VM with symbolic-name proxies. `id`/`cast`/`dur` come from the server `skill_db`/`skill_cast_db` (level 1); EF ids annotated `NAME(num)` from `effectid.lub`; motion from `actorstate.lub`.

Schema per entry: `beginEffectID`/`beginMotionType` (cast), `effectID[]`+`effectNum` (on caster), `groundEffectID[]`+`groundEffectNum` (ground AoE), `onTarget`, `targetEffectID[]`+`targetEffectNum` (on target), `waveFileName`/`targetWaveFileName` (cp949). Raw: [`../data/skilleffectinfolist.tsv`](../data/skilleffectinfolist.tsv), [`../data/effectid-map.tsv`](../data/effectid-map.tsv), [`../data/actorstate-map.tsv`](../data/actorstate-map.tsv).

| id | SKID | name | cast | dur | cast/begin | caster effect | ground effect | on-tgt | target effect | cast wav | target wav |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 524 | NJ_KUNAI | Throw Kunai | 0 | 0 |  | EF_THROWITEM8(614) |  | true |  | effect\닌자_던지기.wav |  |
| ? | ECLAGE_RECALL | ECLAGE_RECALL | - | - | EF_BEGINSPELL(12) |  |  |  |  |  |  |
| ? | ECL_PEONYMAMY | ECL_PEONYMAMY | - | - |  |  |  | true | EF_FLOWERLEAF(699) |  |  |
| ? | ECL_SADAGUI | ECL_SADAGUI | - | - |  |  |  | true | EF_WINDHIT(52) |  |  |
| ? | ECL_SEQUOIADUST | ECL_SEQUOIADUST | - | - |  |  |  | true | EF_EXIT2(314) |  |  |
| ? | ECL_SNOWFLIP | ECL_SNOWFLIP | - | - |  |  |  | true | EF_ICECRASH(135) |  |  |
| ? | KG_KAGEHUMI | KG_KAGEHUMI | - | - | ST_NINJASKILL2(36) | EF_KG_KAGEHUMI(991) |  |  |  |  |  |
| ? | KG_KAGEMUSYA | KG_KAGEMUSYA | - | - |  |  |  | true | EF_ENERGYCOAT(169) |  | effect\mon_금강불괴.wav |
| ? | KG_KYOMU | KG_KYOMU | - | - |  |  |  | true | EF_KG_KYOMU(1001) |  | effect\t_에너지방출.wav |
| ? | KO_BAKURETSU | KO_BAKURETSU | - | - | ST_NINJASKILL2(36) | EF_THROW_BAKURETSU(983) |  | true | EF_GROUND_EXPLOSION(990) | effect\닌자_던지기.wav | effect\폭염룡.wav |
| ? | KO_DOHU_KOUKAI | KO_DOHU_KOUKAI | - | - | EF_BEGINSPELL(12); ST_NINJAREADY(37) |  |  |  |  |  |  |
| ? | KO_GENWAKU | KO_GENWAKU | - | - | EF_BEGINSPELL3(55); ST_NINJAREADY(37) |  |  |  |  |  |  |
| ? | KO_HAPPOKUNAI | KO_HAPPOKUNAI | - | - | ST_NINJASKILL2(36) | EF_THROW_HAPPOKUNAI(981), EF_ROTATE_LINE_GRAY(986) |  |  |  | effect\T_회오리차기.wav |  |
| ? | KO_HUUMARANKA | KO_HUUMARANKA | - | - | ST_NINJASKILL2(36) |  | EF_ROTATE_HUUMARANKA(984), EF_ROTATE_LINE_BLUE(1000), EF_KO_HUUMARANKA(1002) |  |  | effect\T_회오리차기.wav |  |
| ? | KO_HYOUHU_HUBUKI | KO_HYOUHU_HUBUKI | - | - | EF_BLUECASTING(441); ST_NINJAREADY(37) |  |  |  |  |  |  |
| ? | KO_IZAYOI | KO_IZAYOI | - | - | ST_NINJASKILL2(36) | EF_KO_IZAYOI(999) |  |  |  | effect\ab_renovatio.wav |  |
| ? | KO_JYUMONJIKIRI | KO_JYUMONJIKIRI | - | - | ST_ATTACK2(9) |  |  | true | EF_KO_JYUMONJIKIRI(996) | effect\cru_holy cross.wav |  |
| ? | KO_JYUSATSU | KO_JYUSATSU | - | - | EF_BEGINSPELL2(54); ST_NINJAREADY(37) |  |  |  |  |  |  |
| ? | KO_KAHU_ENTEN | KO_KAHU_ENTEN | - | - | EF_BEGINSPELL3(55); ST_NINJAREADY(37) |  |  |  |  |  |  |
| ? | KO_KAIHOU | KO_KAIHOU | - | - |  | EF_KAIHOU(989) |  | true | EF_KAIHOU1(1008) | effect\닌자_던지기.wav |  |
| ? | KO_KAZEHU_SEIRAN | KO_KAZEHU_SEIRAN | - | - | EF_BEGINSPELL4(56); ST_NINJAREADY(37) |  |  |  |  |  |  |
| ? | KO_KYOUGAKU | KO_KYOUGAKU | - | - | EF_BEGINSPELL7(59); ST_NINJAREADY(37) |  |  |  |  |  |  |
| ? | KO_MEIKYOUSISUI | KO_MEIKYOUSISUI | - | - | EF_BEGINSPELL5(57); ST_NINJAREADY(37) |  |  |  |  |  |  |
| ? | KO_MUCHANAGE | KO_MUCHANAGE | - | - |  | EF_THROW_MULTIPLE_COIN(982) |  | true | EF_HITLINE(330) | effect\닌자_던지기.wav |  |
| ? | KO_SETSUDAN | KO_SETSUDAN | - | - | ST_NINJASKILL2(36) |  |  | true | EF_KO_SETSUDAN(997) |  | effect\T_전기.wav |
| ? | KO_ZENKAI | KO_ZENKAI | - | - | ST_NINJASKILL2(36) |  |  |  |  |  |  |
| ? | OB_AKAITSUKI | OB_AKAITSUKI | - | - | EF_BEGINSPELL3(55); ST_NINJAREADY(37) |  |  | true | EF_AKAITSUKI(1009) |  | effect\t_에너지방출.wav |
| ? | OB_OBOROGENSOU | OB_OBOROGENSOU | - | - | ST_NINJAREADY(37) |  |  | true | EF_GENSOU(1011) |  | effect\sign_up.wav |
| ? | OB_ZANGETSU | OB_ZANGETSU | - | - | EF_BEGINSPELL(12); ST_NINJAREADY(37) |  |  | true | EF_ZANGETSU(1010) |  | effect\t_따듯한마법.wav |
