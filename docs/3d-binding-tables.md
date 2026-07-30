# Таблицы соответствий для карты-привязок 3D-моделей (#110)

Цель: связать сущности игры (**спецэффекты / NPC / чары / мобы / модели**) с 3D-моделями
из RoM. Для этого нужен единый ключ на каждую сущность, к которому цепляется RoM-модель.
Здесь — где лежат авторитетные таблицы соответствий и как из них строится карта-привязок.

## Ключ привязки

**Числовой id** — универсальный ключ. Чары, NPC и мобы живут в ОДНОМ пространстве
`JT_*` (job/type), а имя спрайта — мостик к ассету в GRF:

- `npcidentity.lub`/`jobidentity.lub`: **id → `JT_*` enum**.
- `jobname.lub`: **id → имя спрайта** в GRF (`data/sprite/…`).
- серверный `mob_db.txt`: **mob id → Sprite_Name → отображаемое имя**.

Проверено, что они сходятся по id: mob **1002** → `jobname` даёт `Poring`, `mob_db` даёт
`PORING`/`Poring`; mob **1063** → `Lunatic`/`Lunatic`. То есть по одному mob-id получаем
и GRF-спрайт, и человекочитаемое имя — этого достаточно, чтобы сопоставить RoM-модель.

## Таблицы по категориям

Дампы клиентских `.lub` (декодированы из `data.grf`) лежат рядом в
[`binding-tables/`](binding-tables/) в формате `<id>\t<имя>`:

| Категория | Клиент (data.grf) | Сервер (`/root/uAthena`) | Прочее |
|---|---|---|---|
| **Чары/джобы** | `binding-tables/jobidentity.txt` (id→JT, 3482), `binding-tables/jobname.txt` (id→спрайт, 3077) | `src/common/mmo.h` enum `JOB_*` (0…4049 = клиентские class id); `db/const.txt` `Job_*` (78 имён) | пути тела/головы: [sprite-paths-from-exe.md](sprite-paths-from-exe.md) |
| **NPC** | `binding-tables/npcidentity.txt` (view-id→JT, 3089) + `jobname.txt` (id→спрайт) | вью — «сырой» числовой sprite-id в npc-скрипте (без таблицы имён) | JT_PORTAL 10007 → `portal` |
| **Мобы** | те же `npcidentity`/`jobname` (мобовые вью-классы) | **`db/mob_db.txt`** — id **1001–2082** → col2 `Sprite_Name` → col3 kROName (~1001 записей); override: `db/mob_avail.txt` | — |
| **Спецэффекты** | `binding-tables/efstids.txt` (EFST id→enum, 663 — статус-иконки/ауры) | `db/const.txt` `EF_*` (1023 имени, id −1…1021) — это то, что шлёт `clif_specialeffect`/`ZC_NOTIFY_EFFECT2` (0x1f3) | **EFFECTID → `.str`, 166 эффектов**: [effect-ids-from-exe.md](effect-ids-from-exe.md) |
| **Предметы** (если нужны 3D-модели предметов) | — | `db/item_db.txt` id (501+, ~6215) → col2 `AegisName` → col3 Name; col19 `View` | — |
| **3D-модели** | **НЕТ таблицы id→модель** | — | ~23 `.gr2` по имени файла в `data/model/3dmob/` + `…/3dmob_bone/` |

## Главный вывод

Для эффектов/NPC/чаров/мобов **авторитетные таблицы уже есть** (выше). А вот таблицы
**id → 3D-модель НЕТ ни в клиенте, ни на сервере** — существующие 23 `.gr2` привязаны
жёстко по имени файла в коде движка. Значит карта-привязок 3D — это **новая таблица**,
которую мы заводим сами, с числовым id в роли ключа.

## Предлагаемый формат карты-привязок

Простой data-файл (можно `.lub`/`.txt`/`.json`, грузится через VFS — в т.ч. из zip),
по одной строке на привязку, сгруппировано по типу сущности:

```
# type  id     rom_model(.glb)            [anim_set]        note
mob     1002   rom/poring.glb              poring_anim       # Poring
mob     1063   rom/lunatic.glb            lunatic_anim
job     0      rom/novice_m.glb            humanoid_anim     # Novice (муж.)
npc     10007  rom/portal.glb                                # варп
effect  89     rom/fx/stormgust.glb                          # EF/EFFECTID StormGust
```

- `type` выбирает пространство id (mob/job/npc/effect/item).
- `id` — числовой ключ из таблиц выше (mob_db id, JOB id, view-id, EF/EFFECTID).
- `rom_model` — путь к сконвертированной RoM-модели (.glb); грузится загрузчиком glTF
  (см. [rom-study.md](rom-study.md)).
- Пустой `rom_model` → рисуем как раньше (2D-спрайт / .str), т.е. привязка опциональна
  на сущность.

## Как заполнять (контент)

1. Взять список мобов из `mob_db.txt` (id + kROName) и джобов из `jobname.txt`.
2. Сопоставить RoM-модели по имени/визуалу → строка `type id rom_model`.
3. Эффекты — по [effect-ids-from-exe.md](effect-ids-from-exe.md) (EFFECTID) и `EF_*`.

## Провенанс

Клиентские дампы получены декодом Lua-байткода `.lub` из `data.grf` (регистр-симуляция,
техника из `Client/tools/gen_actor_sprites.py`). Серверные — из `db/*.txt` + `src/common/mmo.h`.
Эффекты — из дизассемблера `uaRO.exe`. Пересобрать дампы можно тем же экстрактором.
