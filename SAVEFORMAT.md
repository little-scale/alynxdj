# ALYNXDJ save format (EEPROM / emulator `.eeprom` images)

Keep in sync with `src/tracker.h` (`struct songdata`) and `src/save.c` —
the standing sibling rule. Version: **1** (2026-07-02).

## Physical layer

93C86 serial EEPROM, 16-bit organisation: 1024 words / 2048 bytes.
Driver: `src/eeprom.s` (10-bit address commands, CS = cart counter A7,
CLK = A1, data = AUDIN). Emulator images: Handy writes `<rom>.eeprom`
beside its save dir — 2048 bytes, words little-endian, so the file *is*
the cell array.

**Capacity cap:** stock libretro-handy truncates EEPROM file *loads* to
1024 bytes (`lynx/eeprom.cpp:59` reads 1024 instead of `Size()`), so
cells ≥ 512 do not survive a power cycle in the emulator. The save
therefore uses words 0–511 (`SAVE_CAP_BYTES` 1016 payload). Lift to the
full 2 KB when the core fix ships / real hardware verifies.

## Layout (word cells)

| Cell | Content |
|---|---|
| 0 | magic `'A' \| 'L'<<8` |
| 1 | magic `'D' \| 'J'<<8` |
| 2 | packed payload length in bytes |
| 3 | `version << 8 \| checksum` (checksum = 8-bit sum of packed bytes) |
| 4.. | packed payload, 2 bytes per word, little-endian |

## Payload: RLE over the flat song block

The payload unpacks to `struct songdata` verbatim (7424 bytes):
`song[128][4]` (chain #s), `chains[32][16]` ({phrase, tsp} pairs),
`phrases[64][16]` (4-byte steps), `instrs[32]` (16-byte records),
`tables[16][16]` (4-byte rows), `grooves[16][16]`.

Instrument record bytes: 0 type, 1 vol, 2 atk, 3 hold, 4 dcy,
5 taps bits 7-0, 6 table, 7 pan, 8 fine, 9 taps bit 8, 10 seed bits 7-0,
11 seed bits 11-8, 12-15 reserved (TAPS/SEED per DESIGN.md D11).

RLE tokens: `t < $80` → literal, the next `t+1` bytes are verbatim;
`t >= $80` → run, the next byte repeats `(t-$80)+3` times (3–130).

**Sentinel discipline that keeps packs small:** empty song cells are `$FF`;
empty chain steps are `$FF $FF` (tsp is don't-care until a phrase is
inserted — the editor zeroes it then). Alternating `FF 00` patterns defeat
the RLE (the pre-fix demo packed at 1469 bytes; with FF-FF sentinels it
packs at 480).

## Status codes (FILES screen / `save.c`)

`ST_OK` 1, `ST_TOOBIG` 2 (meter red, save refused — D10: never truncate),
`ST_NODATA` 3 (no/bad magic), `ST_BADSUM` 4 (checksum mismatch, song left
untouched). Boot autoloads when the magic verifies (slot-0 policy).
