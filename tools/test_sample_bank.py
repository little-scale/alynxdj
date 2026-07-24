#!/usr/bin/env python3
"""Regression for the portable PL bank and protected 256 KB cart layout."""
import struct
import sys

sys.dont_write_bytecode = True
from alynxdj_pool import POOL_CAPACITY, SLOT_CAP, validate


ROM_BYTES = 64 + 256 * 1024
POOL_OFFSET = 64 + 45 * 1024
POOL_END = 64 + 250 * 1024
HELP_CODE = POOL_END
HELP_DATA = 64 + 251 * 1024
MIDI = 64 + 254 * 1024


def synthetic_max_slot_bank():
    samples = [bytes(SLOT_CAP)] + [b"\0"] * 7
    offset = 4 + 8 * 5
    directory = bytearray(b"PL\1\0")
    payload = bytearray()
    for sample in samples:
        directory += bytes((offset & 255, offset >> 8 & 255,
                            offset >> 16 & 255))
        directory += struct.pack("<H", len(sample))
        payload += sample
        offset += len(sample)
    return directory + payload


def main(rom_path, bank_path, browser_path, midi_path, help_code_path,
         help_data_path):
    bank = open(bank_path, "rb").read()
    kits, used = validate(bank)
    assert kits == 8 and used == len(bank)
    assert POOL_CAPACITY == POOL_END - POOL_OFFSET == 209920

    rom = open(rom_path, "rb").read()
    midi = open(midi_path, "rb").read()
    help_code = open(help_code_path, "rb").read()
    help_data = open(help_data_path, "rb").read()
    assert len(rom) == ROM_BYTES
    assert rom[POOL_OFFSET:POOL_OFFSET + len(bank)] == bank
    assert not any(rom[POOL_OFFSET + len(bank):POOL_END])
    assert rom[HELP_CODE:HELP_CODE + 4] == b"AHC1"
    assert rom[HELP_CODE + 4:HELP_CODE + 4 + len(help_code)] == help_code
    assert rom[HELP_DATA:HELP_DATA + len(help_data)] == help_data
    assert rom[HELP_DATA:HELP_DATA + 4] == b"AHD1"
    assert rom[MIDI:MIDI + len(midi)] == midi

    maximum = synthetic_max_slot_bank()
    assert validate(maximum)[1] == len(maximum)
    try:
        validate(maximum + b"\0")
    except ValueError as problem:
        assert "trailing" in str(problem)
    else:
        raise AssertionError("trailing sample-bank data was accepted")
    try:
        validate(maximum, len(maximum) - 1)
    except ValueError as problem:
        assert "capacity" in str(problem)
    else:
        raise AssertionError("an over-capacity sample bank was accepted")

    browser = open(browser_path, encoding="utf-8").read()
    for contract in ("const SLOT_CAP = 65535", "const POOL_END = 64 + 250 * 1024",
                     "const PRE_HELP_POOL_END = 64 + 254 * 1024",
                     'hasMagic(bytes,HELP_DATA_OFFSET,"AHD1")',
                     "patched.fill(0,POOL_OFFSET,source.poolEnd)", "function parseBank(input)",
                     "function prepareSample(source", "data-trim-start", "data-trim-end",
                     "Download sample bank", "Import sample bank"):
        assert contract in browser, "browser contract missing: " + contract

    print("sample bank: PASS — factory binary, max u16 slot, protected pool, "
          "HELP/MIDI tails, and browser import/export/trim contract")


if __name__ == "__main__":
    if len(sys.argv) != 7:
        raise SystemExit(
            "usage: test_sample_bank.py ROM BANK BROWSER AUXMIDI AUXHELP HELPDATA")
    main(*sys.argv[1:])
