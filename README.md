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
as a multi-drop sync bus.

## Status

**V0.5 — hardware polish and sample patching** (runs on a real Atari Lynx),
feature-complete against the design brief with an editor/UX pass on top
(see [CHANGELOG.md](CHANGELOG.md) for what changed, [PLAN.md](PLAN.md) for
the milestone table, [DESIGN.md](DESIGN.md) for the design contract and
decision log):

- Full SONG → CHAIN → PHRASE hierarchy, **all ten screens** + map
  indicator + channel meters; engine tick in the VBlank IRQ (tempo is
  render-independent)
- All four voice types, with the **12-bit LFSR fully exposed** (raw taps
  + seed per instrument), per-instrument TSP, TONE/NOISE SWP/free-running sine-VIB/TRM
  modulation, and 32-byte wavetables; everything verified by
  FFT / cross-correlation in the headless harness
- **22 sequencer commands** (incl. slow `G` LFSR-tap glide and signed `B`
  taps offset), per-instrument TBS table clocks, one global groove, block select/cut/paste,
  clipboard, mute/solo, LIVE clip-launcher mode
- **Cart-streamed sample pool**: all eight `samples/` kits at full
  quality (808/909/C78/606 + four speech banks), kit-per-instrument.
  KIT and table-WAV playback can target **any of the four tracks**; two
  timer-fed DAC voices may run concurrently, and a third steals the oldest
- Packed save + machine config in the cart's 93C86 EEPROM with autoload
  ([SAVEFORMAT.md](SAVEFORMAT.md)); **two-unit ComLynx sync** verified in
  a bridged-core harness (0 ms lock)

Remaining to 1.0: the focused real-hardware audio/cart/ComLynx-cable pass
and upstream PRs (libretro-handy EEPROM fix, cc65 `_UART_TIMER`).

## Download

Grab the latest [release](https://github.com/little-scale/alynxdj/releases):
the `.lnx` ROM, the patched macOS-arm64 libretro-Handy core used for
verification, and a demo-song WAV rendered by the headless harness. The ROM
has booted and run on a real Lynx; deterministic software/audio regressions
still use the patched Handy core. Run the ROM in RetroArch or build below.

## Sample patcher

Open [`sample-patch-browser.html`](sample-patch-browser.html) directly in a
modern browser. It is a single self-contained file: load a built ALYNXDJ ROM,
replace or audition individual WAVs (or a complete eight-sample kit), then
patch and save a new `.lnx` image. Conversion to 7.8125 kHz signed 8-bit PCM
happens locally; no files are uploaded.

## Building

```sh
brew install cc65
python3 -m pip install -r requirements.txt
make          # → build/alynxdj.lnx
make shot     # headless run → build/shot.png + audio WAV capture
make test     # DAC/sample boundaries, taps/TBS/envelopes, editor, EEPROM
```

Set `PYTHON=/path/to/python` on any `make` invocation when the build tools
are installed in a virtual environment.

`make shot` drives the ROM in a headless libretro Handy core
(`tools/emu/retroshot`) with scripted controller input — no BIOS file
needed, no GUI. The WAV capture is how every sound feature in this repo
was verified. See [CLAUDE.md](CLAUDE.md) for harness details, button-mask
scripting, and toolchain gotchas.

## Controls (quick reference — full docs in [MANUAL.md](MANUAL.md))

- **D-pad** move cursor · **B** edit/insert
- **A held + d-pad** screen map · **A held + B** play/stop
- **B held + d-pad** edit value · **B held + A** cut · **B double-tap** paste,
  mint the next blank/unreferenced object on an empty SONG/CHAIN cell, or
  slim-clone an occupied one
- On stopped **INSTR**, tap **B** to audition the current instrument
- **Option 1 + B/A/←→** mute / solo / track select

## License / credits

By [little-scale](https://github.com/little-scale) (Sebastian Tomczak).
EEPROM driver derives from Bastian Schick's 93C46 code (cc65 Lynx
library). Built with [cc65](https://cc65.github.io/); dev verification on
[libretro-handy](https://github.com/libretro/libretro-handy).
