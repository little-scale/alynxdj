#!/usr/bin/env python3
"""Headless regression for two symmetric DAC slots on four tracks."""

import os
import shutil
import struct
import subprocess
import sys
import wave


def fail(message):
    raise SystemExit("DAC symmetry test: " + message)


def main():
    if len(sys.argv) != 4:
        fail("usage: test_dac_symmetry.py RETROSHOT CORE ROM")
    harness, core, rom = sys.argv[1:]
    build = os.path.dirname(os.path.abspath(rom))
    test_rom = os.path.join(build, "alynxdj-dac-test.lnx")
    ppm = os.path.join(build, "dac-test.ppm")
    ram_path = os.path.join(build, "dac-test.ram")
    shutil.copyfile(rom, test_rom)  # separate EEPROM namespace from the app

    env = os.environ.copy()
    env["RETROSHOT_RAM_OUT"] = ram_path
    env["RETROSHOT_RAM_POKE"] = "C02D:A5"
    env["RETROSHOT_RAM_POKE_AT"] = "100"
    subprocess.run(
        [harness, core, test_rom, ppm, "300"],
        env=env,
        check=True,
    )

    with open(ram_path, "rb") as f:
        ram = f.read()
    if len(ram) != 65536:
        fail("emulator did not return the 64 KB system RAM")

    count = ram[0xC022]
    trace = [tuple(ram[0xC030 + i * 3:0xC033 + i * 3])
             for i in range(min(count, 8))]
    expected = [(0, 0, 0), (1, 1, 8), (2, 0, 16), (3, 1, 24)]
    if trace[:4] != expected:
        fail("routing/steal trace %r, expected prefix %r" % (trace, expected))
    if tuple(ram[0xC025:0xC027]) != (1, 63):
        fail("same-row S command did not reach slot 1 at rate 63")
    if ram[0xC02B] < 3 or ram[0xC02C] < 2:
        fail("KIT retrigger/stream counts are too low: %d/%d"
             % (ram[0xC02B], ram[0xC02C]))

    with wave.open(ppm + ".wav", "rb") as wav:
        samples = wav.readframes(wav.getnframes())
    peak = max(abs(v) for (v,) in struct.iter_unpack("<h", samples))
    if peak < 100:
        fail("captured audio is silent")

    # Repeat the same four-track routing rig with the instrument changed to
    # table-WAV.  This type shares the exact two-slot budget with KIT.
    wave_ppm = os.path.join(build, "wave-test.ppm")
    wave_ram_path = os.path.join(build, "wave-test.ram")
    env["RETROSHOT_RAM_OUT"] = wave_ram_path
    env["RETROSHOT_RAM_POKE"] = "C02D:A8"
    subprocess.run(
        [harness, core, test_rom, wave_ppm, "300"], env=env, check=True)
    with open(wave_ram_path, "rb") as f:
        wave_ram = f.read()
    wave_count = wave_ram[0xC022]
    wave_trace = [tuple(wave_ram[0xC030 + i * 3:0xC033 + i * 3])
                  for i in range(min(wave_count, 8))]
    if wave_trace[:4] != expected:
        fail("table-WAV routing trace %r, expected prefix %r"
             % (wave_trace, expected))
    with wave.open(wave_ppm + ".wav", "rb") as wav:
        wave_samples = wav.readframes(wav.getnframes())
    wave_peak = max(abs(v) for (v,) in struct.iter_unpack("<h", wave_samples))
    if wave_peak < 100:
        fail("captured table-WAV audio is silent")

    print("DAC symmetry: PASS — KIT + table-WAV on A/B/C/D, two-slot cap, "
          "oldest steal, R retrigger, and same-row S rate")


if __name__ == "__main__":
    main()
