# Разбор содержимого `root/gro.zip` (patcher downloads.list)

Скачан по `downloads.list` (`root/gro.zip`, GDrive id `1d0YNy-zCDVlWjmMRlzxJkO5WzHflG9oq`, 14.6 МБ,
SHA512 совпадает). Это **не GRF, а обычный ZIP** — контент-пак, который монтируется поверх data.grf.
1206 файлов под `data/`. Расширения: `.lub` 449, `.txt` 305, `.imf` 297, `.fna` 23, `.lua` 19, `.xml` 18.

## A. Что можно использовать СРАЗУ (чинит текущие баги / готово к употреблению)

- **`data/english/` (52 файла) — полные АНГЛИЙСКИЕ таблицы.** Проверено: `idnum2itemdesctable.txt`
  реально английский ("A potion made from grinded Red Herbs that restores about 45 HP"). Здесь же:
  `idnum2itemdisplaynametable`, `idnum2itemresnametable`, `skillnametable`, `skilldesctable(2)`,
  `mapnametable`, `questid2display`, `msgstringtable`, `carditemnametable`, `itemslotcounttable` и т.д.
  → **Напрямую чинит "язык описаний не английский"**: клиент уже предпочитает `data/english/`, но у
  тестера этой папки не было. Достаточно смонтировать gro.zip — язык станет английским.
- **`data/imf/` (297 .imf + 23 .fna)** — per-frame layer/attach композиты персонажей. У нас парсер IMF
  уже есть (formats/Imf). Это ДАННЫЕ для него (в т.ч. конные: `구페코_크루세이더_남.fna` и пр.).

## B. Что ДОПОЛНЯЕТ имеющееся (современные Lua-таблицы, наш Lua/GrfData слой их читает)

`data/luafiles514/lua files/` и `data/lua files/` — таблицы renewal-клиента:
- **`stateicon/` (10)** — инфо по статус-иконкам (у нас статус-тултипы уже из GRF; здесь новее/полнее).
- **`skillinfoz/` (9), `newskillinfo/`, `skilleffectinfo/` (effectid.lub, skilleffectinfolist.lub)** —
  скилл-инфо и **маппинг скилл→эффект** → прямой источник для #144 (эффекты скиллов из данных, не из
  roBrowser). `effectid.lub` = таблица EFFECTID.
- **`spreditinfo/` (2dlayerdir/biglayerdir/smalllayerdir + `_new_*`)** — порядок/направление слоёв
  композита персонажа (аксессуары, головняки).
- **`quest/` (14), `questid2display`** — данные квестов (дополняет #136).
- **`navigation/` (18), `worldviewdata/` (6), `mapskydata/`** — навигация/варпы/небо карт.
- **`ai/` (ai.lua, const.lua, util.lua)** — AI хомункулов/наёмников (#148, мы гоняем их в Lua VM).
- **`book/` (47) + `data/english/book/`** — тексты читаемых книг.
- **`turkish/german/french/default/` (~230)** — другие локализации (если понадобится мультиязычность).

## C. Что НОВОЕ / потенциал

- **КЛЮЧЕВОЕ для бага "меч над рукой на пеко":** `ridingspreditinfo/ridingspreditinfo(_f).lub` +
  `offsetitempos/offsetitempos(_f).lub` — **таблицы СМЕЩЕНИЙ конных спрайтов и позиций предметов**.
  Именно они в оригинале задают, куда крепится оружие/аксессуары в конной позе по направлениям.
  Сейчас мы рисуем оружие в origin (без этих смещений) → на пеко по диагонали меч уезжает. Прочитать
  эти .lub в Lua-слой и применять per-job/per-dir смещение — правильный путь к фиксу конного оружия.
- **`effecttool/` (229 .lub, вкл. `effecttoolutil.lub`)** — движок процедурных партиклов (классические
  болты и т.п.). Источник для честных эффектов вместо кодовых заглушек.
- **`hateffectinfo/` (7)** — эффекты головняков (крылья/ореолы) — относится к #122.
- **`damageskin/`, `stylingshop/`, `enchant/`, `itemreform/`, `emotion/`, `chatwndinfo/`** — данные
  UI-фич renewal (скины урона, стилист, зачарование, реформа предметов, эмоции).
- **`.lub` компилированы (Lua 5.1 bytecode)** — наш встроенный Lua5.1 VM их выполняет; где нужны как
  данные, декомпилировать не обязательно (можно исполнять и читать таблицы).

## Вывод
gro.zip — это **renewal data-пак**: локализация (в т.ч. английская — чинит язык описаний), IMF-композиты,
современные Lua-таблицы (стейт-иконки, скилл-эффекты, sprite/riding offsets, квесты, навигация), AI и
процедурные эффекты. Ничего принципиально несовместимого нет — всё ложится в наш VFS + Lua/GrfData слой.
Приоритетно: (1) смонтировать `data/english/` → английские тексты; (2) прочитать `ridingspreditinfo`/
`offsetitempos` → фикс конного оружия; (3) `skilleffectinfo/effectid` → эффекты скиллов (#144).
