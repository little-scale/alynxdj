#!/usr/bin/env python3
"""RAM-level regression for physical-B mint and slim-clone double taps."""

import os
import shutil
import subprocess
import sys
import time
from collections import Counter

from PIL import Image


SD = 0xD400
SONG = SD
CHAINS = SD + 0x0200
PHRASES = SD + 0x0600
INSTRS = SD + 0x1600
TABLES = SD + 0x1800
GROOVES = SD + 0x1C00
WALKS = SD + 0x1E00
CHAIN_SIZE = 32
PHRASE_SIZE = 64
INSTR_SIZE = 16
CMD_NEXT = (1, 22, 3, 18, 5, 20, 13, 8, 15, 10, 11, 17,
            4, 14, 7, 16, 19, 0, 12, 9, 21, 6, 2)
TABLE_COMMAND_CASES = (1, 2, 3, 4, 5, 12, 13, 14,
                       16, 17, 18, 19, 20, 21, 22)
TABLE_VALID_COMMANDS = {0, 2, 4, 5, 6, 7, 8, 9, 10,
                        11, 12, 14, 15, 16, 18, 19, 22}


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


def run_block_selection_visual_case(harness, core, rom, build, depth):
    names = ("song", "chain", "phrase")
    fields = (
        (1, 4, 7, 10, 13),
        (1, 4, 8),
        (1, 4, 9, 12, 13),
    )
    label = "%s-block-highlight" % names[depth]
    test_rom = os.path.join(
        build, "alynxdj-editor-%s-%d-%d.lnx"
        % (label, os.getpid(), time.time_ns()))
    ppm = os.path.join(build, "editor-%s.ppm" % label)
    shutil.copyfile(rom, test_rom)

    pokes = {SONG: 0}
    put(pokes, CHAINS, (0, 0))
    put(pokes, PHRASES, (37, 3, 9, 0x26))
    env = os.environ.copy()
    env["RETROSHOT_RAM_POKE"] = ",".join(
        "%04X:%02X" % item for item in sorted(pokes.items()))
    env["RETROSHOT_RAM_POKE_AT"] = "250"

    # Drill to the requested hierarchy screen, hold physical B then A long
    # enough to enter SELECT, and extend the range from row 0 through row 1.
    drill = "100@10,180@20,100@10,0@40,"
    select = "1@5,101@30,0@15,20@4,0@80"
    script = "0@280," + drill * depth + select
    subprocess.run(
        [harness, core, test_rom, ppm, "760", script], env=env, check=True)

    frame = Image.open(ppm).convert("RGB")
    background = Counter(
        frame.getpixel((x, y))
        for y in range(frame.height)
        for x in range(frame.width)
    ).most_common(1)[0][0]
    title = [frame.getpixel((x, y)) for y in range(6) for x in range(4, 24)]
    accent = Counter(pixel for pixel in title if pixel != background).most_common(
        1)[0][0]

    for row in (0, 1):
        y0 = (row + 1) * 6
        for field in fields[depth]:
            x0 = (field + 8) * 4       # body is shifted eight character cells
            block = [frame.getpixel((x, y))
                     for y in range(y0, y0 + 6)
                     for x in range(x0, x0 + 4)]
            if block.count(accent) < 8:
                fail("%s SELECT row %d field %d is not fully highlighted"
                     % (names[depth].upper(), row, field))

    # The row immediately below the selection must return to ordinary ink.
    y0 = 3 * 6
    for field in fields[depth]:
        x0 = (field + 8) * 4
        block = [frame.getpixel((x, y))
                 for y in range(y0, y0 + 6)
                 for x in range(x0, x0 + 4)]
        if accent in block:
            fail("%s SELECT highlight leaked into row 2"
                 % names[depth].upper())


def run_back_tap_case(harness, core, rom, build, depth):
    names = {1: "chain", 2: "phrase", 3: "instrument"}
    label = "%s-a-tap" % names[depth]
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
    script = "0@280," + drill * depth + "100@20,0@60"
    subprocess.run(
        [harness, core, test_rom, ppm, "600", script], env=env, check=True)
    with open(ram_path, "rb") as f:
        ram = f.read()
    if len(ram) != 65536:
        fail("%s did not return a full RAM dump" % label)
    if ram[0xC003] != depth:
        fail("physical A tap left %s for screen %d"
             % (names[depth].upper(), ram[0xC003]))


