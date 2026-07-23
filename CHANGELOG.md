# Changelog

## Unreleased

## v0.51 — 2026-07-24

- Added non-destructive waveform trimming to the standalone sample browser.
  Every pad has draggable IN/OUT handles with live start, end, and selected-
  length readouts; audition, gain/tanh processing, portable-bank export, and
  patched-ROM export all use the selected region. Revert restores the full
  source sample.
- Added the canonical `samples/alynxdj-factory-samples.bin`: a complete,
  portable 64-slot `PL` bank injected verbatim by the ROM build. The sample
  browser can import/export the same `.bin`, and `make SAMPLE_BANK=...` validates
  and injects a custom bank so sample sets carry cleanly between releases.
  `make factory-samples` deliberately rebuilds the factory binary from WAVs.
- Moved the MIDI/live helper from cart block 200 to the final blocks 254–255.
  The sample region is now a contiguous, protected 214,016 bytes at blocks
  45–253; browser ROM export clears only that region and can no longer overwrite
  MIDI code. The browser also detects older block-200 MIDI ROMs and preserves
  their smaller sample boundary. The per-sample artificial 16,000-byte cap is replaced by the
  format's u16 maximum of 65,535 bytes (~8.39 seconds).
- The standalone sample patch browser now provides per-pad gain from −24 to
  +24 dB and optional tanh drive. Both existing ROM samples and replacement
  WAVs can be processed non-destructively; audition, waveform preview, and
  patched ROM export all use the same resulting signed 8-bit PCM.
- Fixed cartridge seeks across 1 KB pages. The seek-side 16-bit remaining-byte
  counter borrowed after decrementing its low byte, inflating `$0400` to
  `$04FF`; samples crossing a page could therefore replay bytes from the
  preceding page before resuming. Kit-00 F4 now streams its complete 1,822-byte
  mid-conga continuously across both page boundaries, with a byte-exact ring
  regression guarding the path.
- Removed the remaining single-sample cart-refill bottleneck: the active voice
  now retains the sequential cartridge cursor between chunks instead of
  re-selecting its page and discarding up to 1,023 bytes every frame. Two
  simultaneous samples still re-seek when ownership alternates. Refills are
  published in 64-byte pieces, continue immediately across the 512-byte ring
  wrap, and are topped up after playhead redraws, preventing the IRQ from
  holding one DAC value through a missed refill. The `NP` ROM now shows
  `NPxx`, where `xx` is the live DAC-underrun count, and regression requires
  kit-00 F4 to finish with both slot counters at zero.
- PHRASE note entry now remembers the last instrument number explicitly edited
  in the instrument column and assigns it to every subsequently placed note.
  Command/parameter B double-taps are field-safe: they paste a command pair or
  clear only CMD+PARAM, and can no longer overwrite NOTE+INSTR through a stale
  full-row clipboard.
- Reduced table-WAV's normal-range timer-interrupt target from ~12.5 kHz to
  ~6.25 kHz after real hardware showed a tempo drop when the demo's WAV pad
  and 7.8 kHz drum stream overlapped on rows 04–05. Higher WAV notes now skip
  2/4/8 table entries per interrupt, retaining pitch while trading waveform
  resolution for dual-DAC CPU margin; KIT/sample rate is unchanged.
- Changed TONE/NOISE instrument TRM from a triangle-shaped attenuation to a
  descending saw: every cycle begins at the live envelope level, ramps toward
  silence according to depth, then snaps back to the top. The existing packed
  speed/depth byte, rate range, envelope interaction, and save version remain
  unchanged.
- Added a distinct `alynxdj-no-meters.lnx` hardware diagnostic build (`make
  meter-test`). It retains normal engine/audio level calculation but skips all
  right-edge channel-meter redraws and shows `NM` in the top bar, allowing a
  controlled test for main-loop rendering pressure on real hardware.
- Added a deeper `alynxdj-no-meters-no-peaks.lnx` sample-timing diagnostic
  (`make sample-timing-test`). Its `NP` marker identifies a build that also
  compiles DAC peak measurement out of both timer-IRQ sample feeders and skips
  the engine-tick peak snapshot, isolating that overhead without changing the
  normal ROM or the earlier `NM` comparison ROM.
