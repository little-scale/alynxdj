# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

**ALYNXDJ** — an LSDJ-inspired music tracker for the **Atari Lynx**, the third
tracker in the author's family after **SMSGGDJ** (Master System / Game Gear,
`/Users/a1106632/Documents/sms_tracker`) and **GENMDDJ** (Mega Drive,
`/Users/a1106632/Documents/genmddj`). The brief is `task.txt`: port as much of the
design, control scheme, command set, and GUI feel of SMSGGDJ/GENMDDJ as makes sense,
while exploiting everything unique to the Lynx sound hardware (Mikey's 4 channels of
LFSR/polynomial-counter synthesis, per-channel 8-bit DAC access, stereo panning on
later hardware). The ROM and project name is **ALYNXDJ**.

**DESIGN.md is the contract** (with a §0 decision log — read it before design
decisions, don't re-litigate settled ones); **PLAN.md** holds the milestone
table (M0 toolchain ✅ done; M1 boot next). Key settled decisions: 4 identical
tracks each playing TONE/NOISE/WAV/KIT (D1), cc65 C editor + ca65 asm
driver/IRQ/render (D2), 75 Hz VBlank engine tick with no region split (D3),
flat RAM song block but **packed/RLE EEPROM save — 2 KB 93C86 is the whole
persistence budget** (D4/D10), per-channel timer-IRQ PCM capped at 2 voices
(D5/D6), 4×6 font on a 40×17 grid (D7).

## Build and verify

```sh
make          # cl65 -t lynx → build/alynxdj.lnx
make shot     # headless Handy run → build/shot.png + build/shot.ppm.wav (audio!)
              # FRAMES=n and BTN=maskHex or BTN=mask@frames,mask@frames,... for input
make clean
```

- Toolchain: **cc65** (Homebrew). `src/lowcode.s` works around a cc65 2.18
  lynx.cfg bug (optional LOWCODE segment vs defdir.s's unconditional
  `__LOWCODE_SIZE__` import) — keep it linked.
- `tools/emu/retroshot` (harness.c, ported from GENMDDJ) drives
  `handy_libretro.dylib` with **no lynxboot.img** — Handy HLE-boots the cart
  and logs a harmless BIOS warning. It captures the last frame (PPM→PNG) and
  the full audio (WAV) — the WAV is how sound milestones get FFT-verified.
- BTN mask bits (RetroPad→Lynx, probed at M2): `$100`=A, `$1`=B, `$400`=Opt1,
  `$800`=Opt2, `$10`=Up, `$20`=Down, `$40`=Left, `$80`=Right, `$8`=Pause
  (START; lands in `SUZY.switches` bit 0, not the joystick byte).
- `RETROSHOT_RAM_OUT=<path>` dumps the full 64 KB RAM after a run — read any
  fixed address (e.g. debug mirrors) instead of scraping pixels.
- Handy is dev-speed, not silicon: its LFSR-timbre and DAC-timing fidelity is
  suspect (DESIGN.md Q4) — hardware passes at M6/M7.

## The reference projects (read before designing anything)

The sibling trackers are the specification for how ALYNXDJ should *feel*. Their
CLAUDE.md, `DESIGN.md`, `PLAN.md`, and `SAVEFORMAT.md` files are the reference:

- **`/Users/a1106632/Documents/sms_tracker`** (SMSGGDJ) — the original, shipped and
  hardware-verified. Source of the data model (phrase → chain → song + pools),
  the command set, grooves/tables, the flat contiguous save image, and the
  **control-scheme contract**: two modifier buttons (project-level and item-level),
  "the button already held when another arrives selects the action", **no
  simultaneous-press timing windows** (only paste double-taps).
- **`/Users/a1106632/Documents/genmddj`** (GENMDDJ) — the first port; its CLAUDE.md
  §"What ports verbatim vs what's new" is the template for the same analysis here.
  Its `tools/emu/retroshot` libretro screenshot harness is the model for headless
  verification (a Lynx libretro core such as handy/mednafen-lynx should slot into
  the same pattern), and its Makefile + Python tool pipeline (makefont.py,
  maketables.py, sample conversion) is the model for the build.

Process discipline inherited from both: **DESIGN.md is the contract** once written
(with a decision log — don't re-litigate settled decisions), SAVEFORMAT.md stays in
sync with any RAM-layout change, work proceeds in milestones from PLAN.md with a
commit at each boundary, a git-hash **build stamp on the boot splash** to catch
stale flashes, and per-region timing tables where applicable.

## Assets already present

- `samples/` — WAV source material for the sample pool, organised as 8-slot kits,
  mirroring the sibling projects' sample pipeline:
  - `01 808`, `02 909`, `03 C78`, `04 606` — drum-machine kits, 8 one-shots each,
    named `NN XX.wav` (BD/SD/HH/…).
  - `05 speech 1` … `08 speech 4` — speech/phoneme banks (AA.wav, AE.wav, …).
  - A conversion tool (successor to SMSGGDJ's `smsggdj_sample.py`) will need to
    resample these for the Lynx DAC.
- `artwork/` — empty; destined for logo/splash art (siblings use an `art/` dir with
  a makelogo.py-style pipeline).
- `task.txt` — the original project brief.

## Hardware quick facts (full picture: DESIGN.md §1)

65C02 @ ~4 MHz, **64 KB unified RAM — code, song, and framebuffer all live in
it** (the cart is a block-serial device, streamed not mapped). Video 160×102
4bpp framebuffer, ~75 Hz. Mikey audio: 4 identical channels ($FD20+8n), each
a 12-bit LFSR polynomial counter = square / pitched noise / integrate ramps,
**or a directly-written 8-bit signed DAC** (the PCM path). Stereo ATTEN on
Lynx II only (write-always, D8). ComLynx UART = the sync bus. Saves go to a
93Cxx EEPROM, **2 KB max** — the design's tightest constraint. cc65's
`_mikey.h`/`_suzy.h` (in `/opt/homebrew/share/cc65/include/`) are the local
authoritative register maps.