def run_table_command_latch_case(harness, core, rom, build):
    label = "table-command-latch"
    test_rom = os.path.join(
        build, "alynxdj-editor-%s-%d-%d.lnx"
        % (label, os.getpid(), time.time_ns()))
    ppm = os.path.join(build, "editor-%s.ppm" % label)
    ram_path = os.path.join(build, "editor-%s.ram" % label)
    shutil.copyfile(rom, test_rom)

    pokes = {SONG: 0, INSTRS + 6: 0}
    put(pokes, CHAINS, (0, 0))
    put(pokes, PHRASES, (37, 0, 0, 0))
    put(pokes, TABLES, (0, 0, 9, 0x26, 0, 0, 0, 0))
    env = os.environ.copy()
    env["RETROSHOT_RAM_OUT"] = ram_path
    env["RETROSHOT_RAM_POKE"] = ",".join(
        "%04X:%02X" % item for item in sorted(pokes.items()))
    env["RETROSHOT_RAM_POKE_AT"] = "250"

    # Drill SONG -> CHAIN -> PHRASE -> INSTR -> TABLE. Edit row 0's V26
    # parameter to V27, then begin editing row 1's empty CMD field. The shared
    # command latch should insert V27 as a unit rather than beginning at B00.
    drill = "100@10,180@20,100@10,0@40,"
    right = "80@6,0@10,"
    left = "40@6,0@10,"
    down = "20@6,0@10,"
    edit_right = "1@6,81@4,1@6,0@20,"
    script = ("0@280," + drill * 4 + right * 3 + edit_right
              + left + down + edit_right)
    subprocess.run(
        [harness, core, test_rom, ppm, "900", script], env=env, check=True)
    with open(ram_path, "rb") as f:
        ram = f.read()
    if len(ram) != 65536 or ram[0xC003] != 4 or ram[0xC002] != 2:
        fail("table command latch rig missed row 1's command field")
    if tuple(ram[TABLES + 4:TABLES + 8]) != (0, 0, 9, 0x27):
        fail("empty TABLE command did not inherit V27: %r"
             % (tuple(ram[TABLES + 4:TABLES + 8]),))


def run_command_recall_tap_case(harness, core, rom, build):
    label = "command-recall-tap"
    test_rom = os.path.join(
        build, "alynxdj-editor-%s-%d-%d.lnx"
        % (label, os.getpid(), time.time_ns()))
    ppm = os.path.join(build, "editor-%s.ppm" % label)
    ram_path = os.path.join(build, "editor-%s.ram" % label)
    shutil.copyfile(rom, test_rom)

    pokes = {SONG: 0, INSTRS + 6: 0}
    put(pokes, CHAINS, (0, 0))
    put(pokes, PHRASES, (37, 0, 9, 0x26, 0, 0, 0, 0))
    env = os.environ.copy()
    env["RETROSHOT_RAM_OUT"] = ram_path
    env["RETROSHOT_RAM_POKE"] = ",".join(
        "%04X:%02X" % item for item in sorted(pokes.items()))
    env["RETROSHOT_RAM_POKE_AT"] = "250"

    # Edit phrase V26 -> V27 to establish the shared letter/value latch.
    # A clean physical-B tap inserts it into row 1, then another tap after
    # drilling through INSTR inserts the same pair into TABLE row 0.
    drill = "100@10,180@20,100@10,0@40,"
    right = "80@6,0@10,"
    left = "40@6,0@10,"
    down = "20@6,0@10,"
    edit_right = "1@6,81@4,1@6,0@20,"
    tap_b = "1@4,0@30,"
    script = ("0@280," + drill * 2 + right * 3 + edit_right
              + left + down + tap_b + drill * 2 + right * 2
              + tap_b + "0@60")
    subprocess.run(
        [harness, core, test_rom, ppm, "1100", script], env=env, check=True)
    with open(ram_path, "rb") as f:
        ram = f.read()
    if len(ram) != 65536 or ram[0xC003] != 4 or ram[0xC002] != 2:
        fail("command recall tap rig did not finish on TABLE CMD")
    if tuple(ram[PHRASES + 4:PHRASES + 8]) != (0, 0, 9, 0x27):
        fail("PHRASE B tap did not recall V27: %r"
             % (tuple(ram[PHRASES + 4:PHRASES + 8]),))
    if tuple(ram[TABLES:TABLES + 4]) != (0, 0, 9, 0x27):
        fail("TABLE B tap did not share recalled V27: %r"
             % (tuple(ram[TABLES:TABLES + 4]),))


