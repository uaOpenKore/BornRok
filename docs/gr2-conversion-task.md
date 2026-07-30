# Задача для контента: конвертировать модели `.gr2` монстров в glTF

**Зачем:** 3D-модели монстров RO — это Granny `.gr2`, сжатые **Oodle0**, для которого нет
открытого/кроссплатформенного декодера (расшифровывает только проприетарный `granny2.dll`). Поэтому
мы **конвертируем один раз, офлайн** в переносимый формат (glTF); кроссплатформенный клиент затем
грузит glTF напрямую — без Oodle0 в рантайме и без зависимости от DLL. (Решение: вариант A; см.
обсуждение по gr2.)

## Что сделать — два возможных результата (один инструмент)

- **(Предпочтительно) Переупаковать сжатие Oodle0 → Oodle1**, на выходе остаётся `.gr2`. `.gr2` у RO
  — это Oodle0, у которого нет открытого декодера, — но в клиенте уже есть рабочий декодер **Oodle1**
  + парсер Granny, поэтому `.gr2`, упакованный в Oodle1, рендерится напрямую **без нового формата**.
  `divine -a convert-model -g dos2de` из LSLib переупаковывает в формат Divinity:OS2 (Oodle1).
  → `divine.exe -a convert-model -g dos2de -s "input.gr2" -d "output.gr2"`
  *(Мне ещё нужно проверить версию Granny у переупакованного файла на образце — у RO это v6; если
  упаковщик DOS2 поднимет до v7, я расширю парсер. Пришли одну переупакованную модель для проверки.)*
- **(Запасной вариант) Экспорт в glTF** (`.glb`): `divine -s model.gr2 -d model.glb`. Клиент грузит
  glTF вместо `.gr2`. Используй, если переупаковка в Oodle1 окажется нечитаемой.

В любом случае сохраняй меш, диффузные текстуры и, где есть, скелет + анимации
idle/attack/damage/death/walk.

## Как конвертировать — Norbyte LSLib, пошагово

**1. Скачать инструмент.** https://github.com/Norbyte/lslib → **Releases** → последний
`ExportTool-vX.X.X.zip` (например, LSLib v1.20.4 или новее). Внутри: `ConverterApp.exe` (GUI) +
`divine.exe` (CLI).

