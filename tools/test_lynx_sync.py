#!/usr/bin/env python3
"""Two-core regression for the Lynx OUT -> IN transport downbeat."""

import os
import shutil
import subprocess
import sys
import time


def fail(message):
    raise SystemExit("Lynx sync test: " + message)


def main():
    if len(sys.argv) != 4:
        fail("usage: test_lynx_sync.py <duoshot> <core> <rom>")
    duoshot, core, rom = map(os.path.abspath, sys.argv[1:])
    build = os.path.join(os.path.dirname(rom), "tests", "lynx-sync")
    shutil.rmtree(build, ignore_errors=True)
    os.makedirs(build)

    stamp = "%d-%d" % (os.getpid(), time.time_ns())
    test_rom = os.path.join(build, "alynxdj-sync-%s.lnx" % stamp)
    core_a = os.path.join(build, "core-a.dylib")
    core_b = os.path.join(build, "core-b.dylib")
    shutil.copyfile(rom, test_rom)
    shutil.copyfile(core, core_a)
    shutil.copyfile(core, core_b)

    # Both units load DEMO, return through SONG to OPTIONS, and select OUT on
    # A / IN on B. B arms first; A starts later. Before the downbeat fix the
    # IN walk remains exactly one phrase row behind for the entire run.
    boot_demo = (
        "0@280,100@10,120@20,100@10,0@40,"
        "20@6,0@20,20@6,0@20,20@6,0@20,"
        "1@4,0@24,1@4,0@500,"
        "100@10,110@20,100@10,0@40,"
        "100@10,110@20,100@10,0@40,")
    edit_sync = "1@6,81@4,1@6,0@10,"
    to_song = "100@6,120@8,100@6,0@"
    transport = "100@4,101@4,100@4,0@"
    master = (boot_demo + edit_sync + to_song + "170," + transport
              + "400")
    slave = (boot_demo + edit_sync * 2 + to_song + "80," + transport
             + "480")

    out_a = os.path.join(build, "master")
    out_b = os.path.join(build, "slave")
    subprocess.run(
        [duoshot, core_a, core_b, test_rom, out_a, out_b,
         master, slave, "1800"], check=True)

    with open(out_a + ".ram", "rb") as f:
        ram_a = f.read()
    with open(out_b + ".ram", "rb") as f:
        ram_b = f.read()
    if len(ram_a) != 65536 or len(ram_b) != 65536:
        fail("duoshot did not return two complete RAM images")
    if ram_a[0xC00F] != 1 or ram_b[0xC00F] != 2:
        fail("mode selection is OUT=%d IN=%d, expected 1/2"
             % (ram_a[0xC00F], ram_b[0xC00F]))
    if ram_a[0xC011] != 1 or ram_b[0xC011] != 1 or ram_b[0xC016]:
        fail("transport did not reach synchronized PLAY")

    active = 0
    for track in range(4):
        off = 0xF200 + track * 7
        if ram_a[off] != ram_b[off]:
            fail("track %d active state differs" % track)
        if not ram_a[off]:
            continue
        active += 1
        # song row, chain position, and phrase row are the musical playhead.
        fields_a = (ram_a[off + 1], ram_a[off + 3], ram_a[off + 6])
        fields_b = (ram_b[off + 1], ram_b[off + 3], ram_b[off + 6])
        if fields_a != fields_b:
            fail("track %d OUT playhead %r, IN playhead %r"
                 % (track, fields_a, fields_b))
    if not active:
        fail("DEMO produced no active tracks")
    if ram_b[0xC020] < 2:
        fail("IN received too few transport/row bytes (%d)" % ram_b[0xC020])
    print("Lynx sync: OUT Start is the IN downbeat; playheads remain row-locked")


if __name__ == "__main__":
    main()
