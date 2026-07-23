# ALYNXDJ manual (V0.4)

A pocket groovebox for the Atari Lynx in the LSDJ tradition. If you know
LSDJ or SMSGGDJ, you already know most of this.

## The idea

```
SONG  →  CHAIN  →  PHRASE  →  notes + instruments + commands
```

A **phrase** is 16 steps of notes. A **chain** strings up to 16 phrases
(each with a semitone transpose). The **song** is 128 rows × 4 tracks of
chain numbers. Four tracks = Mikey's four channels, and every track can
play every instrument type.

## Controls

The rule: **the button already held when another arrives selects the
action.** No simultaneous-press timing windows.

| Input | Action |
|---|---|
| D-pad | move the cursor (hold to repeat) |
| **B** tap | insert on an empty cell (repeats the last value); a new PHRASE note also inherits the last instrument explicitly edited in the instrument column; on INSTR while stopped, audition the current instrument |
| **B** hold + d-pad | edit the value under the cursor — ←/→ small step, ↑/↓ big step |
| **B** double-tap | paste the clipboard; with nothing cut: mint the next free chain/phrase on an empty cell, or slim-clone a populated one |
| **B** held + **A** tap | cut the field under the cursor into the clipboard |
| **B** held + **A** long-hold | **block SELECT** (SONG/CHAIN/PHRASE): anchors at the cursor; ↑/↓ extend; **B** = copy, **B**-held+**A** = cut, **A** = cancel; paste with B double-tap (rows land at the cursor) |
| **A** tap | back where applicable on detail/utility screens; deliberately does nothing on CHAIN, PHRASE, OPTIONS, or PROJECT |
| **A** hold + d-pad | move around the screen map (see below) |
| **A** held + **B** | play / stop — contextual: SONG plays from the cursor row, CHAIN loops the chain, PHRASE and INSTR loop the phrase last viewed (audition while editing the instrument) |
| **Option 1** + ←/→ | select the current track |
| **Option 1** + **B** | mute the current track (top-bar `1234` flags) |
| **Option 1** + **A** | solo the current track / un-solo |

On a PHRASE command or parameter column, B double-tap is field-scoped and can
never overwrite NOTE or INSTR. A previously cut command pair is pasted there;
without a matching command clipboard, the double-tap clears only `CMD+PARAM`.
Field cut on the command-letter column likewise treats `CMD+PARAM` as one
unit and preserves NOTE+INSTR.

PHRASE note entry remembers instruments separately from notes. Whenever you
edit a number in the instrument column, that number becomes the insertion
instrument. Every later note placed into an empty row—whether by B tap or by
B-held note editing—receives that instrument until you explicitly edit a
different instrument number. The latch begins at instrument `00` and is not
changed merely by viewing, playing, or pasting another row.

A **slim clone** duplicates only the object under the cursor. On SONG it
copies the chain but keeps that chain's phrase references shared; on CHAIN it
copies the phrase but keeps its instrument references shared. The new
chain/phrase can be edited independently without recursively duplicating a
whole tree of song data.
“Free” means both blank and not referenced elsewhere, so an allocated but
not-yet-edited chain or phrase will not be reused accidentally.

## The screen map

The indicator in the top-right corner shows where you are (dim letters =
planned screens):

```
O P   W        OPTIONS  PROJECT  WAVE
S C P I T   =  SONG > CHAIN > PHRASE > INSTR > TABLE
F G            FILES (below SONG)   GROOVE (below CHAIN)
```

All ten screens are live. A-held + → descends and *drills in* (opens the
chain/phrase/instrument under the cursor). PHRASE → INSTR opens the valid
instrument assigned to the selected row; an empty row keeps the previously
viewed instrument. A-held + ← goes back;
A-held + ↑/↓ moves vertically (OPTIONS above SONG, PROJECT above CHAIN,
WAVE above INSTR, FILES and GROOVE below).

**PROJECT:** TMPO — the live BPM readout derived from the groove
(tempo *is* the groove); editing it steps every groove entry together, so
swing is preserved. Plus free-chain/phrase counters.

