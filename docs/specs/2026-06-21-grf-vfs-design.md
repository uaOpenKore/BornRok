# uaRO Client — v1 «GRF / VFS» — Design Spec

- **Дата:** 2026-06-21
- **Ветка:** `client/foundation`
- **Подпроект:** v1 (поверх v0 Foundation)

## 1. Цель

Виртуальная файловая система (`Vfs`) поверх GRF-архивов и loose-папки `data/`,
с порядком переопределения по `data.ini`. Это фундамент для всех загрузчиков
ассетов (v2+): они читают только через `Vfs`, не зная про GRF или диск.

Реальный `data.ini` клиента: `0=new.grf`, `1=Palettes.grf`, `2=data.grf`.

## 2. Объём v1 (честный MVP, проверяемый offline)

**Входит и проверяется тестами:**
- Чтение **GRF версии 0x200** (современный формат): заголовок, zlib-сжатая
  таблица файлов, разбор записей.
- Извлечение **незашифрованных** файлов (zlib-inflate).
- `Vfs`: монтирование нескольких GRF + loose-папок, нормализация путей
  (lowercase, `\` → `/`), порядок приоритета (loose > GRF; среди GRF — первый
  смонтированный выигрывает, что соответствует порядку в `data.ini`).
- `Vfs::mountDataIni()` — монтирование по секции `[Data]`.

**Отложено (следующий инкремент v1.1):**
- **GRF-DES** дешифрование (флаги 0x02/0x04). Зашифрованные записи сейчас
  детектируются и пропускаются с предупреждением. DES реализуем и проверим
  против реального `data.grf` (нельзя честно верифицировать offline без образца).
- GRF версии 0x10x (старый формат с шифрованными именами).
- Case-insensitive поиск loose-файлов на Linux.

## 3. Формат GRF 0x200 (справочно)

Заголовок — 46 байт:
| смещение | поле | тип |
|---|---|---|
| 0 | signature "Master of Magic\0" | char[16] |
| 16 | key (не используется в 0x200) | char[14] |
| 30 | file_table_offset (от байта 46) | u32 LE |
| 34 | seed (m1) | u32 LE |
| 38 | filecount_scrambled (m2) | u32 LE |
| 42 | version (0x200) | u32 LE |

Реальное число файлов = `m2 - m1 - 7`.

Таблица (по адресу `46 + file_table_offset`): `u32 compressed_len`,
`u32 uncompressed_len`, далее zlib-данные → inflate. В распакованной таблице —
последовательность записей: имя (NUL-terminated), затем 17 байт:
`u32 compressed`, `u32 compressed_aligned`, `u32 uncompressed`, `u8 flags`,
`u32 offset`. Флаги: bit0=файл, bit1=mixed-DES, bit2=header-DES.

Данные файла — по адресу `46 + offset`, размером `compressed_aligned` байт →
(DES при флагах) → zlib-inflate до `uncompressed`.

## 4. Архитектура и дерево

```
src/resource/
  Grf.hpp / Grf.cpp     GrfArchive: open, entries, read (0x200, незашифрованные)
  Vfs.hpp / Vfs.cpp     монтирование + lookup с переопределением
  ResourceManager.hpp   (остаётся заглушкой — v2)
```

Новая статическая библиотека **`client_resource`** (зависит от `core` + zlib,
НЕ от SDL/bgfx) → собирается и тестируется в том числе в offline/core-only
конфигурации (zlib — лёгкая стандартная зависимость).

В `core/io/ByteBuffer` добавляется `ByteReader::read_cstr()` (чтение строки до
NUL переменной длины — нужно для таблицы GRF).

## 5. Проверка

Юнит-тесты (offline, GCC + zlib):
- `test_grf`: строит синтетический валидный GRF 0x200 в памяти (zlib-deflate),
  открывает `GrfArchive`, проверяет версию, число записей, извлечение содержимого,
  нормализацию пути (`DATA\\X.TXT` → `data/x.txt`), отсутствующие файлы.
- `test_vfs`: loose-папка переопределяет GRF; среди GRF выигрывает первый;
  `mountDataIni` соблюдает порядок `[Data]`.