- Boot now starts with a clean NEW song when EEPROM has no valid save; valid
  saves still autoload. FILES adds a confirmed DEMO action immediately above
  PURGE, restoring the factory song in working RAM without touching EEPROM.
- `G` now has a hybrid signed tick/row period. Magnitudes 1–7 move one tap
  after 1–7 tracker ticks (`G01`…`G07`, with `GFF`…`GF9` downward); magnitude
  8 starts the row range at one row and later values add one row (`G08` =
  +1/row, `G0B` = +1/4 rows, `GF8`/`GF5` downward). Each command restores the
  active instrument's stored TAPS and leaves it audible for one complete
  selected period before moving. Ordinary notes preserve the live value and
  partially elapsed countdown, while `G00` resets and stops without reseeding
  the LFSR. Positive/negative, loop-reset, and note-continuity regressions
  cover both timing ranges.
- VIB now uses SMSGGDJ's proven nonlinear depth curve while retaining
  ALYNXDJ's smoother sine, slower rate range, transport-scoped phase, and
  key-independent semitone pitch. Low values remain fine; `8` reaches
  ±10/16 semitone and `F` reaches ±3.75 semitones.
- WAVE selection now requires physical-A-held + left/right; unmodified
  up/down no longer changes the current wave.
- NEW instruments now default to TONE, VOL `7F`, ATK `0`, HOLD `5`, DCY `5`,
  and TAPS `001`.
- HOLD `F` is now indefinite sustain until retrigger, `K`, transport stop, or
  live MIDI Note Off; HOLD `0`–`E` retain timed behavior.
- The INSTR TAPS strip now uses nine dim/accent solid blocks in tap order,
  matching the channel meters while retaining the exact hexadecimal value.
- PHRASE and TABLE command selection now steps alphabetically in both
  directions: `A B C D E F G H I J K L N O P R S T V W X Z`. Stored command
  IDs are unchanged, so existing songs and save-format v6 remain compatible.
- Regression ROMs, RAM dumps, screenshots, and audio now live in per-suite
  `build/tests/` directories which are replaced on each run, keeping the
  canonical `build/` outputs uncluttered.
- A clean physical-A tap is now inert on OPTIONS and PROJECT, matching CHAIN
  and PHRASE; those screens use physical-A-held plus D-pad for map navigation.
- Added ComLynx USB-MIDI takeover: MIDI channels 1–4 play tracks A–D with
  instruments 01–04 through the ordinary engine, with no heartbeat timeout.
- Added `IN24` sync. The Pico divides standard 24-PPQN USB MIDI Clock into one
  ComLynx pulse per tracker row and forwards Start/Continue/Stop. Existing
  row-level Lynx-to-Lynx `IN` remains unchanged.
- `IN` and `IN24` transport now arms the user-selected row without sounding
  and displays `WAIT`. The first row pulse starts that exact row and changes
  the display to `PLAY`; an incoming Start does not overwrite a local cue.
- Added the standalone `pico-midi-comlynx/` RP2040 firmware. It enumerates as
  a USB-MIDI destination, forwards notes and row-rate clock/transport
  together, and generates ComLynx's 62.5-kbaud 11-bit open-drain framing in
  PIO.
- Documented the one-Pico/one-Lynx prototype interface: 470 ohm series plus a
  BAT54 clamp to 3V3, common ground, ComLynx +5 V disconnected. Jaycar
  BAT46/BAT48 and 1N5819 parts are listed as practical substitutes, while
  1N5711 is measurement-only and 1N4148 is explicitly rejected for inadequate
  RP2040 clamp margin.
- Added the standalone `song-file-viewer.html` for validating, viewing,
  editing, and exporting complete ALYNXDJ EEPROM/SRAM song images locally.
- Documented the ElCheapoSD hardware boundary: its physical 93C46 provides
  only 128 bytes, so it can run ALYNXDJ but cannot persist the tracker's
  2 KB 93C86 song format. Its custom API is menu-oriented rather than general
  SD filesystem access, so there is no full-song ROM-side fallback.
- Added precise viewer diagnostics for 128-byte and FAT-directory-data `.sav`
  files produced by incompatible SD-cart handling.

## v0.5 — 2026-07-18

Hardware polish and sample patching: this release folds the first focused
hardware listening pass into the tracker and adds a self-contained browser
tool for replacing the ROM's sample kits.

