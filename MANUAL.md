# ALYNXDJ manual (V0.1 dev)

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
| **A** tap | insert on an empty cell (repeats the last value) |
| **A** hold + d-pad | edit the value under the cursor — ←/→ small step, ↑/↓ big step |
| **A** double-tap | paste the clipboard; with nothing cut: mint the next free chain/phrase on an empty cell, or clone a populated one |
| **A** held + **B** | cut the field under the cursor into the clipboard |
| **B** tap | back (PHRASE → CHAIN → SONG) |
| **B** hold + d-pad | move around the screen map (see below) |
| **B** held + **A** | play / stop — contextual: SONG plays from the cursor row, CHAIN loops the chain, PHRASE loops the phrase |
| **Option 1** + ←/→ | select the current track |
| **Option 1** + **A** | mute the current track (top-bar `1234` flags) |
| **Option 1** + **B** | solo the current track / un-solo |

## The screen map

The indicator in the top-right corner shows where you are (dim letters =
planned screens):

```
O P   W        OPTIONS  PROJECT  WAVE
S C P I T   =  SONG > CHAIN > PHRASE > INSTR > TABLE
F G            FILES (below SONG)   GROOVE (below CHAIN)
```

B-held + →  descends and *drills in* (opens the chain/phrase/instrument
under the cursor); B-held + ← / B-tap goes back; B-held + ↑/↓ reaches
FILES and GROOVE.

## Instruments (INSTR screen)

| Field | Meaning |
|---|---|
| TYPE | TONE / NOISE / WAV / KIT |
| VOL | envelope peak, $00–$7F |
| ATK | attack rate per tick (0 = instant) |
| HOLD | ticks held at the peak |
| DCY | decay rate per tick (0 = sustain until the next note) |
| **TAPS** | the raw 12-bit-LFSR tap mask — see below |
| **SEED** | the shifter start state, $000–$FFF |
| TABLE | macro table to run on every note (`--` = none) |

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

Sweep TAPS/SEED with A-held + ←/→ while a note auditions — exploration is
the point.

- **TONE vs NOISE** are the same machine with different default taps.
- **WAV** ignores TAPS: it is the hardware triangle (integrate mode).
  Keep VOL ≤ 10 — the ramp accumulator wraps above that.
- **KIT** plays the sample kit through the channel DAC; the note's
  semitone picks the kit slot (C=1, C#=2, …). Samples share one output
  channel.

## Commands (PHRASE and TABLE command columns)

| Cmd | Name | Param |
|---|---|---|
| `A xx` | table | run table xx on this note (one-shot) |
| `C xy` | chord | loop +0, +x, +y semitones per tick |
| `D xx` | delay | trigger after xx ticks |
| `F xx` | finetune | signed offset in 1/16 semitones (one-shot) |
| `G xx` | groove | switch the global groove |
| `H xx` | hop | phrase: end this phrase after this row; table: loop to row x |
| `K xx` | kill | cut the note after xx ticks (00 = instant) |
| `L xx` | slide | glide into this row's note from the previous pitch, xx/16 semitone per tick |
| `N xx` | taps | live LFSR-taps morph: bits 0–5 = taps 0–5, 6 = tap 7, 7 = tap 10 |
| `O xy` | pan | attenuation left x / right y (Lynx II stereo) |
| `P xx` | pitch | bend, signed, 1/16 semitone per tick |
| `R xy` | retrig | re-fire the note every y ticks, peak −8·x per fire (KIT refires the sample) |
| `S xx` | rate | live PCM sample rate: timer reload (smaller = faster/higher) |
| `V xy` | vibrato | speed x, depth y (1/16 semitones) |
| `W xx` | wait | shorten this row to xx ticks |
| `X xx` | volume | this note's envelope peak |
| `Z xx` | chance | the note plays if an 8-bit roll < xx (`Z80` ≈ 50/50) |

(`T` tempo and the `I`/`J` variation pair are planned — see PLAN.md.)

## Tables (TABLE screen)

16 rows × {volume, transpose, command, param}, stepped one row per tick,
sticking at the last row unless `H` loops. Attach via the instrument's
TABLE field or a phrase `A` command.

## Grooves (GROOVE screen)

A groove is up to 16 tick counts — ticks per phrase row at the ~60 Hz
engine rate. Groove 0 defaults to 6/6 (~112 BPM at 4 rows/beat). Swing =
uneven pairs (e.g. 7/5). `G` switches grooves mid-song. ←/→ pages through
the 16 grooves.

## Saving (FILES screen)

B-held + ↓ from SONG. The song packs into the cart's EEPROM — the PACK
meter shows the packed size against capacity; a song that doesn't fit is
refused, never truncated. The last saved song autoloads at boot.
In an emulator the save lives in the `.eeprom` file beside the ROM
(format: [SAVEFORMAT.md](SAVEFORMAT.md)).

## The demo song

Boots when no save exists: an arp with a table macro, a sustained bass
(panned left), triangle blips (panned right), and 808 drums. Instruments
6–8 (song rows 16–18) are LFSR exploration rigs — play with their TAPS
and SEED.
