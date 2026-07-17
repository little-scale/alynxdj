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
V_ENV_PHASE = VOICE0 + 2
V_ENV_LEVEL = VOICE0 + 3
V_TAP_CUR = VOICE0 + 20
V_TPOS = VOICE0 + 30


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


def run(harness, core, rom, build, label, pokes):
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
    # Physical A held then B starts transport.  The 60-frame tail is below
    # the 96-tick phrase loop, so the rig triggers exactly once.
    script = "0@280,100@3,101@3,100@2,0@60"
    subprocess.run(
        [harness, core, test_rom, ppm, "350", script], env=env, check=True)
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


def main():
    if len(sys.argv) != 4:
        fail("usage: test_hardware_fixes.py RETROSHOT CORE ROM")
    harness, core, rom = sys.argv[1:]
    build = os.path.dirname(os.path.abspath(rom))

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

    # B05 is a one-shot signed +5 offset from the instrument's taps value 1.
    ram, _ = run(harness, core, rom, build, "tap-offset",
                 rig_pokes(cmd=22, param=5))
    tap = ram[V_TAP_CUR] | ram[V_TAP_CUR + 1] << 8
    if tap != 6 << 4:
        fail("B05 produced 9.4 taps $%04X, expected $0060" % tap)

    # G01 now moves 1/16 tap per 59.9-Hz tick.  Over this short run it must
    # move a few taps, not the old roughly-one-tap-per-frame rate.
    ram, _ = run(harness, core, rom, build, "tap-glide",
                 rig_pokes(cmd=4, param=1))
    tap = (ram[V_TAP_CUR] | ram[V_TAP_CUR + 1] << 8) >> 4
    if not 3 <= tap <= 7:
        fail("G01 moved to tap %d; expected the new slow range 3..7" % tap)

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

    print("hardware fixes: PASS — TBS note/tick clocks, finite table VOL "
          "envelope, slow G, signed B taps, and per-sample lengths")


if __name__ == "__main__":
    main()
