#!/usr/bin/env python3
"""Register/audio regressions for the hardware-fix pass."""

import os
import shutil
import subprocess
import sys
import time
import wave

import numpy as np
from PIL import Image


SD = 0xD400
SONG = SD
CHAINS = SD + 0x0200
PHRASES = SD + 0x0600
INSTRS = SD + 0x1600
TABLES = SD + 0x1800
GROOVES = SD + 0x1C00
VOICE0 = SD + 0x1E00 + 4 * 7
WALK0 = SD + 0x1E00
W_PROW = WALK0 + 6
V_ENV_PHASE = VOICE0 + 2
V_ENV_LEVEL = VOICE0 + 3
V_TAP_CUR = VOICE0 + 20
V_TPOS = VOICE0 + 30
G_WAIT0 = 0xC0FC
PCM_UNDERRUN = 0xC027
PCM_STARTED = 0xC02E
PCM_TRIGGERED = 0xC02B
RING0 = 0xD000
RING_SIZE = 512
POOL_FILE_OFFSET = 64 + 45 * 1024
PCM_RATE = 7812.5


def fail(message):
    raise SystemExit("hardware fixes test: " + message)


def put(pokes, address, values):
    for offset, value in enumerate(values):
        pokes[address + offset] = value


def rig_pokes(note=37, cmd=0, param=0, hold=0x0F, env=0,
              table=0xFF, itype=0, bank=0):
    """One isolated note on track A, safely shorter than its phrase loop."""
    chain = 31
    phrase = 63
    instr = 31
    pokes = {}
    put(pokes, SONG, (chain, 0xFF, 0xFF, 0xFF,
                      0xFF, 0xFF, 0xFF, 0xFF))
    put(pokes, CHAINS + chain * 32, bytes([0xFF] * 32))
    put(pokes, CHAINS + chain * 32, (phrase, 0))
    put(pokes, PHRASES + phrase * 64, bytes(64))
    put(pokes, PHRASES + phrase * 64, (note, instr, cmd, param))
    put(pokes, INSTRS + instr * 16,
        (itype, 0x7F, env, hold, bank, 1, table, 0xFF,
         0, 0, 0, 0, 0, 0, 0, 0))
    put(pokes, GROOVES, (6,) + (0,) * 15)
    return pokes


def run(harness, core, rom, build, label, pokes, tail_frames=60,
        tail_script=None, pre_script="", pre_frames=0):
    test_rom = os.path.join(
        build, "alynxdj-hw-%s-%d-%d.lnx" %
        (label, os.getpid(), time.time_ns()))
    ppm = os.path.join(build, "hardware-%s.ppm" % label)
    ram_path = os.path.join(build, "hardware-%s.ram" % label)
    shutil.copyfile(rom, test_rom)
    env = os.environ.copy()
    env["RETROSHOT_RAM_OUT"] = ram_path
    env["RETROSHOT_RAM_POKE"] = ",".join(
        "%04X:%02X" % item for item in sorted(pokes.items()))
    env["RETROSHOT_RAM_POKE_AT"] = "250"
    # Physical A held then B starts transport.  Most cases use a 60-frame
    # tail, below the 96-tick phrase loop, so the rig triggers exactly once.
    if tail_script is None:
        tail_script = "0@%d" % tail_frames
    script = "0@280,%s100@3,101@3,100@2,%s" % (pre_script, tail_script)
    subprocess.run(
        [harness, core, test_rom, ppm,
         str(290 + pre_frames + tail_frames), script],
        env=env, check=True)
    with open(ram_path, "rb") as f:
        ram = f.read()
    if len(ram) != 65536:
        fail("%s did not produce a 64 KB RAM dump" % label)
    return ram, ppm + ".wav"


def sample_duration(path):
    with wave.open(path, "rb") as wav:
        rate = wav.getframerate()
        samples = np.frombuffer(wav.readframes(wav.getnframes()), "<i2")
    samples = samples.reshape(-1, 2).mean(axis=1)
    active = np.flatnonzero(np.abs(samples) > 100)
    if not len(active):
        fail("sample capture %s is silent" % os.path.basename(path))
    return (active[-1] - active[0] + 1) / rate


