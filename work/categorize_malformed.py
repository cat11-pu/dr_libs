#!/usr/bin/env python3
"""Enrich the inconsistency list with mutation class and reference error."""
import collections
import os
import random
import re
import subprocess

WORK = "/home/cat/gsb/uid3557/work"
BIN = os.path.join(WORK, "build", "dr_libs_test")
MUT = ["truncate", "header", "bitflip", "zero", "splice", "append"]

# Replay the generator's seeded RNG, line for line, including the peer cache
# (the first 32 valid files of each format) that splice draws donors from.
rng = random.Random(0x3557_2026)
mut_class = {}
for fmt, count in [("wav", 400), ("flac", 300), ("mp3", 300)]:
    src_dir = os.path.join(WORK, "corpus", "valid", fmt)
    sources = sorted(os.path.join(src_dir, f) for f in os.listdir(src_dir))
    peer_cache = [open(path, "rb").read() for path in sources[:32]]
    for i in range(count):
        kind = MUT[rng.randrange(6)]
        src_path = sources[(i * 7 + len(fmt)) % len(sources)]
        with open(src_path, "rb") as handle:
            data = handle.read()
        n = len(data)
        header_len = {"wav": 44, "flac": 64, "mp3": 16}[fmt]
        if kind == "truncate":
            if rng.random() < 0.5:
                rng.randint(1, max(1, n // 4))
            else:
                rng.randint(max(1, n // 4), n)
        elif kind == "header":
            span = min(header_len, n)
            for _ in range(rng.randint(1, max(1, span // 4))):
                rng.randrange(span)
                rng.randrange(256)
        elif kind == "bitflip":
            for _ in range(rng.randint(1, 8)):
                rng.randrange(n)
                rng.randrange(8)
        elif kind == "zero":
            span = rng.randint(1, min(n, max(1, n // 8)))
            rng.randrange(0, n - span + 1)
        elif kind == "splice":
            donor = peer_cache[rng.randrange(len(peer_cache))]
            dpos = rng.randrange(0, len(donor) // 2)
            rng.randint(1, min(64, len(donor) - dpos, n))
            rng.randrange(0, n)
        else:  # append
            rng.randint(1, 256)
        mut_class[(fmt, i)] = kind


def reference_error(fmt: str, path: str) -> str:
    proc = subprocess.run(
        [BIN, "--test", "decoding", "--exceptions",
         os.path.join(WORK, "empty_exceptions.txt"),
         "--format", fmt, path],
        cwd=WORK, capture_output=True, text=True)
    for line in proc.stderr.splitlines() + proc.stdout.splitlines():
        if "error:" not in line:
            continue
        msg = line.split("error:", 1)[1].strip()
        if "correctness test" in msg:
            continue
        return msg
    return "reference produced no frames (metadata/early stream failure)"


rows = []
incons = os.path.join(WORK, "report", "inconsistencies_malformed.ssv")
with open(incons) as handle:
    for line in handle.read().splitlines()[1:]:
        fields = [x.strip() for x in line.split(";")]
        path = fields[0]
        fmt = path.split(os.sep)[-2]
        idx = int(os.path.basename(path).split("_")[0])
        rows.append((fmt, idx, path, mut_class[(fmt, idx)],
                     reference_error(fmt, path)))

by_error = collections.Counter(r[4] for r in rows)
by_mut = collections.Counter(r[3] for r in rows)
by_fmt_mut = collections.Counter((r[0], r[3]) for r in rows)

with open(os.path.join(WORK, "report", "inconsistencies_categorized.ssv"), "w") as out:
    out.write("# format; index; mutation; reference_error; path\n")
    for fmt, idx, path, kind, err in rows:
        out.write(f"{fmt}; {idx:05d}; {kind}; {err}; {path}\n")

with open(os.path.join(WORK, "report", "inconsistencies_breakdown.txt"), "w") as out:
    out.write("by reference error:\n")
    for err, count in by_error.most_common():
        out.write(f"  {count:4d}  {err}\n")
    out.write("\nby mutation class:\n")
    for kind, count in by_mut.most_common():
        out.write(f"  {count:4d}  {kind}\n")
    out.write("\nby format x mutation:\n")
    for key, count in sorted(by_fmt_mut.items()):
        out.write(f"  {count:4d}  {key[0]:5s} {key[1]}\n")

print(open(os.path.join(WORK, "report", "inconsistencies_breakdown.txt")).read())