def run_table_command_delete_case(harness, core, rom, build):
    label = "table-command-delete"
    test_rom = os.path.join(
        build, "alynxdj-editor-%s-%d-%d.lnx"
        % (label, os.getpid(), time.time_ns()))
    ppm = os.path.join(build, "editor-%s.ppm" % label)
    ram_path = os.path.join(build, "editor-%s.ram" % label)
    shutil.copyfile(rom, test_rom)

    pokes = {SONG: 0, INSTRS + 6: 0}
    put(pokes, CHAINS, (0, 0))
    put(pokes, PHRASES, (37, 0, 0, 0))
    put(pokes, TABLES, (0x34, 0xF4, 9, 0x26))
    env = os.environ.copy()
    env["RETROSHOT_RAM_OUT"] = ram_path
    env["RETROSHOT_RAM_POKE"] = ",".join(
        "%04X:%02X" % item for item in sorted(pokes.items()))
    env["RETROSHOT_RAM_POKE_AT"] = "250"

    # Drill to TABLE CMD, then use the same physical-B-held+A gesture as a
    # PHRASE command cut.  Only CMD+PARAM may be cleared.
    drill = "100@10,180@20,100@10,0@40,"
    right = "80@6,0@10,"
    delete = "1@4,101@4,1@4,0@60"
    script = "0@280," + drill * 4 + right * 2 + delete
    subprocess.run(
        [harness, core, test_rom, ppm, "900", script], env=env, check=True)
    with open(ram_path, "rb") as f:
        ram = f.read()
    if len(ram) != 65536 or ram[0xC003] != 4 or ram[0xC002] != 2:
        fail("TABLE command delete rig missed the command field")
    if tuple(ram[TABLES:TABLES + 4]) != (0x34, 0xF4, 0, 0):
        fail("TABLE command delete changed VOL/TSP or retained CMD/PARAM: %r"
             % (tuple(ram[TABLES:TABLES + 4]),))


def run_option1_context_case(harness, core, rom, build):
    label = "option1-context"
    test_rom = os.path.join(
        build, "alynxdj-editor-%s-%d-%d.lnx"
        % (label, os.getpid(), time.time_ns()))
    ppm = os.path.join(build, "editor-%s.ppm" % label)
    ram_path = os.path.join(build, "editor-%s.ram" % label)
    shutil.copyfile(rom, test_rom)

    pokes = {0xC00F: 2}                 # SYNC IN holds the exact start row
    put(pokes, SONG, (0, 1, 2, 3))
    for chain in range(4):
        put(pokes, CHAINS + chain * CHAIN_SIZE, bytes([0xFF] * CHAIN_SIZE))
        for pos in range(3):
            put(pokes, CHAINS + chain * CHAIN_SIZE + pos * 2,
                (chain * 3 + pos, 0))
    put(pokes, GROOVES, (6,) + (0,) * 15)
    env = os.environ.copy()
    env["RETROSHOT_RAM_OUT"] = ram_path
    env["RETROSHOT_RAM_POKE"] = ",".join(
        "%04X:%02X" % item for item in sorted(pokes.items()))
    env["RETROSHOT_RAM_POKE_AT"] = "250"

    # First prove OPTION 1's held physical-B mute still works and suppresses
    # the clean-tap action. Select chain position 2 and phrase row 3, then
    # clean-tap OPTION 1.
    # Unlike physical A+B's selected-track preview, this starts all four
    # tracks from the same arrangement/chain/phrase context.
    drill = "100@10,180@20,100@10,0@40,"
    down = "20@6,0@10,"
    held_mute = "400@6,401@4,400@6,0@20,"
    option1 = "400@6,0@80"
    script = ("0@280," + held_mute + drill + down * 2 + drill
              + down * 3 + option1)
    subprocess.run(
        [harness, core, test_rom, ppm, "900", script], env=env, check=True)
    with open(ram_path, "rb") as f:
        ram = f.read()
    if len(ram) != 65536 or ram[0xC011] != 1 or ram[0xC016] != 1:
        fail("OPTION 1 did not arm contextual all-track SONG playback")
    if ram[0xC012] != 1:
        fail("OPTION 1 held mute layer was lost or triggered a clean tap")
    for track in range(4):
        walk = tuple(ram[WALKS + track * 7:WALKS + track * 7 + 7])
        expected = (1, 0, track, 2, track * 3 + 2, 0, 3)
        if walk != expected:
            fail("OPTION 1 track %d context is %r, expected %r"
                 % (track, walk, expected))


