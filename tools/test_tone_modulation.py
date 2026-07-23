#!/usr/bin/env python3
"""Headless regression for TONE instrument modulation and B-tap audition."""

import os
import shutil
import subprocess
import sys
import time
import wave

import numpy as np
from PIL import Image


def fail(message):
    raise SystemExit("tone modulation test: " + message)


def audio(path):
    with wave.open(path, "rb") as wav:
        rate = wav.getframerate()
        raw = wav.readframes(wav.getnframes())
    samples = np.frombuffer(raw, dtype="<i2").reshape(-1, 2).mean(axis=1)
    return rate, samples


def sounding_tail(path):
    rate, samples = audio(path)
    onset = np.flatnonzero(np.abs(samples) > 100)
    if not len(onset):
        fail("capture %s is silent" % os.path.basename(path))
    return rate, samples[onset[0]:]


def pitch_track(path):
    rate, samples = sounding_tail(path)
    size = rate // 10
    window = np.hanning(size)
    bins = np.fft.rfftfreq(size, 1.0 / rate)
    lo = np.searchsorted(bins, 100)
    hi = np.searchsorted(bins, 5000)
    out = []
    for start in range(0, len(samples) - size, size):
        spectrum = np.abs(np.fft.rfft(samples[start:start + size] * window))
        out.append(bins[lo + np.argmax(spectrum[lo:hi])])
    return np.asarray(out)


def put(pokes, address, values):
    for offset, value in enumerate(values):
        pokes[address + offset] = value


def crossing_pitch(samples, rate, start, stop):
    window = samples[int(start * rate):int(stop * rate)]
    crossings = np.flatnonzero((window[:-1] <= 0) & (window[1:] > 0))
    if len(crossings) < 3:
        fail("vibrato continuity window did not contain enough cycles")
    return rate / np.mean(np.diff(crossings))


def run_vib_continuity(harness, core, rom, build):
    """Four short C-4 notes must share one transport-scoped LFO phase."""
    sd = 0xD400
    song = sd
    chains = sd + 0x0200
    phrases = sd + 0x0600
    instrs = sd + 0x1600
    grooves = sd + 0x1C00
    chain = 30
    phrase = 63
    instr = 31
    pokes = {}

    # One isolated track, one six-tick groove row, and a retrigger every four
    # rows (24 ticks). Phrase VIB 26 is active on alternating notes, matching
    # the demo lead's V-command / VIB-00 interaction. It traverses a little
    # over half a cycle per use: a per-note phase reset would make both V
    # notes about 20 cents sharp instead of letting the second balance them.
    put(pokes, song, (chain, 0xFF, 0xFF, 0xFF,
                      0xFF, 0xFF, 0xFF, 0xFF))
    put(pokes, chains + chain * 32, bytes([0xFF] * 32))
    put(pokes, chains + chain * 32, (phrase, 0))
    put(pokes, phrases + phrase * 64, bytes(64))
    for row in (0, 4, 8, 12):
        put(pokes, phrases + phrase * 64 + row * 4,
            (37, instr, 9 if row in (0, 8) else 0,
             0x26 if row in (0, 8) else 0))
    put(pokes, instrs + instr * 16,
        (0, 0x7F, 0, 15, 0xFF, 1, 0xFF, 0xFF,
         0, 0, 0, 0, 0, 0, 0, 0))
    put(pokes, grooves, (6,) + (0,) * 15)

    ppm = os.path.join(build, "vib-continuity.ppm")
    env = os.environ.copy()
    env["RETROSHOT_RAM_POKE"] = ",".join(
        "%04X:%02X" % item for item in sorted(pokes.items()))
    env["RETROSHOT_RAM_POKE_AT"] = "250"
    script = "0@280,100@3,101@3,100@2,0@220"
    subprocess.run(
        [harness, core, rom, ppm, "500", script], env=env, check=True)
    return ppm + ".wav"


def run_mod(harness, core, rom, build, label, swp=0, vib=0, trm=0, tsp=0):
    ppm = os.path.join(build, "mod-%s.ppm" % label)
    env = os.environ.copy()
    # sd starts at $D400 and instrs at +$1600 = $EA00.  Sustain instrument
    # 00, set its v4 modulation bytes and v5 TSP byte, then run the stopped
    # audition hook.
    env["RETROSHOT_RAM_POKE"] = (
        "C02D:A9,EA02:00,EA03:00,EA0C:%02X,EA0D:%02X,EA0E:%02X,EA0F:%02X"
        % (swp, vib, trm, tsp))
    env["RETROSHOT_RAM_POKE_AT"] = "100"
    subprocess.run([harness, core, rom, ppm, "420"], env=env, check=True)
    return ppm + ".wav"


