# uaRO Client — v2 «RSM loader» — Design Spec

- **Дата:** 2026-06-21
- **Ветка:** `client/foundation`
- **Подпроект:** v2 — форматы ассетов (3D-модели)

## 1. Цель

Парсер формата **RSM** — 3D-модели RO (здания и объекты, размещаемые на картах
через RSW). Отдаёт иерархию узлов с геометрией (вершины, текстурные координаты,
грани), список текстур и покадровую анимацию узлов. Рендер моделей — позже (v3).

В `client_formats` (зависит только от `core`).

## 2. Формат RSM (раскладка подтверждена реверсом реальных файлов, версия 1.4)

```
"GRSM"                     4 байта
version                    u8 major, u8 minor (1.4)
anim_length                i32
shade_type                 i32 (0 none / 1 flat / 2 smooth)
alpha                      u8  (version >= 1.4)
reserved                   16 байт
texture_count              i32
textures                   texture_count × имя по 40 байт
main_node                  имя по 40 байт
node_count                 i32
node (xN):
  name / parent            по 40 байт
  texture_count            i32; индексы texture_count × i32
  mat3                     9 float (матрица 3x3)
  offset / pos             3 + 3 float
  rot_angle                float; rot_axis 3 float; scale 3 float
  vertex_count             i32; вершины × 3 float
  tvertex_count            i32; (color i32 при v>=1.2) + u float + v float
  face_count               i32; vertIdx[3] u16, tvertIdx[3] u16, texId u16,
                           padding u16, twoSide i32, (smoothGroup i32 при v>=1.2)
  pos_keyframes (v>=1.5)   count + (frame i32 + 3 float)
  rot_keyframes            count + (frame i32 + кватернион 4 float)
```

## 3. Робастность

Все счётчики (узлы, вершины, tvertices, грани, ключевые кадры, текстуры)
проверяются на разумные границы до аллокаций → битый/враждебный файл даёт
`nullopt`, а не падение.

## 4. Проверка

- Юнит-тест (offline): синтетический v1.4 RSM round-trip (узел с 3 вершинами,
  3 tvertices, 1 гранью).
- **Валидация на реальных данных** (`tools/formatcheck` против настоящего
  `data.grf`): **RSM 4201 / 0 ошибок** (все v0x104). Образец: модель `bifrost` —
  2 текстуры, узел с 114 вершинами и 166 гранями.

С этим парсером **все 6 форматов проходят 100% реальных файлов**: SPR 11272,
ACT 11302, GAT 924, GND 921, RSW 924, RSM 4201.