def run_option1_toggle_case(harness, core, rom, build):
    label = "option1-toggle"
    test_rom = os.path.join(
        build, "alynxdj-editor-%s-%d-%d.lnx"
        % (label, os.getpid(), time.time_ns()))
    ppm = os.path.join(build, "editor-%s.ppm" % label)
    ram_path = os.path.join(build, "editor-%s.ram" % label)
    shutil.copyfile(rom, test_rom)

    pokes = {SONG: 0}
    put(pokes, CHAINS, bytes([0xFF] * CHAIN_SIZE))
    put(pokes, CHAINS, (0, 0))
    put(pokes, PHRASES, (37, 0, 0, 0))
    put(pokes, GROOVES, (15,) + (0,) * 15)
    env = os.environ.copy()
    env["RETROSHOT_RAM_OUT"] = ram_path
    env["RETROSHOT_RAM_POKE"] = ",".join(
        "%04X:%02X" % item for item in sorted(pokes.items()))
    env["RETROSHOT_RAM_POKE_AT"] = "250"

    # First clean tap starts contextual playback; the second clean tap stops.
    # The gap is long enough to prove this is an active transport, not two
    # releases coalescing into one input edge.
    option1 = "400@6,0@80,"
    script = "0@280," + option1 + "0@40," + option1
    subprocess.run(
        [harness, core, test_rom, ppm, "700", script], env=env, check=True)
    with open(ram_path, "rb") as f:
        ram = f.read()
    if len(ram) != 65536 or ram[0xC011] != 0 or ram[0xC016] != 0:
        fail("second clean OPTION 1 tap did not stop transport: mode %d, "
             "wait %d" % (ram[0xC011], ram[0xC016]))


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


def run_groove_a_hold_case(harness, core, rom, build):
    label = "groove-a-hold"
    test_rom = os.path.join(
        build, "alynxdj-editor-%s-%d-%d.lnx"
        % (label, os.getpid(), time.time_ns()))
    ppm = os.path.join(build, "editor-%s.ppm" % label)
    ram_path = os.path.join(build, "editor-%s.ram" % label)
    shutil.copyfile(rom, test_rom)

    env = os.environ.copy()
    env["RETROSHOT_RAM_OUT"] = ram_path

    # Physical A+Right enters CHAIN and A+Down enters GROOVE. A long clean
    # hold/release must be inert there rather than invoking the generic
    # detail-screen back action and jumping left to FILES.
    right = "100@10,180@20,100@10,0@40,"
    down = "100@10,120@20,100@10,0@40,"
    script = "0@280," + right + down + "100@90,0@60"
    subprocess.run(
        [harness, core, test_rom, ppm, "700", script], env=env, check=True)
    with open(ram_path, "rb") as f:
        ram = f.read()
    if len(ram) != 65536 or ram[0xC003] != 6:
        fail("physical A hold/release left GROOVE for screen %d"
             % ram[0xC003])


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

    # Drill to the selected row's INSTR, move from the top selector to TYPE,
    # and change TYPE once. Return to an
    # empty PHRASE row, drill again, and change TYPE once more. Instrument 1B
    # must advance LFSR -> WAV -> KIT while 00 stays untouched: populated
    # rows follow their instrument and empty rows retain the previous one.
    drill = "100@10,180@20,100@10,0@40,"
    down = "20@4,0@20,"
    edit = "1@10,81@4,1@10,0@40,"
    back = "100@10,140@4,100@10,0@40,"
    script = ("0@280," + drill * 3 + down + edit + back
              + down + drill + down + edit + "0@60")
    subprocess.run(
        [harness, core, test_rom, ppm, "1000", script], env=env, check=True)
    with open(ram_path, "rb") as f:
        ram = f.read()
    if len(ram) != 65536:
        fail("%s did not return a full RAM dump" % label)
    if ram[0xC003] != 3:
        fail("row-instrument drill did not reach INSTR")
    if ram[INSTRS] != 0 or ram[INSTRS + target * INSTR_SIZE] != 3:
        fail("INSTR entry did not follow/retain row instrument %02X" % target)
    if ram[INSTRS + target * INSTR_SIZE + 4] != 0:
        fail("changing an instrument to KIT did not initialise bank 00")