def main():
    if len(sys.argv) != 4:
        fail("usage: test_tone_modulation.py RETROSHOT CORE ROM")
    harness, core, rom = sys.argv[1:]
    build = os.path.join(os.path.dirname(os.path.abspath(rom)),
                         "tests", "tone")
    shutil.rmtree(build, ignore_errors=True)
    os.makedirs(build)
    test_rom = os.path.join(
        build, "alynxdj-tone-test-%d-%d.lnx" % (os.getpid(), time.time_ns()))
    shutil.copyfile(rom, test_rom)

    swp = pitch_track(run_mod(harness, core, test_rom, build, "swp", swp=1))
    if len(swp) < 8 or np.median(swp[-3:]) > np.median(swp[:3]) / 1.2:
        fail("positive SWP did not produce a sustained downward pitch sweep: %r"
             % swp)

    vib_mid = pitch_track(run_mod(
        harness, core, test_rom, build, "vib-mid", vib=0x48))
    vib = pitch_track(run_mod(harness, core, test_rom, build, "vib", vib=0x4F))
    if len(vib_mid) < 8 or not 15 <= np.ptp(vib_mid) <= 35:
        fail("VIB depth 8 did not follow the 10/16-semitone curve: %r"
             % vib_mid)
    if len(vib) < 8 or np.ptp(vib) < np.ptp(vib_mid) * 4:
        fail("VIB depth F did not reach the nonlinear 60/16-semitone range: %r"
             % vib)

    continuity_path = run_vib_continuity(
        harness, core, test_rom, build)
    rate, continuity = sounding_tail(continuity_path)
    note_time = 24 / 59.9
    continuity_pitch = np.asarray([
        crossing_pitch(continuity, rate, i * note_time + 0.04,
                       (i + 1) * note_time - 0.04)
        for i in range(4)
    ])
    if (continuity_pitch[0] - continuity_pitch[2] < 5
            or abs(np.mean(continuity_pitch[[0, 2]]) - 261.5) > 1.5
            or np.max(np.abs(continuity_pitch[[1, 3]] - 261.5)) > 1.5):
        fail("VIB phase reset on retrigger or acquired a tuning bias: %r"
             % continuity_pitch)

    # Slow, deep TRM must be a repeating decay saw: predominantly descending
    # for the whole cycle, followed by one large upward reset.  A triangle
    # would spend half the cycle rising gradually and fail this shape check.
    trm_path = run_mod(harness, core, test_rom, build, "trm", trm=0x0F)
    rate, trm = sounding_tail(trm_path)
    size = rate // 20
    rms = np.asarray([
        np.sqrt(np.mean(trm[start:start + size].astype(float) ** 2))
        for start in range(0, len(trm) - size, size)
    ])
    if len(rms) < 25 or rms.max() < rms.min() * 5:
        fail("TRM decay saw did not produce a strong level cycle: %r" % rms)
    trm_diff = np.diff(rms[:25])
    snap = int(np.argmax(trm_diff))
    gradual_rises = np.delete(trm_diff, snap) > 100
    if (snap < 16 or trm_diff[snap] < rms.max() * 0.5
            or np.count_nonzero(gradual_rises) > 1):
        fail("TRM is not a descending ramp with one upward reset: %r" % rms)

    base = pitch_track(run_mod(harness, core, test_rom, build, "tsp-base"))
    up = pitch_track(run_mod(
        harness, core, test_rom, build, "tsp-up", tsp=12))
    ratio = np.median(up[:6]) / np.median(base[:6])
    if not 1.85 < ratio < 2.15:
        fail("TSP +12 did not transpose the instrument by an octave: %.3f"
             % ratio)

    # Navigate SONG -> CHAIN -> PHRASE -> INSTR with physical A+Right,
    # then tap physical B.  Long steps tolerate the C editor's redraw time.
    audition_ppm = os.path.join(build, "instrument-audition.ppm")
    audition_ram = os.path.join(build, "instrument-audition.ram")
    env = os.environ.copy()
    env["RETROSHOT_RAM_OUT"] = audition_ram
    # Force every instrument type to TONE after boot so whichever demo track
    # the navigation reaches must render the unused shared BANK row blank.
    env["RETROSHOT_RAM_POKE"] = ",".join(
        "%04X:00" % (0xEA00 + i * 16) for i in range(32))
    env["RETROSHOT_RAM_POKE_AT"] = "250"
    script = (
        "0@280,"
        "100@10,180@20,100@10,0@40,"
        "100@10,180@20,100@10,0@40,"
        "100@10,180@20,100@10,0@40,"
        "1@10,0@180"
    )
    subprocess.run(
        [harness, core, test_rom, audition_ppm, "700", script],
        env=env, check=True)
    with open(audition_ram, "rb") as f:
        ram = f.read()
    if len(ram) != 65536 or ram[0xC003] != 3:
        fail("input script did not reach the INSTR screen")
    frame = np.asarray(Image.open(audition_ppm).convert("RGB"))
    colors, counts = np.unique(frame.reshape(-1, 3), axis=0, return_counts=True)
    background = colors[np.argmax(counts)]
    for row in (2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 16):
        band = frame[row * 6:(row + 1) * 6, :96]
        if np.count_nonzero(np.any(band != background, axis=2)) < 6:
            fail("INSTR field row %d was not fully rendered" % row)
    bank_band = frame[14 * 6:15 * 6, :96]
    if np.count_nonzero(np.any(bank_band != background, axis=2)):
        fail("TONE instrument rendered the unused BANK row")
    _, audition = sounding_tail(audition_ppm + ".wav")
    if np.max(np.abs(audition)) < 100:
        fail("physical B tap on stopped INSTR did not audition")

    print("tone modulation: PASS — TSP/SWP/nonlinear free-running sine-VIB/"
          "decay-saw TRM audible and stopped INSTR physical-B audition works")


if __name__ == "__main__":
    main()
