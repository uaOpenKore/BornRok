# Subsystem 5 — File system / GRF virtual filesystem

All game assets are read through a **layered virtual filesystem**: a chain of "providers"
(GRF archives + a loose-directory provider), fronted by a **resource-name remap table**.
A request for a Korean asset path is remapped, then resolved against the providers in
order; loose files on disk shadow the archived copies.

## Archive list — `DATA.INI`

`FUN_0069422f` @ `0x0069422f` (called first in WinMain) reads the mount list:
- Dynamically resolves `GetPrivateProfileStringA` / `WritePrivateProfileStringA` from
  `KERNEL32`.
- Reads section **`[Data]`** of `.\DATA.INI`, iterating single-character keys `'9'…'0'`.
  Each present key holds a GRF filename → passed to **`FUN_0050ccb0`** (mount).
- If nothing is listed, it writes a default entry (e.g. `data.grf` / `psakdata.grf`) back
  to `DATA.INI` and retries.

## Mount — `FUN_0050ccb0` @ `0x0050ccb0`

Appends a **provider object** to the VFS provider list (`this+4` list, `this+8` count).
Two provider vtables are used:
- **`PTR_FUN_0069c988`** — GRF-archive provider (`FUN_00511120` loads its file table).
- **`PTR_FUN_0069c97c`** — loose-directory provider (`FUN_00510de0`/`FUN_00510b50`),
  which lets on-disk files under `data\` override the archives.

Providers are consulted in registration order per lookup; the loose provider is checked
so unpacked assets win over packed ones.

## GRF format & entry read

- **Table load** — `FUN_00510fd0` @ `0x00510fd0` reads the GRF header + file table.
  Header carries the `"Master of Magic"` signature and version (0x1xx / 0x200); the file
  table lists each entry's offset, packed/unpacked size and flags.
- **Entry read** — `FUN_005118b0` @ `0x005118b0`:
  1. read the entry bytes through the provider (`vtbl+8`),
  2. **DES-decrypt** via `FUN_005120b0` @ `0x005120b0` (the classic GRF DES/shuffle,
     applied to headers / small entries per the entry flag),
  3. **`uncompress`** (zlib inflate) into the output buffer.
  So every asset = optional DES + zlib. `uncompress` sites: `0x00510fd0`, `0x005118b0`,
  plus the generic helper at `FUN_...` (line 37068).

## Resource-name remap — `resNameTable.txt`

`FUN_00512b70` @ `0x00512b70` is a lazy singleton returning the table object
`&DAT_0074bad8` (init `FUN_00512bc0`, registered for teardown via `atexit`-style
`FUN_00671367`). It maps requested resource names (often Korean / friendly names) to the
actual stored GRF path — loaded during startup (Subsystem 1 step 10) before the window is
created. `FUN_00513e80` complements it (second table / alias set).

## Path/encoding notes

- Stored paths are **EUC-KR / cp949** (Korean directory names 유저인터페이스 etc.); the
  client requests them by those raw bytes. (This is exactly what the BornRok port's
  `Cp949` table reproduces cross-platform.)
- The high-level asset loaders (sprite/act, bmp/tga/jpg, rsw/gnd/gat, str) all funnel
  through this VFS to obtain a decompressed byte buffer, then parse it.

## Key addresses (resume)

| Symbol | Addr | Role |
|--------|------|------|
| DATA.INI enumerate | `0x0069422f` | read `[Data]` mount list |
| Mount provider | `0x0050ccb0` | append GRF / loose provider |
| GRF table load | `0x00510fd0` | header + file table |
| GRF entry read | `0x005118b0` | read → DES → zlib |
| GRF DES decrypt | `0x005120b0` | entry decode |
| resNameTable | `0x00512b70` | name remap singleton `DAT_0074bad8` |
| resName alias 2 | `0x00513e80` | secondary table |
| GRF provider vtbl | `PTR_FUN_0069c988` | archive |
| Loose provider vtbl | `PTR_FUN_0069c97c` | directory override |