def pool_sample(rom, kit, member):
    """Return one packed sample directly from the ROM's cart pool."""
    with open(rom, "rb") as f:
        image = f.read()
    base = POOL_FILE_OFFSET
    entry = base + 4 + kit * 40 + (member & 7) * 5
    off = (image[entry] | image[entry + 1] << 8
           | image[entry + 2] << 16)
    length = image[entry + 3] | image[entry + 4] << 8
    return image[base + off:base + off + length]


def main():
    if len(sys.argv) != 4:
        fail("usage: test_hardware_fixes.py RETROSHOT CORE ROM")
    harness, core, rom = sys.argv[1:]
    build = os.path.join(os.path.dirname(os.path.abspath(rom)),
                         "tests", "hardware")
    shutil.rmtree(build, ignore_errors=True)
    os.makedirs(build)

    # TBS 0 advances exactly one table row for the only triggered note.
    p = rig_pokes(hold=0x0F, table=0)
    put(p, TABLES, bytes(64))
    ram, _ = run(harness, core, rom, build, "tbs-note", p)
    if ram[V_TPOS] != 1:
        fail("TBS 0 advanced to table state $%02X, expected row 1" %
             ram[V_TPOS])

    # TBS 1 is the fastest tick mode and reaches/sticks at row F.
    p = rig_pokes(hold=0x1F, table=0)
    put(p, TABLES, bytes(64))
    ram, _ = run(harness, core, rom, build, "tbs-fast", p)
    if (ram[V_TPOS] & 0x0F) != 15:
        fail("TBS 1 did not advance to table row F: $%02X" % ram[V_TPOS])

    # TABLE follows the selected top-bar track and accents its active macro
    # row. Drill SONG -> CHAIN -> PHRASE -> INSTR, start the phrase there,
    # then enter TABLE; TBS 0 applies row 0 once and leaves the engine cursor
    # at row 1.
    p = rig_pokes(hold=0x0F, table=0)
    put(p, TABLES, bytes(64))
    put(p, GROOVES, (15,) + (0,) * 15)
    nav_right = "100@10,180@20,100@10,0@40,"
    ram, table_wav = run(
        harness, core, rom, build, "table-playhead", p,
        tail_frames=240, pre_script=nav_right * 3, pre_frames=240,
        tail_script="0@10," + nav_right + "0@150")
    if ram[0xC003] != 4:
        fail("TABLE playhead rig ended on screen %d, expected TABLE" %
             ram[0xC003])
    frame = np.asarray(Image.open(table_wav[:-4]).convert("RGB"))
    colors, counts = np.unique(frame.reshape(-1, 3), axis=0,
                               return_counts=True)
    background = colors[np.argmax(counts)]

    def dominant_ink(block):
        pixels = block.reshape(-1, 3)
        pixels = pixels[np.any(pixels != background, axis=1)]
        if not len(pixels):
            fail("TABLE playhead visual probe found an empty glyph")
        ink, ink_counts = np.unique(pixels, axis=0, return_counts=True)
        return tuple(ink[np.argmax(ink_counts)])

    # The title remains fixed at pixel x=4; the editor body is offset eight
    # character cells, so TABLE row numbers begin at pixel x=36.
    accent = dominant_ink(frame[0:6, 4:24])
    row0 = dominant_ink(frame[6:12, 36:44])
    row1 = dominant_ink(frame[12:18, 36:44])
    if row0 != accent or row1 == accent:
        fail("TABLE active-row accent is wrong: title %r, row0 %r, row1 %r" %
             (accent, row0, row1))

    # A repeating table VOL must not revive the decay stage.
    p = rig_pokes(hold=0x11, env=0x01, table=0)
    table = bytearray(64)
    table[0::4] = bytes([0x7F] * 16)
    put(p, TABLES, table)
    ram, _ = run(harness, core, rom, build, "table-envelope", p)
    if ram[V_ENV_PHASE] or ram[V_ENV_LEVEL]:
        fail("table VOL kept envelope alive (phase %d, level %d)" %
             (ram[V_ENV_PHASE], ram[V_ENV_LEVEL]))

    # HOLD F is the one indefinite value; K remains its explicit timed exit.
    ram, _ = run(harness, core, rom, build, "hold-sustain",
                 rig_pokes(hold=0x0F, env=0x01, table=0xFF))
    if ram[V_ENV_PHASE] != 2 or ram[V_ENV_LEVEL] != 0x7F:
        fail("HOLD F did not sustain at peak (phase %d, level %d)" %
             (ram[V_ENV_PHASE], ram[V_ENV_LEVEL]))
    ram, _ = run(harness, core, rom, build, "hold-sustain-kill",
                 rig_pokes(cmd=6, param=8, hold=0x0F, env=0x01, table=0xFF))
    if ram[V_ENV_PHASE] or ram[V_ENV_LEVEL]:
        fail("K did not stop HOLD F sustain (phase %d, level %d)" %
             (ram[V_ENV_PHASE], ram[V_ENV_LEVEL]))

    # Phrase H is a pre-row branch.  A note/sample on the marker row must
    # never trigger: row 4 hands directly to the target row 0.
    p = rig_pokes()
    put(p, PHRASES + 63 * 64 + 5 * 4, (42, 30, 5, 0))
    put(p, INSTRS + 30 * 16,
        (3, 0x7F, 0, 0x0F, 0, 1, 0xFF, 0xFF,
         0, 0, 0, 0, 0, 0, 0, 0))
    ram, _ = run(harness, core, rom, build, "phrase-h-prehop", p,
                 tail_frames=180)
    if ram[W_PROW] >= 5:
        fail("H00 allowed phrase row %d to run; expected loop rows 0-4"
             % ram[W_PROW])
    if ram[PCM_TRIGGERED] or ram[PCM_TRIGGERED + 1]:
        fail("H00 marker row triggered its KIT note: %d/%d"
             % (ram[PCM_TRIGGERED], ram[PCM_TRIGGERED + 1]))

    # SONG cells separated by an empty row are independent vertical groups.
    # Starting at rows 2-3 must loop that pair, never wrap into row 0's group.
    p = {}
    put(p, SONG, bytes([0xFF] * 20))
    p[SONG] = 29
    p[SONG + 2 * 4] = 30
    p[SONG + 3 * 4] = 31
    for chain, phrase in ((29, 61), (30, 62), (31, 63)):
        put(p, CHAINS + chain * 32, bytes([0xFF] * 32))
        put(p, CHAINS + chain * 32, (phrase, 0))
        put(p, PHRASES + phrase * 64, bytes(64))
    put(p, GROOVES, (6,) + (0,) * 15)
    ram, _ = run(
        harness, core, rom, build, "song-contiguous-group", p,
        tail_frames=240,
        pre_script="20@4,0@4,20@4,0@4,", pre_frames=16)
    song_row = ram[WALK0 + 1]
    if song_row not in (2, 3):
        fail("non-contiguous SONG group wrapped to row %d, expected 2/3"
             % song_row)
    if ram[WALK0 + 2] not in (30, 31):
        fail("non-contiguous SONG group selected chain %d, expected 30/31"
             % ram[WALK0 + 2])

    # B05 is a one-shot signed +5 offset from the instrument's taps value 1.
    ram, _ = run(harness, core, rom, build, "tap-offset",
                 rig_pokes(cmd=22, param=5))
    tap = ram[V_TAP_CUR] | ram[V_TAP_CUR + 1] << 8
    if tap != 6:
        fail("B05 produced taps $%04X, expected $0006" % tap)

    # Nonzero B is a track accumulator: intervening note triggers retain the
    # live value, and each later signed B adds to it. Row 0 makes 1->2, row 1
    # retriggers without B, and row 2 must therefore make 2->3.
    p = rig_pokes(cmd=22, param=1)
    put(p, PHRASES + 63 * 64 + 1 * 4, (37, 31, 0, 0))
    put(p, PHRASES + 63 * 64 + 2 * 4, (37, 31, 22, 1))
    ram, _ = run(harness, core, rom, build, "tap-accumulator", p)
    tap = ram[V_TAP_CUR] | ram[V_TAP_CUR + 1] << 8
    if tap != 3:
        fail("successive B01 commands with an intervening note produced "
             "tap %d, expected 3" % tap)

    # B00 restores the current instrument's patch TAPS and releases B's
    # ownership, so the following ordinary note remains at the patch value.
    p = rig_pokes(cmd=22, param=5)
    put(p, PHRASES + 63 * 64 + 1 * 4, (37, 31, 22, 1))
    put(p, PHRASES + 63 * 64 + 2 * 4, (37, 31, 22, 0))
    put(p, PHRASES + 63 * 64 + 3 * 4, (37, 31, 0, 0))
    ram, _ = run(harness, core, rom, build, "tap-accumulator-reset", p)
    tap = ram[V_TAP_CUR] | ram[V_TAP_CUR + 1] << 8
    if tap != 1:
        fail("B00 did not restore and release the patch taps: %d" % tap)

    # G01 is the fastest tick-period glide.  Its first increment occurs one
    # complete tracker tick after the command, then once on every tick.
    ram, _ = run(harness, core, rom, build, "tap-glide-fast",
                 rig_pokes(cmd=4, param=1))
    tap = ram[V_TAP_CUR] | ram[V_TAP_CUR + 1] << 8
    tick_clocks = tap - 1
    if tick_clocks < ram[W_PROW] * 5 or ram[G_WAIT0] != 1:
        fail("G01 did not run at tick rate: tap %d, row %d, wait %d" %
             (tap, ram[W_PROW], ram[G_WAIT0]))

    # G07 uses the same tick clock but requires seven complete ticks for each
    # increment.  Compare against G01's exact captured clock count so this
    # also verifies the initial full-period delay.
    ram7, _ = run(harness, core, rom, build, "tap-glide-seven-ticks",
                  rig_pokes(cmd=4, param=7))
    tap7 = ram7[V_TAP_CUR] | ram7[V_TAP_CUR + 1] << 8
    expected = 1 + tick_clocks // 7
    expected_wait = 7 - (tick_clocks % 7)
    if tap7 != expected or ram7[G_WAIT0] != expected_wait:
        fail("G07 produced tap/wait %d/%d after %d ticks; expected %d/%d" %
             (tap7, ram7[G_WAIT0], tick_clocks, expected, expected_wait))

    # G08 begins the row-period range at one row per increment.
    ram, _ = run(harness, core, rom, build, "tap-glide-row-fast",
                 rig_pokes(cmd=4, param=8))
    tap = ram[V_TAP_CUR] | ram[V_TAP_CUR + 1] << 8
    expected = ram[W_PROW] + 1
    if tap != expected or ram[G_WAIT0] != 1:
        fail("G08 moved to tap/wait %d/%d at phrase row %d; expected %d/1" %
             (tap, ram[G_WAIT0], ram[W_PROW], expected))

    # G0B has signed magnitude 11: after subtracting the seven tick-period
    # values it leaves the original taps for four complete rows, then first
    # changes at the start of row 4.
    ram, _ = run(harness, core, rom, build, "tap-glide-row-period",
                 rig_pokes(cmd=4, param=0x0B))
    tap = ram[V_TAP_CUR] | ram[V_TAP_CUR + 1] << 8
    expected = 1 + ram[W_PROW] // 4
    if tap != expected:
        fail("G0B moved to tap %d at phrase row %d; expected %d" %
             (tap, ram[W_PROW], expected))
    expected_wait = 4 - (ram[W_PROW] % 4)
    if ram[G_WAIT0] != expected_wait:
        fail("G0B countdown is %d at phrase row %d; expected %d "
             "(the command row was counted early)" %
             (ram[G_WAIT0], ram[W_PROW], expected_wait))

    # A looping 16-row phrase encounters G7F long before its 120-row period
    # can expire.  Every pass must therefore restart at the instrument taps;
    # run beyond 120 total rows so a countdown that leaks across passes is
    # guaranteed to reveal itself.
    ram, _ = run(harness, core, rom, build, "tap-glide-loop-reset",
                 rig_pokes(cmd=4, param=0x7F), tail_frames=900)
    tap = ram[V_TAP_CUR] | ram[V_TAP_CUR + 1] << 8
    if tap != 1:
        fail("looping G7F reached tap %d at phrase row %d; expected the "
             "row-0 command to keep restoring tap 1" %
             (tap, ram[W_PROW]))
    if not 104 <= ram[G_WAIT0] <= 120:
        fail("looping G7F countdown escaped its current phrase pass: %d "
             "at row %d" % (ram[G_WAIT0], ram[W_PROW]))

    # An ordinary note at row 5 must not restart either G0B's live taps or
    # its partially elapsed countdown.  Motion therefore still lands on the
    # same row-4/row-8 schedule established by the G on row 0.
    p = rig_pokes(cmd=4, param=0x0B)
    put(p, PHRASES + 63 * 64 + 5 * 4, (37, 31, 0, 0))
    ram, _ = run(harness, core, rom, build, "tap-glide-across-note", p)
    tap = ram[V_TAP_CUR] | ram[V_TAP_CUR + 1] << 8
    expected = 1 + ram[W_PROW] // 4
    if tap != expected:
        fail("plain note restarted G0B at tap %d, row %d; expected %d" %
             (tap, ram[W_PROW], expected))

    # GF9 is signed -7: one decrement per seven ticks.
    p = rig_pokes(cmd=4, param=0xF9)
    p[INSTRS + 31 * 16 + 5] = 64
    ram, _ = run(harness, core, rom, build, "tap-glide-negative-tick", p)
    tap = ram[V_TAP_CUR] | ram[V_TAP_CUR + 1] << 8
    expected = 64 - tick_clocks // 7
    if tap != expected:
        fail("GF9 moved to tap %d after %d ticks; expected %d" %
             (tap, tick_clocks, expected))

    # GF5 is signed -11: after the split it means one decrement per four
    # rows, independently of pitch.
    p = rig_pokes(cmd=4, param=0xF5)
    p[INSTRS + 31 * 16 + 5] = 8
    ram, _ = run(harness, core, rom, build, "tap-glide-negative-row", p)
    tap = ram[V_TAP_CUR] | ram[V_TAP_CUR + 1] << 8
    expected = 8 - ram[W_PROW] // 4
    if tap != expected:
        fail("GF5 moved to tap %d at phrase row %d; expected %d" %
             (tap, ram[W_PROW], expected))

    # A later G command restarts from the active instrument's stored taps.
    # By row 8 G01 has moved well away; G00 must restore 1 and stop.
    p = rig_pokes(cmd=4, param=1)
    put(p, PHRASES + 63 * 64 + 8 * 4, (0, 0, 4, 0))
    ram, _ = run(harness, core, rom, build, "tap-glide-reset", p)
    tap = ram[V_TAP_CUR] | ram[V_TAP_CUR + 1] << 8
    if tap != 1:
        fail("second G command retained live tap %d instead of restoring 1" %
             tap)

    # Exercise the longest and shortest members in the active factory bank.
    # Deriving the members and expected durations keeps this boundary check
    # valid when the portable factory bank is replaced through the browser.
    kit0 = [pool_sample(rom, 0, member) for member in range(8)]
    long_member = max(range(8), key=lambda member: len(kit0[member]))
    short_member = min(range(8), key=lambda member: len(kit0[member]))
    long_expected = len(kit0[long_member]) / PCM_RATE
    short_expected = len(kit0[short_member]) / PCM_RATE
    long_p = rig_pokes(note=37 + long_member, itype=3, bank=0)
    short_p = rig_pokes(note=37 + short_member, itype=3, bank=0)
    # Fifteen-tick rows keep the 16-row phrase loop beyond even the current
    # two-second kick, so the duration capture contains only one trigger.
    put(long_p, GROOVES, (15,) + (0,) * 15)
    put(short_p, GROOVES, (15,) + (0,) * 15)
    _, long_wav = run(
        harness, core, rom, build, "sample-long", long_p,
        tail_frames=max(60, int(long_expected * 75) + 30))
    _, short_wav = run(
        harness, core, rom, build, "sample-short", short_p,
        tail_frames=max(60, int(short_expected * 75) + 30))
    long_dur = sample_duration(long_wav)
    short_dur = sample_duration(short_wav)
    if (abs(long_dur - long_expected) > 0.05
            or abs(short_dur - short_expected) > 0.05):
        fail("sample boundaries differ from directory lengths: "
             "%.3fs/%.3fs measured, %.3fs/%.3fs expected" %
             (long_dur, short_dur, long_expected, short_expected))

    # Kit 0 F4 exposed the cart-page seek-borrow bug in the original factory
    # bank. After playback, the ring must still contain the exact final source
    # bytes in their natural modulo-512 positions, regardless of later bank
    # replacement or browser trimming.
    f4_ram, _ = run(harness, core, rom, build, "sample-cart-pages",
                    rig_pokes(note=42, itype=3, bank=0))
    f4 = pool_sample(rom, 0, 5)
    ring = f4_ram[RING0:RING0 + RING_SIZE]
    start = max(0, len(f4) - RING_SIZE)
    for i in range(start, len(f4)):
        pos = i & (RING_SIZE - 1)
        if ring[pos] != f4[i]:
            fail("F4 cart-page stream differs at sample %d/ring $%03X: "
                 "$%02X != $%02X" %
                 (i, pos, ring[pos], f4[i]))
    if f4_ram[PCM_UNDERRUN] or f4_ram[PCM_UNDERRUN + 1]:
        fail("F4 stream underruns: %d/%d" %
             (f4_ram[PCM_UNDERRUN], f4_ram[PCM_UNDERRUN + 1]))

    # Sustained hardware-listening workload: the longest kit-0 sample is
    # retriggered every four six-tick rows while a separate TONE voice runs
    # patch SWP plus row-rate G08. This keeps the 7.8 kHz stream continuously
    # active and is the case that justified removing channel metering.
    p = {}
    put(p, SONG, (31, 30, 0xFF, 0xFF,
                  0xFF, 0xFF, 0xFF, 0xFF))
    put(p, CHAINS + 31 * 32, bytes([0xFF] * 32))
    put(p, CHAINS + 31 * 32, (63, 0))
    put(p, CHAINS + 30 * 32, bytes([0xFF] * 32))
    put(p, CHAINS + 30 * 32, (62, 0))
    put(p, PHRASES + 63 * 64, bytes(64))
    put(p, PHRASES + 63 * 64, (37, 30, 4, 8))
    put(p, PHRASES + 62 * 64, bytes(64))
    for row in (0, 4, 8, 12):
        put(p, PHRASES + 62 * 64 + row * 4,
            (37 + long_member, 31, 0, 0))
    put(p, INSTRS + 30 * 16,
        (0, 0x7F, 0, 0x0F, 0xFF, 1, 0xFF, 0xFF,
         0, 0, 0, 0, 1, 0, 0, 0))
    put(p, INSTRS + 31 * 16,
        (3, 0x7F, 0, 0x0F, 0, 1, 0xFF, 0xFF,
         0, 0, 0, 0, 0, 0, 0, 0))
    put(p, GROOVES, (6,) + (0,) * 15)
    stress_ram, _ = run(harness, core, rom, build, "sample-sustain-stress",
                        p, tail_frames=300)
    if stress_ram[PCM_UNDERRUN] or stress_ram[PCM_UNDERRUN + 1]:
        fail("sustained KIT + TONE G08/SWP underruns: %d/%d" %
             (stress_ram[PCM_UNDERRUN],
              stress_ram[PCM_UNDERRUN + 1]))

    # Repeat the same sustained workload while forcing full SONG/CHAIN/
    # PHRASE/INSTR/WAVE redraws.  Rendering must cooperatively refill PCM and
    # service every latched trigger; before the redraw-yield fix, synchronous
    # page paints could leave a trigger pending long enough to be replaced.
    gesture = {
        "right": "100@2,180@2,100@2,0@6",
        "up": "100@2,110@2,100@2,0@6",
        "down": "100@2,120@2,100@2,0@6",
    }
    nav = ["0@12", gesture["right"], gesture["right"], gesture["right"]]
    for _ in range(7):
        nav.extend((gesture["up"], gesture["down"]))
    nav.append("0@84")
    redraw_ram, _ = run(
        harness, core, rom, build, "sample-redraw-stress", p,
        tail_frames=300, tail_script=",".join(nav))
    if redraw_ram[PCM_UNDERRUN] or redraw_ram[PCM_UNDERRUN + 1]:
        fail("screen redraws caused KIT underruns: %d/%d" %
             (redraw_ram[PCM_UNDERRUN],
              redraw_ram[PCM_UNDERRUN + 1]))
    triggered = tuple(redraw_ram[PCM_TRIGGERED:PCM_TRIGGERED + 2])
    started = tuple(redraw_ram[PCM_STARTED:PCM_STARTED + 2])
    if triggered != started or not sum(started):
        fail("screen redraws lost/deferred KIT starts: triggered %r, "
             "started %r" % (triggered, started))

    print("hardware fixes: PASS — TBS note/tick clocks and selected-track "
          "TABLE playhead, finite table VOL envelope, HOLD-F sustain/K exit, "
          "pre-row phrase H, independent "
          "contiguous SONG groups, signed tick/row G periods with note "
          "continuity, cumulative signed B taps/reset, portable-bank sample "
          "lengths, exact "
          "F4 streaming, and uninterrupted sustained/redraw KIT + G08/SWP")


if __name__ == "__main__":
    main()
