# Generated acceptance sample

These files came from `tests/e2e.sh` at 320x180:

- `cube-A0-to-A2-acceptance.md`: object acceptance with UTC, Unix ms,
  original byte offsets, raw attachment offsets, probes, and hashes.
- `cube-A0-to-A2-comparison.json`: machine-readable comparison.
- `cube-A0-to-A2-scene-evidence.md`: full-scene view timestamps and offsets.
- `cube-A0-to-A2-diff.png`: red pixels are changed final-color pixels.
- `cube-overlay.json`: the local correction used by the sample.
- `e2e-verification.json`: result of `gbuffer-inspector verify`.

The full runnable evidence bundle, including 256-byte-aligned raw GBuffer
files and PPM views, is produced under `<workspace>/exports/`.

The frozen source material is `assets/original_scene.json`. Its checksum is
pinned in `assets/SHA256SUMS`, and imported copies are mode `0444`.
