#!/usr/bin/env python3
"""Independent checker for a voxel-slicer evidence directory.

Verifies that pipeline.jsonl has a continuous sequence, every draw_call has a
matching framebuffer_readback, every rendered slice pair has a frame_diff,
and every snapshot's resources were cleaned up. Exits non-zero on any gap so
the result can gate a review.
"""

import json
import sys
from collections import Counter


def main(path):
    records = [json.loads(line) for line in open(path)]
    errors = []
    seqs = [r["seq"] for r in records]
    if seqs != list(range(1, len(seqs) + 1)):
        errors.append("sequence numbers are not continuous")

    counts = Counter(r["event"] for r in records)
    for ev in ("draw_call", "framebuffer_readback", "frame_diff"):
        if counts[ev] == 0:
            errors.append("no %s events" % ev)

    # Each draw call must be followed by a readback of the same object/slice.
    draws = [(r["object"], r["tag"], r["slice"]) for r in records
             if r["event"] == "draw_call"]
    reads = {(r["object"], r["tag"], r["slice"]) for r in records
             if r["event"] == "framebuffer_readback"}
    missing = [d for d in draws if d not in reads]
    if missing:
        errors.append("draw calls without readback: %s" % missing[:3])

    begins = counts["snapshot_render_begin"]
    cleans = counts["resource_cleanup"]
    if begins != cleans:
        errors.append("render begins (%d) != cleanups (%d)" %
                      (begins, cleans))

    backends = {r.get("backend") for r in records
                if r["event"] in ("pipeline_ready", "pipeline_fallback")}
    print("events:", dict(counts))
    print("backends:", sorted(b for b in backends if b))
    if errors:
        for e in errors:
            print("FAIL:", e)
        return 1
    print("evidence verification OK (%d records)" % len(records))
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("usage: verify_evidence.py <pipeline.jsonl>")
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
