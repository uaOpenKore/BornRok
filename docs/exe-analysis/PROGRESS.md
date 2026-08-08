# uaRO.exe — Reverse-Engineering Progress Log

**Purpose:** resumable master log for the full algorithmic disassembly of `winEXE/uaRO.exe`.
Records every step, subsystem, and key address so the work can be **paused and continued**
from the exact point it stopped. The algorithmic descriptions themselves live in the
per-subsystem `docs/exe-analysis/<subsystem>.md` files (English). This file is the index +
running journal (steps/addresses/status).

## Target binary

- File: `winEXE/uaRO.exe`
- Format: PE32, x86 (32-bit), Windows GUI subsystem
- Size: 3,072,086 bytes; `.text` ≈ 2.6 MB, stripped / unpacked (no packer)
- Notable imports: `DDRAW.DLL` (DirectDraw), `DINPUT.DLL` (DirectInput), `binkw32.dll`
  (Bink video), `ijl15.dll` (Intel JPEG Library), plus standard MSVCP/MSVCRT/WINMM.
- Entry-point language: MSVC (RTTI + vftables present) — classic Gravity RO client codebase.
- Known anchor so far: `CEffect` dispatch @ `0x5c2630` (see `docs/effect-ids-from-exe.md`).

## Decompilation source

- Tool: **Ghidra 11.3** (owner's machine), Java script `DumpDecomp.java` → `uaRO_decomp.c`
  (full decompile of every function, header `/* ==== <addr>  <name> ==== */` per function).
- Owner is preparing `uaRO_decomp.c` and will deliver it as a zip. **Not yet received.**

## Workflow (incremental, per owner's directive 2026-08-09)

1. Owner delivers `uaRO_decomp.c` (Ghidra full decompile).
2. Work subsystem-by-subsystem. For each subsystem:
   - locate its functions in the decompile (by address / vftable / string xref),
   - write an English algorithmic description to `docs/exe-analysis/<subsystem>.md`,
   - record the covered address ranges + status in the table below,
   - post a staged report to the owner (Discord).
3. Keep this file updated **after every step** so an interrupted run can resume.

## Subsystem coverage map

Status: ⬜ not started · 🟨 in progress · ✅ done

| # | Subsystem | Key anchors / addresses | Doc | Status |
|---|-----------|-------------------------|-----|--------|
| 1  | Entry / WinMain / init order          | (tbd)            | —   | ⬜ |
| 2  | Window + message loop (WndProc)       | (tbd)            | —   | ⬜ |
| 3  | DirectDraw render backend             | DDRAW imports    | —   | ⬜ |
| 4  | DirectInput (keyboard/mouse)          | DINPUT imports   | —   | ⬜ |
| 5  | File / GRF virtual filesystem         | (tbd)            | —   | ⬜ |
| 6  | Sprite (.spr/.act) load + animation   | (tbd)            | —   | ⬜ |
| 7  | Map load (RSW/GND/GAT) + renderer     | (tbd)            | —   | ⬜ |
| 8  | Effect engine (CEffect / .str)        | `0x5c2630`       | `effect-ids-from-exe.md` | 🟨 |
| 9  | Network / packet dispatch             | (tbd)            | —   | ⬜ |
| 10 | UI windows / widgets                  | (tbd)            | —   | ⬜ |
| 11 | Audio (Miles/WINMM) + Bink video      | binkw32          | —   | ⬜ |
| 12 | Lua / lub script VM                   | (tbd)            | —   | ⬜ |
| 13 | Game-state / session / login flow     | (tbd)            | —   | ⬜ |

## Journal

- **2026-08-09** — Set up Ghidra on owner's box. Ghidra 11.3 dropped bundled Jython →
  `.py` scripts need PyGhidra + system Python (absent). Switched to Java GhidraScript
  `DumpDecomp.java` (no Python). Import settled: PE / `x86:LE:32:default:windows`.
  Awaiting `uaRO_decomp.c` from full-program decompile. Log scaffold created.