- Added a self-contained `sample-patch-browser.html`: it loads a built ROM,
  validates the block-45 pool, replaces/auditions individual WAVs or full
  kits, enforces sample and cart capacity, and writes a patched `.lnx`
  without uploads or external dependencies.
- Hardware-pass follow-up: NEW instruments now default to TONE, ATK 0,
  HOLD 5, DCY A; empty CHAIN rows present their internal `$FF` transpose
  sentinel as `00`.
- Added instrument **TBS** without growing the 16-byte record: HOLD's high
  nibble stores table speed. TBS 0 advances once per note; 1 is fastest at
  one row per tick; 2–F are progressively slower. Save format is now v6.
- Row-clocked `G` tap glide so `G01` moves exactly one tap value per
  sequencer row regardless of groove/swing; `B` is a signed one-shot offset
  from current taps.
- Hid and disabled the unused BANK row for TONE/NOISE instruments; it appears
  only as WAVE for WAV or KIT for KIT.
- Physical A tap no longer backs out of CHAIN or PHRASE; navigation there is
  consistently A-held plus d-pad.
- Drilling from PHRASE to INSTR now explicitly follows the valid instrument
  assigned to the selected row and retains the previous instrument on an
  empty or invalid row.
- Table VOL now shapes attack/hold but yields permanently at decay, so a
  looping volume table cannot override HOLD/DCY or keep a note alive.
- Fixed real-hardware PCM ring races by atomically snapshotting the IRQ tail
  and publishing the 16-bit head/done state together; samples retain their
  individual directory lengths rather than crossing buffer boundaries.
- Aligned 93C86 write-enable with cc65's canonical all-ones special-command
  pattern for stricter SD-cart EEPROM emulators, addressing save files that
  were created but reloaded as the demo.
- Added a hardware-fix regression covering TBS clocks, finite table-volume
  envelopes, row-clocked G, signed B, and long/short sample boundaries.

## v0.4 — 2026-07-17

Four-channel sampling and expressive instruments: this release restores
Mikey's channel symmetry while adding performance-focused tone modulation
and faster tracker editing.

- Fixed PHRASE command-field cuts so they remove only the command and its
  parameter, preserving the row's note and instrument. The clipboard now
  pastes that command pair only into another command column. Added scripted
  cut and cut/paste RAM regressions for the physical-button gesture.
- Fixed physical **B** double-tap on an empty SONG/CHAIN cell: the first tap's
  immediate remembered-value insert no longer makes the second tap mistake
  the cell for occupied. Empty cells now select the next blank, unreferenced
  chain/phrase (so an allocated-but-unedited object is never reused);
  occupied cells make a slim clone (chain only or phrase only, with referenced
  phrases/instruments still shared). Added four RAM-level input regressions,
  including allocated-but-blank pool entries.
- Added signed **TSP** to the INSTR screen. It transposes TONE, NOISE, and
  WAV pitch and moves KIT pad selection; Left/Right step a semitone and
  Up/Down step an octave. Save format v5 clears the formerly-reserved byte
  when loading older songs.
- Replaced pitch vibrato's coarse triangle with a centred 16-point sine LFO.
  Its 16 speed settings now span approximately 0.47–7.49 Hz, guaranteeing at
  least eight 59.9 Hz pitch updates per cycle at the fastest setting.
- Vibrato phase now free-runs across note retriggers and resets only at a
  transport boundary. This fixes the demo's channel-1 `V26` lead: short notes
  no longer restart on—and repeatedly sample—the sharp half of the sine.
  A repeated-note audio regression verifies that `VIB=00` stays at base pitch
  while successive `V` commands remain centred together.
- Added TONE/NOISE instrument fields **SWP**, **VIB**, and **TRM** using the
  record's three reserved bytes: signed per-tick pitch sweep, packed
  speed/depth vibrato, and packed speed/depth tremolo inside the AHD level.
  Save format v4 explicitly clears these fields when loading older songs.
- The sample pool's five-byte directory entry is now fetched on demand in
  the main-loop trigger path instead of keeping all 320 bytes resident. This
  makes room for the sine/TSP helpers without touching the IRQ audio path.
- A physical **B** tap on INSTR now auditions the current instrument when
  transport is stopped, without starting playback or depending on PRELIS.
- Added an audio/input regression covering all three modulators and the
  stopped-INSTR audition gesture.