**OPTIONS:** SYNC (ComLynx OFF/OUT/IN/MIDI/IN24), PRELIS (audition-on-edit on/off),
REPEAT (key-repeat delay), PALETTE (eight colour schemes numbered 0–7,
ported from SMSGGDJ's COLR presets, applied live; 0 is the default).
OPTIONS settings persist in the cart EEPROM alongside the song.

In **MIDI** takeover, the sequencer stays stopped and incoming USB MIDI from a
ComLynx bridge plays the tracker directly: MIDI channels 1–4 use tracks 1–4
and instruments 01–04. Notes use the normal instrument path, including TSP,
SWP/VIB/TRM, tables, envelopes, WAV and KIT routing. Each MIDI channel is
monophonic. Note Off releases through DECAY (DCY 0 cuts); KIT stops. Velocity
is currently used only to distinguish Note On from velocity-zero Note Off.
CC120/123 release a channel and MIDI System Reset (`FF`) is panic. Messages
must contain their status byte; running status, channels 5–16, Program Change,
pitch bend and other controllers are ignored. There is no heartbeat timeout.

In **IN24**, the same Pico bridge supplies MIDI Clock transport. The Pico
counts standard 24-PPQN USB MIDI Clock and sends the Lynx one pulse per
tracker row (every six source clocks). `FA` Start supplies a row-0 cue when
the user has not already cued one, each received `F8` grants one row, and
`FC` Stop halts. `FB` Continue currently behaves like Start because Song
Position Pointer is not implemented. Groove and `W` row timing are ignored
while externally clocked. The original **IN** mode remains the row-byte
protocol for another Lynx; use **IN24** for a DAW or USB-MIDI clock source.
Incoming IN24 traffic uses the phrase clipboard
as its receive buffer, so entering/using the mode invalidates a previously
copied phrase.

With either **IN** or **IN24** selected, first move the SONG cursor to the row
you want and start transport normally. The top bar says **WAIT** and nothing
sounds yet. The first incoming row pulse starts that cued row and changes the
label to **PLAY**; each later pulse advances one row. A Start message received
while WAIT is showing preserves the locally selected row.
Pico build, wiring, and flashing instructions are in
[`pico-midi-comlynx/README.md`](pico-midi-comlynx/README.md). The required
2.5 mm TRS plug is **tip = Lynx +5 V (leave disconnected and insulate), ring =
ComLynx DATA, sleeve = GND**; do not substitute a normal stereo-audio wiring
assumption.

Four full-height **channel meters** run down the right-hand border, one
per track. TONE/NOISE show the envelope level (which scales their
output); KIT and WAV show a peak of the actual DAC output, so sample and
wavetable voices register too.

The map rows also navigate horizontally: A-held+←/→ moves FILES↔GROOVE
and OPTIONS↔PROJECT↔WAVE.

**WAVE screen:** 32-column bar editor for the 8 wavetables. ←/→ moves between
columns; unmodified ↑/↓ does not change the wave. Hold A + ←/→ to select the
wave number. B-held + d-pad draws (↑/↓ coarse, ←/→ fine). Waves 0–3 ship as
triangle, saw, square, 25% pulse.

## Instruments (INSTR screen)

| Field | Meaning |
|---|---|
| TYPE | TONE / NOISE / WAV / KIT |
| VOL | envelope peak, $00–$7F |
| ATK | attack **time**, 0–F: 0 = instant, F ≈ 2 s (higher = slower) |
| HOLD | peak hold: `0`–`E` timed ticks; `F` sustains until the next note, a `K` command, transport stop, or live MIDI Note Off |
| DCY | decay **time**, 0–F: 0 = sustain until the next note, 1 = instant-ish, F ≈ 2 s |
| **TSP** | signed instrument transpose in semitones; Left/Right ±1, Up/Down ±12. Applies to TONE/NOISE/WAV pitch and KIT pad selection |
| **SWP** | TONE/NOISE pitch sweep, signed 1/16 semitone per tick with period-style direction: `$01`–`$7F` falls, `$FF`–`$80` rises, `00` = off |
| **VIB** | TONE/NOISE sine vibrato, packed speed·depth nibbles; speed 0–F ≈ 0.47–7.49 Hz. Depth uses SMSGGDJ's nonlinear response, from 1/16 semitone at `1` through 10/16 at `8` to 60/16 (±3.75 semitones) at `F`. Phase continues across notes and resets with transport |
| **TRM** | TONE/NOISE repeating decay saw, packed speed·depth nibbles; speed 0–F ≈ 0.94–14.98 Hz. Each cycle starts at the AHD level, ramps downward, then snaps back to the top like a repeating envelope |
| **TAPS** | the raw 12-bit-LFSR tap mask — see below |
| WAVE / KIT | WAV: wavetable 0–7 (`--` = hardware triangle); KIT: sample kit 0–7. This row is blank and inert for TONE/NOISE. |
| **SEED** | the shifter start state, $000–$FFF |
| TABLE | macro table to run on every note (`--` = none) |
| **TBS** | table speed: `0` advances one row per triggered note; `1` is one row per engine tick; `2`–`F` wait that many ticks per row |

TSP clamps at the playable note limits rather than wrapping. SWP/VIB/TRM
show `--` for WAV and KIT. With the transport stopped, tap
physical **B** anywhere on INSTR to trigger the selected instrument at the
last-entered note. This works even when OPTIONS → PRELIS is off and does not
start the sequencer; A-held+B retains the separate phrase-loop transport.
Instrument VIB and the phrase/table `V` command share the same free-running
per-track phase. A zero depth is completely off; it does not alter tuning or
advance the phase.

After **NEW**, every instrument starts with TYPE TONE, VOL `7F`, ATK `0`,
HOLD `5`, DCY `5`, and TAPS `001`. Empty CHAIN rows display transpose `00`;
their compact-save sentinel remains internal and changes to a real zero as
soon as a phrase is inserted.

### TAPS and SEED — the Lynx sound

Each channel is a 12-bit shift register; on every timer tick it shifts in
the *inverted parity* of the tapped bits. **Which taps are on is the
timbre.** Beside the exact hexadecimal TAPS value, nine meter-style blocks
show the bitmap in tap order `0 1 2 3 4 5 7 A B` (A/B are taps 10/11):

- `$001` (tap 0) — a pure square. The default.
- low-tap combos (`$003`–`$03F`) — short loops: buzzy, metallic, pitched
- high taps (`$0C0`+) — long sequences: noise, the higher the note the
  brighter (the note clocks the shifter, so pitch = bandwidth)

**SEED** picks the starting state. For many tap sets the state graph has
several separate cycles — the same TAPS with a different SEED is a
*different waveform*, not a phase shift (try TAPS `$0F1`, SEED `$000` vs
`$555`). Some (TAPS, SEED) pairs land on the lock state and go silent;
that's the real chip, nudge the seed.

Sweep TAPS/SEED with B-held + ←/→ while a note auditions — exploration is
the point.

- **TONE vs NOISE** are the same machine with different default taps.
- **WAV** ignores TAPS. With WAVE = `--` it is the hardware triangle
  (integrate mode; keep VOL ≤ 10 — the ramp accumulator wraps above
  that). With WAVE = 0–7 it loops that **32-byte wavetable** through a
  channel DAC — draw your own on the WAVE screen. The envelope gates length
  only (the DAC is full-amplitude). To keep WAV+KIT playback inside the
  real-hardware interrupt budget, table-WAV targets about 6.25 kHz; higher
  notes step over 2, 4, or 8 entries per interrupt, preserving pitch with
  progressively coarser waveform resolution. Extreme top-octave notes can
  exceed that target after the 8-entry step is exhausted.
- **KIT** streams samples from the cart through the channel DAC; the
  note's semitone picks the kit slot (C=1, C#=2, …) and **BANK picks the
  kit** — the cart ships all eight `samples/` folders (808, 909, C78,
  606, four speech banks) at full quality.

Every track can play KIT or table-WAV on its own physical Mikey DAC. The
CPU budget allows **two simultaneous timer-fed DAC voices** across those
types; a third trigger steals the oldest. Hardware TONE/NOISE/integrate-WAV
voices do not consume this two-voice budget.

## Commands (PHRASE and TABLE command columns)

| Cmd | Name | Param |
|---|---|---|
| `A xx` | table | run table xx on this note (one-shot) |
| `B xx` | taps offset | add signed `xx` once to the current TONE/NOISE taps value (`01` = +1, `FF` = −1), wrapping 0–511 without reseeding |
| `C xy` | chord | loop +0, +x, +y semitones per tick |
| `D xx` | delay | trigger after xx ticks |
| `E xy` | envelope | re-slope live: attack x, decay y (times, like the INSTR fields; current stage and level untouched) |
| `F xx` | finetune | signed offset in 1/16 semitones (one-shot) |
| `G xx` | glide | reset to the active instrument's stored TAPS, then use signed `xx`: magnitudes 1–7 are ticks per step; magnitude 8+ is magnitude−7 rows per step (`01` = +1/tick, `07` = +1/7 ticks, `08` = +1/row, `0B` = +1/4 rows; negative values reverse direction; `00` = reset and stop; wraps 0–511 without reseeding) |
| `H xx` | hop | phrase: end this phrase after this row; table: loop to row x |
| `I xx` | iterate | play this note only on phrase passes whose bit (pass count mod 8) is set — `I55` = even passes, `IFF` = always |
| `J xy` | vary | transpose by x (signed nibble) on passes whose bit (count mod 4) is set in y — `J71` = +7 once every 4 passes |
| `K xx` | kill | cut the note after xx ticks (00 = instant) |
| `L xx` | slide | glide into this row's note from the previous pitch, xx/16 semitone per tick |
| `N xx` | taps | live LFSR-taps morph: bits 0–5 = taps 0–5, 6 = tap 7, 7 = tap 10 |
| `O xy` | pan | attenuation left x / right y (Lynx II stereo) |
| `P xx` | pitch | bend, signed, 1/16 semitone per tick |
| `R xy` | retrig | re-fire the note every y ticks, peak −8·x per fire (KIT refires the sample) |
| `S xx` | rate | live KIT/table-WAV timer reload (smaller = faster/higher) |
| `T xx` | tempo | set the active groove flat to hex BPM xx (flattens swing — that's the point of T) |
| `V xy` | vibrato | speed x, nonlinear depth y (1/16-semitone curve; `8` = 10/16, `F` = 60/16) |
| `W xx` | wait | shorten this row to xx ticks |
| `X xx` | volume | this note's envelope peak |
| `Z xx` | chance | the note plays if an 8-bit roll < xx (`Z80` ≈ 50/50) |

The **full SMSGGDJ-family command set is in.** `I`/`J`/`Z` are the
deterministic-variation trio: phrase variation without cloning. Pass
counts accumulate across the whole arrangement and reset at play-start.
When editing either a PHRASE or TABLE command field, left/down and right/up
move backward and forward through the alphabetical order shown above. The
empty command wraps between `Z` and `A`.

## Tables (TABLE screen)

16 rows × {volume, transpose, command, param}. **TBS** controls the clock:
`0` preserves the playhead and advances exactly once per triggered note;
`1` is the fastest at one row per ~59.9 Hz tick; `2`–`F` are progressively
slower. Tick modes restart at row 0 on a new note. All modes stick at the
last row unless `H` loops. Attach via the instrument's TABLE field or a
phrase `A` command. Table volume may reshape attack/hold, but once decay
starts the envelope owns the level and always reaches its normal end.

`G` treats its parameter as an 8-bit signed direction and split period.
Values `01`–`7F` move upward; `FF`–`80` mean -1 through -128 and move
downward. Absolute magnitudes 1–7 count tracker ticks. Magnitude 8 begins the
row-locked range, whose row period is `magnitude − 7`. Therefore `G01` moves
once per tick, `G07` once per seven ticks, `G08` once per row, and `G0B` once
per four rows. Downward counterparts are `GFF`, `GF9`, `GF8`, and `GF5`.
The slowest positive and negative periods are `G7F` at 120 rows and `G80` at
121 rows.

At the ~59.9 Hz engine rate, `G01` is about 59.9 changes/second and `G07`
about 8.6. With the default six-tick groove, `G08` is about 10 changes/second
and `G0B` about 2.5. The `G07`→`G08` boundary is intentionally not monotonic:
`G08` switches from absolute tick timing to musically row-locked timing.

The command begins a complete first period at the original TAPS value.
`G07` first changes seven ticks after its command; `G0B` exposes the original
value for rows 0–3 and first changes at row 4, then rows 8, 12, and so on.
Each track has its own countdown. The row range stays musically locked through
groove, swing, `IN`, and `IN24`; the tick range follows the engine tick. Every
newly encountered `G`
first restores the active instrument's saved TAPS value, so a later `G`
begins a fresh sweep. Ordinary notes without `G` preserve both the current
live TAPS value and the partially elapsed countdown, so the motion behaves
like continuous track automation across a melody and across phrase/chain
boundaries. `G00` explicitly resets and stops. Live writes do not reseed the
LFSR.

This also applies when a phrase loops: `G7F` on row 0 of a 16-row looping
phrase is encountered again after only 16 rows, so it continually restarts
and can never reach its first tap change. A note on that same row is still a
new note on every pass; it retriggers the envelope and reseeds the oscillator,
which can produce an audible loop-boundary event even though TAPS did not
change.

## Groove (GROOVE screen)

The groove is up to 16 tick counts — ticks per phrase row at the ~60 Hz
engine rate. It defaults to 6/6 (~112 BPM at 4 rows/beat); swing is
uneven pairs (e.g. 7/5). There is **one global groove** (no pool) — it
sets the tempo and swing for the whole song, and PROJECT's TMPO steps
every entry of it together. `T xx` flattens it to a plain hex BPM.

## LIVE mode (Option 2 on SONG)

The SONG screen becomes a clip launcher: **A-held + B on a cell queues
that chain on that track**, launching at the next 16-row bar (queued
cells show inverted-accent until they fire). Queue an **empty** cell to
stop the track at the bar. Every track loops its chain independently —
the first launch starts the engine and defines the bar grid. Option 2
again returns to arrangement SONG mode.

## Saving (FILES screen)

A-held + ↓ from SONG: **SAVE / LOAD / NEW / DEMO / PURGE**. The song packs into
the cart's EEPROM — the PACK meter shows the packed size against
capacity; a song that doesn't fit is refused, never truncated. The last
valid saved song autoloads at boot; if no valid save exists, ALYNXDJ starts
with a clean NEW song. In an emulator the save lives in the
`.eeprom` file beside the ROM (format: [SAVEFORMAT.md](SAVEFORMAT.md)).
Some SD carts call this an E2P, SAV, or SRAM file even though the ROM speaks
the Lynx serial-EEPROM protocol. The write-enable command is emitted in the
canonical 93C86 form used by cc65's hardware driver for flashcart
compatibility.

ALYNXDJ needs the full **2 KB 93C86**. The BennVenn **ElCheapoSD for Lynx has
a physical 128-byte 93C46** and therefore cannot persist ALYNXDJ songs, even
though it can load and run the ROM normally. Its SD-facing API is intended for
menu loaders rather than general file access, so the tracker cannot bypass the
small EEPROM and write a larger song file directly. A 128-byte `.sav` from
that cart contains no recoverable ALYNXDJ payload; do not pad it. Carts that
emulate a 93C86, and the patched Handy core, support the complete save image.

**NEW** wipes the whole song back to a blank slate (it does not touch the
EEPROM — your last save survives until you SAVE over it). It asks for a
second B-press: the row turns to SURE, moving the cursor disarms it.

**DEMO** replaces the working song with the factory demo. Like NEW, it asks
for confirmation and does not touch EEPROM unless you subsequently SAVE.

**PURGE** deletes every chain no song row references, then every phrase
no remaining chain references, and repacks — the PACK meter shows the
bytes reclaimed. Run it before saving a tight song.

## The demo song

Load it with **FILES → DEMO**: an eight-bar A-minor groove on a 7/5 swing —
buzzy-taps bass with `Z` ghost notes and a `J` lift, chord-shimmer arps
(`C37`), a vibrato lead with `L` slides, sawtooth wavetable pads, seed-
timbre plucks morphed live with `N`, and drums that move from the 808 to
the 909 with an `R` retrig fill and speech cuts. Song rows 16+ hold the
engine verification rigs — leave them or wipe them, they never play in
the demo loop.
