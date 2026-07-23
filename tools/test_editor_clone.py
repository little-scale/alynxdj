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
INSTRS = SD + 0x1600
TABLES = SD + 0x1800
CHAIN_SIZE = 32
PHRASE_SIZE = 64
INSTR_SIZE = 16
CMD_NEXT = (1, 22, 3, 18, 5, 20, 13, 8, 15, 10, 11, 17,
            4, 14, 7, 16, 19, 0, 12, 9, 21, 6, 2)


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


def run_command_double_tap_safety_case(harness, core, rom, build):
    label = "command-double-tap-safety"
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

    # Cut empty row 1 as a full-step clipboard, return to row 0's CMD field,
    # then double-tap physical B.  This stale empty step used to overwrite all
    # four row bytes.  Command context must now clear only CMD/PARAM.
    drill = "100@10,180@20,100@10,0@40,"
    cut_empty = "20@4,0@10,1@4,101@4,1@4,0@20,"
    select_cmd = "10@4,0@10,80@4,0@10,80@4,0@10,"
    double_tap = "1@4,0@4,1@4,0@60"
    script = "0@280," + drill * 2 + cut_empty + select_cmd + double_tap
    subprocess.run(
        [harness, core, test_rom, ppm, "760", script], env=env, check=True)
    with open(ram_path, "rb") as f:
        ram = f.read()
    if len(ram) != 65536 or ram[0xC003] != 2 or ram[0xC002] != 2:
        fail("command double-tap safety rig missed the PHRASE command field")
    if tuple(ram[PHRASES:PHRASES + 4]) != (37, 3, 0, 0):
        fail("command double-tap changed NOTE/INSTR or retained CMD/PARAM: %r"
             % (tuple(ram[PHRASES:PHRASES + 4]),))


def run_phrase_instrument_latch_case(harness, core, rom, build):
    label = "phrase-instrument-latch"
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

    # Change row 0's instrument from 00 to 03 in the instrument column, then
    # tap notes into blank rows 1/5 and edit a note into row 9.  Every new note
    # must inherit 03 regardless of which note-entry gesture created it.
    drill = "100@10,180@20,100@10,0@40,"
    edit_right = "1@4,81@4,1@4,0@10,"
    to_instr = "80@4,0@10,"
    to_row1_note = "40@4,0@10,20@4,0@10,"
    insert_note = "1@4,0@12,"
    to_row5 = "20@4,0@8," * 4
    script = ("0@280," + drill * 2 + to_instr + edit_right * 3
              + to_row1_note + insert_note + to_row5 + insert_note
              + to_row5 + edit_right + "0@60")
    subprocess.run(
        [harness, core, test_rom, ppm, "920", script], env=env, check=True)
    with open(ram_path, "rb") as f:
        ram = f.read()
    if len(ram) != 65536 or ram[0xC003] != 2:
        fail("instrument latch rig did not finish on PHRASE")
    for row, note in ((0, 37), (1, 37), (5, 37), (9, 38)):
        actual = tuple(ram[PHRASES + row * 4:PHRASES + row * 4 + 4])
        if actual[:2] != (note, 3):
            fail("PHRASE row %d note/instrument is %r, expected (%d, 3)"
                 % (row, actual[:2], note))


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


def run_map_back_tap_case(harness, core, rom, build, project):
    label = "project-a-tap" if project else "options-a-tap"
    expected_screen = 1 if project else 0
    expected_mode = 2 if project else 1
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
    # Physical A+Up enters OPTIONS; A+Right continues to PROJECT. A clean tap
    # must remain inert there.  The same button must then remain usable as the
    # modifier for A+Down navigation and A+B transport immediately afterward.
    up = "100@10,110@20,100@10,0@40,"
    right = "100@10,180@20,100@10,0@40,"
    down = "100@10,120@20,100@10,0@40,"
    transport = "100@10,101@6,100@6,0@40"
    script = ("0@280," + up + (right if project else "")
              + "100@4,0@20," + down + transport)
    subprocess.run(
        [harness, core, test_rom, ppm, "650", script], env=env, check=True)
    with open(ram_path, "rb") as f:
        ram = f.read()
    if len(ram) != 65536 or ram[0xC003] != expected_screen:
        fail("physical A failed after clean tap on %s (screen %d)"
             % ("PROJECT" if project else "OPTIONS", ram[0xC003]))
    if ram[0xC011] != expected_mode:
        fail("physical A+B failed after clean tap on %s (mode %d)"
             % ("PROJECT" if project else "OPTIONS", ram[0xC011]))


