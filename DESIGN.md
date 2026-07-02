# ALYNXDJ — an LSDJ-inspired tracker for the Atari Lynx

The design contract. Read the relevant section before making design decisions;
don't re-litigate settled ones — when a decision must change, change it
deliberately and update §0. The proven ancestors are **SMSGGDJ**
(`/Users/a1106632/Documents/sms_tracker`) and **GENMDDJ**
(`/Users/a1106632/Documents/genmddj`); where a behaviour below is marked
*ported*, their DESIGN.md / source is the reference implementation.

---

## 0. Decision log (load-bearing forks)

| D | Decision | Alternative (documented, rejected) |
|---|---|---|
| D1 | **4 identical tracks (CH1–CH4), every track plays every instrument type** (TONE/NOISE/WAV/KIT). The Lynx has no fixed channel roles — that *is* its pitch. | Fixed roles (3 tone + 1 noise, SMS-style): simpler arbitration, wastes the hardware's symmetry. |
| D2 | **cc65 C for editor/UI/data, ca65 assembly for the sound driver, PCM IRQ, and text render.** cc65 codegen is slow but the editor workload is small at 160×102; the audible paths stay in asm. | All-asm (sibling style): faster everywhere, much slower to build; revisit per-routine if profiling demands. |
| D3 | **Engine tick = VBlank at ~59.9 Hz** (measured at M1: the cc65 crt0 display timing, 159 µs/line × 105 lines = 16.695 ms — Handy's 75 fps `retro_run` pacing is a frontend artifact, the emulated timer 2 runs at 59.9 Hz). Groove/BPM tables use the siblings' NTSC-60 math near-verbatim. No PAL/NTSC split exists on Lynx — single tables. | Reprogram timer 0/2 for a 75 Hz display (many Lynx games do): more tick resolution, breaks BPM-table parity with the siblings, LCD flicker tradeoffs — only revisit with hardware in hand. |
| D4 | **Song lives as one contiguous flat RAM block (save image order), but the EEPROM save is a packed/RLE serialization of it.** The Lynx persists to a 93Cxx serial EEPROM — 2 KB max (93C86) — so the siblings' verbatim-copy save cannot survive. Flat block keeps the engine + tooling model; the codec is the one new layer. | Shrink the song to fit 2 KB raw: guts the data model. RAM-only + ComLynx export only: no self-contained save on cart. |
| D5 | **Samples are 8-bit signed PCM streamed from cart at a fixed per-note timer rate; each KIT voice owns a timer-IRQ-fed channel DAC.** Direct 8-bit DACs × 4 are the Lynx's sample superpower (vs the SMS 4-bit log trick, vs GENMDDJ's single shared YM2612 DAC). | Software-mix N samples into one channel: more polyphony, needs a mixing loop the 65C02 can't afford at useful rates. |
| D6 | **Concurrent PCM voices capped at 2** (Q2 measures the real ceiling). KIT triggers beyond the cap steal the oldest PCM voice. | Uncapped: IRQ load would eat the editor and jitter the engine tick. |
| D7 | **4×6 pixel font, 40×17 character grid** (160×102). 8×8 gives only 20×12 — no room for a 16-row phrase grid + chrome. Hex-heavy glyphs designed first, sibling discipline. | 8×8 font (sibling look): too few rows; a paged UI would break the LSDJ feel. |
| D8 | **Stereo is written unconditionally (ATTEN/PAN/MSTEREO), audible on Lynx II, harmless on Lynx I (mono).** Same one-song-pans-where-panning-exists policy as the SMS/GG `O` split. | Gate on `MIKEYREV`: adds a probe for zero benefit — the writes are ignored on rev 1. |
| D9 | **Software text render into a single 8160-byte framebuffer; no Suzy blitter, no double buffer.** A text grid updates a few cells per frame; VBlank-window writes avoid visible tear. | Suzy sprite font: hardware-fast but drags in SCB management + Suzy/CPU bus handover for a workload that doesn't need it. |
| D10 | **The working song stays full-size in RAM; SAVE packs into EEPROM with a live "fits/doesn't" size meter** on PROJECT. A song too big to save is the user's tradeoff, surfaced honestly. | Hard-cap song size to guaranteed-packable: punishes every song for the worst case. |
| D11 | **The 12-bit LFSR is fully exposed per instrument: a raw 9-bit TAPS mask + a 12-bit SEED**, replacing curated timbre presets. User encoding is contiguous (bits 0-5 = taps 0-5, 6 = tap 7, 7 = tap 10, 8 = tap 11) so value-sweeping walks musically adjacent configs; the engine remaps to the scattered FEEDBACK/control layout at trigger. The seed picks the state-graph *cycle* — for many tap sets different seeds are genuinely different waveforms (verified: taps $0F1 seeds 000/555 → spectral cosine 0.05), and some (taps, seed) pairs hit the lock state and go silent, like the silicon. TONE and NOISE are now the same machine with different default taps. | Preset banks only (the M6 design): safe but hides the instrument that makes the Lynx unique. |

