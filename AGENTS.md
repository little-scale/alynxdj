# AGENTS.md

This file provides guidance to Codex (Codex.ai/code) when working with code in this repository.

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
history through M26. Key settled decisions: 4 identical
tracks each playing TONE/NOISE/WAV/KIT (D1), cc65 C editor + ca65 asm
driver/IRQ/render (D2), 59.9 Hz VBlank engine tick with no region split (D3),
flat RAM song block but **packed/RLE EEPROM save — 2 KB 93C86 is the whole
persistence budget** (D4/D10), per-channel timer-IRQ PCM capped at 2 voices
(D5/D6), 4×6 font on a 40×17 grid (D7). KIT and table-WAV dynamically
target the owning channel A–D; there are no fixed sample buses. TONE/NOISE
patches persist TSP/SWP/VIB/TRM plus TBS in save-format v6 (D15–D17); VIB is a
centred ~0.47–7.49 Hz sine whose phase free-runs across notes and resets at
the transport boundary. D19/D20 add receive-only ComLynx MIDI takeover and
clock sync: full-status MIDI channels 1–4 route to tracks A–D/instruments
01–04, while the Pico divides USB MIDI's 24 PPQN into one `IN24` pulse per
tracker row. `IN` and `IN24` arm locally at the cued row as `WAIT`; their
first row pulse starts that row and changes the transport to `PLAY`. There is
no heartbeat.
Command values retain their historical/save-format IDs, but PHRASE and TABLE
selection steps through the implemented letters alphabetically in both
directions (D21). A clean Option-1 tap starts all four tracks from the
selected SONG/CHAIN/PHRASE context while its held mute/solo layer remains
unchanged (D28). Phrase `H` is a pre-row branch and each track loops only its
current contiguous vertical SONG group (D29). D31 adds a nine-page,
build-validated HELP screen above TABLE; it stops transport and cold-loads over
the idle PCM rings. D32 adds deterministic row-00 hierarchy entry, an in-page
INSTR selector, and field-safe TABLE command deletion. D18's B command is a
cumulative signed taps accumulator; B00 restores the active patch. D33 gives
TABLE a playhead for the explicitly selected top-bar track without adding
engine-tick work.

## Build and verify

```sh
make          # cl65 -t lynx → build/alynxdj.lnx
make factory-samples # rebuild samples/alynxdj-factory-samples.bin from WAVs
make SAMPLE_BANK=/path/custom.bin # validate + inject one portable sample bank
make shot     # headless Handy run → build/shot.png + build/shot.ppm.wav (audio!)
make test     # DAC/sample + modulation/TBS/taps + editor + EEPROM + MIDI UART
make pico     # Pico USB-MIDI bridge UF2 (PICO_SDK_PATH + full Arm toolchain)
              # FRAMES=n and BTN=maskHex or BTN=mask@frames,mask@frames,... for input
make clean
```

Python tools require `requirements.txt`; set `PYTHON=/path/to/python` when
using a virtual environment.

- Toolchain: **cc65** (Homebrew). `src/lowcode.s` works around a cc65 2.18
  lynx.cfg bug (optional LOWCODE segment vs defdir.s's unconditional
  `__LOWCODE_SIZE__` import) — keep it linked.
- `tools/emu/retroshot` (harness.c, ported from GENMDDJ) drives
  `handy_libretro.dylib` with **no lynxboot.img** — Handy HLE-boots the cart
  and logs a harmless BIOS warning. It captures the last frame (PPM→PNG) and
  the full audio (WAV) — the WAV is how sound milestones get FFT-verified.
- The core dylib is **repo-built from libretro-handy master +
  `tools/emu/handy-alynxdj.patch`** (two fixes: EEPROM file loads truncated
  to 1024 bytes at eeprom.cpp:59, and `handy_comlynx_*` C exports for the
  bridge). Rebuild: clone libretro-handy, `git apply` the patch,
  `make platform=osx`, copy the dylib into tools/emu/. Both fixes are
  upstream-PR-worthy (the EEPROM one especially).