def run_instr_entry_top_case(harness, core, rom, build):
    label = "instrument-entry-top"
    test_rom = os.path.join(
        build, "alynxdj-editor-%s-%d-%d.lnx"
        % (label, os.getpid(), time.time_ns()))
    ppm = os.path.join(build, "editor-%s.ppm" % label)
    ram_path = os.path.join(build, "editor-%s.ram" % label)
    shutil.copyfile(rom, test_rom)

    pokes = {SONG: 0}
    put(pokes, CHAINS, (0, 0))
    put(pokes, PHRASES + 2 * 4, (37, 5, 0, 0))
    env = os.environ.copy()
    env["RETROSHOT_RAM_OUT"] = ram_path
    env["RETROSHOT_RAM_POKE"] = ",".join(
        "%04X:%02X" % item for item in sorted(pokes.items()))
    env["RETROSHOT_RAM_POKE_AT"] = "250"

    drill = "100@10,180@20,100@10,0@40,"
    down = "20@5,0@10,"
    script = "0@280," + drill * 2 + down * 2 + drill + "0@60"
    subprocess.run(
        [harness, core, test_rom, ppm, "800", script], env=env, check=True)
    with open(ram_path, "rb") as f:
        ram = f.read()
    if len(ram) != 65536 or ram[0xC003] != 3:
        fail("row-02 instrument entry did not reach INSTR")
    if ram[0xC001] != 14:
        fail("PHRASE -> INSTR retained field %d instead of top selector"
             % ram[0xC001])


def run_instr_selector_case(harness, core, rom, build):
    label = "instrument-selector"
    test_rom = os.path.join(
        build, "alynxdj-editor-%s-%d-%d.lnx"
        % (label, os.getpid(), time.time_ns()))
    ppm = os.path.join(build, "editor-%s.ppm" % label)
    ram_path = os.path.join(build, "editor-%s.ram" % label)
    shutil.copyfile(rom, test_rom)

    pokes = {SONG: 0, INSTRS: 0, INSTRS + INSTR_SIZE: 1}
    put(pokes, CHAINS, (0, 0))
    put(pokes, PHRASES, (37, 0, 0, 0))
    env = os.environ.copy()
    env["RETROSHOT_RAM_OUT"] = ram_path
    env["RETROSHOT_RAM_POKE"] = ",".join(
        "%04X:%02X" % item for item in sorted(pokes.items()))
    env["RETROSHOT_RAM_POKE_AT"] = "250"

    # INSTR opens on its top selector. Down reaches TYPE: prove current LFSR
    # 00 steps directly to WAV 02, never legacy ID 01. Up returns to the
    # selector and chooses instrument 01; Down reaches TYPE again, where
    # legacy ID 01 must likewise step to WAV.
    drill = "100@10,180@20,100@10,0@40,"
    up = "10@6,0@20,"
    down = "20@6,0@20,"
    edit_right = "1@6,81@4,1@6,0@40,"
    script = ("0@280," + drill * 3 + down + edit_right + up + edit_right
              + down + edit_right + "0@80")
    subprocess.run(
        [harness, core, test_rom, ppm, "1000", script],
        env=env, check=True)
    with open(ram_path, "rb") as f:
        ram = f.read()
    if len(ram) != 65536 or ram[0xC003] != 3 or ram[0xC001] != 0:
        fail("instrument selector rig did not return to the TYPE field")
    if ram[INSTRS] != 2 or ram[INSTRS + INSTR_SIZE] != 2:
        fail("INSTR selector exposed/created legacy type 01")


