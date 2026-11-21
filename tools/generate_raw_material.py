#!/usr/bin/env python3
"""Generate the immutable demonstration material for Curved Trim Plane.

This is a one-shot authoring tool. The application never calls it and never
writes inside materials/raw.
"""

import hashlib
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RAW = ROOT / "materials" / "raw"
OBJECTS = RAW / "objects"


def write_obj(path, nx, nz, x0, x1, z0, z1, height):
    positions = []
    for iz in range(nz + 1):
        z = z0 + (z1 - z0) * iz / nz
        for ix in range(nx + 1):
            x = x0 + (x1 - x0) * ix / nx
            positions.append((x, height(x, z), z))

    triangles = []
    stride = nx + 1
    for iz in range(nz):
        for ix in range(nx):
            a = iz * stride + ix
            b = a + 1
            c = a + stride
            d = c + 1
            triangles.append((a, c, b))
            triangles.append((b, c, d))

    normals = [[0.0, 0.0, 0.0] for _ in positions]
    for a, b, c in triangles:
        pa, pb, pc = positions[a], positions[b], positions[c]
        u = (pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2])
        v = (pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2])
        n = (
            u[1] * v[2] - u[2] * v[1],
            u[2] * v[0] - u[0] * v[2],
            u[0] * v[1] - u[1] * v[0],
        )
        for index in (a, b, c):
            for axis in range(3):
                normals[index][axis] += n[axis]
    for normal in normals:
        length = math.sqrt(sum(value * value for value in normal)) or 1.0
        for axis in range(3):
            normal[axis] /= length

    lines = [
        "# Curved Trim Plane read-only raw material",
        f"# grid={nx}x{nz}",
    ]
    lines.extend(f"v {p[0]:.9f} {p[1]:.9f} {p[2]:.9f}" for p in positions)
    lines.extend(f"vn {n[0]:.9f} {n[1]:.9f} {n[2]:.9f}" for n in normals)
    lines.extend(
        f"f {a + 1}//{a + 1} {b + 1}//{b + 1} {c + 1}//{c + 1}"
        for a, b, c in triangles
    )
    lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8")


def main():
    OBJECTS.mkdir(parents=True, exist_ok=True)
    write_obj(
        OBJECTS / "segment_01_ridge.obj",
        44,
        30,
        -2.25,
        -0.85,
        -0.85,
        0.85,
        lambda x, z: 0.18 + 0.22 * math.sin(2.2 * x + 0.5) * math.cos(2.0 * z)
        + 0.08 * math.sin(5.0 * z),
    )
    write_obj(
        OBJECTS / "segment_02_saddle.obj",
        42,
        34,
        -0.82,
        0.82,
        -0.95,
        0.95,
        lambda x, z: 0.34 + 0.32 * math.sin(3.0 * x) * math.cos(2.4 * z)
        + 0.12 * (x * x - z * z),
    )
    write_obj(
        OBJECTS / "segment_03_apron.obj",
        44,
        30,
        0.85,
        2.25,
        -0.82,
        0.82,
        lambda x, z: -0.12 + 0.24 * math.sin(2.5 * x - 0.2) * math.cos(2.2 * z)
        + 0.10 * math.sin(4.0 * z),
    )

    scene = {
        "format": "curved-trim-raw-v1",
        "name": "Three-anchor curved trim demonstration",
        "description": "Read-only raw OBJ fixtures for three independently renderable segments.",
        "created_at": "2026-11-21T16:49:19Z",
        "units": "mm",
        "anchors": [
            {
                "id": 0,
                "name": "A",
                "position_mm": [-1.70, 0.10, 0.0],
                "normal": [0.0, 1.0, 0.0],
                "raw_offset_mm": -0.35,
                "raw_offset_text": "-0.350000",
            },
            {
                "id": 1,
                "name": "B",
                "position_mm": [0.0, 0.38, 0.0],
                "normal": [0.0, 1.0, 0.0],
                "raw_offset_mm": 0.18,
                "raw_offset_text": "0.180000",
            },
            {
                "id": 2,
                "name": "C",
                "position_mm": [1.70, -0.08, 0.0],
                "normal": [0.0, 1.0, 0.0],
                "raw_offset_mm": -0.12,
                "raw_offset_text": "-0.120000",
            },
        ],
        "objects": [
            {
                "id": "seg01_ridge",
                "name": "Segment 01 / Ridge",
                "obj": "objects/segment_01_ridge.obj",
                "material": "brushed-aluminium",
            },
            {
                "id": "seg02_saddle",
                "name": "Segment 02 / Saddle",
                "obj": "objects/segment_02_saddle.obj",
                "material": "oxidized-brass",
            },
            {
                "id": "seg03_apron",
                "name": "Segment 03 / Apron",
                "obj": "objects/segment_03_apron.obj",
                "material": "painted-steel",
            },
        ],
    }
    scene_path = RAW / "scene.raw.json"
    scene_path.write_text(json.dumps(scene, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    files = []
    for path in sorted(list(OBJECTS.glob("*.obj")) + [scene_path]):
        data = path.read_bytes()
        files.append(
            {
                "path": path.relative_to(RAW).as_posix(),
                "bytes": len(data),
                "sha256": hashlib.sha256(data).hexdigest(),
            }
        )
    aggregate = hashlib.sha256()
    for entry in sorted(files, key=lambda item: item["path"]):
        aggregate.update(entry["path"].encode())
        aggregate.update(b"\n")
        aggregate.update(entry["sha256"].encode())
        aggregate.update(b"\n")
        aggregate.update(str(entry["bytes"]).encode())
        aggregate.update(b"\n")
    manifest = {
        "format": "clipfix-raw-material-v1",
        "aggregate_sha256": aggregate.hexdigest(),
        "files": files,
    }
    (RAW / "MANIFEST.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    for path in RAW.rglob("*"):
        if path.is_file():
            path.chmod(0o444)
    print(f"raw material: {RAW}")
    print(f"aggregate:    {aggregate.hexdigest()}")


if __name__ == "__main__":
    main()
