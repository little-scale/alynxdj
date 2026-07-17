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

Milestone rows retain the implementation history. **M13 supersedes the
fixed channel-C/channel-D sample-bus details recorded in M7/M8.**

| M | Milestone | Proves |
|---|---|---|
| M0 | ✅ **DONE** — toolchain: cc65 lynx target builds a bootable `.lnx`; headless Handy retroshot harness (PNG + audio WAV capture), `make`/`make shot` | The pipeline |
| M1 | ✅ **DONE** — boot: palette + SCRBASE + framebuffer at $A000 (alynxdj.cfg RAM map), 4×6 font (makefont.py, 40×17 grid, legibility screenshot-verified), splash with version + git-hash build stamp, VBlank IRQ (timer 2) + live frame counter. **Q3 resolved: VBL = 59.90 Hz with crt0 timing** (96 ticks/120 Handy frames) | The board comes up |
| M2 | ✅ **DONE** — pad read (raw + edge; DAS repeat deferred to the M4 editor), pad→note squares on channel A, **FFT-verified 220.0/293.0/440.0 Hz** from the WAV capture. Proven square recipe: `FEEDBACK=$01, SHIFT=0` (Mikey shifts in inverted tap parity, so it self-starts from 0), `f = 1/(2·(BKUP+1)·clock)` exact. RetroPad→Lynx button map probed and recorded in CLAUDE.md; `$C000` debug mirror + RAM-dump verification pattern established | The sound path |
| M3 | ✅ **DONE** — note table (maketables.py: 96 notes C-1..B-8 as clocksel+BKUP pairs, worst error 9.5 cents at F#8 only), engine pipeline (groove → row → trigger → AHD envelope → shadow → flush, voice-0 scope), A = play/stop. **FFT-verified**: 16-row C-major arp plays all pitches within cents (261/329/393/523/658/786 Hz), 100 ms rows, clean loop. Flush discipline: full channel reprogram on trigger only; envelope ticks are volume-only writes (never restart the oscillator) | The sequencer |
| M4 | ✅ **DONE** — PHRASE editor: 16-step grid (row/note/instr/cmd columns), inverse-video cursor + DAS key repeat, A-hold+dpad edit (±semitone/±octave, verified: C-4→C#4 auditioned at 278 Hz), A-tap-on-release insert (chord-consumption rule ported), B-held+A transport with PLAY/STOP label + accent playhead. All verified headless via scripted BTN input + RAM mirrors ($C001/2 cursor) + FFT. **Boot-to-mainloop ≈ 124 frames (cc65 C render, D2 risk on record — optimize the clear/grid paint when it matters)** | First playable build |
| M5 | ✅ **DONE** — flat song block (`struct songdata sd`: 128×4 song, 32 chains, 64 phrases, $FF sentinels), per-track walkers (song→chain→phrase, chain-end song-row advance, wrap-once loop), contextual transport (SONG=play from cursor row / CHAIN=loop chain / PHRASE=loop phrase), SONG+CHAIN screens with shared cursor/edit/insert machinery, B-held+L/R screen nav with drill-down (loads chain/phrase under cursor), B-tap back, per-screen playheads, src/tracker.h single-source data model. **FFT-verified**: 3 tracks play together (bass C-2 + odd harmonics, arp, G-5 blips); chain step 2's +12 transpose audible (C4 vanishes, C6 appears). Nav verified via screen-id RAM mirror | Song structure |
| M6 | ✅ **instruments DONE** — 16-byte records in the song block (type/vol/AHD/timbre + reserved table/pan/fine), per-instrument AHD latched at trigger (atk 0 = instant, dcy 0 = sustain), LFSR timbre banks (8 tone + 8 noise feedback presets — hardware curation stays Q4), INSTR screen (PHRASE→INSTR drill, field edit w/ audition). **Verified**: NOISE hats on track 4 are spectrally noise-like (top-bin share 0.018), pulse at 4.8 Hz as sequenced, bandwidth follows the note clock (C-6 → ~3.9 kHz shift rate — hiss wants higher notes); TYPE field edit TONE→NOISE on-screen. ◻️ **tables + TABLE screen + shared command executor → folded into M9** (one command pass) | The voice model |
| M7 | ✅ **core DONE** — alynxdj_sample.py (WAV→8-bit signed PCM @7812.5 Hz, silence-trim + budget caps: the 808 kit fits 12.1 KB RAM-resident, linked into the ROM), src/pcm.s-in-irq.s (timer-7 IRQ feeds channel D OUTPUT, ~45 cyc/byte ≈ 9% CPU, APPZP pointer, self-stopping), IT_KIT (note semitone → kit slot, mono sample bus on channel D, SMSGGDJ T3 policy). **Verified: captured audio cross-correlates with the source 808 WAVs at 1.11 (BD) / 0.83 (SD) / 0.69 (HH); full 4-track mix keeps all melodic components (engine unstarved).** **streaming DONE**: alynxdj_pool.py packs all 8 sample folders (102 KB, full quality) at cart block 40 (256 KB cart image); src/cart.s block-select/sequential reader (from the cc65 recipe); 1 KB RAM ring at $D000, main-loop pump (3×64 B/frame vs ~131 drain), IRQ consumes with hold-on-underrun; KIT instruments pick a kit via BANK. **Verified: streamed 808 xcorr 0.91/0.66/0.61 (better than the RAM-resident era); BANK→909 discriminated by snare xcorr.** Freed ~8 KB of MAIN. `S` shipped in M9c. ◻️ hardware: Q2 cap + real-cart streaming pass | The flagship — samples |
| M8 | ✅ **core DONE** — IT_WAV = **hardware triangle** (integrate mode + tap-11-only feedback: inverted parity cycles the zero shifter through 12 ones/12 zeros → triangle at shiftrate/24; note lookup +43 semitones via the extended 139-entry table; instrument vol ≤10 or the 8-bit accumulator wraps). **Instrument pan**: pan nibbles → per-channel ATTEN at trigger, PAN reg enabled, write-always (D8). **Verified solo: 786 Hz for G-5, H2 0.002 / H3 0.113 (textbook triangle), R/L rms 3.75 = exactly the $4F ATTEN ratio — Handy models Lynx II stereo.** **wavetables DONE**: 32-byte tables in the song block (8 waves, factory tri/saw/square/pulse), timer-5 IRQ loops them through channel C's DAC (note→clock/BACKUP/step from maketables, step-doubling keeps the feed ≤ ~12.5 kHz), WAV instruments pick a wave (-- = hardware triangle), bar-graph WAVE screen above INSTR (map W lit). **Verified: factory saw plays C-4 at 264 Hz with H2/H3/H4 = 0.50/0.32/0.23 (theory 0.50/0.33/0.25)**; `E xy` live envelope re-slope also landed (dcy-4 instrument re-sloped to 1.6 s, prelisten-verified). `O` shipped back in M9 | Synthesis depth |
| M9 | ✅ **commands+tables DONE** — grooves pool + shared executor + 11 commands (`A C D G H K O P V W X`): tables (16×16 vol/tsp/cmd/param, 1 row/tick, `H` loop, stick-at-end), 1/16-semitone pitch engine (bend/vibrato/chord/table-tsp resolved to interpolated BACKUP, reload-only writes — no phase restarts; clock-boundary crossings do a full retime), `D` delayed trigger via the peek, `W` row shorten, `G` groove switch, `O` live pan, `X` env-peak accent, `K` tick-kill. PHRASE cmd/param columns + TABLE screen + INSTR TABLE field. **Verified by rig battery**: K 100/33 ms alternation, P +3.1 semis/0.45 s, V ±0.5 semis, C & A-table arps spectrally present, G rate switch. (`D` code-complete, no rig yet.) **M9b DONE**: single-field clipboard (A-held+B = cut, A double-tap = paste; PHRASE command cuts/pastes carry CMD+PARAM without touching NOTE+INSTR; unmatched double-tap = mint-next-free / clone on SONG & CHAIN cells), **mute/solo** on the Option-1 layer (O1+A mute, O1+B solo, O1+L/R track select; engine mute mask applied in both flush paths; top-bar 1234 flags), **GROOVE screen** (below CHAIN, L/R pages grooves, gpos playhead, map G lit). Verified: field cut/paste and command-row preservation via song-block RAM dumps; mute/solo by FFT (arp −23 dB when muted, bass −12× when arp soloed — watch the transposed-pass frequency aliasing when testing!). **M9c DONE**: `F` finetune, `L` slide (glide-in via a decaying pitch offset; found+fixed a cc65 signed-vs-uchar comparison quirk), `N` live LFSR-taps morph (the D11 payoff as a command), `R` retrig (envelope re-key + PCM refire, −8x fade), `S` live PCM rate (TIM7BKUP write — pitches a playing sample, resets at next trigger), `Z` probability (16-bit Galois roll in the trigger peek, seeded at play-start), phrase-level `H` (ends the phrase after its row — 1-row-cost simplification of the sibling zero-cost hop). 17 commands total. **Rig-verified**: L climbs 268→500 Hz, R pulses at exactly 20 Hz, F +48 cents, N spectral cosine 0.083, S 136/81 ms alternation w/ reset, H 500 ms 5-row loop. **M9d block ops DONE**: A-held+B long-hold = row-range SELECT on SONG/CHAIN/PHRASE (inverted row numbers mark the range; ↑/↓ extend, A copy, A+B cut, B cancel, A double-tap pastes at the cursor; verified by RAM diff — arp rows 0-3 duplicated to 8-11). **COMPLETE** — `T` (BPM→flat groove, verified 234 ms rows for T78), `I` iteration mask + `J` pass-transpose via per-phrase play counts (reset at play-start, counted at each pass's last row; verified: I55 fires every 3.2 s, J71 = [7,0,0,0,7] across passes). **The full command set (22) is in.** | Editing power |
| M10 | ✅ **DONE (Q1 ✅ Q5 ✅)** — custom 93C86 driver (src/eeprom.s, 13-bit commands; **full 16-bit reads work** — the M10a low-byte quirk was cc65-driver-specific), RLE packer + ALDJ header + checksum (src/save.c), FILES screen (SAVE/LOAD, PACK meter, status; B+Down from SONG, F lit in the map), boot autoload. **Round trip checksum-verified across power cycles; the demo packs to 480 of 1016 bytes.** **Full 2 KB capacity live** (repo-built core fixes the stock eeprom.cpp:59 load truncation — `tools/emu/handy-alynxdj.patch`; boundary-crossing 1784-byte save verified across power cycles). Pack-ratio lever: **FF-FF empty-chain sentinels** (FF 00 alternation defeated RLE: 1469 B → 480 B). SAVEFORMAT.md written. ◻️ deferred: browser savetool, multi-slot | Songs survive |
| M11 | ✅ **emulation DONE — two-unit ComLynx lock verified** — src/sync.c driver (timer-4 bit clock, polled SERCTL/SERDAT, 1-byte ROW/START/STOP protocol), OUT master (transport + row clocks), IN slave (clock-driven rows, groove ignored), SYNC on FILES. Root-caused a **cc65 lynx.h bug** on the way (`_UART_TIMER` = $FD14 = timer 5, not timer 4's $FD10 — the driver was configuring the wrong timer). Verified on the repo-built core: single-unit loopback = START + 31 row clocks in 3.1 s (exact); **duoshot** (two bridged core instances via new `handy_comlynx_*` exports): slave started remotely and clocked to **0 ms onset skew, −1.4 ms envelope lag** over 5 s. ◻️ remaining: hardware pass (real cable), IN24 bridge mode, cross-family via ESP32 |  Sync |
| M12 | 🔶 **PROJECT + OPTIONS screens DONE** (TMPO live-BPM readout + rung-stepping edit that preserves swing, free-block counters; SYNC moved to OPTIONS + PRELIS + key-REPEAT settings; all ten map positions live). **Engine tick moved into the VBlank IRQ** (cc65 zp block saved around the C call; PCM/wave IRQs nest during the tick; drum triggers latch and execute from the main-loop pump; transport/audition sit in sei/cli windows) — **tempo is now render-independent, verified: onset intervals identical under redraw hammering (236.7 vs 237.7 ms means)**. Map rows navigate horizontally (F↔G, O↔P↔W); OPTIONS PALETTE row (8 SMSGGDJ-style schemes, live). **Config persistence** (EEPROM cells 1020-1023, saved on OPTIONS edits, loaded at boot — verified: palette + sync survive a power cycle; song cap now 2032 B) and **channel activity meters** (right column, engine-exported levels, dirty-cached) DONE. **LIVE mode DONE** (Option 2 on SONG = clip launcher: B+A queues chains per track, bar-quantized launches — verified 11 ms grid alignment; empty cell = queued stop, verified track-0 arp stops at its boundary while the bass loops on; first launch defines the grid). **Demo song composed** (8-bar swung Am arrangement exercising taps/seed timbres, wavetable pads, both drum kits + speech, and the C/L/V/R/Z/J/N/F/S commands; rigs kept at rows 16+). ◻️ remaining: control hints, hardware pass | Release-ready |
| M13 | ✅ **SYMMETRIC SAMPLED-VOICE STABILIZATION** — KIT and table-WAV now route to the owning physical channel A–D through two dynamic timer slots (7/5), with oldest-first stealing on the third sampled trigger. The 1 KB ring is split 2×512 B; independent cart cursors re-seek every chunk and reject stale IRQ requests. Fixed KIT `R`/`K`, same-row `S`, sampled mute/meters, and 16-bit steal-clock wrap. Cold-loaded code overlays at `$C900`/`$F600` preserve the full editor/song model. `make test` proves A/B/C/D offsets, the two-slot cap/steal order, retrigger/rate behavior, non-silent DAC output, and a 1233/2032-byte EEPROM power-cycle checksum round trip. | D1 symmetry is real, not just a data-model promise |
| M14 | ✅ **TONE PATCH MODULATION + DIRECT AUDITION** — SWP/VIB/TRM now occupy the three reserved instrument bytes for TONE/NOISE: signed 1/16-semitone sweep, packed speed/depth pitch vibrato, and packed speed/depth tremolo inside AHD. Phrase/table `P` and `V` still override patch defaults. Save v4 clears the bytes for older songs. A stopped INSTR screen auditions the selected patch on physical B without starting transport. A third cold-code tail keeps the 16-byte record, 7680-byte song, dual PCM rings, and 2032-byte save budget intact; the pool moves to cart block 45. `make test-tone` audio-verifies all modulators and scripts the physical-button gesture. | Patch identity survives outside phrase commands |
| M15 | ✅ **INSTRUMENT TSP + SMOOTHER VIBRATO** — byte 15 is now signed TSP for all instrument types (semitone/octave editing; pitch/pad mapping at key-on, safe note-range clamp), with save v5 migration. VIB uses a centred 16-point sine and 16 distinct ~0.47–7.49 Hz rates, guaranteeing ≥8 pitch updates/cycle; its phase free-runs across notes and resets with transport, removing the short-note tuning bias exposed by the demo's `V26` lead. The sample directory is fetched five bytes on demand, releasing 320 bytes of HIRAM; HICODE3 expands to `$F320–$F5FF` while the song, 640-byte stack, sample rings, and save payload remain unchanged. Audio regressions prove +12 = one octave, phase-centred retriggers, and the updated INSTR form/audition. | Patch-level pitch identity without 60 Hz LFO aliasing |
| M16 | ✅ **DOUBLE-TAP MINT / SLIM CLONE** — physical B double-tap now remembers whether tap one filled an empty SONG/CHAIN cell, so tap two mints the lowest blank and unreferenced chain/phrase instead of cloning the remembered value just inserted. Occupied SONG cells copy one chain while sharing phrases; occupied CHAIN cells copy one phrase while sharing instruments and preserve the chain-step TSP. Cursor/screen movement cancels the tap context. Four scripted RAM regressions cover empty/occupied behavior at both hierarchy levels and prove that allocated-but-blank entries are skipped. | Fast variation without accidental deep song duplication |
| M17 | ✅ **HARDWARE LISTENING PASS FIXES** — NEW defaults TONE/ATK0/HOLD5/DCYA; empty CHAIN transpose displays 00 while retaining the FF-FF packed sentinel; TBS 0–F added in HOLD's high nibble with note/tick semantics and save v6; table VOL no longer revives decay; G slowed to 1/16 tap/tick and B repurposed as signed current-taps offset. The PCM pump atomically snapshots/publishes its 16-bit ring pointers, and 93C86 EWEN now matches cc65's canonical command for strict SD-cart emulators. `make test-hardware` proves table clocks/lifetime, taps rates, and distinct sample lengths. | Silicon feedback closes the emulator blind spots |

Commit at each milestone boundary. Hardware-verify **M6 (LFSR timbres)** and
**M7 (PCM feed)** early — Handy is a 20-year-old core; its polynomial-counter
and DAC timing fidelity is exactly where emulation lies (Q4). Holani is the
second-opinion core.

## Top risks

- **The 2 KB EEPROM ceiling** — Q1 is resolved and power-cycle-tested, but
  the current factory image already uses 1265/2032 packed bytes. Keep the
  FILES meter and PURGE workflow honest; never truncate.
- **PCM IRQ jitter/cost (Q2)** — 2 voices at 8 kHz ≈ 20 % CPU in IRQs on a
  4 MHz 6502; display DMA steals make jitter worse. Mitigate: asm handler,
  two asm-fed rings; the policy/regression is fixed, final margin is a
  hardware stress pass.
- **Handy fidelity (Q4)** — LFSR tap timbres and integrate mode may sound
  different on silicon. Mitigate: hardware pass at M6/M7, curated preset
  list only after silicon listening.
- **cc65 editor sluggishness (D2)** — if screen paints lag, move the dirty-
  cell renderer fully to asm before blaming the design.
- **64 KB code packing** — after M17, MAIN has 7 bytes free,
  HICODE1/HICODE3 have 16/26, HICODE2 has 1, and SONG/MIRRORRAM are exact fits; the C stack
  is 512 bytes. Growth needs measured placement/reclamation, not an
  unplanned resident helper.
- **Scope creep** — echo (`Q`), user instrument bank, ComLynx song exchange
  are explicitly stretch (M11+/post-1.0). The FM-editor lesson from GENMDDJ:
  one new large surface per project; here it's the PCM layer, nothing else.

## Status

**M0–M16 implemented as of 2026-07-17.** The tracker has all ten screens,
the full command/editor workflow, ComLynx OUT/IN, packed 93C86 persistence,
four genuinely symmetric logical/hardware tracks, persistent TSP on every
instrument type, and TONE/NOISE SWP/sine-VIB/TRM patch modulation. Two sampled
voices are the deliberate CPU cap,
not fixed channel roles. Remaining 1.0 work is the focused two-stream/LFSR/
ComLynx-cable silicon pass, control-hint polish, and the upstream Handy/cc65
fixes.
