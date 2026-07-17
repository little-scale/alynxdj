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
| **B** tap | insert on an empty cell (repeats the last value); on INSTR while stopped, audition the current instrument |
| **B** hold + d-pad | edit the value under the cursor — ←/→ small step, ↑/↓ big step |
| **B** double-tap | paste the clipboard; with nothing cut: mint the next free chain/phrase on an empty cell, or slim-clone a populated one |
| **B** held + **A** tap | cut the field under the cursor into the clipboard |
| **B** held + **A** long-hold | **block SELECT** (SONG/CHAIN/PHRASE): anchors at the cursor; ↑/↓ extend; **B** = copy, **B**-held+**A** = cut, **A** = cancel; paste with B double-tap (rows land at the cursor) |
| **A** tap | back (PHRASE → CHAIN → SONG) |
| **A** hold + d-pad | move around the screen map (see below) |
| **A** held + **B** | play / stop — contextual: SONG plays from the cursor row, CHAIN loops the chain, PHRASE and INSTR loop the phrase last viewed (audition while editing the instrument) |
| **Option 1** + ←/→ | select the current track |
| **Option 1** + **B** | mute the current track (top-bar `1234` flags) |
| **Option 1** + **A** | solo the current track / un-solo |

On a PHRASE command column, field cut treats `CMD+PARAM` as one unit: it
removes and copies those two values without touching the row's NOTE or INSTR.
Its B-double-tap paste is likewise command-column-only.

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
chain/phrase/instrument under the cursor); A-held + ← / A-tap goes back;
A-held + ↑/↓ moves vertically (OPTIONS above SONG, PROJECT above CHAIN,
WAVE above INSTR, FILES and GROOVE below).

**PROJECT:** TMPO — the live BPM readout derived from the groove
(tempo *is* the groove); editing it steps every groove entry together, so
swing is preserved. Plus free-chain/phrase counters.

