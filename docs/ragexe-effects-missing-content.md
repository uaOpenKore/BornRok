# Недостающий эффект-контент (модерн-Ragexe → нет в GRO.grf/data.grf)

Из switch-таблицы Ragexe извлечены EFFECTID→.str (см. `ragexe-effect-map.md`). Почти всё
нашлось в GRO.grf и влито в `effectFxStr` (0x1f3, коммит 4791fc95). **Реально отсутствуют
только 5 `.str`** — их нужно найти/импортировать в GRF, после чего добавить
`case <id>: return "<путь>";` в effectFxStr (id уже известен).

| EFFECTID | ожидаемый путь `data/texture/effect/<...>.str` | примечание |
|---:|---|---|
| 1548 | `new_poisonsmoke/new_poisonsmoke_cast/new_poisonsmoke_cast.str` |  |
| 2261 | `diluvio/diluvio_cold_force _bottom/diluvio_cold_force _bottom.str` | пробел в имени (опечатка оригинала?) |
| 2376 | `npc_20th_crystal/npc_20th_crystal_s.str` |  |
| 2377 | `npc_20th_crystal/npc_20th_crystal_m.str` |  |
| 2378 | `npc_20th_crystal/npc_20th_crystal_l.str` |  |
