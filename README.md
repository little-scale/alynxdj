# ALYNXDJ

An LSDJ-inspired music tracker for the **Atari Lynx**, written in cc65 C +
ca65 assembly. Third in a family after
[SMSGGDJ](https://github.com/little-scale) (Sega Master System / Game Gear)
and GENMDDJ (Mega Drive): the data model, control philosophy, command set,
and grooves/tables carry over; the Lynx brings what the siblings never had.

## Why the Lynx

Mikey's four audio channels are *identical and symmetric* — there are no
fixed roles. Each channel is a timer clocking a 12-bit polynomial shift
register, and every track can be any of:

- **TONE** — square waves and short-loop "poly" timbres from the LFSR taps
- **NOISE** — long-sequence taps; pitched noise whose bandwidth is the note
- **WAV** — a hardware triangle (integrate mode + tap-11 feedback)
- **KIT** — true 8-bit PCM through the channel DAC (sample drums!)

The **12-bit LFSR is fully exposed per instrument**: a raw 9-bit tap mask
(taps 0–5, 7, 10, 11) plus the 12-bit shifter seed. For many tap sets the
seed selects between disjoint state cycles — genuinely different waveforms
from the same taps. This is the heart of the Lynx sound and the heart of
this tracker. Plus: per-channel stereo attenuation (Lynx II), and ComLynx
as a multi-drop sync bus (planned).

## Status

**V0.1 dev** — milestones M0–M10 built and verified (see
[PLAN.md](PLAN.md) for the full table, [DESIGN.md](DESIGN.md) for the
design contract and decision log):

- Full SONG → CHAIN → PHRASE hierarchy, 7 screens
  (SONG/CHAIN/PHRASE/INSTR/TABLE/FILES/GROOVE) + the map indicator
- All four voice types sounding, verified by FFT / cross-correlation
  against source samples in a headless emulator harness
- 11 commands (`A C D G H K O P V W X`), tables, grooves,
  1/16-semitone pitch engine (bends, vibrato, chords)
- Cut/paste/clone, mute/solo, DAS key repeat
- Packed save to the cart's 93C86 EEPROM with autoload
  ([SAVEFORMAT.md](SAVEFORMAT.md))

Remaining to 1.0: block ops + remaining commands (M9c), ComLynx sync
(M11), LIVE mode + polish + a real-hardware pass (M12).

## Building

```sh
brew install cc65
make          # → build/alynxdj.lnx
make shot     # headless run → build/shot.png + audio WAV capture
```

`make shot` drives the ROM in a headless libretro Handy core
(`tools/emu/retroshot`) with scripted controller input — no BIOS file
needed, no GUI. The WAV capture is how every sound feature in this repo
was verified. See [CLAUDE.md](CLAUDE.md) for harness details, button-mask
scripting, and toolchain gotchas.

## Controls (quick reference — full docs in [MANUAL.md](MANUAL.md))

- **D-pad** move · **A** edit/insert · **B** back/navigate
- **B held + d-pad** screen map · **B held + A** play/stop
- **A held + d-pad** edit value · **A held + B** cut · **A double-tap** paste
- **Option 1 + A/B/←→** mute / solo / track select

## License / credits

By [little-scale](https://github.com/little-scale) (Sebastian Tomczak).
EEPROM driver derives from Bastian Schick's 93C46 code (cc65 Lynx
library). Built with [cc65](https://cc65.github.io/); dev verification on
[libretro-handy](https://github.com/libretro/libretro-handy).
