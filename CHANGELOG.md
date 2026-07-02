# Changelog

## V0.1 (dev, unreleased)

Initial development cycle, 2026-07-02 onward. Milestones M0–M10 built and
verified headlessly (FFT / cross-correlation / RAM-dump forensics via the
retroshot harness); details per milestone in [PLAN.md](PLAN.md).

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