**OPTIONS:** SYNC (ComLynx OFF/OUT/IN), PRELIS (audition-on-edit on/off),
REPEAT (key-repeat delay), PALETTE (eight colour schemes numbered 0–7,
ported from SMSGGDJ's COLR presets, applied live; 0 is the default).
OPTIONS settings persist in the cart EEPROM alongside the song.

Four full-height **channel meters** run down the right-hand border, one
per track. TONE/NOISE show the envelope level (which scales their
output); KIT and WAV show a peak of the actual DAC output, so sample and
wavetable voices register too.

The map rows also navigate horizontally: A-held+←/→ moves FILES↔GROOVE
and OPTIONS↔PROJECT↔WAVE.

**WAVE screen:** 32-column bar editor for the 8 wavetables. ←/→ move,
↑/↓ page waves, B-held + d-pad draws (↑/↓ coarse, ←/→ fine). Waves 0–3
ship as triangle, saw, square, 25% pulse.

## Instruments (INSTR screen)

| Field | Meaning |
|---|---|
| TYPE | TONE / NOISE / WAV / KIT |
| VOL | envelope peak, $00–$7F |
| ATK | attack **time**, 0–F: 0 = instant, F ≈ 2 s (higher = slower) |
| HOLD | ticks held at the peak, 0–F |
| DCY | decay **time**, 0–F: 0 = sustain until the next note, 1 = instant-ish, F ≈ 2 s |
| **TSP** | signed instrument transpose in semitones; Left/Right ±1, Up/Down ±12. Applies to TONE/NOISE/WAV pitch and KIT pad selection |
| **SWP** | TONE/NOISE pitch sweep, signed 1/16 semitone per tick with period-style direction: `$01`–`$7F` falls, `$FF`–`$80` rises, `00` = off |
| **VIB** | TONE/NOISE sine vibrato, packed speed·depth nibbles; speed 0–F ≈ 0.47–7.49 Hz, depth 0–F in 1/16 semitones. Phase continues across notes and resets with transport |
| **TRM** | TONE/NOISE tremolo, packed speed·depth nibbles; a one-way volume dip inside the AHD envelope |
| **TAPS** | the raw 12-bit-LFSR tap mask — see below |
| BANK | labelled by TYPE — **WAVE** (WAV: wavetable 0–7, `--` = hardware triangle) / **KIT** (sample kit 0–7) / BANK (otherwise) |
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

After **NEW**, every instrument starts as TONE with ATK `0`, HOLD `5`, and
DCY `A`. Empty CHAIN rows display transpose `00`; their compact-save sentinel
remains internal and changes to a real zero as soon as a phrase is inserted.

### TAPS and SEED — the Lynx sound

Each channel is a 12-bit shift register; on every timer tick it shifts in
the *inverted parity* of the tapped bits. **Which taps are on is the
timbre.** The TAPS value lights its taps on the glyph strip
(`0 1 2 3 4 5 7 A B` — A/B are taps 10/11):

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
  only (the DAC is full-amplitude), and very high notes read the table
  coarser to keep the feed rate sane.
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
| `F xx` | finetune | signed offset in 1/16 semitones (one-shot) |
| `G xx` | glide | signed 1/16-tap-per-tick sweep: `01`–`7F` ramps up, `FF`–`80` down, `00` off — a slow live timbre morph (wraps 0–511; no reseed) |
| `H xx` | hop | phrase: end this phrase after this row; table: loop to row x |
| `K xx` | kill | cut the note after xx ticks (00 = instant) |
| `L xx` | slide | glide into this row's note from the previous pitch, xx/16 semitone per tick |
| `N xx` | taps | live LFSR-taps morph: bits 0–5 = taps 0–5, 6 = tap 7, 7 = tap 10 |
| `O xy` | pan | attenuation left x / right y (Lynx II stereo) |
| `P xx` | pitch | bend, signed, 1/16 semitone per tick |
| `R xy` | retrig | re-fire the note every y ticks, peak −8·x per fire (KIT refires the sample) |
| `S xx` | rate | live KIT/table-WAV timer reload (smaller = faster/higher) |
| `V xy` | vibrato | speed x, depth y (1/16 semitones) |
| `W xx` | wait | shorten this row to xx ticks |
| `X xx` | volume | this note's envelope peak |
| `Z xx` | chance | the note plays if an 8-bit roll < xx (`Z80` ≈ 50/50) |

| `E xy` | envelope | re-slope live: attack x, decay y (times, like the INSTR fields; current stage and level untouched) |
| `T xx` | tempo | set the active groove flat to hex BPM xx (flattens swing — that's the point of T) |
| `I xx` | iterate | play this note only on phrase passes whose bit (pass count mod 8) is set — `I55` = even passes, `IFF` = always |
| `J xy` | vary | transpose by x (signed nibble) on passes whose bit (count mod 4) is set in y — `J71` = +7 once every 4 passes |

The **full SMSGGDJ-family command set is in.** `I`/`J`/`Z` are the
deterministic-variation trio: phrase variation without cloning. Pass
counts accumulate across the whole arrangement and reset at play-start.

## Tables (TABLE screen)

16 rows × {volume, transpose, command, param}. **TBS** controls the clock:
`0` preserves the playhead and advances exactly once per triggered note;
`1` is the fastest at one row per ~59.9 Hz tick; `2`–`F` are progressively
slower. Tick modes restart at row 0 on a new note. All modes stick at the
last row unless `H` loops. Attach via the instrument's TABLE field or a
phrase `A` command. Table volume may reshape attack/hold, but once decay
starts the envelope owns the level and always reaches its normal end.

For scale, the former `G01` moved one whole taps value per tick: about 59.9
values/second and an 8.55-second full wrap. It now moves 1/16 as fast: about
3.74 values/second and a 136.8-second wrap. `G10` recreates the old `G01`
speed.

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

A-held + ↓ from SONG: **SAVE / LOAD / NEW / PURGE**. The song packs into
the cart's EEPROM — the PACK meter shows the packed size against
capacity; a song that doesn't fit is refused, never truncated. The last
saved song autoloads at boot. In an emulator the save lives in the
`.eeprom` file beside the ROM (format: [SAVEFORMAT.md](SAVEFORMAT.md)).
Some SD carts call this an E2P, SAV, or SRAM file even though the ROM speaks
the Lynx serial-EEPROM protocol. The write-enable command is emitted in the
canonical 93C86 form used by cc65's hardware driver for flashcart
compatibility.

**NEW** wipes the whole song back to a blank slate (it does not touch the
EEPROM — your last save survives until you SAVE over it). It asks for a
second B-press: the row turns to SURE, moving the cursor disarms it.

**PURGE** deletes every chain no song row references, then every phrase
no remaining chain references, and repacks — the PACK meter shows the
bytes reclaimed. Run it before saving a tight song.

## The demo song

Boots when no save exists: an eight-bar A-minor groove on a 7/5 swing —
buzzy-taps bass with `Z` ghost notes and a `J` lift, chord-shimmer arps
(`C37`), a vibrato lead with `L` slides, sawtooth wavetable pads, seed-
timbre plucks morphed live with `N`, and drums that move from the 808 to
the 909 with an `R` retrig fill and speech cuts. Song rows 16+ hold the
engine verification rigs — leave them or wipe them, they never play in
the demo loop.
