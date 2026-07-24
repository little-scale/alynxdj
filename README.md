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

**V0.52 — on-console help, playback headroom, and editor polish** (runs on a real
Atari Lynx),
feature-complete against the design brief with an editor/UX pass on top
(see [CHANGELOG.md](CHANGELOG.md) for what changed, [PLAN.md](PLAN.md) for
the milestone table, [DESIGN.md](DESIGN.md) for the design contract and
decision log):

- Full SONG → CHAIN → PHRASE hierarchy, **all eleven screens** + map,
  including a nine-page on-console HELP reference above TABLE and a
  selected-track TABLE play indicator; engine tick in the VBlank IRQ
  (tempo is render-independent)
- All four voice types, with the **12-bit LFSR fully exposed** (raw taps
  + seed per instrument), per-instrument TSP, TONE/NOISE SWP/free-running
  sine-VIB/repeating-decay TRM
  modulation, and 32-byte wavetables; everything verified by
  FFT / cross-correlation in the headless harness
- **22 sequencer commands** (incl. slow `G` LFSR-tap glide and cumulative
  signed `B` taps automation), selected alphabetically in PHRASE and TABLE;
  in-page instrument selection, row-safe TABLE command deletion, per-instrument
  TBS table clocks, one global groove, block select/cut/paste,
  clipboard, mute/solo, LIVE clip-launcher mode
- **Cart-streamed portable sample bank**: all eight `samples/` kits at full
  quality (808/909/C78/606 + four speech banks), kit-per-instrument. The
  factory bank is one reusable `.bin`, and custom banks carry between releases.
  KIT and table-WAV playback can target **any of the four tracks**; two
  timer-fed DAC voices may run concurrently, and a third steals the oldest.
  Full screen redraws cooperatively service their cart rings, so navigation
  cannot hold sample starts or refills behind a complete framebuffer paint
- Packed save + machine config in the cart's 93C86 EEPROM with valid-save
  autoload, a clean no-save boot, and an explicit FILES demo loader
  ([SAVEFORMAT.md](SAVEFORMAT.md)); **two-unit ComLynx sync** verified in
  a bridged-core harness (0 ms lock), plus receive-only **MIDI takeover**:
  USB-MIDI channels 1–4 drive tracks A–D / instruments 01–04 through the
  tracker engine via the included Pico bridge. **IN24** also follows standard
  MIDI Start/Stop, with the Pico dividing 24-PPQN clock to one pulse per row

Hardware song persistence requires a **2 KB 93C86** (physical or emulated).
The BennVenn **ElCheapoSD for Lynx contains a 128-byte 93C46 only**: it runs
the ROM, but cannot store an ALYNXDJ song. Use a cart that supports 93C86
(such as an EEPROM-emulating Lynx GameDrive) or an emulator for persistent
songs. A 128-byte ElCheapo `.sav` cannot be padded into a valid save.

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
set per-sample gain from −24 to +24 dB, optionally apply tanh soft saturation,
drag non-destructive IN/OUT points over each waveform to trim it,
import/export a complete portable sample-bank `.bin`, and save a new `.lnx`
image. Processing affects browser audition, bank export, and the patched
7.8125 kHz signed 8-bit PCM; no files are uploaded. Each slot can be up to
65,535 bytes (~8.39 seconds), while the complete bank has 209,920 bytes.

## Song file viewer

Open [`song-file-viewer.html`](song-file-viewer.html) directly in a modern
browser to inspect or edit an ALYNXDJ SRAM / EEPROM song file. The standalone
tool validates the header, checksum, packed payload, and hierarchy references;
exposes song rows, chains, phrases, instruments, tables, grooves, and waves;
then exports a hardware-ready 2,048-byte save-format-v6 file. All processing is
local and offline.

## Pico USB-MIDI bridge

[`pico-midi-comlynx/`](pico-midi-comlynx/) contains buildable RP2040 firmware
for the one-Pico/one-Lynx bridge. It appears to a computer or DAW as
**ALYNXDJ MIDI Bridge**, forwards MIDI channels 1–4 plus Clock/Start/Continue/
Stop, divides every six USB MIDI clocks into one tracker-row pulse, and
generates the Lynx's 62.5-kbaud open-drain 11-bit UART framing in PIO. The
same output works with the Lynx's `MIDI` and `IN24` modes, with no Pico mode
switch and no heartbeat.

`IN` and `IN24` are armed transports: cue a SONG row and press transport to
show **WAIT**. The first incoming row pulse starts that exact row and changes
the label to **PLAY**; later pulses advance it normally.

The bridge README includes the 470-ohm + BAT54 prototype circuit, documented
Jaycar BAT46/BAT48 and 1N5819 substitutes, the preferred buffered alternatives,
the reason a 1N4148 is not a safe clamp, exact wiring
(2.5 mm TRS **tip = +5 V/unconnected, ring = DATA, sleeve = GND**),
build instructions, and the current USB-device limitation: it accepts MIDI
from a computer/DAW, but does not yet host a USB keyboard directly.

## Building

```sh
brew install cc65
python3 -m pip install -r requirements.txt
make          # → build/alynxdj.lnx
make factory-samples # rebuild the tracked factory .bin from samples/**/*.wav
make SAMPLE_BANK=/path/to/custom-samples.bin # inject one portable bank
make shot     # headless run → build/shot.png + audio WAV capture
make test     # sound/editor/EEPROM plus IRQ-backed ComLynx MIDI takeover
make pico     # companion RP2040 UF2; requires PICO_SDK_PATH + Arm toolchain
```

The standard image remains 256 KB: it provides 209,920 bytes for sample-bank
data, followed by the protected HELP and MIDI blocks.
Although Lynx carts can be 512 KB, that size uses 2 KB cart blocks instead of
the current 1 KB layout, so it is an addressing migration rather than a simple
size flag. It is deferred until real banks outgrow the current 209,920-byte cap.

Regression captures are isolated under `build/tests/<suite>/` and each suite
replaces its previous run, so the top-level `build/` directory remains limited
to the current ROM, Pico UF2, and compiler-generated build inputs.

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
- **A held + Up from TABLE** HELP · plain d-pad turns pages · **A+Down** returns
- **Option 1 tap** restart all four tracks from the selected
  SONG/CHAIN/PHRASE context
- **B held + d-pad** edit value · **B held + A** cut · **B double-tap** paste,
  mint the next blank/unreferenced object on an empty SONG/CHAIN cell, or
  slim-clone an occupied one
- New PHRASE notes inherit the last instrument explicitly edited in the
  instrument column; command-field double-taps never erase NOTE/INSTR
- On stopped **INSTR**, tap **B** to audition the current instrument
- **Option 1 + B/A/←→** mute / solo / track select

## License / credits

By [little-scale](https://github.com/little-scale) (Sebastian Tomczak).
EEPROM driver derives from Bastian Schick's 93C46 code (cc65 Lynx
library). Built with [cc65](https://cc65.github.io/); dev verification on
[libretro-handy](https://github.com/libretro/libretro-handy).
