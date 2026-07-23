#!/usr/bin/env python3
"""ComLynx MIDI takeover regression through Handy's UART bridge."""

import os
import shutil
import subprocess
import sys
import time

SONG = 0xD400
CHAINS = 0xD600
PHRASES = 0xDA00


def fail(message):
    raise SystemExit("MIDI takeover test: " + message)


def add_song_fixture(env, row, track):
    """Install one playable chain after the intentionally clean boot."""
    pokes = {
        SONG + row * 4 + track: 0,
        CHAINS: 0,
        CHAINS + 1: 0,
        PHRASES: 37,
        PHRASES + 1: 0,
    }
    env["RETROSHOT_RAM_POKE"] = ",".join(
        "%04X:%02X" % item for item in sorted(pokes.items()))
    env["RETROSHOT_RAM_POKE_AT"] = "250"


def run_case(harness, core, rom, build, label, events, held):
    test_rom = os.path.join(
        build, "alynxdj-midi-%s-%d-%d.lnx"
        % (label, os.getpid(), time.time_ns()))
    ppm = os.path.join(build, "midi-%s.ppm" % label)
    ram_path = os.path.join(build, "midi-%s.ram" % label)
    shutil.copyfile(rom, test_rom)

    # Physical A+Up enters OPTIONS.  Physical B+Right edits SYNC forward
    # three times: OFF -> OUT -> IN -> MIDI.
    edit = "1@6,81@4,1@6,0@10,"
    script = "0@280,100@10,110@20,100@10,0@30," + edit * 3 + "0@140"
    env = os.environ.copy()
    env["RETROSHOT_RAM_OUT"] = ram_path
    env["RETROSHOT_COMLYNX"] = ",".join(
        "%d:%02X" % event for event in events)
    subprocess.run(
        [harness, core, test_rom, ppm, "600", script], env=env, check=True)

    with open(ram_path, "rb") as f:
        ram = f.read()
    if len(ram) != 65536:
        fail("%s did not return a full RAM dump" % label)
    if ram[0xC00F] != 3:
        fail("%s did not enter MIDI mode (mode %d)" % (label, ram[0xC00F]))
    if tuple(ram[0xC00A:0xC00E]) != tuple(held):
        fail("%s held notes %r, expected %r"
             % (label, tuple(ram[0xC00A:0xC00E]), tuple(held)))
    if ram[0xC020] < len(events):
        fail("%s received only %d of %d UART bytes"
             % (label, ram[0xC020], len(events)))
    if ram[0xC004] != ram[0xC005] or ram[0xC006]:
        fail("%s left RX queue head/tail/overflow at %d/%d/%d"
             % (label, ram[0xC004], ram[0xC005], ram[0xC006]))


def run_clock_case(harness, core, rom, build):
    label = "clock"
    test_rom = os.path.join(
        build, "alynxdj-midi-%s-%d-%d.lnx"
        % (label, os.getpid(), time.time_ns()))
    ppm = os.path.join(build, "midi-%s.ppm" % label)
    ram_path = os.path.join(build, "midi-%s.ram" % label)
    shutil.copyfile(rom, test_rom)

    # Four edits select IN24: OFF -> OUT -> IN -> MIDI -> IN24.
    edit = "1@6,81@4,1@6,0@10,"
    script = "0@280,100@10,110@20,100@10,0@30," + edit * 4 + "0@400"
    events = [(500, 0xFA)]
    events.extend((520 + i * 30, 0xF8) for i in range(4))
    events.append((660, 0xFC))
    env = os.environ.copy()
    env["RETROSHOT_RAM_OUT"] = ram_path
    add_song_fixture(env, 0, 1)
    env["RETROSHOT_COMLYNX"] = ",".join(
        "%d:%02X" % event for event in events)
    subprocess.run(
        [harness, core, test_rom, ppm, "800", script], env=env, check=True)

    with open(ram_path, "rb") as f:
        ram = f.read()
    if len(ram) != 65536:
        fail("IN24 did not return a full RAM dump")
    if ram[0xC00F] != 4:
        fail("IN24 selected mode %d, expected 4" % ram[0xC00F])
    # The fixture makes track B active at song row 0; eng_walk[1].prow is $F20D.
    # Start arms row 0; pulse one presents it and the next three reach row 3.
    if ram[0xF20D] != 3:
        fail("4 row clocks reached phrase row %d, expected 3" % ram[0xF20D])
    if ram[0xC011] != 0:
        fail("MIDI Stop left engine mode %d running" % ram[0xC011])
    if ram[0xC010]:
        fail("MIDI Stop left row clock pending at %d" % ram[0xC010])
    if ram[0xC020] < len(events):
        fail("IN24 received only %d of %d UART bytes"
             % (ram[0xC020], len(events)))


