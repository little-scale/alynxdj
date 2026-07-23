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
    build = os.path.join(os.path.dirname(os.path.abspath(rom)),
                         "tests", "dac")
    shutil.rmtree(build, ignore_errors=True)
    os.makedirs(build)
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
    # The first two simultaneous voices must occupy both DAC slots. Later KIT
    # one-shots may either steal the oldest live slot or reuse a slot whose
    # short sample has already completed; main-loop speed legitimately changes
    # which case occurs. Track routing and channel register offsets stay exact.
    # The looping table-WAV rig below retains the strict oldest-steal check,
    # because none of those voices can complete between triggers.
    if len(trace) < 4:
        fail("DAC routing trace is too short: %r" % (trace,))
    expected_routes = [(0, 0), (1, 8), (2, 16), (3, 24)]
    actual_routes = [(ch, dac_off) for ch, _slot, dac_off in trace[:4]]
    if actual_routes != expected_routes:
        fail("DAC channel routing %r, expected %r"
             % (actual_routes, expected_routes))
    if trace[0][1] != 0 or trace[1][1] != 1:
        fail("first two simultaneous DAC voices did not use both slots: %r"
             % (trace,))
    if any(slot not in (0, 1) for _ch, slot, _dac_off in trace[:4]):
        fail("DAC routing used an invalid slot: %r" % (trace,))
    if tuple(ram[0xC025:0xC027]) != (1, 63):
        fail("same-row S command did not reach slot 1 at rate 63")
    trigger_counts = tuple(ram[0xC02B:0xC02D])
    # Slot ownership can legitimately skew once short one-shots finish, but
    # both streamers must have run and R02 must produce repeated triggers.
    if min(trigger_counts) < 1 or sum(trigger_counts) < 5:
        fail("KIT retrigger/stream counts are too low: %d/%d"
             % trigger_counts)

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
    expected_steal = [(0, 0, 0), (1, 1, 8), (2, 0, 16), (3, 1, 24)]
    if wave_trace[:4] != expected_steal:
        fail("table-WAV routing trace %r, expected prefix %r"
             % (wave_trace, expected_steal))
    with wave.open(wave_ppm + ".wav", "rb") as wav:
        wave_samples = wav.readframes(wav.getnframes())
    wave_peak = max(abs(v) for (v,) in struct.iter_unpack("<h", wave_samples))
    if wave_peak < 100:
        fail("captured table-WAV audio is silent")

    print("DAC symmetry: PASS — KIT + table-WAV on A/B/C/D, two-slot cap, "
          "oldest steal, R retrigger, and same-row S rate")


if __name__ == "__main__":
    main()