- `tools/emu/duoshot` runs **two bridged core instances** (ComLynx
  cross-wired TX→RX) for two-unit sync tests: needs two *file copies* of
  the dylib (dlopen dedups identical paths). Per-unit scripts, WAV/PPM/RAM
  outputs. Verified: OUT→IN lock at −1.4 ms envelope lag.
- **cc65 lynx.h bug:** `_UART_TIMER` points at $FD14 = **timer 5**; timer 4
  (the real UART clock) is $FD10 — use explicit addresses (src/sync.c).
- BTN mask bits (RetroPad→Lynx, probed at M2): `$100`=A, `$1`=B, `$400`=Opt1,
  `$800`=Opt2, `$10`=Up, `$20`=Down, `$40`=Left, `$80`=Right, `$8`=Pause
  (START; lands in `SUZY.switches` bit 0, not the joystick byte).
- **A/B are swapped in firmware** (main.c reads `SUZY.joystick` then exchanges
  bits 0/1 — hardware ergonomics fix): the app's edit/insert (internal "A")
  fires on the *physical B* button = harness mask `$1`, and back/nav
  (internal "B") on *physical A* = `$100`. So in scripts the transport
  gesture is now **physical A-held then B** (`100@n,101@n,...`), the inverse
  of the old B-then-A. MANUAL/README control tables use physical labels.
- `RETROSHOT_RAM_OUT=<path>` dumps the full 64 KB RAM after a run — read any
  fixed address (e.g. debug mirrors) instead of scraping pixels.
- Regression artifacts live under `build/tests/<suite>/`; each suite clears
  and replaces only its own directory before running. Keep the top level of
  `build/` for canonical build products and compiler-generated inputs.
- RAM pressure is explicit: cart blocks 40/42/44 cold-load code to `$C900`,
  `$F600`, and `$F320`; the sample bank occupies blocks 45–249. HELP code is
  cart block 250 and its `AHD1` text is blocks 251–253; MIDI remains 254–255.
  The C stack is 512 bytes and `$D000-$D3FF` is normally two 512-byte rings.
  Entering HELP stops transport and cold-loads its renderer at `$D004` over
  those idle rings; FILES PURGE shares that stopped-only code overlay, and
  its scratch buffers are `$D3B0-$D3FF`. Keep
  `alynxdj.cfg`, `Makefile`, `src/main.c`, `src/pool.c`, and
  `sample-patch-browser.html` aligned.
- MIDI/sync, contextual-play, and editor cold code is packed into cart blocks
  254–255 and linked at `$C100-$C8FC`, overlaying the EEPROM pack buffer
  between SAVE/LOAD calls.
  The MIDI RX ring overlays phrase pass counters at `$C048-$C087`, valid only
  because takeover stops the sequencer. `IN24` must retain those counters, so
  it has a separate 64-byte IRQ ring over the phrase clipboard at
  `$C088-$C0C7` with state at
  `$C0F9-$C0FB`; `$C0FC-$C0FF` holds the four per-track G countdowns.
  `$C8FD` is the HELP page and `$C8FE-$C8FF` remains spare. Reloading the
  full two-block helper window also resets the HELP page byte to zero.
  Fixed live bytes occupy
  `$C004-$C016`. Preserve the post-pack
  reloads and these mutual-exclusion rules.
- Cart sample seeks use a software 16-bit bytes-remaining counter. Its low
  byte must be tested before decrement so `$0400` borrows to `$03FF`; getting
  that order wrong replays the preceding page at every 1 KB sample boundary.
  The kit-00 F4 ring regression in `test_hardware_fixes.py` covers two crosses.
- Sample refills retain the physical cart cursor while one voice owns it;
  directory reads invalidate `$C029`, and alternating voices re-seek. Refill
  in 64-byte pieces, publish every piece atomically, continue across the ring
  wrap in the same pump, and top up after playhead redraws. Full screen paints
  must yield every four glyphs, and `clear_grid()` must stay split into sixteen
  six-pixel bands; otherwise rendering can starve a pending trigger or ring.
  Same-track KIT retriggers leave the old stream live until `do_trigger()` has
  prepared the replacement. A single long chunk or a one-sided ring refill
  causes the IRQ to hold the last DAC value.
  `$C027/$C028` are saturating slot-underrun counters; isolated and sustained
  sample/redraw rigs must complete at `00/00` in `test_hardware_fixes.py`;
  `$C02E/$C02F` count successfully started streams.
