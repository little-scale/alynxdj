# ALYNXDJ save format (EEPROM / emulator `.eeprom` images)

Keep in sync with `src/tracker.h` (`struct songdata`) and `src/save.c` —
the standing sibling rule. Version: **6** (verified 2026-07-17).

`song-file-viewer.html` is the standalone reference inspector/editor for this
format. It accepts the same 2,048-byte image under `.eeprom`, `.e2p`, `.eep`,
`.sram`, `.srm`, `.sav`, or `.bin` names and exports a checksum-correct v6
image; all parsing and editing happens locally in the browser.

## Physical layer

93C86 serial EEPROM, 16-bit organisation: 1024 words / 2048 bytes.
Driver: `src/eeprom.s` (10-bit address commands, CS = cart counter A7,
CLK = A1, data = AUDIN). Its EWEN special command uses the canonical
all-ones address pattern from cc65's hardware driver; this matters to some
strict SD-cart EEPROM emulators even though those bits are don't-care on a
physical 93C86. Emulator images: Handy writes `<rom>.eeprom`
beside its save dir — 2048 bytes, words little-endian, so the file *is*
the cell array.

**Capacity:** 1016 payload words / **2032 bytes**. Cells 0–3 are the song
header and cells 1020–1023 are reserved for machine config, so song payload
occupies cells 4–1019.
Requires the repo-built core (stock libretro-handy truncates EEPROM file
*loads* to 1024 bytes, `lynx/eeprom.cpp:59`; fixed in
`tools/emu/handy-alynxdj.patch`, verified with a boundary-crossing save).

## Layout (word cells)

| Cell | Content |
|---|---|
| 0 | magic `'A' \| 'L'<<8` |
| 1 | magic `'D' \| 'J'<<8` |
| 2 | packed payload length in bytes |
| 3 | `version << 8 \| checksum` (checksum = 8-bit sum of packed bytes) |
| 4..1019 | packed payload, 2 bytes per word, little-endian |
| 1020 | config magic `'C' \| 'F'<<8` |
| 1021 | palette \| prelisten<<8 |
| 1022 | key repeat \| sync mode<<8 |
| 1023 | reserved |

## Payload: RLE over the flat song block

The payload unpacks to `struct songdata` verbatim (**7680 bytes**):
`song[128][4]` (chain #s), `chains[32][16]` ({phrase, tsp} pairs),
`phrases[64][16]` (4-byte steps), `instrs[32]` (16-byte records),
`tables[16][16]` (4-byte rows), `grooves[16][16]`, `waves[8][32]`
(signed 8-bit wavetables, appended in v3 — older payloads simply leave
the factory waves in place).

Instrument record bytes: 0 type, 1 vol, 2 **env** (ATK<<4 | DCY, 4-bit
times through the engine's `env_rate[]` curve — v2), 3 **TBS<<4 | HOLD**
(v6 table speed in the high nibble; envelope hold in the low nibble),
4 wave ($FF = hardware triangle, 0-7 = wavetable; v3), 5 taps bits 7-0,
6 table, 7 pan, 8 fine, 9 taps bit 8, 10 seed bits 7-0,
11 seed bits 11-8, 12 **SWP** (signed 1/16-semitone per tick; positive falls),
13 **VIB** (speed/depth nibbles), 14 **TRM** (speed/depth nibbles),
15 **TSP** (signed semitones). SWP/VIB/TRM are v4 and apply to TONE/NOISE;
TSP is v5 and applies to every instrument type before pitch/pad selection.
TBS is v6: 0 advances one table row per triggered note; 1–15 advance every
N engine ticks.

**v1 → v2 migration** (done automatically at load when the header version
is 1): the old per-tick ATK/DCY rate bytes fold onto the nearest
`env_rate[]` nibble; hold clamps to 15.

**v3 → v4 migration:** bytes 12–14 were reserved. The loader explicitly
clears them for every pre-v4 song so old or third-party saves cannot acquire
accidental sweep, vibrato, or tremolo.

**v4 → v5 migration:** byte 15 was reserved. The loader clears it for every
pre-v5 song so an old file cannot acquire an accidental instrument transpose.

**v5 → v6:** no rewrite is needed. All earlier writers stored HOLD as a
canonical 0–15 byte, so the newly assigned high nibble is already zero and
loads as note-clocked TBS 0.

RLE tokens: `t < $80` → literal, the next `t+1` bytes are verbatim;
`t >= $80` → run, the next byte repeats `(t-$80)+3` times (3–130).

**Sentinel discipline that keeps packs small:** empty song cells are `$FF`;
empty chain steps are `$FF $FF` (tsp is don't-care until a phrase is
inserted — the editor zeroes it then). Alternating `FF 00` patterns defeat
the RLE. The pre-fix demo packed at 1469 bytes; the current factory image,
including its out-of-loop verification rigs and v6 defaults, packs at
**1265/2032 bytes**.

## Status codes (FILES screen / `save.c`)

`ST_OK` 1, `ST_TOOBIG` 2 (meter red, save refused — D10: never truncate),
`ST_NODATA` 3 (no/bad magic), `ST_BADSUM` 4 (checksum mismatch, song left
untouched). Boot autoloads when the magic verifies (slot-0 policy).