def run_instr_follow_case(harness, core, rom, build):
    label = "follow-row-instrument"
    test_rom = os.path.join(
        build, "alynxdj-editor-%s-%d-%d.lnx"
        % (label, os.getpid(), time.time_ns()))
    ppm = os.path.join(build, "editor-%s.ppm" % label)
    ram_path = os.path.join(build, "editor-%s.ram" % label)
    shutil.copyfile(rom, test_rom)

    target = 0x1B
    pokes = {SONG: 0, INSTRS: 0, INSTRS + target * INSTR_SIZE: 0}
    put(pokes, CHAINS, (0, 0))
    put(pokes, PHRASES, (37, target, 0, 0, 0, 0, 0, 0))
    env = os.environ.copy()
    env["RETROSHOT_RAM_OUT"] = ram_path
    env["RETROSHOT_RAM_POKE"] = ",".join(
        "%04X:%02X" % item for item in sorted(pokes.items()))
    env["RETROSHOT_RAM_POKE_AT"] = "250"

    # Drill to the selected row's INSTR and change TYPE once. Return to an
    # empty PHRASE row, drill again, and change TYPE once more. Instrument 1B
    # must advance TONE -> NOISE -> WAV while 00 stays untouched: populated
    # rows follow their instrument and empty rows retain the previous one.
    drill = "100@10,180@20,100@10,0@40,"
    edit = "1@10,81@4,1@10,0@40,"
    back = "100@10,140@4,100@10,0@40,"
    script = ("0@280," + drill * 3 + edit + back
              + "20@4,0@20," + drill + edit + "0@60")
    subprocess.run(
        [harness, core, test_rom, ppm, "1000", script], env=env, check=True)
    with open(ram_path, "rb") as f:
        ram = f.read()
    if len(ram) != 65536:
        fail("%s did not return a full RAM dump" % label)
    if ram[0xC003] != 3:
        fail("row-instrument drill did not reach INSTR")
    if ram[INSTRS] != 0 or ram[INSTRS + target * INSTR_SIZE] != 2:
        fail("INSTR entry did not follow/retain row instrument %02X" % target)


def run_wave_navigation_case(harness, core, rom, build, direction):
    label = "wave-plain-%s" % direction
    test_rom = os.path.join(
        build, "alynxdj-editor-%s-%d-%d.lnx"
        % (label, os.getpid(), time.time_ns()))
    ppm = os.path.join(build, "editor-%s.ppm" % label)
    ram_path = os.path.join(build, "editor-%s.ram" % label)
    shutil.copyfile(rom, test_rom)

    pokes = {SONG: 0, INSTRS + 4: 3}
    put(pokes, CHAINS, (0, 0))
    put(pokes, PHRASES, (37, 0, 0, 0))
    env = os.environ.copy()
    env["RETROSHOT_RAM_OUT"] = ram_path
    env["RETROSHOT_RAM_POKE"] = ",".join(
        "%04X:%02X" % item for item in sorted(pokes.items()))
    env["RETROSHOT_RAM_POKE_AT"] = "250"

    drill = "100@10,180@20,100@10,0@40,"
    to_wave = "100@10,110@20,100@10,0@40,"
    plain = "10@6,0@20," if direction == "up" else "20@6,0@20,"
    next_wave = "100@10,180@20,100@10,0@40"
    script = "0@280," + drill * 3 + to_wave + plain + next_wave
    subprocess.run(
        [harness, core, test_rom, ppm, "850", script], env=env, check=True)
    with open(ram_path, "rb") as f:
        ram = f.read()
    if len(ram) != 65536 or ram[0xC003] != 7:
        fail("%s did not finish on WAVE" % label)
    if ram[0xC001] != 4:
        fail("plain %s changed WAVE number before A+Right (got %d)"
             % (direction, ram[0xC001]))


