# Changelog

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