def run_cued_case(harness, core, rom, build, label, mode, pulse, start):
    test_rom = os.path.join(
        build, "alynxdj-midi-%s-%d-%d.lnx"
        % (label, os.getpid(), time.time_ns()))
    ppm = os.path.join(build, "midi-%s.ppm" % label)
    ram_path = os.path.join(build, "midi-%s.ram" % label)
    shutil.copyfile(rom, test_rom)

    # Select the requested sync mode, return to SONG with physical A+Down,
    # cue row 5, and press transport locally.  No external Start is required;
    # a clean physical-A tap is deliberately inert on OPTIONS.
    edit = "1@6,81@4,1@6,0@10,"
    down = "20@6,0@10,"
    to_song = "100@6,120@8,100@6,0@12,"
    script = ("0@280,100@10,110@20,100@10,0@30," + edit * mode
              + to_song + down * 5
              + "100@4,101@4,100@4,0@180")
    # A source commonly sends Start immediately before its first clock.  The
    # armed local cue must win over that default-row-0 request.
    start_byte = 0x02 if mode == 2 else 0xFA
    events = ((600, start_byte), (620, pulse)) if start else ()
    env = os.environ.copy()
    env["RETROSHOT_RAM_OUT"] = ram_path
    add_song_fixture(env, 5, 0)
    if events:
        env["RETROSHOT_COMLYNX"] = ",".join(
            "%d:%02X" % event for event in events)
    subprocess.run(
        [harness, core, test_rom, ppm, "800", script], env=env, check=True)

    with open(ram_path, "rb") as f:
        ram = f.read()
    if len(ram) != 65536:
        fail("%s did not return a full RAM dump" % label)
    if ram[0xC00F] != mode or ram[0xC003] != 0 or ram[0xC001] != 5:
        fail("%s did not select mode %d and cue SONG row 5" % (label, mode))
    if ram[0xC011] != 1:              # MODE_SONG remains armed/running
        fail("%s transport mode is %d, expected SONG" % (label, ram[0xC011]))
    if ram[0xF201] != 5:              # eng_walk[0].song_row
        fail("%s loaded song row %d, expected 5" % (label, ram[0xF201]))
    expected_wait = 0 if start else 1
    expected_label = 2 if start else 1       # PLAY / WAIT mirror values
    if ram[0xC016] != expected_wait or ram[0xC017] != expected_label:
        fail("%s wait/label state is %d/%d, expected %d/%d"
             % (label, ram[0xC016], ram[0xC017],
                expected_wait, expected_label))
    if start and ram[0xF206] != 0:     # first pulse starts, never skips row 0
        fail("%s first pulse skipped to phrase row %d" % (label, ram[0xF206]))


def main():
    if len(sys.argv) != 4:
        fail("usage: test_midi_takeover.py <retroshot> <core> <rom>")
    harness, core, rom = map(os.path.abspath, sys.argv[1:])
    build = os.path.join(os.path.dirname(rom), "tests", "midi")
    shutil.rmtree(build, ignore_errors=True)
    os.makedirs(build)

    events = []
    frame = 450
    for status, note in ((0x90, 60), (0x91, 61), (0x92, 62), (0x93, 63)):
        events.extend(((frame, status), (frame + 1, note), (frame + 2, 100)))
        frame += 5
    # A stale Note Off must not cut the newer ch.1 key; ch.2 releases normally;
    # CC123 releases ch.3.  Ch.1 and ch.4 therefore remain held.
    events.extend(((475, 0x80), (476, 59), (477, 0),
                   (480, 0x81), (481, 61), (482, 0),
                   (485, 0xB2), (486, 0x7B), (487, 0)))
    run_case(harness, core, rom, build, "notes", events,
             (60, 0xFF, 0xFF, 63))

    run_case(harness, core, rom, build, "panic",
             ((450, 0x90), (451, 64), (452, 100), (460, 0xFF)),
             (0xFF, 0xFF, 0xFF, 0xFF))
    run_cued_case(harness, core, rom, build, "in-wait", 2, 0x01, False)
    run_cued_case(harness, core, rom, build, "in-play", 2, 0x01, True)
    run_cued_case(harness, core, rom, build, "in24-wait", 4, 0xF8, False)
    run_cued_case(harness, core, rom, build, "in24-play", 4, 0xF8, True)
    run_clock_case(harness, core, rom, build)
    print("MIDI bridge: 4-channel takeover; IN/IN24 WAIT; row-clock "
          "Start/Clock/Stop OK")


if __name__ == "__main__":
    main()