**2. Добавить нужные DLL (критично).** LSLib не может поставлять Granny/Oodle (лицензия RAD); без них
получишь *«Granny2.dll is required for Oodle0/Oodle1 compressed gr2 files.»* Нужны **старые** DLL
эпохи Oodle0/Oodle1, а не новые Kraken/Mermaid `oo2core_9` из BG3:
- **Лучший источник — Divinity: Original Sin 2** (эпоха перехода Oodle0/1):
  - `granny2.dll` по пути `…\Divinity Original Sin 2\DefEd\bin\granny2.dll`
  - `oo2core_5_win64.dll` (или `oo2core_4_win64.dll`) в той же папке `…\DefEd\bin\`.
- **Или** любая игра 2017–2020 на Granny+Oodle (Life is Strange 2, XCOM 2): возьми `granny2.dll` +
  `oo2core_X_win64.dll` из корня игры (рядом с её `.exe`).

**3. Куда копировать.** Свежие сборки ExportTool переставили файлы, поэтому скопируй DLL в **оба**
места:
- корень программы (рядом с `ConverterApp.exe`), **и**
- подпапку инструментов (`Tools/` или `External/`) — `divine.exe` ищет их локально.

**4. Конвертировать.**
- **GUI:** запусти `ConverterApp.exe` → верхний выпадающий список **Game** = **Divinity: Original
  Sin 2 (64-bit)** (это форсит старый упаковщик Oodle даже для наших моделей RO) → вкладка
  **GR2 Tools** → *Source path* = RO-`.gr2` (Oodle0) → *Destination path* = новый файл → **Convert**.
- **CLI:** `divine.exe -a convert-model -g dos2de -s "input.gr2" -d "output.gr2"`
  (`-g dos2de` выбирает профиль DOS2 = упаковка Oodle1; вместо этого `-d ….glb` — для запасного glTF).

## Ссылки-референсы

- **Спецификация формата:** https://github.com/rdw-archive/RagnarokFileFormats/blob/master/GR2.MD
  · STR.MD: https://github.com/rdw-archive/RagnarokFileFormats/blob/master/STR.MD
- **Конвертер (LSLib / Divine):** https://github.com/Norbyte/lslib
- **Вьюверы / парсеры** (осмотреть модели): https://github.com/NoFr1ends/opengr2-viewer ·
  https://github.com/arves100/opengr2 · https://github.com/herenow/gr2-web *(использует настоящий granny2.dll)*

## Вход — 8 базовых моделей (`data/model/3dmob/`)

| файл .gr2 | что это | id монстра |
|---|---|---|
| `dragon_5.gr2` | Дракон | 1181 |
| `Guildflag90_1.gr2` | Гильдийский штандарт / флаг | 722 |
| `Hugeling90_6.gr2` | Hugeling | 1284 |
| `Aguardian90_8.gr2` | Bow Guardian (страж-лучник) | 1285 |
| `Kguardian90_7.gr2` | Knight Guardian (страж-рыцарь) | 1286 |
| `Sguardian90_9.gr2` | Sword Guardian (страж-мечник) | 1287 |
| `Empelium90_0.gr2` | Эмпериум | 1288 |
| `TREASUREBOX_2.gr2` | Сундук с сокровищами | 1324–1356 |

Плюс вспомогательные файлы **поз/анимаций** в `data/model/3dmob_bone/` — они несут анимации
атаки/урона/смерти/ходьбы для стражей/сундука (используют меш + текстуры базовой модели). Слей
каждую в glTF её базовой модели как именованные анимации, если инструмент позволяет; иначе
экспортируй рядом.

## Полный список файлов (проверено в data.grf — всего 23)

Точные пути и регистр имён, как они лежат в GRF (проверено grfinfo по актуальному `data.grf`):

**Модели (8) — `data/model/3dmob/`:**
```
aguardian90_8.gr2
dragon_5.gr2
empelium90_0.gr2
guildflag90_1.gr2
hugeling90_6.gr2
kguardian90_7.gr2
sguardian90_9.gr2
treasurebox_2.gr2
```

**Анимации/скелет (15) — `data/model/3dmob_bone/`:**
```
1_attack.gr2
2_damage.gr2   2_dead.gr2
7_attack.gr2   7_damage.gr2   7_dead.gr2   7_move.gr2
8_attack.gr2   8_damage.gr2   8_dead.gr2   8_move.gr2
9_attack.gr2   9_damage.gr2   9_dead.gr2   9_move.gr2
```

Все 23 файла не парсятся движком напрямую из-за сжатия Oodle0 — их нужно переупаковать в Oodle1
(предпочтительно) или экспортировать в glTF (запасной вариант), см. шаги выше. Оригиналы не трогать,
результат класть в отдельную папку (`3dmob_oodle1/` или `3dmob_gltf/`).

## Выход

- **Путь переупаковки Oodle1 (предпочтительно):** один переупакованный `.gr2` на каждую основу
  (например, `dragon_5.gr2`), сжатие Oodle1, в **`data/model/3dmob_oodle1/`** (оригиналы не трогай).
  Существующий парсер клиента + декодер Oodle1 их рендерят.
- **Запасной путь glTF:** один `.glb` на каждую основу (текстура встроена) в **`data/model/3dmob_gltf/`**.

## Затем — сначала один образец

**Пришли мне СНАЧАЛА ОДНУ переупакованную модель (например, `dragon_5.gr2` через `divine -g
dos2de`).** Я проверю, что клиент её декодирует (версия Granny + Oodle1), прежде чем ты пакетно
конвертируешь все 21 — у RO это Granny **v6**, и если упаковщик DOS2 поднимет до **v7**, я расширю
парсер (или перейдём на запасной glTF). Как только образец заработает — конвертируй остальные. Затем
я подключу `GameScene::upsertActor`, чтобы он грузил модель для id монстров выше (вместо спрайта) и
рендерил через bgfx.

*Статичного меша idle-позы на модель достаточно для первого прохода — стражи/Эмпериум, стоящие в WoE;
скелет/анимация — бонус, если инструмент выдаёт их чисто.*
