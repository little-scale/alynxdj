#!/usr/bin/env python3
"""Multi-kit sample pool for the cart (streamed at runtime, DESIGN §10).

Scans the sample kit directories (8 WAVs each, sorted), converts to mono
8-bit signed PCM at PCM_RATE, and emits pool.bin:

    +0  'P' 'L'  nkits  pad
    +4  nkits * 8 * { u24 offset-from-pool-start, u16 length }
    ..  sample bytes

The Makefile appends the pool at cart block POOL_BLOCK (byte offset
POOL_BLOCK*1024 into the cart address space).

Usage: alynxdj_pool.py <samplesroot> <out.bin>
"""
import os
import sys
import wave

import numpy as np

PCM_RATE = 7812.5
SLOT_CAP = 16000            # ~2 s per slot — the cart is roomy
TRIM_DB = -48.0


def load(path):
    w = wave.open(path)
    n, ch, sw, fr = (w.getnframes(), w.getnchannels(),
                     w.getsampwidth(), w.getframerate())
    raw = w.readframes(n)
    if sw == 2:
        d = np.frombuffer(raw, np.int16).astype(np.float32) / 32768.0
    else:
        d = (np.frombuffer(raw, np.uint8).astype(np.float32) - 128) / 128.0
    if ch > 1:
        d = d.reshape(-1, ch).mean(axis=1)
    t = np.arange(int(len(d) * PCM_RATE / fr)) * (fr / PCM_RATE)
    d = np.interp(t, np.arange(len(d)), d)
    thresh = 10 ** (TRIM_DB / 20)
    keep = np.where(np.abs(d) > thresh)[0]
    if len(keep):
        d = d[: min(keep[-1] + int(0.01 * PCM_RATE), len(d))]
    d = d[:SLOT_CAP]
    peak = np.abs(d).max() or 1.0
    return np.clip(np.round(d / peak * 120), -127, 127).astype(np.int8)


def main(root, out):
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
    dirsize = 4 + nk * 8 * 5
    off = dirsize
    hdr = bytearray(b"PL")
    hdr.append(nk)
    hdr.append(0)
    data = bytearray()
    for row in slots:
        for s in row:
            hdr += bytes([off & 0xFF, (off >> 8) & 0xFF, (off >> 16) & 0xFF,
                          len(s) & 0xFF, (len(s) >> 8) & 0xFF])
            data += s.tobytes()
            off += len(s)
    with open(out, "wb") as f:
        f.write(hdr + data)
    print("pool: %d kits (%s), %d bytes" % (nk, ", ".join(kits), off))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
