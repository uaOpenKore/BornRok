# Спецэффекты: карта EFFECTID → .str (из оригинального uaRO.exe)

> Источник: дизассемблирование `uaRO.exe` (PE32 x86, не запакован). Диспетчер эффектов
> `CEffect::Initialize` по адресу **0x005c2630**; четырёхуровневый switch по EFFECTID
> (`[esi+0x100]`). Индекс-таблицы байт: 0x5c32ac / 0x5c3414 / 0x5c34dc; jump-таблицы:
> 0x5c315c / 0x5c3360 / 0x5c3490; default (без .str — кодовый/частичный эффект) → 0x5c30f6.
> Извлечено capstone-обходом всех уровней — **166 эффектов, id 10..677**.

## Зачем это нужно

Сервер uaRO рассылает эти эффекты пакетом **`ZC_NOTIFY_EFFECT2` (0x01f3)** через
`clif_specialeffect(bl, EFFECTID, AREA)` — **31 вызов** в `src/map/{clif,skill,status}.c`.
Поле пакета (10 байт: `id(2) aid(4) effectId(4)`) несёт **ровно этот EFFECTID**.
Сверка подтверждает таблицу: `skill.c:11143 → 608` и `:11184 → 609`, а здесь
608=`cook_suc.str`, 609=`cook_fail.str`. Клиент сейчас 0x1f3 **не рендерит** (хэндлер есть
только на 0x019b — left-up). Подключение 0x1f3 к этой таблице даёт серверные эффекты
«бесплатно» и с правильными id (готовка, рефайн, приручение, статусные ауры, .str-эффекты).

`%d`-имена — это покадровые мультифайловые анимации (Meteor1.str, Meteor2.str…),
их грузит отдельный путь рендера. Корейские имена (euc-kr) — потионы/ауры
Concentration/Awakening/Berserk; для них нужен путь в исходной кодировке euc-kr.
Эффекты, попадающие в default (0x5c30f6) — **кодовые/частичные** (например серверные
421/423/438/159), у них нет .str и через эту таблицу они не рендерятся.

## Таблица

