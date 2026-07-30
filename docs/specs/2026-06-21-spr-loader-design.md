# uaRO Client — v2 (start) «SPR loader» — Design Spec

- **Дата:** 2026-06-21
- **Ветка:** `client/foundation`
- **Подпроект:** v2 — форматы ассетов (первый формат: SPR)

## 1. Цель

Парсер формата **SPR** (спрайты RO) — индексированные и truecolor-кадры +
палитра. Это первый из форматов v2; читает байтовый буфер (из `Vfs`), отдаёт
кадры и палитру, умеет разворачивать индексированный кадр в RGBA8. Рендер кадров
спрайтов — позже (v3/v5), здесь только разбор + конвертация в RGBA.

Новая статическая библиотека **`client_formats`** (зависит только от `core`,
не от SDL/bgfx/zlib) → собирается и тестируется offline.

## 2. Формат SPR (справочно)

```
"SP"                     2 байта, сигнатура
version                  u16 LE  (0x100, 0x101, 0x200, 0x201)
indexed_count            u16 LE
rgba_count               u16 LE  — только если version >= 0x101
indexed frames (xN):
    width u16, height u16
    version <  0x201: width*height байт сырых индексов палитры
    version >= 0x201: data_len u16, далее RLE (длина серий цвета 0)
rgba frames (xM):        (только version >= 0x101)
    width u16, height u16, далее width*height*4 байт RGBA
palette                  1024 байта (256 * RGBA), в КОНЦЕ файла (если есть индексные кадры)
```

**RLE (v2.1):** литеральные байты копируются как есть; байт `0x00` означает серию
прозрачных пикселей (цвет 0) — за ним идёт байт-счётчик N, что даёт N пикселей
цвета 0 суммарно.

**Прозрачность:** индекс палитры 0 — прозрачный (alpha = 0 при конвертации в RGBA).

## 3. API

```cpp
struct SprPalColor { u8 r, g, b, a; };
struct SprFrame    { u16 width, height; std::vector<u8> pixels; }; // 1B/px (indexed) или 4B/px (rgba)
class Sprite {
  static std::optional<Sprite> parse(const std::vector<u8>& bytes);
  u16 version() const;
  const std::vector<SprFrame>& indexedFrames() const;
  const std::vector<SprFrame>& rgbaFrames() const;
  const std::array<SprPalColor,256>& palette() const;
  std::vector<u8> indexedToRgba(usize frame) const;   // index 0 -> прозрачный
};
```

## 4. Проверка (offline)

`test_spr`: строит синтетические SPR в памяти и парсит обратно —
- v0x100 raw indexed + палитра → проверка размеров, индексов, конвертации в RGBA
  (индекс 0 → alpha 0; индекс N → цвет палитры, alpha 255);
- v0x201 RLE indexed → проверка декодера серий нулей (round-trip энкодер в тесте);
- v0x101 с truecolor RGBA-кадром → проверка чтения RGBA.

**Валидация на реальных данных:** `tools/formatcheck` прогнал ВСЕ SPR из настоящего
`data.grf` — **11 272 файла, 0 ошибок** (40×v200, 11232×v201). Проверка выявила баг
RLE-декодера (не потреблял ровно `data_len` байт на кадр → рассинхрон); исправлено
через `seek(end)` + санити-капы на размеры кадров.
