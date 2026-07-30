# Changelog

## Unreleased

## v0.58 — 2026-07-30

- Fix FILES → DEMO, SAVE, LOAD, and the live PACK meter racing the VBlank
  engine tick while `$C100` temporarily contains packed song data instead of
  MIDI/sync code. VBlank now defers the complete tick until both helper blocks
  have been restored; frame timing and IRQ acknowledgement continue normally.
- Make palette `01` the default when no valid OPTIONS config exists. Persisted
  palette selections still load unchanged.
- Extend editor and EEPROM regressions to compare the restored live helper
  byte-for-byte after DEMO, SAVE, and LOAD, and pin the clean palette default.
- Add opto-isolated 31,250-baud TRS/DIN MIDI input to the RP2040 bridge on
  GP13. Serial running status and interleaved real-time bytes are normalized
  through the same channel/clock dispatcher as USB MIDI; SysEx and System
  Common are safely ignored.
- Add a dedicated Waveshare RP2040-Zero Chipbridge build with ComLynx output
  on PCB `DATA1`/GP1, TRS MIDI input on GP13, and the active-high ready LED on
  GP7. Document the mandatory custom Lynx cable: ring-to-ring and
  sleeve-to-sleeve, with both tips disconnected and insulated so the Lynx's
  +5 V tip never reaches PCB `DATA0`/GP0.
- Add host-side serial parser regressions covering running status, one-byte
  channel messages, real-time interleaving, System Common, SysEx, and reset.

## v0.57 — 2026-07-30

- Make four-channel MIDI takeover independent of screen redraw. The timer-4
  UART IRQ continues capturing complete messages while the 59.9 Hz engine
  tick drains them before voice work, so simultaneous notes are buffered
  instead of being swallowed and navigation no longer interrupts MIDI audio.
  The resulting trigger latency is bounded to the next engine tick
  (0–16.7 ms).
- Map MIDI channels 1–4 to tracks A–D and zero-based instruments `00`–`03`,
  matching the tracker UI.
- Route MIDI CC74 through the existing `N` command executor. Values `0`–`127`
  span `N00`–`NFF`, providing a note-local live LFSR taps control whose next
  note restores the instrument/G/B state.
- Route standard MIDI Pitch Bend through the existing `F` finetune executor.
  Its MSB gives an absolute ±2-semitone target at 1/16-semitone resolution;
  each channel retains that target across note retriggers and clears it on
  panic or sync-mode change.
- Remove the one-row MIDI-clock startup delay. The Pico bridge emits an
  immediate row grant after Start/Continue, then resumes one row per six MIDI
  clocks. A locally cued `IN24` transport therefore changes from `WAIT` to
  `PLAY` on the downbeat while keeping steady-state four-rows-per-quarter
  timing.
- Replace the compiler-heavy MIDI message parser with a compact full-status
  assembly parser, retaining the complete song, 512-byte stack, two PCM rings,
  and save format v6. Extend the MIDI/IN24 regression suite for four-channel
  receive, CC74, both Pitch Bend endpoints, held-bend retrigger, panic, and
  transport.
- Verify the complete MIDI pass on Atari Lynx hardware using a XIAO RP2040
  bridge, including simultaneous-channel buffering, redraw-safe playback,
  CC74, Pitch Bend, and immediate IN24 startup.

## v0.56 — 2026-07-26

- Fix phrase playback failing to advance rows on real hardware when uninitialized
  zero-page RAM left the VBlank re-entry guard nonzero. Startup now explicitly
  clears the guard before enabling the timer IRQ; the headless harness can seed
  dirty pre-boot RAM and permanently checks that playback advances.
- Display LFSR `TAPS` and `SEED` at their exact three-hex-digit widths:
  `000`–`1FF` and `000`–`FFF`. The nine-bit block map remains beside TAPS,
  without a redundant leading zero in either field.
- Restrict TABLE command selection and recall to commands its macro engine
  actually applies: `B C E F G H K N O P R S T V W X`. The editor now skips
  `A D I J L Z` in both directions instead of offering commands that would be
  ignored.