def run_instr_field_visibility_case(harness, core, rom, build):
    # Entry starts at INSTR; the first Down reaches TYPE. These additional
    # Down counts must land on the next meaningful
    # field after every type-specific omission. PAN is universal: it occupies
    # LFSR's formerly unused BANK row, while WAV/KIT move their selector to
    # the first row those types previously skipped.
    cases = (
        ("lfsr-type-vol-edit", 0, 1, 1),
        ("lfsr-seed-pan-table", 0, 12, 12),
        ("lfsr-table-left", 0, 12, 12),
        ("legacy-lfsr-seed-pan-table", 1, 12, 12),
        ("wav-tsp-wave", 2, 6, 6),
        ("kit-type-vol", 3, 1, 1),
        ("kit-vol-bank", 3, 3, 6),
        ("lfsr-pan", 0, 11, 11),
        ("wav-pan", 2, 7, 11),
        ("kit-pan", 3, 4, 11),
    )
    drill = "100@10,180@20,100@10,0@40,"
    down = "20@5,0@8,"

    for label, instrument_type, steps, expected_row in cases:
        test_rom = os.path.join(
            build, "alynxdj-editor-%s-%d-%d.lnx"
            % (label, os.getpid(), time.time_ns()))
        ppm = os.path.join(build, "editor-%s.ppm" % label)
        ram_path = os.path.join(build, "editor-%s.ram" % label)
        shutil.copyfile(rom, test_rom)

        pokes = {SONG: 0, INSTRS: instrument_type}
        if label == "lfsr-type-vol-edit":
            pokes[INSTRS + 1] = 0x3C
        if label == "lfsr-table-left":
            pokes[INSTRS + 6] = 0x05
        if instrument_type == 3:
            pokes[INSTRS + 1] = 0x4D
        if label.endswith("-pan"):
            pokes[INSTRS + 7] = 0x87
        put(pokes, CHAINS, (0, 0))
        put(pokes, PHRASES, (37, 0, 0, 0))
        env = os.environ.copy()
        env["RETROSHOT_RAM_OUT"] = ram_path
        env["RETROSHOT_RAM_POKE"] = ",".join(
            "%04X:%02X" % item for item in sorted(pokes.items()))
        env["RETROSHOT_RAM_POKE_AT"] = "250"

        edit_left = ("1@6,41@4,1@6,0@20,"
                     if label in ("kit-vol-bank", "lfsr-table-left") else "")
        # KIT: Left/Right are inert; Up then Down quantizes 4D back to 40.
        edit_volume = (
            "1@6,41@4,1@6,0@10,"
            "1@6,81@4,1@6,0@10,"
            "1@6,11@4,1@6,0@10,"
            "1@6,21@4,1@6,0@20,"
        ) if label == "kit-type-vol" else ""
        # LFSR retains ordinary fine + coarse editing. Include Left first:
        # 3C -> 3B -> 3C -> 4C. A stale Z flag once made Left a no-op and
        # produced 4D here.
        edit_full_volume = (
            "1@6,41@4,1@6,0@10,"
            "1@6,81@4,1@6,0@10,"
            "1@6,11@4,1@6,0@20,"
        ) if label == "lfsr-type-vol-edit" else ""
        # PAN edits its two Lynx-II level nibbles independently:
        # Up raises left, Right raises right (87 -> 98).
        edit_pan = (
            "1@6,11@4,1@6,0@10,"
            "1@6,81@4,1@6,0@20,"
        ) if label.endswith("-pan") else ""
        script = ("0@280," + drill * 3 + down * (steps + 1)
                  + edit_left + edit_volume + edit_full_volume + edit_pan
                  + "0@60")
        subprocess.run(
            [harness, core, test_rom, ppm, "1000", script],
            env=env, check=True)
        with open(ram_path, "rb") as f:
            ram = f.read()
        if len(ram) != 65536 or ram[0xC003] != 3:
            fail("%s did not finish on INSTR" % label)
        if ram[0xC001] != expected_row:
            fail("%s landed on field %d, expected %d"
                 % (label, ram[0xC001], expected_row))
        if instrument_type == 3 and ram[INSTRS + 4] != 0:
            fail("%s left KIT bank at %02X instead of clamping to 00"
                 % (label, ram[INSTRS + 4]))
        if label == "kit-type-vol" and ram[INSTRS + 1] != 0x40:
            fail("KIT VOL fine edit was not blocked/coarsened: %02X"
                 % ram[INSTRS + 1])
        if label == "lfsr-type-vol-edit" and ram[INSTRS + 1] != 0x4C:
            fail("LFSR VOL lost fine/coarse editing: %02X"
                 % ram[INSTRS + 1])
        if label == "lfsr-table-left" and ram[INSTRS + 6] != 0x04:
            fail("LFSR TABLE Left produced %02X instead of 04"
                 % ram[INSTRS + 6])
        if label.endswith("-pan") and ram[INSTRS + 7] != 0x98:
            fail("%s PAN edit produced %02X instead of 98"
                 % (label, ram[INSTRS + 7]))


