#!/usr/bin/env python3
"""Build and validate portable ALYNXDJ sample banks (DESIGN §10).

Scans the sample kit directories (8 WAVs each, sorted), converts to mono
8-bit signed PCM at PCM_RATE, peak-normalizes, applies +12 dB into a tanh
soft limiter, and emits a self-contained ``.bin`` bank:

    +0  'P' 'L'  nkits  rate-id
    +4  nkits * 8 * { u24 offset-from-pool-start, u16 length }
    ..  sample bytes

Rate ID 1 is the canonical 5,208.333... Hz format (1 MHz timer clock,
reload 191). Other rate IDs are deliberately rejected so a mismatched bank
cannot play at the wrong pitch.

The Makefile injects this bank verbatim at cart block 45.  The same file can
be imported/exported by sample-patch-browser.html and supplied to a later ROM
build, so custom samples are independent of tracker releases.

Usage:
    alynxdj_pool.py <samplesroot> <out.bin>
    alynxdj_pool.py --validate <bank.bin> [--capacity BYTES]
"""
import argparse
import os
import wave

import numpy as np

RATE_ID = 1
PCM_RATE = 1_000_000 / 192
SLOT_CAP = 65535            # u16 directory length; ~12.58 s at PCM_RATE
POOL_CAPACITY = (250 - 45) * 1024
TRIM_DB = -48.0
FACTORY_GAIN_DB = 12.0
FACTORY_GAIN = 10 ** (FACTORY_GAIN_DB / 20)


def decode_pcm(raw, sample_width):
    """Decode little-endian integer WAV PCM to normalized float32."""
    if sample_width == 1:
        return (np.frombuffer(raw, np.uint8).astype(np.float32) - 128) / 128.0
    if sample_width == 2:
        return np.frombuffer(raw, "<i2").astype(np.float32) / 32768.0
    if sample_width == 3:
        octets = np.frombuffer(raw, np.uint8).reshape(-1, 3).astype(np.int32)
        values = octets[:, 0] | octets[:, 1] << 8 | octets[:, 2] << 16
        values = (values ^ 0x800000) - 0x800000
        return values.astype(np.float32) / 8388608.0
    if sample_width == 4:
        return np.frombuffer(raw, "<i4").astype(np.float32) / 2147483648.0
    raise ValueError("unsupported %d-bit PCM WAV" % (sample_width * 8))


def master_pcm(samples):
    """Peak-normalize, apply the canonical factory gain, and tanh-limit."""
    peak = np.abs(samples).max() if len(samples) else 0.0
    if not peak:
        peak = 1.0
    normalized = samples / peak * (120.0 / 127.0)
    driven = np.tanh(normalized * FACTORY_GAIN)
    return np.clip(np.round(driven * 127), -127, 127).astype(np.int8)


def load(path):
    w = wave.open(path)
    n, ch, sw, fr = (w.getnframes(), w.getnchannels(),
                     w.getsampwidth(), w.getframerate())
    raw = w.readframes(n)
    d = decode_pcm(raw, sw)
    if ch > 1:
        d = d.reshape(-1, ch).mean(axis=1)
    t = np.arange(int(len(d) * PCM_RATE / fr)) * (fr / PCM_RATE)
    d = np.interp(t, np.arange(len(d)), d)
    thresh = 10 ** (TRIM_DB / 20)
    keep = np.where(np.abs(d) > thresh)[0]
    if len(keep):
        d = d[: min(keep[-1] + int(0.01 * PCM_RATE), len(d))]
    d = d[:SLOT_CAP]
    return master_pcm(d)


