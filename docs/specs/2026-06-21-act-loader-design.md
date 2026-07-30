# uaRO Client — v2 «ACT loader» — Design Spec

- **Дата:** 2026-06-21
- **Ветка:** `client/foundation`
- **Подпроект:** v2 — форматы ассетов (второй формат: ACT, в пару к SPR)

## 1. Цель

Парсер формата **ACT** — действия и анимации спрайтов RO. ACT ссылается на кадры
SPR и описывает, как из них собираются анимации (позиция/зеркало/цвет/масштаб/
поворот слоёв, событие, скорость). Вместе SPR+ACT полностью задают анимацию
персонажей/NPC/эффектов. Здесь — только разбор; проигрывание анимаций — в v3/v5.

Кладётся в существующую `client_formats` (зависит только от `core`) → offline.

## 2. Формат ACT (раскладка roBrowser/GRFEditor, версии 0x200–0x205)

```
"AC"                     2 байта
version                  u16 LE (low=minor, high=major: 0x205 = 2.5)
action_count             u16 LE
reserved                 10 байт

action (xN):
  frame_count            u32 LE
  frame (xK):
    range                32 байта (skip) — зарезервированный box (8 * int32)
    layer_count          u32 LE
    layer (xL):
      x, y               i32, i32
      spr_index          i32  (кадр SPR; -1 = нет)
      mirror             i32
      version >= 0x200:
        r,g,b,a          4 * u8
        scaleX           f32
        scaleY           f32  если version >= 0x204, иначе = scaleX
        rotation         i32
        spr_type         i32  (0=indexed, 1=rgba)
        version >= 0x205: width i32, height i32
    event_id             i32   (version >= 0x200; -1 = нет)
    version >= 0x203:
      anchor_count       u32
      anchor (xA):       reserved i32, x i32, y i32, attr i32  (16 байт)

events (version >= 0x201):
  event_count            u32
  event (xE):            name char[40] (NUL внутри поля)
delays (version >= 0x202):
  delay[action_count]    f32  — скорость анимации на действие
```

## 3. API

```cpp
struct ActLayer  { i32 x,y, sprIndex, mirror; u8 r,g,b,a; f32 scaleX,scaleY; i32 rotation, sprType, width, height; };
struct ActFrame  { std::vector<ActLayer> layers; i32 eventId; std::vector<std::array<i32,3>> anchors; };
struct ActAction { std::vector<ActFrame> frames; f32 delay; };
struct ActEvent  { std::string name; };
class Action {
  static std::optional<Action> parse(const std::vector<u8>& bytes);
  u16 version() const; const std::vector<ActAction>& actions() const; const std::vector<ActEvent>& events() const;
};
```

## 4. Проверка (offline)

`test_act`: строит синтетические ACT в памяти и парсит обратно — v0x205 (полный
слой + width/height + event + delay), v0x200 (без width/height, scaleY=scaleX),
отклонение битой сигнатуры.

**Валидация на реальных данных:** инструмент `tools/formatcheck` прогнал ВСЕ ACT
из настоящего `data.grf` — **11 302 файла, 0 ошибок** (версии: 3×v200, 261×v201,
358×v203, 2284×v204, 8396×v205). Именно эта проверка выявила реальный баг: у кадра
ОДИН 32-байтный range, а не два по 32 (исходная синтетика это пропускала).