Open questions: see §14 and PLAN.md — Q1 EEPROM pack ratio, Q2 PCM voice
ceiling, Q3 exact display/tick rate constants, Q4 Handy LFSR fidelity.

---

## 1. Hardware constraints that drive the design

- **CPU:** 65C02 core in Mikey, ~4 MHz minus video DMA/refresh steals (~3.6 MHz
  effective). Comparable to the siblings' Z80 in throughput; poorer at 16-bit
  math — ROM lookup tables everywhere, no mul/div on hot paths (ported rule).
- **RAM: 64 KB unified, and everything lives in it** — code, song, framebuffer,
  stack. There is no runtime ROM: the cart is a block-addressed serial device
  read through `RCART`; code is loaded to RAM at boot by the loader. Budget
  (§13): ~24 KB code, 8 KB framebuffer, ~12 KB song, ~2 KB engine/UI state,
  the rest free for sample ring buffers and growth.
- **Cart:** 128–512 KB, read sequentially within a 1–2 KB block after a block
  seek. Fine for streaming sample PCM (D5); useless for random byte access —
  anything random-access lives in RAM.
- **Video:** 160×102, 4 bpp packed framebuffer (80 bytes/line), 16 pens from a
  4096-colour palette, refresh ~75 Hz driven by timer 0 (HBlank) → timer 2
  (VBlank, linked). No tiles, no tilemap — pure framebuffer (D7/D9).
- **Sound (Mikey):** 4 identical channels at `$FD20+8n`, each: `VOLUME` (8-bit
  signed amplitude), `FEEDBACK` (LFSR tap enables), `OUTPUT` (8-bit signed DAC
  value, directly writable), `SHIFT`/`OTHER` (12-bit LFSR state), `BACKUP`
  (timer reload), `CONTROL` (clock prescale 1–64 µs + link, count enable,
  integrate bit, tap 7). Each channel is a down-counter clocking a 12-bit
  polynomial shift register:
  - taps → alternating sequence = **square wave** at `clock/(BACKUP+1)/2`;
  - other tap sets = **pitched polynomial noise**, the Lynx's signature
    metallic/gritty timbres — a *timbre family*, not one noise;
  - **integrate mode** accumulates ±VOLUME per underflow = triangle/saw ramps;
  - timer off + CPU writes to `OUTPUT` = **8-bit signed PCM DAC**, per channel.
- **Stereo (Lynx II only):** `ATTEN_A–D` ($FD40–43, 4-bit L/R nibbles),
  `PAN` ($FD44), `MSTEREO` ($FD50). Mono mix on Lynx I (D8).
- **Timers:** 8 system timers. 0 = HBlank, 2 = VBlank (display pair), 4 = UART
  baud (ComLynx). 1/3/5/6/7 free — PCM feed IRQs and the EEPROM bit-bang
  delay live here.
- **ComLynx:** open-collector UART (`SERCTL`/`SERDAT`), multi-drop up to ~16
  units — the sync port (§11). Byte-oriented, unlike the siblings' 2-bit
  DE-9 counter.
- **Persistence:** 93C46/66/86 serial EEPROM (128 B / 512 B / **2 KB max**),
  bit-banged via `IODAT`/`AUDIN`; declared in the `.lnx` header (byte 60) and
  emulated by Handy. The entire save design flows from this constraint (D4).
- **Input:** D-pad + **A** + **B** + **Option 1** + **Option 2** + **Pause** —
  one more pair of buttons than the SMS pad, one fewer than the MD 3-button.
- **No region split.** One machine, one clock, one set of tables (D3).

## 2. Song data model *(ported — SMSGGDJ §2 verbatim in shape)*

```
SONG → CHAIN → PHRASE → notes + INSTRUMENT refs + commands
                         INSTRUMENT → optional TABLE
GROOVE tables control tick timing globally
```

Four tracks **CH1–CH4** (D1). Structure sizes (RAM tier — save packing in §12):