- Restored the Lynx's channel symmetry for sampled sound: KIT and
  table-WAV voices now route to the owning track's DAC on channels A–D.
  Two shared timer slots run concurrently; a third sampled trigger steals
  the oldest, regardless of which logical track or sampled type owns it.
- Fixed KIT `R` retrigger, KIT `K` kill, same-row `S` rate changes, sampled
  mute/meters, and cart-stream races during rapid trigger/steal sequences.
- Split the 1 KB PCM ring into two independent 512-byte rings and made the
  cart pump re-seek each stream, including after EEPROM/config traffic.
- Added cold-loaded high-RAM code overlays so the full editor/song/save
  model still fits in 64 KB, plus deterministic `make test` regressions for
  four-channel DAC routing/stealing and packed-save power cycling.

## v0.3 — 2026-07-17

The first hardware-validated release — ALYNXDJ runs well on a real Atari
Lynx, and this cycle is the round of fixes and features that came out of
playing it on the device. (Emulator caveat carried over: this build of
libretro-Handy doesn't emulate the LFSR feedback, so TAPS/`N`/`G` are
register-level + hardware verified only.)

- **A/B buttons swapped** to match hardware ergonomics: edit/insert is the
  physical B button, back/navigate is physical A (every derived gesture —
  transport, cut, paste, block select, LIVE queue, the Option-1 layer —
  flips with them), done at the single input read in `main.c`.
- **Boot splash** (SMSGGDJ-style): the ALDJ logo centred, the version on a
  full-width inverted bar, and the build hash below, held ~100 frames
  (~1.7 s). The logo is 1-bit `art/aldj.png` downscaled to 144x38 by
  `tools/makelogo.py` and drawn in the palette's highlight pen, so it follows
  the selected (persistent) palette. VERSION is emitted into the build header.

- **New `B` command — set the WAV wavetable (0–7) live**, so a sustaining
  WAV note can switch timbre mid-phrase (verified: triangle→square FFT
  change). The INSTR **BANK** field is now labelled by TYPE (WAVE for WAV,
  KIT for KIT), making the wave number obvious to edit there.
- Editor gestures: on TABLE, hold A + up/down changes the table number;
  on WAVE, hold A + left/right changes the wave number. WAVE↔PROJECT
  screen travel removed. WAVE edited column drawn in the brightest shade,
  the rest mid. INSTR audition: hold A + tap B loops the last phrase seen.
- FILES: every action confirms with `SURE?` (tap once to arm, again to
  run); a lone back-tap no longer leaves the screen.

