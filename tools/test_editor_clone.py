#!/usr/bin/env python3
"""RAM-level regression for physical-B mint and slim-clone double taps."""

import os
import shutil
import subprocess
import sys
import time


SD = 0xD400
SONG = SD
CHAINS = SD + 0x0200
PHRASES = SD + 0x0600
CHAIN_SIZE = 32
PHRASE_SIZE = 64


def fail(message):
    raise SystemExit("editor clone test: " + message)


def put(pokes, address, values):
    for offset, value in enumerate(values):
        pokes[address + offset] = value


def run_case(harness, core, rom, build, label, pokes, chain_screen=False):
    test_rom = os.path.join(
        build, "alynxdj-editor-%s-%d-%d.lnx"
        % (label, os.getpid(), time.time_ns()))
    ppm = os.path.join(build, "editor-%s.ppm" % label)
    ram_path = os.path.join(build, "editor-%s.ram" % label)
    shutil.copyfile(rom, test_rom)

    env = os.environ.copy()
    env["RETROSHOT_RAM_OUT"] = ram_path
    env["RETROSHOT_RAM_POKE"] = ",".join(
        "%04X:%02X" % item for item in sorted(pokes.items()))
    env["RETROSHOT_RAM_POKE_AT"] = "250"
    nav = "100@10,180@20,100@10,0@40," if chain_screen else ""
    script = "0@280," + nav + "1@4,0@4,1@4,0@60"
    subprocess.run(
        [harness, core, test_rom, ppm, "460", script], env=env, check=True)
    with open(ram_path, "rb") as f:
        ram = f.read()
    if len(ram) != 65536:
        fail("%s did not return a full RAM dump" % label)
    expected_screen = 1 if chain_screen else 0
    if ram[0xC003] != expected_screen:
        fail("%s ended on screen %d, expected %d"
             % (label, ram[0xC003], expected_screen))
    return ram


def run_command_case(harness, core, rom, build, label, paste=False):
    test_rom = os.path.join(
        build, "alynxdj-editor-%s-%d-%d.lnx"
        % (label, os.getpid(), time.time_ns()))
    ppm = os.path.join(build, "editor-%s.ppm" % label)
    ram_path = os.path.join(build, "editor-%s.ram" % label)
    shutil.copyfile(rom, test_rom)

    pokes = {SONG: 0}
    put(pokes, CHAINS, (0, 0))
    put(pokes, PHRASES, (37, 3, 9, 0x26))
    env = os.environ.copy()
    env["RETROSHOT_RAM_OUT"] = ram_path
    env["RETROSHOT_RAM_POKE"] = ",".join(
        "%04X:%02X" % item for item in sorted(pokes.items()))
    env["RETROSHOT_RAM_POKE_AT"] = "250"

    # Physical A+Right drills SONG -> CHAIN -> PHRASE; Right twice selects
    # CMD. Physical B-held+A performs the field cut. The optional physical-B
    # double tap pastes the command pair back into the same column.
    drill = "100@10,180@20,100@10,0@40,"
    script = ("0@280," + drill + drill
              + "80@4,0@4,80@4,0@20,"
              + "1@4,101@4,1@4,0@20,")
    if paste:
        script += "1@4,0@4,1@4,0@60"
    else:
        script += "0@60"
    subprocess.run(
        [harness, core, test_rom, ppm, "700", script], env=env, check=True)
    with open(ram_path, "rb") as f:
        ram = f.read()
    if len(ram) != 65536:
        fail("%s did not return a full RAM dump" % label)
    if ram[0xC003] != 2 or ram[0xC002] != 2:
        fail("%s did not finish on the PHRASE command column" % label)
    return ram


def run_back_tap_case(harness, core, rom, build, depth):
    label = "chain-a-tap" if depth == 1 else "phrase-a-tap"
    test_rom = os.path.join(
        build, "alynxdj-editor-%s-%d-%d.lnx"
        % (label, os.getpid(), time.time_ns()))
    ppm = os.path.join(build, "editor-%s.ppm" % label)
    ram_path = os.path.join(build, "editor-%s.ram" % label)
    shutil.copyfile(rom, test_rom)

    pokes = {SONG: 0}
    put(pokes, CHAINS, (0, 0))
    put(pokes, PHRASES, (37, 0, 0, 0))
    env = os.environ.copy()
    env["RETROSHOT_RAM_OUT"] = ram_path
    env["RETROSHOT_RAM_POKE"] = ",".join(
        "%04X:%02X" % item for item in sorted(pokes.items()))
    env["RETROSHOT_RAM_POKE_AT"] = "250"

    drill = "100@10,180@20,100@10,0@40,"
    script = "0@280," + drill * depth + "100@4,0@60"
    subprocess.run(
        [harness, core, test_rom, ppm, "600", script], env=env, check=True)
    with open(ram_path, "rb") as f:
        ram = f.read()
    if len(ram) != 65536:
        fail("%s did not return a full RAM dump" % label)
    if ram[0xC003] != depth:
        fail("physical A tap left %s for screen %d"
             % ("CHAIN" if depth == 1 else "PHRASE", ram[0xC003]))


