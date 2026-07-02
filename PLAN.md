# ALYNXDJ — project plan

Roadmap, scope, risks, open questions. The spec is [DESIGN.md](DESIGN.md);
the ancestors are **SMSGGDJ** (`/Users/a1106632/Documents/sms_tracker`) and
**GENMDDJ** (`/Users/a1106632/Documents/genmddj`).

## Vision

A pocket groovebox in the LSDJ tradition on real Atari Lynx hardware, third
in the SMSGGDJ family. It is a **port of the SMSGGDJ design, not a rewrite**:
data model, engine pipeline, control philosophy, command set, grooves/tables,
explicit-save discipline all carry over. The Lynx earns the port with what
neither sibling has: **four identical channels that each do square /
polynomial-noise / integrate-wave / true 8-bit PCM**, per-channel stereo
attenuation, a byte-oriented multi-drop sync bus (ComLynx, up to ~16 units),
and a portable form factor with its own screen — the whole tracker in your
hands, no TV.

The minimalist ethos is kept everywhere (two-button core, deterministic
variation, lookup tables, verify-on-silicon). The one place ALYNXDJ spends
resources is the **PCM/sample layer** — 8-bit DACs are the Lynx's reason to
exist. The one place it *cannot* spend is the save: 2 KB of EEPROM forces
the packed-save layer (DESIGN.md D4/D10/§12), the project's riskiest novelty.

## Architecture at a glance

Single 65C02 owns everything (no coprocessor split): VBlank tick runs the
ported engine pipeline over 4 Mikey-shaped channel shadows and flushes
changed registers; free timers run per-PCM-voice DAC-feed IRQs; the editor
and data model live in cc65 C, the driver/render/IRQ paths in ca65 asm
(D2). Song = flat RAM block; EEPROM save = packed serialization (D4).

## Milestones

| M | Milestone | Proves |
|---|---|---|
| M0 | ✅ **DONE** — toolchain: cc65 lynx target builds a bootable `.lnx`; headless Handy retroshot harness (PNG + audio WAV capture), `make`/`make shot` | The pipeline |
| M1 | Boot: display init (timers 0/2, DISPCTL, palette, framebuffer), 4×6 font + makefont.py, splash with version + git-hash build stamp, VBlank IRQ + frame counter | The board comes up |
| M2 | First sound + input: pad read (edge + DAS repeat), pad→note on a TONE square (LFSR tap set), audio-capture FFT verification of pitch | The sound path |
| M3 | Engine core: note table (maketables.py, 75 Hz constants — resolves Q3), phrase playback pipeline (groove → row → trigger → AHD → shadow → flush), demo arp FFT-verified | The sequencer |
| M4 | PHRASE editor: 16-step grid, cursor + key-repeat, A-hold edit / A-tap insert, prelisten, live playback | First playable build |
| M5 | Structure: 4-voice engine, CHAIN + SONG walkers and screens, 2D screen map nav, transport (B+A), playheads, drill-down | Song structure |
| M6 | Instruments: TONE timbre presets + NOISE preset list (curates Q4), INSTR screen, tables + TABLE screen, shared command executor | The voice model |
| M7 | **PCM** — KIT instruments: sample pool tool + cart directory, timer-IRQ DAC feed, cart block streaming, voice-steal cap (resolves Q2), `S` rate command; factory kits from `samples/` | The flagship — samples |
| M8 | WAV voices: integrate-mode shapes + 32-byte wavetable loop, WAVE screen; stereo `O` command (ATTEN) | Synthesis depth |
| M9 | Full command set + GROOVE screen; copy/paste/clone, block select/cut/paste; mute/solo (Option 1 layer) | Editing power |
| M10 | Persistence: packed EEPROM save/load + size meter (resolves Q1, Q5), SAVEFORMAT.md, PROJECT/OPTIONS screens, browser savetool | Songs survive |
| M11 | ComLynx sync: OUT/IN row clock + transport bytes, two-unit lock in emulator, hardware pass; IN24 reserved for the ESP32 bridge | Sync |
| M12 | LIVE mode, activity meters, demo song, control hints, hardware verification pass, MANUAL.md | Release-ready |

Commit at each milestone boundary. Hardware-verify **M6 (LFSR timbres)** and
**M7 (PCM feed)** early — Handy is a 20-year-old core; its polynomial-counter
and DAC timing fidelity is exactly where emulation lies (Q4). Holani is the
second-opinion core.

## Top risks

- **The 2 KB EEPROM save (Q1)** — the packed-save codec is the one genuinely
  novel subsystem. Mitigate: write the packer early against synthetic songs
  (M9), keep the save-tier pool sizes a tunable, surface the meter (D10).
- **PCM IRQ jitter/cost (Q2)** — 2 voices at 8 kHz ≈ 20 % CPU in IRQs on a
  4 MHz 6502; display DMA steals make jitter worse. Mitigate: asm handler,
  ring buffer, measure at M7 before committing kit rates.
- **Handy fidelity (Q4)** — LFSR tap timbres and integrate mode may sound
  different on silicon. Mitigate: hardware pass at M6/M7, curated preset
  list only after silicon listening.
- **cc65 editor sluggishness (D2)** — if screen paints lag, move the dirty-
  cell renderer fully to asm before blaming the design.
- **Scope creep** — echo (`Q`), user instrument bank, ComLynx song exchange
  are explicitly stretch (M11+/post-1.0). The FM-editor lesson from GENMDDJ:
  one new large surface per project; here it's the PCM layer, nothing else.

## Status

**M0 done** (2026-07-02): cc65 + Handy headless harness proven end-to-end
(`make shot` → PNG + WAV from a bootable `.lnx`). Next: M1 boot/splash.