- Correct four live command behaviours. `Exy` installs both envelope rates and
  restarts attack so its high nibble is audible; `Nxx` overrides the low eight
  taps for only the current note without replacing tap 11 or G/B automation;
  `Pxx` now follows SWP direction (positive down, negative up); and `Rxy`
  continues counting and restarts an LFSR note after a short envelope has
  naturally ended, until the next note or `K`.
- Update the manual, design contract, source guidance, and in-ROM HELP for the
  revised command set and behaviours. Save format remains v6.

## v0.55 — 2026-07-26

- Replace `Sxx`'s unsafe raw Mikey timer reload with a bounded KIT source-rate
  override matching the sibling trackers. The low two bits select
  `0`=1×, `1`=2×, `2`=4×, or `3`=0.5×; the fixed 5,208.333 Hz interrupt
  never changes, table-WAV ignores S, and the next KIT note or `R` retrigger
  restores the instrument TSP rate. Same-row duration and underrun
  regressions cover all four overrides.
- Reuse KIT instrument `TSP` as a dense five-position source-rate control:
  `FF` repeats each PCM byte for 0.5×, while `00`–`03` select exact 1×–4×
  source strides. The DAC timer stays at
  the low-overhead 5,208.333 Hz rate, while each note/`R` retrigger restores
  the patch setting.
  Six-piece frame/render refills keep the rings supplied; the IRQ clamps odd
  strides at the published head. Measured duration and redraw/stress
  regressions complete all five states with zero underruns. A tested 5× state
  was rejected because its ~26 KB/s cartridge demand underruns long samples.
  LFSR/WAV retain signed-semitone TSP, KIT pad selection follows only the
  phrase/chain note, and the SRAM viewer exposes the same five states.
- Make command entry consistent across PHRASE and TABLE. Editing either
  command letter or value remembers the complete pair globally; tapping B on
  an empty command-letter cell inserts both bytes, while occupied cells remain
  unchanged. TABLE skips phrase-only `A`.
- Make PHRASE `Axx` work with note-clocked TBS 0 tables: repeated selection of
  the same table advances it once per note, while a new table target restarts
  from row 0. Historical `A` bytes inside TABLE are ignored.
- Make DCY `0` an immediate decay, so AHD `0/0/0` produces one audible engine
  tick instead of sustaining indefinitely. HOLD `F` remains the sole
  indefinite patch sustain.
- Enter INSTR from PHRASE at the top selector, using the row's instrument when
  present, rather than retaining a stale middle/lower field cursor.
- Make the standalone sample patcher expose all eight software banks as
  zero-based KIT `00`–`07`, independent of how many kits the loaded factory
  bank declares. Missing kits display as empty, accept individual WAVs,
  whole-kit replacement, or slicing, and export with minimal one-byte silent
  pads wherever they remain unfilled; the existing shared-capacity check
  still gates ROM output.
- Keep a clean or held physical-A press inert on GROOVE. Releasing it no
  longer invokes the generic detail-screen back action and jumps to FILES;
  A-held directional map navigation remains unchanged.
- Fix physical-B + Left editing for full-resolution instrument VOL and TABLE.
  VOL now decrements by one as intended, and TABLE decrements `05→04` instead
  of jumping to `--`; targeted editor regressions cover both directions.
- Make phrase `H00` an early phrase boundary in CHAIN/SONG/LIVE playback.
  When another phrase follows in the chain it begins immediately; one-phrase
  chains and standalone PHRASE playback retain their row-00 loop. `H01`–`H0F`
  remain local pre-row branches.
- Make sample-patcher rate conversion deterministic. The standalone file now
  parses PCM/float RIFF/WAVE rates directly and uses band-limited resampling
  to 5,208.333 Hz, preserving duration and pitch while filtering aliases that
  previously sounded like false low-frequency/low-pitch content. The slicer
  displays the source→Lynx rate and before/after duration.
