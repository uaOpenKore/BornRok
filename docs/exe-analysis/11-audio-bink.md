# Subsystem 11 — Audio (Miles Sound System) & Bink video

Audio runs on the **Miles Sound System (MSS / mss32)** — the binary is full of `AIL_*`
imports. MP3 background music is decoded by the Miles **`mp3dec.asi`** codec (shipped next
to the exe); the Bink intro's audio uses DirectSound.

## Audio init — `FUN_004214f0` @ `0x004214f0`

1. `AIL_set_redist_directory` + `AIL_startup`.
2. **Digital driver**: parameters from the settings globals —
   - sample rate `DAT_007731b0`: `0x5622` (22050) / `0x2B11` (11025) / `8000`,
   - bit depth `DAT_007731b8`: 16 / 8,
   - channels `DAT_007731a8`: stereo / mono (`DAT_0070e92c`).
   `AIL_open_digital_driver(rate, bits, chans, 1)` → driver `DAT_0070e748`.
   (`AIL_set_preference(0xF, …)` tunes the mixer.)
3. **Voice pool**: `AIL_allocate_sample_handle` × N, where N = `DAT_0070ec08` = **0x30 / 0x20
   / 0x10 (48/32/16)** voices selected by the quality setting `DAT_007731b4`.
4. **3D audio**: `AIL_enumerate_3D_providers` → `AIL_open_3D_provider` (`DAT_0070eb34`) +
   `AIL_3D_speaker_type` — positional SFX with distance attenuation.
Failure returns 0 → WinMain aborts with `"Audio System Init Failed"` (Subsystem 1).

## Sound effects (.wav)

SFX are `.wav` assets pulled from the GRF (Subsystem 5) into **AIL sample handles** and
played with a **3D position** relative to the camera/player, so footsteps, attack swings
(`_attack_<weapon>.wav`), hits (`_enemy_hit_normal*`), skill and effect sounds attenuate
with distance. Triggers:
- **ACT frame sound index** (Subsystem 6) — per-animation-frame sound (attack/step/hurt),
- **effect `.wav`** referenced by the effect engine (Subsystem 8),
- **skill** cast/hit sounds.

## Background music (.mp3)

Per-map BGM: the map name maps to an `.mp3` (via the BGM name table / RSW), streamed
through Miles (`AIL_open_stream` + `mp3dec.asi`), looped, at the settings BGM volume.
`FUN_004219c0` @ `0x004219c0` starts a track (e.g. `bgm\01.mp3` on the login/first map).

## Bink intro video

`openning.bik` is played by Bink (`binkw32.dll`) with **`BinkOpenDirectSound`** as its
sound system; the frame loop (`FUN_0050b740` / per-frame `FUN_0050b390`) blits to the
DirectDraw primary surface (Subsystems 1 & 3).

## Settings globals

| Global | Meaning |
|--------|---------|
| `DAT_007731a8` | sound enabled / stereo-mono |
| `DAT_007731b0` | sample rate select |
| `DAT_007731b4` | quality → voice count |
| `DAT_007731b8` | bit depth |
| `DAT_007731bc` | mixer preference / volume |

## Key addresses (resume)

| Symbol | Addr | Role |
|--------|------|------|
| Audio init (MSS) | `0x004214f0` | AIL driver + voices + 3D provider |
| BGM play | `0x004219c0` | stream mp3 |
| Intro Bink loop | `0x0050b740` | openning.bik |
| Digital driver | `DAT_0070e748` | AIL driver handle |
| 3D provider | `DAT_0070eb34` | positional audio |

> Port note: BornRok replaces MSS with an SDL3-core audio backend + `dr_libs`
> (wav/mp3/flac), and XAudio2 on Xbox/UWP (memory `audio-backend-sdl3-core`).