def run_new_defaults_case(harness, core, rom, build):
    label = "new-instrument-defaults"
    test_rom = os.path.join(
        build, "alynxdj-editor-%s-%d-%d.lnx"
        % (label, os.getpid(), time.time_ns()))
    ppm = os.path.join(build, "editor-%s.ppm" % label)
    ram_path = os.path.join(build, "editor-%s.ram" % label)
    shutil.copyfile(rom, test_rom)

    env = os.environ.copy()
    env["RETROSHOT_RAM_OUT"] = ram_path
    to_files = "100@10,120@20,100@10,0@40,"
    down_twice = "20@6,0@20,20@6,0@20,"
    # Confirmation taps are separated past the paste double-tap window.
    confirm_new = "1@4,0@24,1@4,0@80"
    script = "0@280," + to_files + down_twice + confirm_new
    subprocess.run(
        [harness, core, test_rom, ppm, "650", script], env=env, check=True)
    with open(ram_path, "rb") as f:
        ram = f.read()
    expected = bytes((0, 0x7F, 0x05, 0x05, 0xFF, 0x01, 0xFF, 0xFF,
                      0, 0, 0, 0, 0, 0, 0, 0))
    for index in range(32):
        actual = ram[INSTRS + index * INSTR_SIZE:
                     INSTRS + (index + 1) * INSTR_SIZE]
        if actual != expected:
            fail("NEW instrument %02X defaults are %s, expected %s"
                 % (index, actual.hex(), expected.hex()))


def run_demo_case(harness, core, rom, build):
    label = "files-demo"
    test_rom = os.path.join(
        build, "alynxdj-editor-%s-%d-%d.lnx"
        % (label, os.getpid(), time.time_ns()))
    ppm = os.path.join(build, "editor-%s.ppm" % label)
    ram_path = os.path.join(build, "editor-%s.ram" % label)
    shutil.copyfile(rom, test_rom)

    env = os.environ.copy()
    env["RETROSHOT_RAM_OUT"] = ram_path
    to_files = "100@10,120@20,100@10,0@40,"
    down_three = "20@6,0@20,20@6,0@20,20@6,0@20,"
    confirm_demo = "1@4,0@24,1@4,0@80"
    script = "0@280," + to_files + down_three + confirm_demo
    subprocess.run(
        [harness, core, test_rom, ppm, "700", script], env=env, check=True)
    with open(ram_path, "rb") as f:
        ram = f.read()

    expected = bytes((
        0xFF, 20, 0xFF, 22, 25, 20, 0xFF, 22,
        25, 21, 24, 22, 26, 21, 24, 23,
        25, 20, 27, 23, 26, 20, 27, 23,
        29, 20, 28, 22, 26, 21, 25, 23,
    ))
    if len(ram) != 65536 or ram[0xC003] != 5:
        fail("DEMO action did not remain on FILES")
    if ram[SONG:SONG + len(expected)] != expected:
        fail("FILES DEMO did not restore the factory song arrangement")
    if tuple(ram[CHAINS + 20 * CHAIN_SIZE:
                 CHAINS + 20 * CHAIN_SIZE + 4]) != (20, 0, 20, 0):
        fail("FILES DEMO did not restore the factory chain data")