- `pico-midi-comlynx/` is the companion RP2040 USB-device firmware. Its PIO
  output is open-drain (output-low/input only), 62.5 kbaud, start + 8 data +
  fixed-Space ninth + stop. It forwards channel 1–4 messages, divides every
  six USB `F8` clocks into one row-rate `F8`, and forwards `FA/FB/FC/FF`; it
  deliberately has no heartbeat and is not a USB host.
- Handy is dev-speed, not silicon: its LFSR-timbre and DAC-timing fidelity is
  suspect (DESIGN.md Q4) — hardware passes at M6/M7. **Measured 2026-07-16:
  this core renders every FEEDBACK/tap config identically** (static square
  vs noise FFTs are bit-identical, cosine 1.0000) — it does not emulate the
  LFSR feedback at all. So TAPS, the `N` override, and the `G` tap-glide
  can't be heard or FFT-verified in Handy; verify them at the register level
  (dump the voice shadow / `sh_feedback`) and on real hardware only.
- **cc65 comparison gotcha:** `int_var > uchar_var` can compile as an
  *unsigned* compare — a negative int silently becomes huge (bit the L
  slide: it diverged an octave down). Cast explicitly:
  `int r = (int)uchar_var;` then compare against `r`.
- Boot starts with `song_clear()` and **autoloads** a valid EEPROM save over
  it; the factory `song_demo()` is loaded only through FILES → DEMO. Use a
  unique ROM basename for save/demo/rig tests to isolate the emulator EEPROM
  namespace.
- **ElCheapoSD for Lynx is not song-save compatible:** it contains a physical
  128-byte 93C46 only, while ALYNXDJ requires a 2 KB 93C86. It can run the ROM
  but cannot persist a full song, and its cart API is menu-oriented rather
  than general SD file access. A 128-byte `.sav` cannot be padded or migrated.

## The reference projects (read before designing anything)

The sibling trackers are the specification for how ALYNXDJ should *feel*. Their
AGENTS.md, `DESIGN.md`, `PLAN.md`, and `SAVEFORMAT.md` files are the reference:

- **`/Users/a1106632/Documents/sms_tracker`** (SMSGGDJ) — the original, shipped and
  hardware-verified. Source of the data model (phrase → chain → song + pools),
  the command set, grooves/tables, the flat contiguous save image, and the
  **control-scheme contract**: two modifier buttons (project-level and item-level),
  "the button already held when another arrives selects the action", **no
  simultaneous-press timing windows** (only paste double-taps).
- **`/Users/a1106632/Documents/genmddj`** (GENMDDJ) — the first port; its AGENTS.md
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
  - `alynxdj-factory-samples.bin` is the portable, build-ready 64-slot factory
    bank. `tools/alynxdj_pool.py` rebuilds/validates it; the browser imports and
    exports the same `PL` format. Slots are u16-sized (65,535 bytes maximum),
    and the 256 KB ROM reserves 209,920 bytes for the complete bank.
- `artwork/` — empty; destined for logo/splash art (siblings use an `art/` dir with
  a makelogo.py-style pipeline).
- `task.txt` — the original project brief.

## Hardware quick facts (full picture: DESIGN.md §1)

65C02 @ ~4 MHz, **64 KB unified RAM — code, song, and framebuffer all live in
it** (the cart is a block-serial device, streamed not mapped). Video 160×102
4bpp framebuffer, ~59.9 Hz with the current crt0 timing. Mikey audio: 4 identical channels ($FD20+8n), each
a 12-bit LFSR polynomial counter = square / pitched noise / integrate ramps,
**or a directly-written 8-bit signed DAC** (the PCM path). Stereo ATTEN on
Lynx II only (write-always, D8). ComLynx UART = the sync bus. Saves go to a
93Cxx EEPROM, **2 KB max** — the design's tightest constraint. cc65's
`_mikey.h`/`_suzy.h` (in `/opt/homebrew/share/cc65/include/`) are the local
authoritative register maps.