def run_drill_row_reset_case(harness, core, rom, build, phrase):
    label = "phrase-entry-row-reset" if phrase else "chain-entry-row-reset"
    test_rom = os.path.join(
        build, "alynxdj-editor-%s-%d-%d.lnx"
        % (label, os.getpid(), time.time_ns()))
    ppm = os.path.join(build, "editor-%s.ppm" % label)
    ram_path = os.path.join(build, "editor-%s.ram" % label)
    shutil.copyfile(rom, test_rom)

    pokes = {SONG: 0}
    put(pokes, CHAINS, (0, 0))
    env = os.environ.copy()
    env["RETROSHOT_RAM_OUT"] = ram_path
    env["RETROSHOT_RAM_POKE"] = ",".join(
        "%04X:%02X" % item for item in sorted(pokes.items()))
    env["RETROSHOT_RAM_POKE_AT"] = "250"

    drill = "100@10,180@20,100@10,0@40,"
    down = "20@5,0@8,"
    back = "100@10,140@20,100@10,0@40,"
    script = ("0@280," + drill * (2 if phrase else 1)
              + down * 10 + back + drill + "0@80")
    subprocess.run(
        [harness, core, test_rom, ppm, "1100", script],
        env=env, check=True)
    with open(ram_path, "rb") as f:
        ram = f.read()
    expected_screen = 2 if phrase else 1
    if len(ram) != 65536 or ram[0xC003] != expected_screen:
        fail("%s did not return to its child screen" % label)
    if ram[0xC001] != 0:
        fail("%s retained row %02X instead of entering at 00"
             % (label, ram[0xC001]))


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
    if ram[0xC013] != 1:
        fail("clean EEPROM palette is %02X, expected default 01"
             % ram[0xC013])


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
    # The final live PACK meter walks all 7680 song bytes with VBlank safely
    # gated. Leave enough wall-clock frames for that pass and its 2 KB helper
    # reload to finish before inspecting RAM.
    confirm_demo = "1@4,0@24,1@4,0@500"
    script = "0@280," + to_files + down_three + confirm_demo
    subprocess.run(
        [harness, core, test_rom, ppm, "1400", script], env=env, check=True)
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
    with open(test_rom, "rb") as f:
        image = f.read()
    helper = image[64 + 254 * 1024:64 + 256 * 1024 - 3]
    if ram[0xC100:0xC8FD] != helper:
        fail("FILES DEMO left the live MIDI/sync helper overwritten")