- Make every numbered slice region in the patcher's waveform directly
  clickable for preview. The active region is highlighted and clicking it
  again stops playback; the existing mapping-card audition buttons remain.
- Add a universal **PAN** field to LFSR, WAV, and KIT instruments using the
  existing save-format byte. `xy` sets the Lynx II left/right output levels
  (`0` mute, `F` full); Up/Down edits the left nibble and Left/Right edits the
  right nibble. New instruments retain the existing centred/full `FF` default.
  The existing `Oxy` command uses the same encoding as a live per-voice
  override, with the instrument PAN restored at the next note trigger.
  Zero nibbles now also drive Mikey's older MSTEREO channel-side gates, making
  `00` an exact mute and `F0`/`0F` exact hard pans on hardware paths that do
  not honor the later fractional attenuation registers.
- Add **Slice to kit** to the standalone sample patcher. One longer WAV can be
  region-trimmed, divided into 1–8 equal slices, reordered onto unique pads,
  auditioned, and inserted into the current kit. This path deliberately skips
  peak normalization and applies one shared gain/tanh stage so adjacent slices
  retain their relative level and waveform continuity. An optional 1/2/5/10 ms
  per-slice fade-out reduces end clicks for independent one-shots; it defaults
  off for continuous material.

## v0.54 — 2026-07-25

- Align `Jxy` with SMSGGDJ and GENMDDJ: high nibble `x` is the four-pass
  mask and low nibble `y` is signed transpose. The factory demo's J values
  are nibble-swapped to preserve their existing musical behavior.
- Make every table speed cycle automatically from row `0F` back to `00`,
  matching SMSGGDJ and GENMDDJ. TBS 0 advances once per triggered note;
  TBS 1–F advance on their tick periods, and `Hxx` defines shorter/custom
  loops for either clock mode.
- Make a clean Option-1 tap a true all-track transport toggle: PLAY and WAIT
  stop immediately; while stopped, the existing contextual SONG/CHAIN/PHRASE
  start behavior is retained. Held track-select, mute, and solo actions are
  unchanged.
- Master factory WAV conversion through +12.00 dB of gain followed by tanh
  soft saturation. The processing is baked into the portable sample bank,
  yielding louder samples without adding any Lynx playback work.
- Lower the canonical KIT sample rate from 7,812.5 Hz to 5,208.333 Hz
  (timer reload 191), reducing each feeder's interrupt load by one third.
  `PL` header byte 3 is now rate ID `1`; the ROM builder and standalone
  patcher reject mismatched banks, and each KIT trigger restores the new
  default before a same-row `S` override.
- Rebuild the complete factory bank from the current `samples/` tree at the
  new rate (79,816 bytes). The WAV converter now correctly decodes signed
  24-bit PCM, as used by the new kit 00, plus 8/16/32-bit integer PCM.

## v0.53 — 2026-07-25

- Block KIT VOL's inaudible fine nibble on INSTR. It now displays `0-`–`7-`,
  ignores Left/Right, and uses Up/Down for coarse selection. Playback also
  ignores the stored low nibble completely: `0-` mute, `1-` quarter,
  `2-`–`5-` half, and `6-`/`7-` full. The SRAM viewer uses the same
  eight-position control.
- Make KIT bank a total `00`–`07` selector. Changing TYPE to KIT now
  initializes an unset shared WAVE/KIT byte to `00`; old KIT patches carrying
  `--` are normalized on view, and decrementing bank `00` stays at `00`.
  WAV retains `--` as its hardware-triangle setting. The standalone SRAM
  viewer applies the same rule.
- Block SELECT now inverts every rendered field across each selected
  SONG/CHAIN/PHRASE row instead of marking only the row index. A captured-image
  regression checks the complete range on all three hierarchy screens.
- Rename the single Mikey polynomial instrument to **LFSR** and remove the
  artificial TONE/NOISE split from TYPE selection. Existing type-`01` patches
  remain an invisible LFSR compatibility alias, preserving old songs and
  save-format v6 while new editing cycles only LFSR/WAV/KIT.