| Structure | Per-unit | Count | Bytes |
|---|---|---|---|
| Song rows | 4 tracks × chain # (1 B) | 128 rows | 512 |
| Chains | 16 × (phrase #, transpose) | 32 | 1024 |
| Phrases | 16 × (note, instr, cmd, param) | 64 | 4096 |
| Instruments | 16 B fixed record | 32 | 512 |
| Tables | 16 × (vol, pitch, cmd, param) | 16 | 1024 |
| Grooves | 16 ticks | 16 | 256 |
| Waves | 32-byte 8-bit wavetable | 8 | 256 |
| Song globals | title, defaults, pan, sync | | 64 |
| **Total** | | | **≈ 7.7 KB** |

Empty slots are `$FF` sentinels (ported). "Blocks free" counter on PROJECT.
The block is contiguous and offset-stable — SAVEFORMAT.md will fix the map.

## 3. Control input *(ported control philosophy, remapped to the Lynx pad)*

The settled frame: **the button already held when another arrives selects the
action; no simultaneous-press timing windows** (the only windows are
sequential double-taps of A). Mapping:

| Lynx | Role (sibling equivalent) |
|---|---|
| **A** | item-level modifier — SMSGGDJ button 1: tap = insert/activate, hold + D-pad = edit value, double-tap = paste/mint/clone |
| **B** | project-level modifier — SMSGGDJ button 2: tap = back, hold + D-pad = screen-map nav, held + A = play/stop transport |
| **A held + B** | cut; + long-hold = block SELECT (ported gesture set) |
| **Option 1** | mute/solo layer: O1 held + D-pad L/R selects track, O1+A = mute toggle, O1+B = solo — *the gesture the SMS never had a button for; parked there, real here* |
| **Option 2** | LIVE-mode page / secondary chrome toggle (reserved until M12) |
| **Pause** | play/stop alias; double-press = panic (silence, abort PCM, re-arm sync) — ported NMI semantics, but on the Lynx Pause is a normal pad line read per-frame |

Key-repeat (DAS-style) ported from `input.asm`.

## 4. GUI layout and screens *(ported 2D screen map, new geometry)*

40×17 character grid (D7). Persistent chrome: top bar = screen name, song
title, BPM, play state, sync state, position `SS:CC:PP`; right column = screen
map indicator + 4 per-track activity meters (name + level bar, ported from
GENMDDJ M12).

Screen map (B-held + D-pad), same 2D shape as SMSGGDJ — OPTIONS above
SONG, PROJECT above CHAIN, **WAVE above INSTR**, **FILES below SONG**,
GROOVE below CHAIN:

```
OPTIONS  PROJECT             WAVE
   |        |                  |
SONG  →  CHAIN  →  PHRASE  →  INSTR  →  TABLE
   |        |
FILES    GROOVE
LIVE is a SONG-screen mode toggle (M12).
```

The right column carries the **map indicator** (ported): three rows
(`OP_W_` / `SCPIT` / `FG___`), current screen inverted, unshipped screens
dim. FILES is the save-slot manager (lands with M10b persistence).

Inverse-video cursor, playhead row highlight, selection box — ported. The
16-step phrase grid + chrome fits 17 rows exactly; no paging (the GG build's
paged-WAVE compromise is not needed at 40 columns).

## 5. Sound engine

### 5.1 Timing
Engine tick = VBlank IRQ at ~59.9 Hz (D3, measured at M1). The groove model
is ported unchanged: a groove is up to 16 tick-counts, tempo *is* the groove,
PROJECT TMPO walks the NTSC-60 BPM rungs, `T` does BPM→groove conversion
with 60 Hz constants. In sync-slave mode row timing is clock-driven and
grooves are ignored (ported semantics, §11).

### 5.2 Per-tick pipeline *(ported from engine.asm, one voice type richer)*
groove → row advance → trigger/command peek (`D`/`L`/`I`/`Z`/`J` pre-trigger,
ported) → command execute (one shared executor for phrase + table columns) →
AHD envelope → kill → **channel shadow compose** → **flush**. The shadow set
is Mikey-shaped: per channel `{BACKUP, CONTROL, FEEDBACK, VOLUME, ATTEN}` +
dirty bits; flush writes only what changed (ported `psg_flush` discipline).
No SCB/BUSREQ split — one CPU owns everything; the flush is a plain
subroutine, the *engine order* is what ports.

### 5.3 Voice model — where ALYNXDJ diverges
Every channel runs one of four voice types, per-instrument (D1):
- **TONE** — square via the LFSR-alternating tap set; pitch = prescale +
  BACKUP from the note table; software AHD envelope into the 7-bit volume
  magnitude (the 4→7-bit upgrade over the SMS makes envelopes *smooth*).
  A TIMBRE field selects among "square-adjacent" tap presets.
- **NOISE** — arbitrary tap preset + clock: pitched polynomial noise. The
  preset list is curated in ROM (verified-on-hardware timbres, Q4), indexed
  by the instrument and the `N` command.
- **WAV** — two sub-modes: *integrate* (hardware tri/saw ramps, free) and
  *table* (32-byte 8-bit wavetable looped through the channel DAC by a timer
  IRQ — PCM-cost, counts against the D6 cap).
- **KIT** — 8-slot sample kits streamed from cart through the channel DAC at
  a timer-IRQ rate set by the note/RATE (D5). True variable-rate pitch: `S`
  is a real sample-rate command here, not a decimation trick.

### 5.4 Playback modes *(ported)*
Song / chain-loop / phrase-loop contextual transport; LIVE mode (clip
launcher, quantized launch) ports at M12.

## 6. Instruments

16-byte fixed record, union by type (ported shape): common = type, initial
volume, AHD (attack/hold/decay), table #, pan nibbles, finetune; TONE adds
TIMBRE (tap preset); NOISE adds preset + clock; WAV adds wave # + sub-mode;
KIT adds kit #, member map, RATE, loop flags. Factory presets in cart, copied
in on demand (the GENMDDJ 3-tier library is out of scope — EEPROM has no room
for a user bank; revisit if Q1 leaves slack).

## 7. Tables *(ported verbatim)*
16-row macro sequencer: vol, pitch(transpose), cmd, param columns; `A`
command one-shot override; `H` loop semantics identical to SMSGGDJ §7.

## 8. Command set *(ported, with Lynx re-aims)*

The SMSGGDJ set carries over letter-for-letter where meaning survives:
`A B C D E F G H K L M P R T V W X Z I J Q` as in SMSGGDJ §8. Divergences:

| Cmd | Lynx meaning |
|---|---|
| `N xy` | noise **tap-preset** x + clock y — selects the polynomial, the Lynx timbre lever (no CH3-steal legacy: channels are symmetric) |
| `O xy` | pan: ATTEN left x / right y nibbles — GG semantics, 16 levels per side, Lynx II audible (D8) |
| `S xx` | sample/wavetable **rate** — a real timer-reload pitch bend for PCM voices (not decimation) |
| `B x`  | wave bank 0–7 (ported; applies to WAV-table voices) |
| `Y xx` | **timbre**: live tap-preset override on TONE/NOISE (the FM-program slot, re-aimed at the LFSR) |
| `Q xx` | echo gate *if* the echo post-pass ports (M12 stretch); otherwise reserved |

## 9. Grooves *(ported verbatim — see §5.1 for the 75 Hz constants)*

## 10. Samples

- **Tool:** `tools/alynxdj_sample.py` (port of `smsggdj_sample.py`): WAV →
  8-bit signed PCM at per-kit fixed rates, 8 slots/kit, self-describing pool
  directory baked into the cart image after the code blocks. The repo's
  `samples/` kits (808/909/C78/606 + 4 speech banks) are the factory pool.
- **Playback:** per-voice timer IRQ reads a RAM ring buffer, writes the
  channel `OUTPUT` register; a main-loop pump refills the ring from the cart
  block reader (sequential `RCART` reads). Ring underrun = hold last sample
  (click-free-ish), logged to the debug overlay.
- **Budget:** at 8 kHz one PCM voice ≈ 8000 × ~45 cycles ≈ 10 % CPU; two ≈
  20 % (D6, measured properly at M7 = Q2).
- **No 4-bit log mapping, no DAC arbitration with tone duties** — each PCM
  voice owns a whole channel. The SMSGGDJ §10 machinery this replaces stays
  behind as documentation only.

## 11. Sync — ComLynx *(new transport, ported semantics)*

`SERCTL`/`SERDAT` UART, open-collector multi-drop. Modes (OPTIONS → SYNC,
numbered like the siblings): **OFF / OUT / IN / IN24**.
- **OUT:** master sends one byte per row (row-clock, the siblings' settled
  1-clock-per-row model) + transport bytes (start/stop/position).
- **IN:** slave row-advances per received row byte; grooves/`W` ignored
  (ported slave semantics).
- **IN24:** 24 PPQN byte stream for an external bridge (a future
  **alynxdj-link-esp32** ComLynx analogue of smsggdj-link-esp32).
- Multi-drop means one master can drive up to ~15 slaves — a Lynx orchestra
  is a headline feature; keep the wire protocol dumb (single-byte opcodes,
  no addressing) so it stays achievable.
- PULSE (analog Volca-style) does not exist here — ComLynx is digital-only.
  Cross-family (DE-9 ↔ ComLynx) sync happens through the ESP32 bridge, not
  by wire.

## 12. Persistence *(the Lynx-unique problem — D4, D10)*

- Working song: full-size flat block in RAM, RAM-only until explicit SAVE
  (GENMDDJ's settled persistence model).
- SAVE: pack the flat block — `$FF`-run RLE + per-region occupancy bitmaps —
  into the 93C86 (2 KB), with a 16-byte header (magic `ALDJ`, version,
  packed length, checksum). PROJECT shows a live packed-size meter (D10);
  SAVE refuses (with the meter red) rather than truncates.
- LOAD: unpack EEPROM → RAM at boot if the magic checks (slot-0 autoload,
  ported policy).
- Full-fidelity interchange: emulator `.sav`(EEPROM image) files via a
  browser `savetool` port (the codec is small and lives in one JS file +
  one Python file, both repo-side); ComLynx song dump/restore is a stretch
  goal at M11.
- SAVEFORMAT.md is written at M10 and kept in sync with any RAM-map change
  (standing sibling rule).

## 13. Frame budget (65C02, ~3.6 MHz effective, 59.9 Hz tick ≈ 60 K cycles)

| Consumer | Budget |
|---|---|
| Engine tick (4 voices, pipeline §5.2) | ≤ 6 K cycles |
| Flush (shadow → Mikey) | ≤ 1 K |
| PCM IRQs (2 × 8 kHz, D6) | ~10 K amortized/frame |
| Text render (dirty cells only) | ≤ 8 K typical |
| Input + editor logic (cc65 C) | the remainder (~20 K) |

Rules (ported): no mul/div on hot paths; note/curve/BPM tables in cart→RAM;
the PCM IRQ handler must not touch editor state; VBlank does engine-then-
render in fixed order so audio never waits on drawing.

## 14. Open questions

| Q | Question | Blocks | Path |
|---|---|---|---|
| Q1 | ✅ **RESOLVED at M10b** — the 4-track demo song RLE-packs to **480 bytes** (capacity 1016, capped by a stock-Handy EEPROM-load bug — see SAVEFORMAT.md; 2040 once fixed/hardware-verified). The lever that made it work: **FF-FF empty-chain-step sentinels** (FF 00 alternation had defeated the RLE at 1469 bytes). Save→power-cycle→autoload round trip checksum-verified | M10 | — |
| Q2 | Real PCM voice ceiling + max mix rate | M7 | Cycle-count the IRQ handler on Handy, verify on hardware |
| Q3 | ✅ **RESOLVED at M1 — 59.90 Hz** (crt0 timing kept; 96 VBL ticks per 120 Handy frames, i.e. Handy paces 75 fps but the emulated timer 2 runs 159 µs × 105 lines). maketables.py uses 59.90 Hz | M3 | — |
| Q4 | Handy's LFSR/integrate fidelity vs real Mikey | M6 | Curate tap presets on hardware; Holani core as a second opinion |
| Q5 | ✅ **RESOLVED at M10a** — `.lnx` byte 60 = 1 → Handy emulates a 93C46; cc65's `lynx_eeprom_write` is byte-exact in the persisted file; the file flushes on `retro_unload_game` (harness now calls it) and loads on the next run — **full power-cycle round trip proven**. Caveat: through cc65's read routine Handy returns only the low byte per cell (high byte constant junk) — treat cells as byte-wide until the custom 93C86 driver lands (M10b); cc65's driver is silicon-proven, so this is likely a Handy artifact — recheck on hardware | M10 | — |

## 15. Deliverables & toolchain

`make` → `build/alynxdj.lnx` (cc65: `cl65 -t lynx`, project-local cfg as the
cart layout grows); `make shot` → headless Handy screenshot + audio WAV
(the audio capture is the FFT-verification path for every sound milestone,
ported practice from GENMDDJ). Python tools: `makefont.py` (4×6 font),
`maketables.py` (note/BPM tables for 75 Hz), `alynxdj_sample.py` (pool).
Build stamp: git hash on the boot splash (ported; catches stale flashes).
Truth on real hardware via an SD cart; Handy is dev-speed, not silicon.
