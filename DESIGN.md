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
| D5 | **Samples are 8-bit signed PCM streamed from cart; the owning track selects its own physical channel DAC.** KIT and table-WAV can therefore play on any of channels A–D, matching Mikey's symmetry. Direct 8-bit DACs × 4 are the Lynx's sample superpower (vs the SMS 4-bit log trick, vs GENMDDJ's single shared YM2612 DAC). | Fixed sample buses on C/D: simpler, but contradicts D1 and wastes Mikey's per-channel DACs. Software-mix N samples into one channel: more polyphony, too much hot-loop work. |
| D6 | **Timer-fed sampled voices are capped at 2 across KIT + table-WAV.** Timer 7 feeds slot 0, timer 5 feeds slot 1; each slot dynamically targets its owning track. A third trigger steals the oldest sampled voice. | Four concurrent streams because the chip has four DACs: hardware-valid, but the timer-IRQ CPU load would eat the editor and jitter the engine tick. |
| D7 | **4×6 pixel font, 40×17 character grid** (160×102). 8×8 gives only 20×12 — no room for a 16-row phrase grid + chrome. Hex-heavy glyphs designed first, sibling discipline. | 8×8 font (sibling look): too few rows; a paged UI would break the LSDJ feel. |
| D8 | **Stereo is written unconditionally (ATTEN/PAN/MSTEREO), audible on Lynx II, harmless on Lynx I (mono).** Same one-song-pans-where-panning-exists policy as the SMS/GG `O` split. | Gate on `MIKEYREV`: adds a probe for zero benefit — the writes are ignored on rev 1. |
| D9 | **Software text render into a single 8160-byte framebuffer; no Suzy blitter, no double buffer.** A text grid updates a few cells per frame; VBlank-window writes avoid visible tear. | Suzy sprite font: hardware-fast but drags in SCB management + Suzy/CPU bus handover for a workload that doesn't need it. |
| D10 | **The working song stays full-size in RAM; SAVE packs into EEPROM with a live "fits/doesn't" size meter** on FILES. A song too big to save is the user's tradeoff, surfaced honestly. | Hard-cap song size to guaranteed-packable: punishes every song for the worst case. |
| D11 | **The 12-bit LFSR is fully exposed per instrument: a raw 9-bit TAPS mask + a 12-bit SEED**, replacing curated timbre presets. User encoding is contiguous (bits 0-5 = taps 0-5, 6 = tap 7, 7 = tap 10, 8 = tap 11) so value-sweeping walks musically adjacent configs; the engine remaps to the scattered FEEDBACK/control layout at trigger. The seed picks the state-graph *cycle* — for many tap sets different seeds are genuinely different waveforms (verified: taps $0F1 seeds 000/555 → spectral cosine 0.05), and some (taps, seed) pairs hit the lock state and go silent, like the silicon. TONE and NOISE are now the same machine with different default taps. | Preset banks only (the M6 design): safe but hides the instrument that makes the Lynx unique. |
| D12 | **One save slot: the packed song owns the whole 2,032-byte EEPROM payload** (cells 1020–1023 stay reserved for machine config). FILES manages the working set instead: NEW (confirmed wipe) and PURGE (drop unreferenced chains/phrases to shrink the pack). Off-cart backup is ComLynx's job (a future SEND/RECV pair — 2 KB moves in ~0.3 s at 62.5 kbaud); emulator users swap `.eeprom` files for free slots. | Multiple slots (SMSGGDJ has 6 in 32 KB SRAM): halving 2 KB caps every song under half capacity — the demo alone packs ~1.2 KB. Revisit only if a real flashcart offers bigger persistent storage. |
| D13 | **One global groove, no pool.** The GROOVE screen edits a single groove that sets tempo + swing for the whole song; PROJECT's TMPO steps its entries together. The `G` (switch-groove) command is dropped from the editor menu (its CMD id is retained so every later command keeps its save encoding, and the engine executor is self-limiting to groove 0). | A 16-groove pool + `G` per-phrase (LSDJ/SMSGGDJ model): more rhythmic variation, but the pool UI and a live-mutable tempo source are complexity the Lynx build doesn't need — one swing feel per song is the common case. |
| D14 | **`G` repurposed as a live tap glide** (the retired groove-switch slot, D13). Row boundaries write FEEDBACK/control taps live without reseeding the shifter, so the LFSR keeps running while timbre morphs. Its final hardware-pass rate is signed whole taps per sequencer row (D18), wrapping the 9-bit value. Reuses CMD_G's id so every other command keeps its save encoding. | A brand-new command letter/id: cleaner semantically, but grows the command set and leaves a dead reserved slot. Reusing G keeps the count flat. |
| D15 | **TONE and NOISE instruments own persistent SWP/VIB/TRM modulation.** The three formerly-reserved record bytes hold signed 1/16-semitone-per-tick sweep plus packed speed/depth vibrato and tremolo. TONE and NOISE share this surface because both are the same Mikey polynomial oscillator; WAV/KIT show the fields disabled. Save v4 clears the new bytes when loading older songs. | Commands only (`P`/`V`) and tables: capable, but forces routine patch identity into phrase data and leaves no persistent tremolo. Applying the fields to sampled voices: their rate/volume paths have different costs and semantics. |
| D16 | **Instrument byte 15 is signed TSP for every type; vibrato is a centred, transport-scoped sine capped at ~7.5 Hz.** TSP resolves once at key-on before TONE/NOISE/WAV pitch or KIT pad mapping and clamps at the playable limits. The 16-point sine uses an 8-bit phase accumulator with 16 distinct ~0.47–7.49 Hz rates, so even the maximum has eight engine updates per cycle. Phase free-runs across note retriggers and resets at the transport boundary; depth zero neither changes pitch nor advances it. Save v5 clears the formerly-reserved TSP byte on older songs. | TONE-only transpose: inconsistent with the sibling instrument model and leaves pitched WAV/KIT selection behind. A faster sine: at the 59.9 Hz engine tick it would still staircase/alias. Per-note phase reset: deterministic, but short notes repeatedly sample one half-cycle and acquire a tuning bias. |
| D17 | **Instrument TBS occupies HOLD's formerly-unused high nibble.** TBS 0 advances the attached table once per triggered note without resetting its row; TBS 1–F restart at row 0 and advance every N engine ticks. The live row byte packs its countdown above the 0–15 playhead, so the 16-byte instrument record, voice RAM, and 2 KB persistence ceiling do not grow. Save v6 assigns the nibble; older writers already canonicalized it to zero. | Add instrument/voice bytes: clearer but impossible in the fixed save/RAM budgets. Always one row per tick: too fast and loses the sibling trackers' note-clocked table mode. |
| D18 | **Tap motion has complementary relative commands:** `G` is a signed whole-tap-per-sequencer-row continuous glide (`G01` = exactly +1 each row); `B` is a signed one-shot offset from the current taps value. Both update FEEDBACK and the scattered tap-7 control bit without reseeding. | Keep `B` as a live WAV-bank command: useful but narrow, while WAV bank is already persistent per instrument and hardware listening called for finer LFSR control. Tick-clock G: its musical distance changes with groove length and does not land predictably on row boundaries. |

Open questions: see §14 and PLAN.md — the two-voice software policy is
regression-tested; its final silicon timing margin and Handy's LFSR fidelity
still require focused hardware comparison.

---

## 1. Hardware constraints that drive the design

- **CPU:** 65C02 core in Mikey, ~4 MHz minus video DMA/refresh steals (~3.6 MHz
  effective). Comparable to the siblings' Z80 in throughput; poorer at 16-bit
  math — ROM lookup tables everywhere, no mul/div on hot paths (ported rule).
- **RAM: 64 KB unified, and everything lives in it** — code, song, framebuffer,
  stack. There is no runtime ROM: the cart is a block-addressed serial device
  read through `RCART`; most code is loaded by the standard loader, with three
  cold-code overlays copied from cart blocks 40, 42, and 44 into `$C900`,
  `$F600`, and `$F320` before use; the sample pool starts at block 45. The
  1 KB at `$D000` is split into two 512-byte sample rings; the framebuffer
  remains at `$A000` and the 512-byte C stack below it.
- **Cart:** 128–512 KB, read sequentially within a 1–2 KB block after a block
  seek. Fine for streaming sample PCM (D5); useless for random byte access —
  anything random-access lives in RAM.
- **Video:** 160×102, 4 bpp packed framebuffer (80 bytes/line), 16 pens from a
  4096-colour palette, ~59.9 Hz with the current crt0 timing, driven by timer 0 (HBlank) → timer 2
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
| **Total** | | | **7680 B** |

Empty slots are `$FF` sentinels (ported). "Blocks free" counter on PROJECT.
The block is contiguous and offset-stable — SAVEFORMAT.md will fix the map.

## 3. Control input *(ported control philosophy, remapped to the Lynx pad)*

The settled frame: **the button already held when another arrives selects the
action; no simultaneous-press timing windows** (the only windows are
sequential double-taps of B). Mapping uses the physical button labels:

| Lynx | Role (sibling equivalent) |
|---|---|
| **B** | item-level modifier — SMSGGDJ button 1: tap = insert/activate (on stopped INSTR: audition current patch), hold + D-pad = edit value, double-tap = paste/mint/slim-clone. Empty SONG/CHAIN cells mint the lowest blank and unreferenced chain/phrase; occupied cells copy only that chain/phrase, retaining its nested references. |
| **A** | project-level modifier — SMSGGDJ button 2: tap = back, hold + D-pad = screen-map nav, held + B = play/stop transport |
| **B held + A** | cut; + long-hold = block SELECT (ported gesture set). A PHRASE command-column cut carries only CMD+PARAM and preserves NOTE+INSTR. |
| **Option 1** | mute/solo layer: O1 held + D-pad L/R selects track, O1+B = mute toggle, O1+A = solo |
| **Option 2** | LIVE-mode page toggle on SONG |
| **Pause** | play/stop alias; double-press = panic (silence, abort PCM, re-arm sync) — ported NMI semantics, but on the Lynx Pause is a normal pad line read per-frame |

Key-repeat (DAS-style) ported from `input.asm`.

## 4. GUI layout and screens *(ported 2D screen map, new geometry)*

40×17 character grid (D7). Persistent chrome: top bar = screen name, song
title, BPM, play state, sync state, position `SS:CC:PP`; right column = screen
map indicator + 4 per-track activity meters (name + level bar, ported from
GENMDDJ M12).

Screen map (physical A-held + D-pad), same 2D shape as SMSGGDJ — OPTIONS above
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
  Per-instrument SWP/VIB shape pitch and TRM dips the live AHD level (D15);
  VIB is a centred, note-continuous sine at ~0.47–7.49 Hz (D16).
- **NOISE** — the same oscillator with a different default raw tap mask.
  Both types expose the full 9-bit TAPS mask and 12-bit SEED (D11).
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
volume, AHD (attack/hold/decay), table #, pan nibbles, finetune, raw TAPS and
SEED. HOLD's high nibble is **TBS** (D17), while its low nibble remains the
envelope hold time. Bytes 12–14 are TONE/NOISE **SWP** (signed 1/16 semitone per tick,
period-style direction: positive falls),
**VIB** (speed/depth nibbles), and **TRM** (speed/depth nibbles); phrase/table
`P` and `V` commands override the corresponding instrument state for that
note. Byte 15 is signed **TSP** in semitones for every type (D16), resolved
before pitch/pad selection and clamped to the playable note range. WAV uses
WAVE `$FF` for hardware integrate or 0–7 for a table; KIT uses the same byte
as kit bank 0–7 while the transposed note semitone selects its member.

On INSTR, physical B while stopped directly auditions the selected instrument
at the last-entered note without changing transport. Physical A-held+B keeps
the contextual phrase-loop transport, so patch preview and musical-context
preview remain distinct gestures.

## 7. Tables *(ported verbatim)*
16-row macro sequencer: vol, pitch(transpose), cmd, param columns; `A`
command one-shot override; `H` loop semantics identical to SMSGGDJ §7.
TBS 0 advances once per note and preserves the row, while 1–F advance every
N ticks and restart on note-on (D17). VOL can set the live level/peak during
attack or hold, but never writes during decay, so a looping table cannot
extend the envelope lifetime.

## 8. Command set *(ported, with Lynx re-aims)*

The implemented set is `A B C D E F G H I J K L N O P R S T V W X Z`.
The Lynx-specific or re-aimed commands are:

| Cmd | Lynx meaning |
|---|---|
| `G xx` | signed whole-tap-per-row sweep of the raw 9-bit tap value, without reseeding |
| `B xx` | signed one-shot offset from the current raw tap value, without reseeding |
| `N xx` | live low 8-bit raw-tap override (taps 0–5, 7, 10) |
| `O xy` | pan: ATTEN left x / right y nibbles — GG semantics, 16 levels per side, Lynx II audible (D8) |
| `S xx` | sample/wavetable **rate** — a real timer-reload pitch bend for PCM voices (not decimation) |

## 9. Groove *(ported timing model; one global groove, 59.9 Hz constants)*

## 10. Samples

- **Tool:** `tools/alynxdj_pool.py`: WAV →
  8-bit signed PCM at per-kit fixed rates, 8 slots/kit, self-describing pool
  directory baked into the cart image after the code blocks. The repo's
  `samples/` kits (808/909/C78/606 + 4 speech banks) are the factory pool.
  The main-loop trigger reads only the selected five-byte directory entry;
  the full 320-byte directory is not held in RAM.
- **Playback:** timer 7/5 feed two dynamic DAC slots, each targeting the
  owning track's `OUTPUT` register. Two independent 512-byte rings share the
  1 KB at `$D000`; the main-loop pump re-seeks before every 192-byte chunk so
  two cart streams and intervening EEPROM traffic cannot corrupt each other.
  The IRQ-owned tail is snapshotted atomically, and each completed chunk's
  16-bit head plus done flag is published under one brief IRQ mask; this
  removes real-hardware torn-pointer overwrites at ring wrap.
  Ring underrun holds the last sample until refill.
- **Budget:** at 8 kHz one PCM voice ≈ 8000 × ~45 cycles ≈ 10 % CPU; two ≈
  20 % (D6, measured properly at M7 = Q2).
- **No 4-bit log mapping, no DAC arbitration with tone duties** — each PCM
  voice owns a whole channel. The SMSGGDJ §10 machinery this replaces stays
  behind as documentation only.

## 11. Sync — ComLynx *(new transport, ported semantics)*

`SERCTL`/`SERDAT` UART, open-collector multi-drop. Implemented OPTIONS modes:
**OFF / OUT / IN**.
- **OUT:** master sends one byte per row (row-clock, the siblings' settled
  1-clock-per-row model) + transport bytes (start/stop/position).
- **IN:** slave row-advances per received row byte; grooves/`W` ignored
  (ported slave semantics).
- Multi-drop means one master can drive up to ~15 slaves — a Lynx orchestra
  is a headline feature; keep the wire protocol dumb (single-byte opcodes,
  no addressing) so it stays achievable.
- PULSE (analog Volca-style) does not exist here — ComLynx is digital-only.
  Cross-family (DE-9 ↔ ComLynx) sync happens through the ESP32 bridge, not
  by wire.

## 12. Persistence *(the Lynx-unique problem — D4, D10)*

- Working song: full-size flat block in RAM, RAM-only until explicit SAVE
  (GENMDDJ's settled persistence model).
- SAVE: RLE-pack the 7680-byte flat block into the 2032-byte song payload of
  the 93C86, with an 8-byte/four-word header (magic `ALDJ`, version, packed
  length, checksum). FILES shows a live packed-size meter (D10);
  SAVE refuses (with the meter red) rather than truncates.
- Current format is v6: instrument HOLD's high nibble is TBS, bytes 12–14
  are SWP/VIB/TRM, and byte 15 is TSP. Older writers always stored HOLD in
  0–15, so v5 and earlier naturally load as TBS 0 (D15–D17).
- LOAD: unpack EEPROM → RAM at boot if the magic checks (slot-0 autoload,
  ported policy).
- Hardware writes use cc65's canonical 93C86 EWEN bit pattern (all ten
  special-command address bits high), because SD-cart EEPROM emulators may
  decode it more strictly than the physical chip's don't-care definition.
- Emulator persistence is the core's 2048-byte `.eeprom` image. Browser
  tooling and ComLynx song dump/restore remain post-1.0 work.
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

Rules (ported): no mul/div on hot paths; note/curve/BPM tables live in RAM;
the PCM IRQ handler does not touch editor state; VBlank runs the engine while
the main loop handles input, cart-ring refill, and rendering.

## 14. Open questions

| Q | Question | Blocks | Path |
|---|---|---|---|
| Q1 | ✅ **RESOLVED at M10b** — the current v6 factory song + out-of-loop rigs RLE-packs to **1265/2032 bytes**. `make test` power-cycles a unique Handy EEPROM and verifies the song checksum. | M10 | — |
| Q2 | 🔶 **Software policy resolved:** exactly two timer-fed sampled voices, symmetric across A–D, third steals oldest; deterministic routing/rate/retrigger regression passes. Final silicon timing margin remains. | M7 | Stress on hardware with two long 7.8 kHz streams while editing |
| Q3 | ✅ **RESOLVED at M1 — 59.90 Hz** (crt0 timing kept; 96 VBL ticks per 120 Handy frames, i.e. Handy paces 75 fps but the emulated timer 2 runs 159 µs × 105 lines). maketables.py uses 59.90 Hz | M3 | — |
| Q4 | Handy's LFSR/integrate fidelity vs real Mikey | M6 | Curate tap presets on hardware; Holani core as a second opinion |
| Q5 | 🔶 **Software/emulator resolved; cart re-test pending.** `.lnx` byte 60 = 5 selects the 93C86; the custom driver and patched core power-cycle all 2048 bytes. M17 changed EWEN to cc65's canonical all-ones special-command pattern after an SD cart created a save file but reloaded the demo. | M10/M17 | Re-test SAVE → power cycle → autoload on the reporting cart; record cart model, firmware, and save-file size if it still fails |

## 15. Deliverables & toolchain

`make` → `build/alynxdj.lnx` (cc65: `cl65 -t lynx`, project-local cfg as the
cart layout grows); `make shot` → headless Handy screenshot + audio WAV
(the audio capture is the FFT-verification path for every sound milestone,
ported practice from GENMDDJ). Python tools: `makefont.py` (4×6 font),
`maketables.py` (note/BPM tables for 59.9 Hz), `alynxdj_pool.py` (pool).
Build stamp: git hash on the boot splash (ported; catches stale flashes).
Truth on real hardware via an SD cart; Handy is dev-speed, not silicon.