| EFFECTID | hex | .str-файл | примечание |
|---:|---|---|---|
| 10 | 0x00a | `memor_min.str` |  |
| 13 | 0x00d | `SafetyWall.str` |  |
| 23 | 0x017 | `StoneCurse.str` |  |
| 25 | 0x019 | `Firewall%d.str` | анимация (мультифайл, %d = кадр) |
| 29 | 0x01d | `Lightning.str` |  |
| 30 | 0x01e | `ThunderStorm.str` |  |
| 40 | 0x028 | `Cross.str` |  |
| 41 | 0x029 | `jong_mini.str` |  |
| 49 | 0x031 | `FireHit%d.str` | анимация (мультифайл, %d = кадр) |
| 52 | 0x034 | `WindHit%d.str` | анимация (мультифайл, %d = кадр) |
| 64 | 0x040 | `ArrowShot.str` |  |
| 65 | 0x041 | `Invenom.str` |  |
| 66 | 0x042 | `cure_min.str` |  |
| 67 | 0x043 | `Provoke.str` |  |
| 68 | 0x044 | `Mvp.str` |  |
| 69 | 0x045 | `SkidTrap.str` |  |
| 70 | 0x046 | `Brandish.str` |  |
| 75 | 0x04b | `gloria_min.str` |  |
| 76 | 0x04c | `magnificat_min.str` |  |
| 77 | 0x04d | `resurrection_min.str` |  |
| 78 | 0x04e | `Recovery.str` |  |
| 83 | 0x053 | `Sanctuary.str` |  |
| 84 | 0x054 | `Impositio.str` |  |
| 85 | 0x055 | `lexaeterna_min.str` |  |
| 86 | 0x056 | `Aspersio.str` |  |
| 87 | 0x057 | `LexDivina.str` |  |
| 88 | 0x058 | `suffragium_min.str` |  |
| 89 | 0x059 | `StormGust.str` |  |
| 90 | 0x05a | `Lord.str` |  |
| 91 | 0x05b | `Benedictio.str` |  |
| 92 | 0x05c | `Meteor%d.str` | анимация (мультифайл, %d = кадр) |
| 95 | 0x05f | `Quagmire.str` |  |
| 96 | 0x060 | `FirePillar.str` |  |
| 97 | 0x061 | `FirePillarBomb.str` |  |
| 101 | 0x065 | `RepairWeapon.str` |  |
| 102 | 0x066 | `CrashEarth.str` |  |
| 103 | 0x067 | `WeaponPerfection_min.str` |  |
| 104 | 0x068 | `maximize_min.str` |  |
| 106 | 0x06a | `BlastMine.str` |  |
| 107 | 0x06b | `Claymore.str` |  |
| 108 | 0x06c | `Freezing.str` |  |
| 109 | 0x06d | `Bubble%d.str` | анимация (мультифайл, %d = кадр) |
| 110 | 0x06e | `GasPush.str` |  |
| 111 | 0x06f | `Spring.str` |  |
| 112 | 0x070 | `kyrie_min.str` |  |
| 113 | 0x071 | `Magnus.str` |  |
| 124 | 0x07c | `VenomDust.str` |  |
| 126 | 0x07e | `PoisonReact_1st.str` |  |
| 127 | 0x07f | `PoisonReact.str` |  |
| 129 | 0x081 | `VenomSplasher.str` |  |
| 130 | 0x082 | `TwoHand.str` |  |
| 131 | 0x083 | `AutoCounter.str` |  |
| 133 | 0x085 | `Freeze.str` |  |
| 134 | 0x086 | `Freezed.str` |  |
| 135 | 0x087 | `IceCrash.str` |  |
| 136 | 0x088 | `slowp.str` |  |
| 139 | 0x08b | `SandMan.str` |  |
| 141 | 0x08d | `Pneuma%d.str` | анимация (мультифайл, %d = кадр) |
| 143 | 0x08f | `SonicBlow.str` |  |
| 144 | 0x090 | `Brandish2.str` |  |
| 145 | 0x091 | `ShockWave.str` |  |
| 146 | 0x092 | `ShockWaveHit.str` |  |
| 147 | 0x093 | `EarthHit.str` |  |
| 148 | 0x094 | `Pierce.str` |  |
| 149 | 0x095 | `Bowling.str` |  |
| 150 | 0x096 | `SpearStab.str` |  |
| 151 | 0x097 | `SpearBoomerang.str` |  |
| 152 | 0x098 | `HolyHit.str` |  |
| 153 | 0x099 | `Concentration.str` |  |
| 154 | 0x09a | `bs_RefineSuccess.str` |  |
| 155 | 0x09b | `bs_RefineFailed.str` |  |
| 156 | 0x09c | `JobChange.str` |  |
| 157 | 0x09d | `LevelUP.str` |  |
| 158 | 0x09e | `JobLvUP.str` |  |
| 167 | 0x0a7 | `TamingSuccess.str` |  |
| 168 | 0x0a8 | `TamingFailed.str` |  |
| 169 | 0x0a9 | `EnergyCoat.str` |  |
| 170 | 0x0aa | `CartRevolution.str` |  |
| 181 | 0x0b5 | `MentalBreak.str` |  |
| 182 | 0x0b6 | `magical.str` |  |
| 183 | 0x0b7 | `sui_explosion.str` |  |
| 185 | 0x0b9 | `suicide.str` |  |
| 186 | 0x0ba | `yunta_1.str` |  |
| 187 | 0x0bb | `yunta_2.str` |  |
| 188 | 0x0bc | `yunta_3.str` |  |
| 189 | 0x0bd | `yunta_4.str` |  |
| 190 | 0x0be | `yunta_5.str` |  |
| 191 | 0x0bf | `homing.str` |  |
| 192 | 0x0c0 | `poison.str` |  |
| 193 | 0x0c1 | `silence.str` |  |
| 194 | 0x0c2 | `stun.str` |  |
| 195 | 0x0c3 | `StoneCurse.str` |  |
| 197 | 0x0c5 | `sleep.str` |  |
| 199 | 0x0c7 | `Pong%d.str` | анимация (мультифайл, %d = кадр) |
| 204 | 0x0cc | `빨간포션.str` | красное зелье; нужен euc-kr путь |
| 205 | 0x0cd | `주홍포션.str` | оранж. зелье; нужен euc-kr путь |
| 206 | 0x0ce | `노란포션.str` | жёлтое зелье; нужен euc-kr путь |
| 207 | 0x0cf | `하얀포션.str` | белое зелье; нужен euc-kr путь |
| 208 | 0x0d0 | `파란포션.str` | синее зелье; нужен euc-kr путь |
| 209 | 0x0d1 | `초록포션.str` | зелёное зелье; нужен euc-kr путь |
| 210 | 0x0d2 | `fruit.str` |  |
| 211 | 0x0d3 | `fruit_.str` |  |
| 213 | 0x0d5 | `Deffender.str` |  |
| 214 | 0x0d6 | `Keeping.str` |  |
| 218 | 0x0da | `집중.str` | Concentration (аура); нужен euc-kr путь |
| 219 | 0x0db | `각성.str` | Awakening (аура); нужен euc-kr путь |
| 220 | 0x0dc | `버서크.str` | Berserk (аура); нужен euc-kr путь |
| 234 | 0x0ea | `spell.str` |  |
| 235 | 0x0eb | `디스펠.str` | Dispell; нужен euc-kr путь |
| 244 | 0x0f4 | `매직로드.str` | Magic Rod; нужен euc-kr путь |
| 245 | 0x0f5 | `holy_cross.str` |  |
| 246 | 0x0f6 | `shield_charge.str` |  |
| 248 | 0x0f8 | `providence.str` |  |
| 250 | 0x0fa | `TwoHand.str` |  |
| 251 | 0x0fb | `devotion.str` |  |
| 255 | 0x0ff | `enc_fire.str` |  |
| 256 | 0x100 | `enc_ice.str` |  |
| 257 | 0x101 | `enc_wind.str` |  |
| 258 | 0x102 | `enc_earth.str` |  |
| 268 | 0x10c | `steal_coin.str` |  |
| 269 | 0x10d | `strip_weapon.str` |  |
| 270 | 0x10e | `strip_shield.str` |  |
| 271 | 0x10f | `strip_armor.str` |  |
| 272 | 0x110 | `strip_helm.str` |  |
| 273 | 0x111 | `연환.str` | Chain Crush; нужен euc-kr путь |
| 305 | 0x131 | `p_success.str` |  |
| 306 | 0x132 | `p_failed.str` |  |
| 311 | 0x137 | `loud.str` |  |
| 315 | 0x13b | `SafetyWall.str` |  |
| 337 | 0x151 | `JobLvUP.str` |  |
| 369 | 0x171 | `TwoHand.str` |  |
| 371 | 0x173 | `angel.str` |  |
| 372 | 0x174 | `devil.str` |  |
| 390 | 0x186 | `melt.str` |  |
| 391 | 0x187 | `cart.str` |  |
| 392 | 0x188 | `sword.str` |  |
| 406 | 0x196 | `소울번.str` | Soul Burn; нужен euc-kr путь |
| 407 | 0x197 | `사람효과.str` | эффект на персонаже; нужен euc-kr путь |
| 440 | 0x1b8 | `asum.str` |  |
| 491 | 0x1eb | `찹쌀떡.str` | mochi; нужен euc-kr путь |
| 492 | 0x1ec | `ramadan.str` |  |
| 507 | 0x1fb | `mapae.str` |  |
| 508 | 0x1fc | `itempokjuk.str` |  |
| 565 | 0x235 | `moonlight_1.str` |  |
| 566 | 0x236 | `moonlight_2.str` |  |
| 567 | 0x237 | `moonlight_3.str` |  |
| 568 | 0x238 | `h_levelup.str` |  |
| 569 | 0x239 | `defense.str` |  |
| 593 | 0x251 | `food_str.str` |  |
| 594 | 0x252 | `food_int.str` |  |
| 595 | 0x253 | `food_vit.str` |  |
| 596 | 0x254 | `food_agi.str` |  |
| 597 | 0x255 | `food_dex.str` |  |
| 598 | 0x256 | `food_luk.str` |  |
| 603 | 0x25b | `FireHit%d.str` | анимация (мультифайл, %d = кадр) |
| 608 | 0x260 | `cook_suc.str` |  |
| 609 | 0x261 | `cook_fail.str` |  |
| 612 | 0x264 | `itempokjuk.str` |  |
| 635 | 0x27b | `fire dragon.str` |  |
| 636 | 0x27c | `icy.str` |  |
| 646 | 0x286 | `트랙킹.str` | Tracking; нужен euc-kr путь |
| 649 | 0x289 | `불스아이.str` | Bull's Eye; нужен euc-kr путь |
| 668 | 0x29c | `dfear.str` |  |
| 669 | 0x29d | `wideb.str` |  |
| 670 | 0x29e | `dfear.str` |  |
| 677 | 0x2a5 | `cwound.str` |  |
