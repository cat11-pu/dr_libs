#!/usr/bin/env python3
"""Classify malformed-file behavior: dr_libs vs the reference decoder.

Input SSV rows:
  <path> <dr_open> <dr_any> <dr_complete> <ref_open> <ref_any> <ref_complete>

A decoder "accepts" a file only when it completes the stream without a hard
error (complete=1). Everything else is a rejection at the open or decode
stage. Acceptance rule: dr_libs must reject exactly as the reference does,
or reject more strictly. Rows where dr accepts but the reference rejects are
inconsistencies (dr is too lenient).
"""
import collections
import os
import sys

RAW = sys.argv[1]
REPORT_DIR = sys.argv[2]

rows = []
with open(RAW) as handle:
    for line in handle:
        parts = line.split()
        path, do, da, dc, ro, ra, rc = parts
        rows.append((path, int(do), int(da), int(dc),
                     int(ro), int(ra), int(rc)))


def verdict(do, da, dc, ro, ra, rc):
    """Return (bucket, detail)."""
    dr_accept = bool(dc)
    ref_accept = bool(rc)
    if not dr_accept and not ref_accept:
        # Both reject the file: the end verdict matches, even if they reject
        # at different pipeline stages. Stage ordering is recorded separately.
        if do == 0 and ro == 1:
            return "stricter", "both reject; dr fails at open, ref fails later"
        if do == 1 and ro == 0:
            return "match", "both reject; ref fails at open, dr fails later"
        return "match", f"both reject (open dr={do} ref={ro})"
    if dr_accept and ref_accept:
        return "match", "both complete the stream"
    if not dr_accept and ref_accept:
        return "stricter", "dr rejects; reference completes"
    return "inconsistent", "dr completes; reference rejects"


buckets = collections.Counter()
per_format = collections.defaultdict(collections.Counter)
details = []
for path, do, da, dc, ro, ra, rc in rows:
    fmt = path.split(os.sep)[-2]
    bucket, detail = verdict(do, da, dc, ro, ra, rc)
    buckets[bucket] += 1
    per_format[fmt][bucket] += 1
    details.append((bucket, fmt, path, do, da, dc, ro, ra, rc, detail))

with open(os.path.join(REPORT_DIR, "malformed_summary.txt"), "w") as out:
    total = len(rows)
    out.write(f"malformed files: {total}\n")
    for bucket in ("match", "stricter", "inconsistent", "other"):
        out.write(f"{bucket:13s}: {buckets[bucket]}\n")
    out.write("\nper format:\n")
    for fmt in ("wav", "flac", "mp3"):
        c = per_format[fmt]
        out.write(f"  {fmt:5s} total={sum(c.values()):4d} "
                  f"match={c['match']:4d} stricter={c['stricter']:4d} "
                  f"inconsistent={c['inconsistent']:4d} other={c['other']:4d}\n")

with open(os.path.join(REPORT_DIR, "inconsistencies_malformed.ssv"), "w") as out:
    out.write("# path; dr_open; dr_any; dr_complete; ref_open; ref_any; ref_complete; detail\n")
    for bucket, fmt, path, do, da, dc, ro, ra, rc, detail in details:
        if bucket == "inconsistent":
            out.write(f"{path}; {do}; {da}; {dc}; {ro}; {ra}; {rc}; {detail}\n")

with open(os.path.join(REPORT_DIR, "malformed_full.ssv"), "w") as out:
    out.write("# path; dr_open; dr_any; dr_complete; ref_open; ref_any; ref_complete; bucket; detail\n")
    for bucket, fmt, path, do, da, dc, ro, ra, rc, detail in details:
        out.write(f"{path}; {do}; {da}; {dc}; {ro}; {ra}; {rc}; {bucket}; {detail}\n")

print(open(os.path.join(REPORT_DIR, "malformed_summary.txt")).read())
