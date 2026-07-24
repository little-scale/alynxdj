#!/usr/bin/env python3
"""HELP data-format and on-console navigation regression."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import time


HELP_PAGE = 0xC8FD
MIRROR_SCREEN = 0xC003
SCR_HELP = 10
SCR_TABLE = 4


def fail(message):
    raise SystemExit("help screen test: " + message)


def decode(data):
    if data[:4] != b"AHD1":
        fail("generated help data is missing the AHD1 signature")
    count = data[4]
    if count < 2 or count > 9:
        fail("generated help has an invalid page count")
    offsets = [int.from_bytes(data[6 + i * 2:8 + i * 2], "little")
               for i in range(count)]
    pages = []
    for offset in offsets:
        rows = []
        nrows = data[offset]
        offset += 1
        if nrows > 16:
            fail("a generated page exceeds 16 rows")
        for _ in range(nrows):
            tag = data[offset]
            offset += 1
            packed = data[offset:offset + (tag & 0x3F)]
            offset += tag & 0x3F
            row = bytearray()
            for byte in packed:
                row.extend(b" " * byte if byte < 0x20 else bytes((byte,)))
            if len(row) > 38:
                fail("a decoded HELP row exceeds 38 columns")
            rows.append(row.decode("ascii"))
        pages.append(rows)
    return pages


def run(harness, core, rom, build, label, tail):
    test_rom = build / (
        "alynxdj-help-%s-%d-%d.lnx" % (label, os.getpid(), time.time_ns()))
    ppm = build / ("help-%s.ppm" % label)
    ram_path = build / ("help-%s.ram" % label)
    shutil.copyfile(rom, test_rom)
    env = os.environ.copy()
    env["RETROSHOT_RAM_OUT"] = str(ram_path)

    # Five Right attempts are intentional: four drill SONG -> TABLE and the
    # harmless fifth makes the rig insensitive to the first post-splash edge.
    entry = ("0@300,"
             "100@10,180@20,100@10,180@20,100@10,180@20,"
             "100@10,180@20,100@10,180@20,100@10,110@20,0@20,")
    subprocess.run(
        [harness, core, str(test_rom), str(ppm), "560", entry + tail],
        env=env, check=True)
    ram = ram_path.read_bytes()
    if len(ram) != 65536:
        fail(label + " did not return a full RAM dump")
    return ram, ppm.read_bytes()


def main(harness, core, rom, build_dir, help_path):
    build = Path(build_dir)
    build.mkdir(parents=True, exist_ok=True)
    data = Path(help_path).read_bytes()
    pages = decode(data)
    if not pages[0][0].startswith("NAVIGATION:"):
        fail("page 1 is not the navigation quick reference")
    if not any(row.startswith("COMMANDS A-L:") for row in pages[5]):
        fail("command reference ordering changed unexpectedly")
    if len(data) > 3 * 1024:
        fail("HELP data exceeds its three cart blocks")

    entered, first_image = run(
        harness, core, rom, build, "entered", "0@100")
    if entered[MIRROR_SCREEN] != SCR_HELP or entered[HELP_PAGE] != 0:
        fail("A+Up from TABLE did not enter HELP page 1")

    next_page, second_image = run(
        harness, core, rom, build, "next", "80@4,0@96")
    if next_page[MIRROR_SCREEN] != SCR_HELP or next_page[HELP_PAGE] != 1:
        fail("plain Right did not advance HELP to page 2")
    if first_image == second_image:
        fail("turning the HELP page did not change the rendered frame")

    wrapped, _ = run(
        harness, core, rom, build, "wrap", "40@4,0@96")
    if wrapped[MIRROR_SCREEN] != SCR_HELP or wrapped[HELP_PAGE] != len(pages) - 1:
        fail("plain Left did not wrap HELP to the final page")

    exited, _ = run(
        harness, core, rom, build, "exit", "100@8,120@8,0@84")
    if exited[MIRROR_SCREEN] != SCR_TABLE:
        fail("A+Down did not return from HELP to TABLE")

    print("help screen: PASS — validated source data, TABLE entry/exit, "
          "paged rendering, and wraparound")


if __name__ == "__main__":
    if len(sys.argv) != 6:
        raise SystemExit(
            "usage: test_help_screen.py HARNESS CORE ROM BUILD_DIR HELPDATA")
    main(*sys.argv[1:])