def run_command_order_case(harness, core, rom, build, backwards):
    label = "command-order-prev" if backwards else "command-order-next"
    test_rom = os.path.join(
        build, "alynxdj-editor-%s-%d-%d.lnx"
        % (label, os.getpid(), time.time_ns()))
    ppm = os.path.join(build, "editor-%s.ppm" % label)
    ram_path = os.path.join(build, "editor-%s.ram" % label)
    shutil.copyfile(rom, test_rom)

    pokes = {SONG: 0, INSTRS + 6: 0}  # chain 0, instrument 0 -> table 0
    put(pokes, CHAINS, (0, 0))
    for row, cmd in enumerate(range(16)):
        put(pokes, PHRASES + row * 4, (0, 0, cmd, 0x80 + row))
    for row, cmd in enumerate(range(16, 23)):
        put(pokes, TABLES + row * 4, (0, 0, cmd, 0x40 + row))

    env = os.environ.copy()
    env["RETROSHOT_RAM_OUT"] = ram_path
    env["RETROSHOT_RAM_POKE"] = ",".join(
        "%04X:%02X" % item for item in sorted(pokes.items()))
    env["RETROSHOT_RAM_POKE_AT"] = "250"

    drill = "100@10,180@20,100@10,0@40,"
    right = "80@6,0@10,"
    down = "20@6,0@10,"
    edit = ("1@6,41@4,1@6,0@10," if backwards
            else "1@6,81@4,1@6,0@10,")
    phrase_edits = "".join(edit + (down if row < 15 else "")
                           for row in range(16))
    table_edits = "".join(edit + (down if row < 6 else "")
                          for row in range(7))
    script = ("0@280," + drill * 2 + right * 2 + phrase_edits
              + drill * 2 + right * 2 + table_edits + "0@80")
    subprocess.run(
        [harness, core, test_rom, ppm, "1800", script], env=env, check=True)

    with open(ram_path, "rb") as f:
        ram = f.read()
    if len(ram) != 65536 or ram[0xC003] != 4 or ram[0xC002] != 2:
        fail("%s did not finish on the TABLE command column" % label)
    previous = [0] * len(CMD_NEXT)
    for command, following in enumerate(CMD_NEXT):
        previous[following] = command
    expected = previous if backwards else CMD_NEXT
    for row, cmd in enumerate(range(16)):
        actual = ram[PHRASES + row * 4 + 2]
        if actual != expected[cmd] or ram[PHRASES + row * 4 + 3] != 0x80 + row:
            fail("%s PHRASE command %d stepped to %d, expected %d"
                 % (label, cmd, actual, expected[cmd]))
    for row, cmd in enumerate(range(16, 23)):
        actual = ram[TABLES + row * 4 + 2]
        if actual != expected[cmd] or ram[TABLES + row * 4 + 3] != 0x40 + row:
            fail("%s TABLE command %d stepped to %d, expected %d"
                 % (label, cmd, actual, expected[cmd]))


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
    build = os.path.join(os.path.dirname(os.path.abspath(rom)),
                         "tests", "editor")
    shutil.rmtree(build, ignore_errors=True)
    os.makedirs(build)

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

    run_command_double_tap_safety_case(harness, core, rom, build)
    run_phrase_instrument_latch_case(harness, core, rom, build)

    run_back_tap_case(harness, core, rom, build, 1)
    run_back_tap_case(harness, core, rom, build, 2)
    run_map_back_tap_case(harness, core, rom, build, False)
    run_map_back_tap_case(harness, core, rom, build, True)
    run_instr_follow_case(harness, core, rom, build)
    run_wave_navigation_case(harness, core, rom, build, "up")
    run_wave_navigation_case(harness, core, rom, build, "down")
    run_new_defaults_case(harness, core, rom, build)
    run_demo_case(harness, core, rom, build)
    run_command_order_case(harness, core, rom, build, False)
    run_command_order_case(harness, core, rom, build, True)

    print("editor clone: PASS — empty cells mint next blank/unreferenced; "
          "occupied cells slim-clone; command cuts/double-taps preserve note "
          "rows; new PHRASE notes inherit the last explicitly edited instrument; "
          "CHAIN/PHRASE/OPTIONS/PROJECT ignore physical-A back taps; "
          "PHRASE drill follows the selected row's instrument; WAVE number "
          "requires physical A; NEW patch defaults are canonical; "
          "empty boots stay clean and FILES DEMO restores the factory song; "
          "PHRASE/TABLE commands step alphabetically in both directions")


if __name__ == "__main__":
    main()
