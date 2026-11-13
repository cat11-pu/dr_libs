#!/usr/bin/env python3
"""Derive 1,000 malformed files from the legal corpus.

Each mutant is produced from a different source file with a deterministic,
seeded mutation so runs are reproducible. Mutation classes:
  truncate   - cut the file at a random offset (header / mid / tail)
  header     - overwrite bytes inside the format's header region
  bitflip    - flip one or more bits at random offsets
  zero       - zero a random span (often hits chunk sizes / frame headers)
  splice     - splice bytes from another file of the same format in
  append     - append garbage / fake chunk data past the real end
"""
import os
import random
import shutil
import sys

CORPUS = sys.argv[1]
OUT = os.path.join(CORPUS, "malformed")

SOURCES = {
    "wav": sorted(os.path.join(CORPUS, "valid", "wav", f)
                  for f in os.listdir(os.path.join(CORPUS, "valid", "wav"))),
    "flac": sorted(os.path.join(CORPUS, "valid", "flac", f)
                   for f in os.listdir(os.path.join(CORPUS, "valid", "flac"))),
    "mp3": sorted(os.path.join(CORPUS, "valid", "mp3", f)
                  for f in os.listdir(os.path.join(CORPUS, "valid", "mp3"))),
}

# 400 WAV, 300 FLAC, 300 MP3, mirroring the valid-corpus proportions.
PLAN = [("wav", 400), ("flac", 300), ("mp3", 300)]
MUTATIONS = ["truncate", "header", "bitflip", "zero", "splice", "append"]


def mutate(data: bytes, rng: random.Random, peers: list[bytes], fmt: str) -> bytes:
    kind = MUTATIONS[rng.randrange(len(MUTATIONS))]
    n = len(data)
    header_len = {"wav": 44, "flac": 64, "mp3": 16}[fmt]

    if kind == "truncate":
        # Keep at least 1 byte; bias towards cuts inside the first quarter,
        # which most often corrupts structural metadata.
        if rng.random() < 0.5:
            cut = rng.randint(1, max(1, n // 4))
        else:
            cut = rng.randint(max(1, n // 4), n)
        return data[:cut]

    if kind == "header":
        buf = bytearray(data)
        span = min(header_len, n)
        for _ in range(rng.randint(1, max(1, span // 4))):
            pos = rng.randrange(span)
            buf[pos] = rng.randrange(256)
        return bytes(buf)

    if kind == "bitflip":
        buf = bytearray(data)
        for _ in range(rng.randint(1, 8)):
            pos = rng.randrange(n)
            buf[pos] ^= 1 << rng.randrange(8)
        return bytes(buf)

    if kind == "zero":
        buf = bytearray(data)
        span = rng.randint(1, min(n, max(1, n // 8)))
        pos = rng.randrange(0, n - span + 1)
        buf[pos:pos + span] = b"\x00" * span
        return bytes(buf)

    if kind == "splice":
        donor = peers[rng.randrange(len(peers))]
        if len(donor) < 8 or n < 8:
            return data
        dpos = rng.randrange(0, len(donor) // 2)
        span = rng.randint(1, min(64, len(donor) - dpos, n))
        pos = rng.randrange(0, n - span + 1)
        buf = bytearray(data)
        buf[pos:pos + span] = donor[dpos:dpos + span]
        return bytes(buf)

    # append
    garbage = bytes(rng.randrange(256) for _ in range(rng.randint(1, 256)))
    if fmt == "wav":
        tag = b"junk" + (len(garbage) + 8).to_bytes(4, "little") + garbage
    elif fmt == "flac":
        tag = b"\x06garb" + len(garbage).to_bytes(3, "big") + b"\x00" + garbage
    else:
        tag = b"ID3" + garbage
    return data + tag


def main() -> None:
    rng = random.Random(0x3557_2026)
    for fmt, count in PLAN:
        out_dir = os.path.join(OUT, fmt)
        os.makedirs(out_dir, exist_ok=True)
        sources = SOURCES[fmt]
        peer_cache = [open(path, "rb").read() for path in sources[:32]]
        for i in range(count):
            src_path = sources[(i * 7 + fmt.__len__()) % len(sources)]
            with open(src_path, "rb") as handle:
                data = handle.read()
            mutant = mutate(data, rng, peer_cache, fmt)
            name = f"{i:05d}_{os.path.basename(src_path)}"
            with open(os.path.join(out_dir, name), "wb") as handle:
                handle.write(mutant)
    for fmt, _ in PLAN:
        n = len(os.listdir(os.path.join(OUT, fmt)))
        print(f"{fmt}: {n}")


if __name__ == "__main__":
    main()
