#!/usr/bin/env python3
"""Register/audio regressions for the hardware-fix pass."""

import os
import shutil
import subprocess
import sys
import time
import wave

import numpy as np


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
RING0 = 0xD000
RING_SIZE = 512
POOL_FILE_OFFSET = 64 + 45 * 1024


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


def run(harness, core, rom, build, label, pokes, tail_frames=60):
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
    script = "0@280,100@3,101@3,100@2,0@%d" % tail_frames
    subprocess.run(
        [harness, core, test_rom, ppm, str(290 + tail_frames), script],
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

    # B05 is a one-shot signed +5 offset from the instrument's taps value 1.
    ram, _ = run(harness, core, rom, build, "tap-offset",
                 rig_pokes(cmd=22, param=5))
    tap = ram[V_TAP_CUR] | ram[V_TAP_CUR + 1] << 8
    if tap != 6:
        fail("B05 produced taps $%04X, expected $0006" % tap)

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

    # Pool members 0 and 1 in kit 0 have deliberately very different source
    # lengths (2270 vs 202 bytes).  Playback must honor each directory length.
    _, long_wav = run(harness, core, rom, build, "sample-long",
                      rig_pokes(note=37, itype=3, bank=0))
    _, short_wav = run(harness, core, rom, build, "sample-short",
                       rig_pokes(note=38, itype=3, bank=0))
    long_dur = sample_duration(long_wav)
    short_dur = sample_duration(short_wav)
    if long_dur < 0.18 or short_dur > 0.09 or long_dur < short_dur * 3:
        fail("sample boundaries look shared: %.3fs vs %.3fs" %
             (long_dur, short_dur))

    # Kit 0 F4 is the 1,822-byte 808 mid-conga.  It begins 693 bytes into a
    # cart page and therefore crosses two 1 KB boundaries.  A broken borrow
    # in cart_seek() made each cross replay the start of the previous page,
    # producing the characteristic gaps/re-attacks heard on hardware.  After
    # playback, the ring must contain the exact final 512 source bytes in its
    # natural modulo-512 positions.
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

    print("hardware fixes: PASS — TBS note/tick clocks, finite table VOL "
          "envelope, HOLD-F sustain/K exit, signed tick/row G periods with "
          "note continuity, signed B taps, per-sample lengths, and exact "
          "underrun-free cross-page F4 streaming")


if __name__ == "__main__":
    main()