def run_purge_overlay_case(harness, core, rom, build):
    label = "files-purge-overlay"
    test_rom = os.path.join(
        build, "alynxdj-editor-%s-%d-%d.lnx"
        % (label, os.getpid(), time.time_ns()))
    ppm = os.path.join(build, "editor-%s.ppm" % label)
    ram_path = os.path.join(build, "editor-%s.ram" % label)
    shutil.copyfile(rom, test_rom)

    pokes = {SONG: 0}
    put(pokes, CHAINS, (0, 0))
    put(pokes, CHAINS + CHAIN_SIZE, (1, 0))
    put(pokes, PHRASES, (37, 0, 0, 0))
    put(pokes, PHRASES + PHRASE_SIZE, (38, 1, 0, 0))
    env = os.environ.copy()
    env["RETROSHOT_RAM_OUT"] = ram_path
    env["RETROSHOT_RAM_POKE"] = ",".join(
        "%04X:%02X" % item for item in sorted(pokes.items()))
    env["RETROSHOT_RAM_POKE_AT"] = "250"

    to_files = "100@10,120@20,100@10,0@40,"
    up_to_purge = "10@6,0@20,"
    confirm = "1@4,0@24,1@4,0@80"
    script = "0@280," + to_files + up_to_purge + confirm
    subprocess.run(
        [harness, core, test_rom, ppm, "750", script], env=env, check=True)
    with open(ram_path, "rb") as f:
        ram = f.read()
    if len(ram) != 65536 or ram[0xC003] != 5:
        fail("PURGE overlay action did not remain on FILES")
    if tuple(ram[CHAINS:CHAINS + 2]) != (0, 0) or ram[PHRASES] != 37:
        fail("PURGE overlay removed referenced chain/phrase data")
    if any(value != 0xFF for value in
           ram[CHAINS + CHAIN_SIZE:CHAINS + CHAIN_SIZE * 2]):
        fail("PURGE overlay retained the unreferenced chain")
    if any(ram[PHRASES + PHRASE_SIZE:PHRASES + PHRASE_SIZE * 2]):
        fail("PURGE overlay retained the unreferenced phrase")


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
    for row, cmd in enumerate(TABLE_COMMAND_CASES):
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
    table_edits = "".join(
        edit + (down if row + 1 < len(TABLE_COMMAND_CASES) else "")
        for row in range(len(TABLE_COMMAND_CASES)))
    script = ("0@280," + drill * 2 + right * 2 + phrase_edits
              + drill * 2 + right * 2 + table_edits + "0@80")
    subprocess.run(
        [harness, core, test_rom, ppm, "2400", script], env=env, check=True)

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
    for row, cmd in enumerate(TABLE_COMMAND_CASES):
        actual = ram[TABLES + row * 4 + 2]
        table_expected = expected[cmd]
        while table_expected not in TABLE_VALID_COMMANDS:
            table_expected = expected[table_expected]
        if (actual != table_expected
                or ram[TABLES + row * 4 + 3] != 0x40 + row):
            fail("%s TABLE command %d stepped to %d, expected %d"
                 % (label, cmd, actual, table_expected))


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
    for depth in range(3):
        run_block_selection_visual_case(harness, core, rom, build, depth)

    run_back_tap_case(harness, core, rom, build, 1)
    run_back_tap_case(harness, core, rom, build, 2)
    run_back_tap_case(harness, core, rom, build, 3)
    run_map_back_tap_case(harness, core, rom, build, False)
    run_map_back_tap_case(harness, core, rom, build, True)
    run_groove_a_hold_case(harness, core, rom, build)
    run_table_command_latch_case(harness, core, rom, build)
    run_command_recall_tap_case(harness, core, rom, build)
    run_table_command_delete_case(harness, core, rom, build)
    run_option1_context_case(harness, core, rom, build)
    run_option1_toggle_case(harness, core, rom, build)
    run_instr_follow_case(harness, core, rom, build)
    run_instr_entry_top_case(harness, core, rom, build)
    run_instr_selector_case(harness, core, rom, build)
    run_instr_field_visibility_case(harness, core, rom, build)
    run_drill_row_reset_case(harness, core, rom, build, False)
    run_drill_row_reset_case(harness, core, rom, build, True)
    run_wave_navigation_case(harness, core, rom, build, "up")
    run_wave_navigation_case(harness, core, rom, build, "down")
    run_new_defaults_case(harness, core, rom, build)
    run_demo_case(harness, core, rom, build)
    run_purge_overlay_case(harness, core, rom, build)
    run_command_order_case(harness, core, rom, build, False)
    run_command_order_case(harness, core, rom, build, True)

    print("editor clone: PASS — empty cells mint next blank/unreferenced; "
          "occupied cells slim-clone; command cuts/double-taps preserve note "
          "rows; new PHRASE notes inherit the last explicitly edited instrument; "
          "block SELECT highlights every row field on SONG/CHAIN/PHRASE; "
          "CHAIN/PHRASE/INSTR/GROOVE/OPTIONS/PROJECT ignore physical-A back taps; "
          "PHRASE/TABLE share command-pair recall and TABLE deletes field-safely; "
          "INSTR has an in-page selector, universal PAN, and skips fields "
          "unused by each instrument type; hierarchy "
          "drill enters row 00; "
          "KIT bank defaults/clamps to 00 and VOL blocks its fine nibble; "
          "OPTION 1 toggles contextual all-track transport; "
          "PHRASE drill follows the selected row's instrument; WAVE number "
          "requires physical A; NEW patch defaults are canonical; "
          "empty boots stay clean; FILES DEMO and overlaid PURGE work; "
          "PHRASE/TABLE commands step alphabetically, with TABLE skipping "
          "A/D/I/J/L/Z")


if __name__ == "__main__":
    main()