def chain_fixture(empty_cell):
    pokes = {}
    put(pokes, SONG, bytes([0xFF] * 0x200))
    pokes[SONG] = 0xFF if empty_cell else 0
    source = bytearray([0xFF] * CHAIN_SIZE)
    source[0:4] = bytes((5, 2, 6, 0xFF))
    put(pokes, CHAINS, source)
    put(pokes, CHAINS + CHAIN_SIZE, bytes([0xFF] * CHAIN_SIZE))
    put(pokes, CHAINS + CHAIN_SIZE * 2, bytes([0xFF] * CHAIN_SIZE))
    if empty_cell:
        # Chain 1 is blank but allocated by another SONG cell, so mint 2.
        pokes[SONG + 1] = 1
    return pokes, bytes(source)


def phrase_fixture(empty_cell):
    pokes = {}
    put(pokes, SONG, bytes([0xFF] * 0x200))
    pokes[SONG] = 0
    put(pokes, CHAINS, bytes([0xFF] * 0x400))
    put(pokes, CHAINS, bytes((0xFF, 0xFF) if empty_cell else (0, 0xFE)))
    if empty_cell:
        # Phrase 1 is blank but allocated by another CHAIN row, so mint 2.
        put(pokes, CHAINS + 2, bytes((1, 0)))
    source = bytearray(PHRASE_SIZE)
    source[0:8] = bytes((37, 3, 0, 0, 40, 4, 0, 0))
    put(pokes, PHRASES, source)
    put(pokes, PHRASES + PHRASE_SIZE, bytes(PHRASE_SIZE))
    put(pokes, PHRASES + PHRASE_SIZE * 2, bytes(PHRASE_SIZE))
    return pokes, bytes(source)


def main():
    if len(sys.argv) != 4:
        fail("usage: test_editor_clone.py RETROSHOT CORE ROM")
    harness, core, rom = sys.argv[1:]
    build = os.path.dirname(os.path.abspath(rom))

    pokes, _ = chain_fixture(True)
    ram = run_case(harness, core, rom, build, "mint-chain", pokes)
    if ram[SONG] != 2:
        fail("empty SONG cell selected chain %d, expected unreferenced chain 2"
             % ram[SONG])
    if ram[CHAINS + CHAIN_SIZE:CHAINS + CHAIN_SIZE * 3] != bytes([0xFF] * 64):
        fail("empty SONG cell cloned the remembered chain instead of minting")

    pokes, source_chain = chain_fixture(False)
    ram = run_case(harness, core, rom, build, "clone-chain", pokes)
    if ram[SONG] != 1:
        fail("occupied SONG cell did not select cloned chain 1")
    if ram[CHAINS + CHAIN_SIZE:CHAINS + CHAIN_SIZE * 2] != source_chain:
        fail("SONG slim clone did not copy exactly one chain")

    pokes, _ = phrase_fixture(True)
    ram = run_case(
        harness, core, rom, build, "mint-phrase", pokes, chain_screen=True)
    if tuple(ram[CHAINS:CHAINS + 2]) != (2, 0):
        fail("empty CHAIN cell did not select unreferenced phrase 2 at TSP 00")
    if ram[PHRASES + PHRASE_SIZE:PHRASES + PHRASE_SIZE * 3] != bytes(128):
        fail("empty CHAIN cell cloned the remembered phrase instead of minting")

    pokes, source_phrase = phrase_fixture(False)
    ram = run_case(
        harness, core, rom, build, "clone-phrase", pokes, chain_screen=True)
    if tuple(ram[CHAINS:CHAINS + 2]) != (1, 0xFE):
        fail("occupied CHAIN cell did not select phrase clone or preserve TSP")
    if ram[PHRASES + PHRASE_SIZE:PHRASES + PHRASE_SIZE * 2] != source_phrase:
        fail("CHAIN slim clone did not copy exactly one phrase")

    ram = run_command_case(
        harness, core, rom, build, "cut-command", paste=False)
    if tuple(ram[PHRASES:PHRASES + 4]) != (37, 3, 0, 0):
        fail("command cut changed the note/instrument or left CMD/PARAM behind")

    ram = run_command_case(
        harness, core, rom, build, "paste-command", paste=True)
    if tuple(ram[PHRASES:PHRASES + 4]) != (37, 3, 9, 0x26):
        fail("command paste overwrote the row or lost CMD/PARAM")

    run_back_tap_case(harness, core, rom, build, 1)
    run_back_tap_case(harness, core, rom, build, 2)

    print("editor clone: PASS — empty cells mint next blank/unreferenced; "
          "occupied cells slim-clone; command cuts preserve note rows; "
          "CHAIN/PHRASE ignore physical-A back taps")


if __name__ == "__main__":
    main()
