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
| M1 | ✅ **DONE** — boot: palette + SCRBASE + framebuffer at $A000 (alynxdj.cfg RAM map), 4×6 font (makefont.py, 40×17 grid, legibility screenshot-verified), splash with version + git-hash build stamp, VBlank IRQ (timer 2) + live frame counter. **Q3 resolved: VBL = 59.90 Hz with crt0 timing** (96 ticks/120 Handy frames) | The board comes up |
| M2 | ✅ **DONE** — pad read (raw + edge; DAS repeat deferred to the M4 editor), pad→note squares on channel A, **FFT-verified 220.0/293.0/440.0 Hz** from the WAV capture. Proven square recipe: `FEEDBACK=$01, SHIFT=0` (Mikey shifts in inverted tap parity, so it self-starts from 0), `f = 1/(2·(BKUP+1)·clock)` exact. RetroPad→Lynx button map probed and recorded in CLAUDE.md; `$C000` debug mirror + RAM-dump verification pattern established | The sound path |
| M3 | ✅ **DONE** — note table (maketables.py: 96 notes C-1..B-8 as clocksel+BKUP pairs, worst error 9.5 cents at F#8 only), engine pipeline (groove → row → trigger → AHD envelope → shadow → flush, voice-0 scope), A = play/stop. **FFT-verified**: 16-row C-major arp plays all pitches within cents (261/329/393/523/658/786 Hz), 100 ms rows, clean loop. Flush discipline: full channel reprogram on trigger only; envelope ticks are volume-only writes (never restart the oscillator) | The sequencer |
| M4 | ✅ **DONE** — PHRASE editor: 16-step grid (row/note/instr/cmd columns), inverse-video cursor + DAS key repeat, A-hold+dpad edit (±semitone/±octave, verified: C-4→C#4 auditioned at 278 Hz), A-tap-on-release insert (chord-consumption rule ported), B-held+A transport with PLAY/STOP label + accent playhead. All verified headless via scripted BTN input + RAM mirrors ($C001/2 cursor) + FFT. **Boot-to-mainloop ≈ 124 frames (cc65 C render, D2 risk on record — optimize the clear/grid paint when it matters)** | First playable build |
| M5 | ✅ **DONE** — flat song block (`struct songdata sd`: 128×4 song, 32 chains, 64 phrases, $FF sentinels), per-track walkers (song→chain→phrase, chain-end song-row advance, wrap-once loop), contextual transport (SONG=play from cursor row / CHAIN=loop chain / PHRASE=loop phrase), SONG+CHAIN screens with shared cursor/edit/insert machinery, B-held+L/R screen nav with drill-down (loads chain/phrase under cursor), B-tap back, per-screen playheads, src/tracker.h single-source data model. **FFT-verified**: 3 tracks play together (bass C-2 + odd harmonics, arp, G-5 blips); chain step 2's +12 transpose audible (C4 vanishes, C6 appears). Nav verified via screen-id RAM mirror | Song structure |
| M6 | ✅ **instruments DONE** — 16-byte records in the song block (type/vol/AHD/timbre + reserved table/pan/fine), per-instrument AHD latched at trigger (atk 0 = instant, dcy 0 = sustain), LFSR timbre banks (8 tone + 8 noise feedback presets — hardware curation stays Q4), INSTR screen (PHRASE→INSTR drill, field edit w/ audition). **Verified**: NOISE hats on track 4 are spectrally noise-like (top-bin share 0.018), pulse at 4.8 Hz as sequenced, bandwidth follows the note clock (C-6 → ~3.9 kHz shift rate — hiss wants higher notes); TYPE field edit TONE→NOISE on-screen. ◻️ **tables + TABLE screen + shared command executor → folded into M9** (one command pass) | The voice model |
| M7 | ✅ **core DONE** — alynxdj_sample.py (WAV→8-bit signed PCM @7812.5 Hz, silence-trim + budget caps: the 808 kit fits 12.1 KB RAM-resident, linked into the ROM), src/pcm.s-in-irq.s (timer-7 IRQ feeds channel D OUTPUT, ~45 cyc/byte ≈ 9% CPU, APPZP pointer, self-stopping), IT_KIT (note semitone → kit slot, mono sample bus on channel D, SMSGGDJ T3 policy). **Verified: captured audio cross-correlates with the source 808 WAVs at 1.11 (BD) / 0.83 (SD) / 0.69 (HH); full 4-track mix keeps all melodic components (engine unstarved).** ◻️ remaining: cart-streamed multi-kit pool + kit-select, `S` rate command, 2-voice cap measurement on hardware (Q2) | The flagship — samples |
| M8 | ✅ **core DONE** — IT_WAV = **hardware triangle** (integrate mode + tap-11-only feedback: inverted parity cycles the zero shifter through 12 ones/12 zeros → triangle at shiftrate/24; note lookup +43 semitones via the extended 139-entry table; instrument vol ≤10 or the 8-bit accumulator wraps). **Instrument pan**: pan nibbles → per-channel ATTEN at trigger, PAN reg enabled, write-always (D8). **Verified solo: 786 Hz for G-5, H2 0.002 / H3 0.113 (textbook triangle), R/L rms 3.75 = exactly the $4F ATTEN ratio — Handy models Lynx II stereo.** ◻️ remaining: 32-byte wavetable loop mode + WAVE screen, `O` command (needs M9 executor) | Synthesis depth |
| M9 | ✅ **commands+tables DONE** — grooves pool + shared executor + 11 commands (`A C D G H K O P V W X`): tables (16×16 vol/tsp/cmd/param, 1 row/tick, `H` loop, stick-at-end), 1/16-semitone pitch engine (bend/vibrato/chord/table-tsp resolved to interpolated BACKUP, reload-only writes — no phase restarts; clock-boundary crossings do a full retime), `D` delayed trigger via the peek, `W` row shorten, `G` groove switch, `O` live pan, `X` env-peak accent, `K` tick-kill. PHRASE cmd/param columns + TABLE screen + INSTR TABLE field. **Verified by rig battery**: K 100/33 ms alternation, P +3.1 semis/0.45 s, V ±0.5 semis, C & A-table arps spectrally present, G rate switch. (`D` code-complete, no rig yet.) ◻️ remaining → M9b: GROOVE screen, copy/paste/clone + block ops, mute/solo, phrase-level `H`, `T`/`L`/`R`/`F`/`M`/`N`/`S`/`Z`/`I`/`J` | Editing power |
| M10 | 🔶 **plumbing proven (M10a, Q5 ✅)** — `.lnx` header byte 60 patched in the Makefile (93C46), cc65 `lynx_eeprom_*` driver works against Handy (writes byte-exact in the `.eeprom` file), harness calls `retro_unload_game` so the file flushes, **write→power-cycle→read-back round trip verified**. Read path returns low byte only through Handy (byte-per-cell convention for now; hardware recheck). ◻️ remaining (M10b): custom 10-bit-address 93C86 driver (2 KB), the RLE packer + size meter (Q1), SAVE/LOAD UI + slot header, SAVEFORMAT.md, browser savetool | Songs survive |
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

**M0–M10a done** (2026-07-02): toolchain + headless harness; boot + 59.90 Hz
tick; squares, sequencer, song hierarchy, TONE/NOISE instruments, 8-bit PCM
drums (xcorr-verified), hardware-triangle WAV voices, stereo pan, tables +
the shared executor + 11 commands (rig-battery-verified), and the EEPROM
save plumbing (power-cycle round trip, Q5). Five screens:
SONG/CHAIN/PHRASE/INSTR/TABLE. Next: M10b (93C86 driver + RLE packer +
SAVE/LOAD UI — resolves Q1) or M9b editing power (copy/paste/clone, block
ops, mute/solo, remaining commands).
