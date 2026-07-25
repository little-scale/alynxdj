#!/usr/bin/env python3
"""Regression for the portable PL bank and protected 256 KB cart layout."""
import os
import struct
import sys

import numpy as np

sys.dont_write_bytecode = True
from alynxdj_pool import (FACTORY_GAIN_DB, PCM_RATE, POOL_CAPACITY, RATE_ID,
                          SLOT_CAP, decode_pcm, load, master_pcm, rate_for_bank,
                          validate)


ROM_BYTES = 64 + 256 * 1024
POOL_OFFSET = 64 + 45 * 1024
POOL_END = 64 + 250 * 1024
HELP_CODE = POOL_END
HELP_DATA = 64 + 251 * 1024
MIDI = 64 + 254 * 1024
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FACTORY_BANK = os.path.join(ROOT, "samples", "alynxdj-factory-samples.bin")


def synthetic_max_slot_bank():
    samples = [bytes(SLOT_CAP)] + [b"\0"] * 7
    offset = 4 + 8 * 5
    directory = bytearray((ord("P"), ord("L"), 1, RATE_ID))
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
    assert 1 <= kits <= 8 and used == len(bank)
    assert bank[3] == RATE_ID and abs(rate_for_bank(bank) - 5208.333333) < 0.001
    assert POOL_CAPACITY == POOL_END - POOL_OFFSET == 209920

    # The current factory kit includes 24-bit WAV sources. Decode a signed
    # 24-bit triplet at both rails and around zero so byte/sample confusion
    # cannot silently triple duration again.
    pcm24 = bytes((0, 0, 128, 255, 255, 255, 0, 0, 0,
                   1, 0, 0, 255, 255, 127))
    decoded = decode_pcm(pcm24, 3)
    assert len(decoded) == 5
    assert decoded[0] == -1.0 and decoded[2] == 0.0
    assert decoded[1] < 0 < decoded[3] < decoded[4] < 1.0

    # Factory WAV conversion peak-normalizes to the former 120/127 baseline,
    # then applies +12 dB before tanh limiting. Pin exact signed bytes so a
    # later refactor cannot silently revert to linear normalization.
    mastering_probe = master_pcm(
        np.array((-1.0, -0.5, -0.25, 0.0, 0.25, 0.5, 1.0),
                 dtype=np.float32))
    assert FACTORY_GAIN_DB == 12.0
    assert tuple(mastering_probe) == (-127, -121, -93, 0, 93, 121, 127)

    # The tracked factory binary must actually be rebuilt from every current
    # WAV with the canonical converter, rather than merely passing PL layout
    # validation after conversion behavior changes.
    if os.path.realpath(bank_path) == os.path.realpath(FACTORY_BANK):
        sample_root = os.path.join(ROOT, "samples")
        kit_dirs = sorted(
            directory for directory in os.listdir(sample_root)
            if os.path.isdir(os.path.join(sample_root, directory)))
        expected_slots = []
        for directory in kit_dirs:
            kit_path = os.path.join(sample_root, directory)
            wavs = sorted(name for name in os.listdir(kit_path)
                          if name.lower().endswith(".wav"))[:8]
            expected_slots.extend(load(os.path.join(kit_path, name))
                                  for name in wavs)
            expected_slots.extend(np.zeros(1, np.int8)
                                  for _ in range(8 - len(wavs)))
        assert len(expected_slots) == kits * 8
        for index, expected in enumerate(expected_slots):
            entry = 4 + index * 5
            offset = bank[entry] | bank[entry + 1] << 8 | bank[entry + 2] << 16
            length = bank[entry + 3] | bank[entry + 4] << 8
            assert bank[offset:offset + length] == expected.tobytes(), \
                "factory bank is stale at slot %d" % index

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
    for contract in ("const RATE_ID = 1", "const PCM_RATE = 1000000 / 192",
                     "const MAX_KITS = 8", "function expandKits(kits)",
                     "const SLOT_CAP = 65535", "const POOL_END = 64 + 250 * 1024",
                     "const PRE_HELP_POOL_END = 64 + 254 * 1024",
                     'hasMagic(bytes,HELP_DATA_OFFSET,"AHD1")',
                     "patched.fill(0,POOL_OFFSET,source.poolEnd)", "function parseBank(input)",
                     "function prepareSample(source", "data-trim-start", "data-trim-end",
                     "function convertChannelsForSlicing", "function sliceSample(source",
                     "Normalization off.", "data-slice-pad", "data-slice-fade",
                     "Download sample bank", "Import sample bank"):
        assert contract in browser, "browser contract missing: " + contract

    print("sample bank: PASS — 5,208.333 Hz factory binary, +12 dB tanh "
          "mastering, 24-bit WAV input, max u16 slot, protected pool, "
          "HELP/MIDI tails, and browser contract")


if __name__ == "__main__":
    if len(sys.argv) != 7:
        raise SystemExit(
            "usage: test_sample_bank.py ROM BANK BROWSER AUXMIDI AUXHELP HELPDATA")
    main(*sys.argv[1:])