def build(root, out):
    kits = sorted(d for d in os.listdir(root)
                  if os.path.isdir(os.path.join(root, d)))
    slots = []
    for k in kits:
        wavs = sorted(f for f in os.listdir(os.path.join(root, k))
                      if f.lower().endswith(".wav"))[:8]
        row = [load(os.path.join(root, k, f)) for f in wavs]
        while len(row) < 8:
            row.append(np.zeros(1, np.int8))
        slots.append(row)

    nk = len(slots)
    if not 1 <= nk <= 8:
        raise ValueError("sample banks must contain 1 to 8 kit directories")
    dirsize = 4 + nk * 8 * 5
    off = dirsize
    hdr = bytearray(b"PL")
    hdr.append(nk)
    hdr.append(RATE_ID)
    data = bytearray()
    for row in slots:
        for s in row:
            hdr += bytes([off & 0xFF, (off >> 8) & 0xFF, (off >> 16) & 0xFF,
                          len(s) & 0xFF, (len(s) >> 8) & 0xFF])
            data += s.tobytes()
            off += len(s)
    bank = hdr + data
    validate(bank, POOL_CAPACITY)
    with open(out, "wb") as f:
        f.write(bank)
    print("sample bank: %d kits (%s), %d bytes at %.3f Hz, "
          "+%.2f dB tanh mastered" %
          (nk, ", ".join(kits), off, PCM_RATE, FACTORY_GAIN_DB))


def _u16(data, pos):
    return data[pos] | data[pos + 1] << 8


def _u24(data, pos):
    return data[pos] | data[pos + 1] << 8 | data[pos + 2] << 16


def validate(data, capacity=POOL_CAPACITY):
    """Validate a raw PL bank and return (kit_count, used_bytes)."""
    if len(data) < 4 or data[:2] != b"PL":
        raise ValueError("missing ALYNXDJ PL sample-bank header")
    nk = data[2]
    if not 1 <= nk <= 8:
        raise ValueError("sample bank must declare 1 to 8 kits")
    if data[3] != RATE_ID:
        raise ValueError("sample bank has unsupported PCM rate id %d" % data[3])
    directory_size = 4 + nk * 8 * 5
    if len(data) < directory_size:
        raise ValueError("sample-bank directory is truncated")
    previous_end = directory_size
    for index in range(nk * 8):
        entry = 4 + index * 5
        offset = _u24(data, entry)
        length = _u16(data, entry + 3)
        end = offset + length
        if length < 1:
            raise ValueError("kit %d pad %d has an empty sample" %
                             (index // 8 + 1, index % 8 + 1))
        if offset < directory_size or offset < previous_end:
            raise ValueError("kit %d pad %d overlaps the directory or an earlier sample" %
                             (index // 8 + 1, index % 8 + 1))
        if end > len(data):
            raise ValueError("kit %d pad %d points outside the bank" %
                             (index // 8 + 1, index % 8 + 1))
        previous_end = end
    if previous_end != len(data):
        raise ValueError("sample bank has %d trailing bytes" %
                         (len(data) - previous_end))
    if len(data) > capacity:
        raise ValueError("sample bank is %d bytes; ROM capacity is %d" %
                         (len(data), capacity))
    return nk, len(data)


def rate_for_bank(data, capacity=POOL_CAPACITY):
    """Return the PCM rate declared by a validated PL bank."""
    validate(data, capacity)
    return PCM_RATE


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("samplesroot", nargs="?")
    parser.add_argument("output", nargs="?")
    parser.add_argument("--validate", metavar="BANK")
    parser.add_argument("--capacity", type=int, default=POOL_CAPACITY)
    args = parser.parse_args()
    if args.validate:
        if args.samplesroot or args.output:
            parser.error("--validate cannot be combined with build paths")
        with open(args.validate, "rb") as source:
            data = source.read()
        kits, used = validate(data, args.capacity)
        rate = rate_for_bank(data, args.capacity)
        print("sample bank valid: %d kits, %d bytes / %d capacity, %.3f Hz" %
              (kits, used, args.capacity, rate))
    else:
        if not args.samplesroot or not args.output:
            parser.error("provide <samplesroot> <out.bin>, or --validate <bank.bin>")
        build(args.samplesroot, args.output)


if __name__ == "__main__":
    main()
