#!/usr/bin/env python3
"""Asserts concrete acceptance evidence for the nmts pipeline."""
import json
import os
import re
import struct
import sys

REC = 132
ISO = re.compile(r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z$")


def fail(msg):
    print("ASSERT FAIL:", msg)
    sys.exit(1)


def load_records(path):
    data = open(path, "rb").read()
    if len(data) % REC:
        fail(f"{path} size {len(data)} not a multiple of {REC}")
    for i in range(0, len(data), REC):
        row = data[i:i + REC]
        io, ev = struct.unpack_from("<QQ", row, 0)
        digest = row[16:48]
        idx, cid, mask, backend = struct.unpack_from("<IIII", row, 48)
        yield {"io": io, "ev": ev, "digest": digest, "idx": idx,
               "cid": cid, "mask": mask, "backend": backend,
               "row": row}


def main():
    out, expected_hash = sys.argv[1], sys.argv[2]
    material = os.path.join(out, "original", "material.nmts")
    fsize = os.path.getsize(material)
    actual_hash = os.popen(f"sha256sum {material}").read().split()[0]
    if actual_hash != expected_hash:
        fail("original material hash changed")

    full = os.path.join(out, "runs", "full", "evidence", "results.bin")
    records = list(load_records(full))
    if len(records) != 40:
        fail(f"expected 40 records, got {len(records)}")

    union = 0
    cases = {}
    prev_idx = -1
    for rec in records:
        if rec["idx"] != prev_idx + 1:
            fail("non-contiguous instance index")
        prev_idx = rec["idx"]
        # Every offset must address bytes inside the ORIGINAL material.
        if not (0 <= rec["ev"] and rec["ev"] + 4 <= fsize):
            fail(f"evidence offset out of range: {rec['ev']}")
        if not (0 <= rec["io"] and rec["io"] + 56 <= fsize):
            fail(f"instance offset out of range: {rec['io']}")
        if rec["digest"] == b"\x00" * 32:
            fail("empty check digest")
        union |= rec["mask"]
        cases.setdefault(rec["cid"], rec["mask"])
    if union != 0x7F:
        fail(f"all 7 check bits must be exercised, union=0x{union:02x}")
    if len(cases) != 5:
        fail(f"expected 5 extreme cases, got {len(cases)}")

    # Timestamps must be concrete UTC instants in every report.
    for name in ("full", "paused", "oom", "shrink"):
        report = os.path.join(out, "runs", name, "evidence",
                              "run_report.json")
        doc = json.load(open(report))
        for key in ("started_at", "ended_at", "generated_at"):
            if not ISO.match(doc[key]):
                fail(f"{name}.{key} not ISO-8601 UTC: {doc[key]}")

    # Pause/resume checkpoint continuity.
    ckpt = json.load(open(os.path.join(
        out, "runs", "paused", "state", "checkpoint.json")))
    if ckpt["next_instance"] != 40 or ckpt["total_instances"] != 40:
        fail("resumed checkpoint did not reach 40/40")
    if ckpt["material_sha256"] != expected_hash:
        fail("checkpoint material hash mismatch")

    # OOM degrade path must have produced CPU evidence and event entries.
    oom = json.load(open(os.path.join(
        out, "runs", "oom", "evidence", "run_report.json")))
    if oom["backend"] != "cpu_reference":
        fail("OOM run did not degrade to CPU")
    if oom["degrade"]["reason"] != "allocation_failed":
        fail("OOM run missing allocation_failed reason")
    events = json.load(open(os.path.join(
        out, "runs", "oom", "evidence", "event_log.json")))
    kinds = {e["event"] for e in events}
    for required in ("resource_pressure", "degrade_to_cpu",
                     "chunk_committed", "run_complete"):
        if required not in kinds:
            fail(f"event log missing {required}")
    for e in events:
        if not ISO.match(e["ts"]):
            fail("event timestamp not ISO-8601 UTC")

    shrink = json.load(open(os.path.join(
        out, "runs", "shrink", "evidence", "run_report.json")))
    if shrink["chunk_shrink_events"] < 1:
        fail("shrink run recorded no chunk_shrink_events")
    if shrink["instances"]["committed"] != 40:
        fail("shrink run did not process all instances")

    print(f"evidence OK: 40 records, 5 cases, mask union 0x7f, "
          f"offsets within {fsize} bytes, timestamps concrete")


if __name__ == "__main__":
    main()
