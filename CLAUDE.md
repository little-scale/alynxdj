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

**Current state: greenfield.** No code, no toolchain, no Makefile, no design docs
exist yet. The first tasks per the brief are: (1) set up a Lynx toolchain plus
**headless emulation** so progress can be self-verified (screenshots, like
GENMDDJ's `make shot` retroshot harness), (2) write the design/plan documents,
deciding what ports from the sibling trackers and what is Lynx-unique, then
(3) build in milestones.

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

## Toolchain notes (to be decided and pinned in DESIGN.md)

Nothing is installed or committed yet. The Lynx's CPU is a 65C02 (Mikey), so the
sibling Z80/68k assemblers do not carry over; the established Lynx homebrew
toolchain is **cc65** (`ca65`/`cl65` with the `lynx` target, which can produce a
headered `.lnx` ROM). Emulation candidates for the headless harness: the
**handy/holani/mednafen** Lynx cores under a libretro screenshot runner (port of
GENMDDJ's retroshot). Whatever is chosen, record it (and its gotchas) here and in
DESIGN.md, and keep the GENMDDJ pattern: `make` → `build/alynxdj.lnx`,
`make shot` → headless `build/shot.png`, `make run` → windowed emulator.
