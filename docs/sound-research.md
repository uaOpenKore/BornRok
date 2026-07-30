# Звук в клиенте: что в оригинале и что берём (research, #103)

> Дизассемблирование `uaRO.exe` + изучение референса (roBrowser). Вывод: ассеты звука (WAV/MP3)
> полностью переиспользуемы; движок оригинала (Miles) нам не подходит — берём **SDL3_mixer**.

## Что в оригинальном клиенте (из exe)

- **Движок: Miles Sound System** (`mss32.dll`, функции `AIL_*`). Проприетарный, только Windows —
  нам НЕ подходит.
- **SFX** — короткие WAV, через `AIL_*_sample` (allocate/start/end sample, set_sample_volume).
- **BGM** — MP3-стрим, через `AIL_open_stream` + `AIL_set_stream_loop_count` (зацикленный) +
  `AIL_set_stream_volume`. Команда `/bgm`, флаг `bgmIsPaused`.
- **3D-позиционный звук** — `AIL_set_3D_position/orientation/distances/room_type`: слушатель на
  персонаже, SFX затухают по расстоянию и панорамируются. (Звук монстра слева/издалека.)
- **Конфиг**: `soundVolume`, `streamVolume`, `isSoundOn`, `SOUNDMODE`, команды `/sound` `/bgm`.

## Ассеты (в GRF / loose) — переиспользуемы

- **SFX: `data\wav\…wav`** — в нашем GRF **2112 файлов**. Категории:
  - `data\wav\effect\…` — звуки скиллов/эффектов (есть таблица EFFECTID→wav, вытащена из exe вместе
    с EFFECTID→.str — см. effect-ids работу).
  - боевые: `_attack_<weapon>.wav` / `_hit_<weapon>.wav` / `_enemy_hit_<elem>.wav` — звук удара по
    типу оружия (sword/axe/mace/spear/bow/rod/fist) и попадания по стихии (таблицы уже извлечены).
  - звуки мобов/NPC/UI (`*_attack.wav`, `*_die.wav`, `button.wav`, …).
- **BGM: `BGM\<NN>.mp3`** — **loose-файлы рядом с клиентом** (в GRF их нет: только 1 mp3). Выбор по
  карте: **`data\mp3nametable.txt`** (есть english/default/… версии) — строки `<map>.rsw#<NN>.mp3#`.
  Зацикливается; дефолт `01.mp3`. BGM-папку должен поставлять сервер/контент (как в ориг. клиенте).

## Как сделано в roBrowser (референс)

- `Audio/SoundManager.js`: `Sound.play('<file>', vol)` → грузит `data/wav/<file>` из GRF, громкость
  = `sfxVol × globalVol`, кэширует загруженные; вызывается на событиях (`Sound.play('_stun.wav')`,
  по ударам/скиллам/статусам).
- `Audio/BGM.js`: `BGM.play(mapInfo.mp3 || '01.mp3')`, зациклено; том + on/off в `SoundOption`.
- Громкость BGM/SFX + вкл/выкл — в окне настроек.

## Что берём для нашего клиента (SDL3)

Движок Miles не используем; всё остальное переиспользуем 1:1.

1. **Бэкенд: SDL3_mixer** (новая зависимость сборки) — WAV (SFX) + MP3 (BGM) + микширование +
   зацикливание + громкость + `Mix_SetPosition` (дистанция/панорама = аналог 3D-звука Miles).
2. **Загрузка** из нашего VFS (GRF): прочитать байты `data/wav/<file>` → `Mix_LoadWAV_IO`; BGM mp3 из
   loose BGM-папки → `Mix_LoadMUS_IO`.
3. **SFX-хуки** (данные уже есть из exe):
   - удар (0x8a) → `_attack_<weapon>`/`_hit_<weapon>` по типу оружия персонажа;
   - скилл/эффект → `effect\<...>.wav` по таблице EFFECTID→wav;
   - статусы (стан/заморозка/яд/…) → `_stun.wav` и т.п.
   - позиция источника относительно игрока → `Mix_SetPosition` (затухание + пан).
4. **BGM**: распарсить `data\mp3nametable.txt` (карта→mp3), играть mp3 карты зациклено из BGM-папки,
   фейд при смене карты.
5. **Громкость**: слайдеры SFX/BGM + вкл/выкл в Sound-панели ESC-меню (#104).

## Что нужно от тебя

- Подтвердить добавление зависимости **SDL3_mixer** в сборку (vcpkg: `sdl3-mixer`).
- BGM-mp3: лежат ли BGM-файлы рядом с клиентом / в дата-папке? (в GRF их нет — нужен путь к ним.)
- Порядок: сначала **SFX** (удары/скиллы — слышно сразу) или сначала **BGM** (музыка карт)?