- Make INSTR type-aware: hide and skip parameters unused by WAV/KIT and
  remove LFSR's invisible BANK cursor stop.
- Add coarse KIT instrument volume without adding DAC-interrupt work. VOL
  selects full, half, quarter, or mute; signed PCM bytes are shifted while
  64-byte cart pieces enter the ring. A 256-byte pre-start cushion keeps the
  quieter paths redraw-safe, and the sustained/redraw regression finishes
  with every trigger started and both underrun counters at zero.

## v0.52 — 2026-07-24

- Added a TABLE play indicator. The active macro row number is accented while
  the viewed table is running on the selected top-bar track (`T1`–`T4`); it
  disappears for an inactive track or a different table.
- Updated the portable factory sample bank with the revised kit 00 created in
  the standalone patcher. The production build now validates and injects the
  120,544-byte bank directly, preserving it across future ROM versions.
- Added an `INSTR 00–1F` selector directly to the instrument form, made
  SONG→CHAIN and CHAIN→PHRASE entry reliably land on child row `00`, and made
  TABLE command deletion clear only CMD+PARAM while preserving VOL+TSP.
- Changed `Bxx` into cumulative signed TAPS automation across ordinary notes
  and structural boundaries. `B00` restores the active instrument's TAPS and
  releases the accumulator; a new G or transport stop also releases it.
- Documented TRM's exact tick-derived speed model: its high nibble advances a
  6-bit descending-saw phase by 1–16 per 59.9 Hz tick, yielding approximately
  0.94–14.98 Hz independently of groove/BPM.
- Added a read-only, nine-page HELP screen above TABLE. Its ordered
  navigation/editing, structure, instrument/sample, command, sync, FILES, and
  limits reference comes from validated `help.txt` data rather than hard-coded
  UI prose. Plain D-pad turns pages with wraparound; physical-A-held + Up from
  TABLE enters, and A-held + Down returns. HELP stops transport and cold-loads
  over the idle PCM rings. The sample patcher recognizes and protects its cart
  blocks; sample-bank capacity is now 209,920 bytes at blocks 45–249.
- Added a clean Option-1 tap as contextual all-track transport. It restarts
  arrangement playback from the selected SONG row, CHAIN position, and PHRASE
  row where those levels are in context; the existing held track-select,
  mute, and solo layer is unchanged, and `IN`/`IN24` still arm as `WAIT`.
- LIVE queues now show explicit pending intent: a queued start displays its
  chain number in inverted accent, while an empty-cell stop displays inverted
  `ST` on the row from which it was armed.
- Changed phrase `Hxx` into a pre-row branch: the H marker row is never
  triggered, so `H00` on row 5 hands directly from row 4 to row 0. Table H
  keeps its table-loop behavior. SONG playback now treats empty cells as
  per-track group delimiters and loops only the current contiguous run rather
  than wrapping into an earlier disconnected group.
- Shifted every editor body except the full-width WAVE and HELP views eight
  character columns right while keeping the top bar and screen map fixed.
  Physical A no longer backs out of INSTR on release, and editing an empty
  TABLE command repeats the nearest prior command letter and value from that
  table.
- Made full screen changes cooperative with sample playback. Rendering now
  services pending KIT starts and PCM refills every four glyphs, and clears the
  large framebuffer grid in sixteen audio-safe bands instead of one blocking
  pass. Refills remain efficient 64-byte cart transactions, while same-track
  KIT retriggers keep the outgoing sample live until the replacement buffer is
  ready. The hardware regression repeatedly redraws the expensive WAVE screen
  during sustained KIT + TONE G08/SWP playback and requires every trigger to
  start with zero slot underruns.
- Removed channel activity metering from the ROM to return its entire cost to
  audio and interaction. The normal build no longer performs KIT/WAV peak
  calculations in the timer-IRQ feeders, exports per-track meter levels, or
  redraws right-edge bars. The former `meter-test` and `sample-timing-test`
  variants are therefore retired; internal slot-underrun counters remain and
  now saturate so `00/00` unambiguously means a clean run.

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
