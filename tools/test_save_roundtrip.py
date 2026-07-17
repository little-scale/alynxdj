#!/usr/bin/env python3
"""Power-cycle regression for the packed 93C86 song save."""

import os
import shutil
import subprocess
import sys
import time


def fail(message):
    raise SystemExit("save roundtrip test: " + message)


def run(harness, core, rom, ppm, ram, poke=False):
    env = os.environ.copy()
    env["RETROSHOT_RAM_OUT"] = ram
    if poke:
        env["RETROSHOT_RAM_POKE"] = "C02D:A6"
        env["RETROSHOT_RAM_POKE_AT"] = "100"
    subprocess.run([harness, core, rom, ppm, "2500"], env=env, check=True)
    with open(ram, "rb") as f:
        return f.read()


def u16(data, address):
    return data[address] | data[address + 1] << 8


def main():
    if len(sys.argv) != 4:
        fail("usage: test_save_roundtrip.py RETROSHOT CORE ROM")
    harness, core, rom = sys.argv[1:]
    build = os.path.dirname(os.path.abspath(rom))
    # The PID creates a clean emulator-side EEPROM namespace without deleting
    # any user's or prior test's persistent file.
    test_rom = os.path.join(
        build, "alynxdj-save-test-%d-%d.lnx" % (os.getpid(), time.time_ns()))
    shutil.copyfile(rom, test_rom)

    first = run(harness, core, test_rom,
                os.path.join(build, "save-write.ppm"),
                os.path.join(build, "save-write.ram"), True)
    packed = u16(first, 0xC01A)
    checksum = u16(first, 0xC018)
    if first[0xC01D] != 1:
        fail("write status is %d, expected ST_OK" % first[0xC01D])
    if not 0 < packed <= 2032:
        fail("packed size %d is outside the 93C86 payload budget" % packed)

    second = run(harness, core, test_rom,
                 os.path.join(build, "save-load.ppm"),
                 os.path.join(build, "save-load.ram"))
    if second[0xC01D] != 1:
        fail("power-cycle load status is %d, expected ST_OK" % second[0xC01D])
    if u16(second, 0xC018) != checksum:
        fail("song checksum changed across the power cycle")

    print("save roundtrip: PASS — %d/2032 bytes, checksum $%04X"
          % (packed, checksum))


if __name__ == "__main__":
    main()