- **New `G` command — LFSR-tap glide** (D14): a signed per-tick sweep of
  the 12-bit tap value (`01`–`7F` up, `FF`–`80` down, `00` off), reclaiming
  the retired groove-switch slot. It writes the FEEDBACK register live each
  tick without reseeding the shifter, so the timbre morphs continuously.
  Register-level verified (the tap value advances and maps correctly);
  audible only on real hardware — this build of libretro-Handy renders all
  tap configs identically (LFSR feedback isn't emulated), so it can't be
  heard in the emulator.
- Palettes are shown on OPTIONS as a **number 0–7** (was scheme names);
  default is 0.
- Small fixes: PHRASE/TABLE close the gap between a command letter and its
  value; WAVE bars use the solid-block glyph.

## v0.2 — 2026-07-03

Editor/UX polish pass on top of the feature-complete v0.1. Still
emulator-verified throughout (retroshot headless harness).

- **Channel meters reworked.** Four full-height bars down the right-hand
  border, one per track, drawn as solid blocks with a dim track behind
  each so all four are always visible. KIT (PCM) and WAV voices now show
  a peak of the **real DAC output** (routed to the owning track) instead
  of the envelope — which for KIT was always zero, so drum meters were
  dead. TONE/NOISE keep the envelope level, which does scale their output.
- **FILES gains NEW and PURGE.** NEW blanks the song back to a clean
  slate (with a SURE confirm step; it doesn't touch the EEPROM, so your
  last save survives). PURGE drops chains no song row references, then
  phrases no surviving chain references, and repacks — the PACK meter
  shows the bytes reclaimed.
- **Eight palettes** ported from SMSGGDJ's COLR presets — WHT / WB / AMBR
  / CYAN / PINK / NEON / KIDD / MINT — shown by name on OPTIONS (was six
  numbered schemes).
- **Single global groove** (D13): the groove pool and the `G` command are
  retired; the GROOVE screen edits one groove that drives the whole song,
  and PROJECT's TMPO steps it. The command set is now 20.
- **INSTR screen regrouped** with blank rows between TYPE / envelope /
  LFSR / routing bands; the screen-map indicator moved left and down.
- Save slot fixed at one packed song per the 2 KB EEPROM budget (D12).

## v0.1 — 2026-07-03

First release: the initial development cycle, 2026-07-02/03 —
feature-complete against the design brief. Emulator-verified throughout;
the real-hardware pass is the road to v1.0. Everything verified headlessly (FFT /
cross-correlation / RAM-dump forensics via the retroshot harness);
details per milestone in [PLAN.md](PLAN.md).

### Since the first push
- Raw LFSR exposure per instrument (9-bit TAPS + 12-bit SEED) and the
  `N` live-morph command; nibble time-semantic envelopes + `E`
- Cart-streamed sample pool (all 8 kits, 256 KB cart image) replacing
  RAM-resident samples; 32-byte wavetables + WAVE screen
- Full 22-command set (`T`/`I`/`J` with per-phrase pass counts)
- PROJECT (TMPO rungs) + OPTIONS (sync/prelisten/repeat/palette) screens,
  block ops, LIVE clip-launcher mode, channel meters, config persistence
- Engine tick moved into the VBlank IRQ — tempo unaffected by rendering
- Repo-built emulator core (EEPROM fix + ComLynx bridge exports);
  two-unit sync lock verified at 0 ms; cc65 lynx.h _UART_TIMER bug found
- A composed demo song (swung Am groove touring the whole instrument)

### Sound engine
- Four voices on Mikey's four symmetric channels; per-tick pipeline
  (groove → row → trigger → AHD envelope → shadow → flush), reload-only
  pitch writes so the oscillator phase never restarts
- **TONE/NOISE**: the 12-bit LFSR fully exposed per instrument — raw
  9-bit TAPS mask + 12-bit SEED (D11); seed selects between disjoint
  state cycles (verified against a software simulation of the shifter)
- **WAV**: hardware triangle via integrate mode + tap-11 feedback
  (shiftrate/24, extended 139-entry note table)
- **KIT**: 8-bit PCM through the channel-D DAC at 7.8 kHz (timer-7 IRQ,
  ~45 cycles/byte); 808 kit converted from WAV sources, playback verified
  by cross-correlation against the originals
- 1/16-semitone pitch engine: bends (`P`), vibrato (`V`), chords (`C`),
  table transposes, with BACKUP interpolation
- Commands `A C D G H K O P V W X`; 16 macro tables (1 row/tick, `H`
  loop); 16 grooves; per-instrument stereo pan via ATTEN (Lynx II)

### Editor
- Screens: SONG, CHAIN, PHRASE, INSTR, TABLE, FILES, GROOVE + the
  right-column screen-map indicator (current screen highlighted)
- SMSGGDJ control scheme: held-button chords, no timing windows; DAS key
  repeat; drill-down navigation; contextual transport; playheads on all
  grid screens
- Cut / paste / mint / clone (single-field clipboard); mute/solo on the
  Option-1 layer with top-bar flags
- Prelisten on note entry and instrument edits

### Persistence
- Packed save (RLE + checksum) of the full song to the cart 93C86 EEPROM,
  FILES screen with pack-size meter, boot autoload
  ([SAVEFORMAT.md](SAVEFORMAT.md))
- Custom 10-bit-address EEPROM driver (full 16-bit reads, unlike the
  stock cc65 93C46 routine against Handy)
- Found upstream: libretro-handy truncates EEPROM file loads to 1024
  bytes (`lynx/eeprom.cpp:59`) — save capped at 508 words until fixed

### Toolchain
- cc65 build → headered `.lnx`; headless libretro-Handy harness
  (`tools/emu/retroshot`): screenshots, full audio WAV capture, scripted
  controller input, RAM/EEPROM dumps — no BIOS required
- Generators: 4×6 font, 139-entry note table (worst playable error
  9.5 cents), WAV→PCM kit converter with silence trim + budget caps
- Git-hash build stamp on the boot splash
