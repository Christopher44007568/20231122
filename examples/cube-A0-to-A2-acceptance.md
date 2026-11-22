# GBuffer object acceptance

- Object: `cube`
- Verdict: **CHANGED**
- Generated: `2026-11-22T03:08:47.310Z`
- Original material SHA-256: `7179cc5fdaa58f86a99c3bdc706489138eef560c2bfb68d1bb4a8f1587c7a392`

## Anchored evidence

| Side | Anchor | Capture | View UTC | Unix ms | Raw file | Raw SHA-256 |
|---|---|---|---|---:|---|---|
| left | `A0` | `A0-2026_11_22T03_08_46_795Z-77fb82` | `2026-11-22T03:08:46.902Z` | 1795316926902 | `objects/cube.gbuffer.bin` | `393f152b8c78dec2c31e7b98f6ed75a21b1163a5ecffa85717e3bd5335451b45` |
| right | `A2` | `A2-2026_11_22T03_08_47_114Z-9c2b7a` | `2026-11-22T03:08:47.175Z` | 1795316927175 | `objects/cube.gbuffer.bin` | `668a16ee01cb4a7e205078efe18db5e6f9399b52099acd6f0b3f34d18103b304` |

## Raw offsets

Source object `cube`: `originals/scene.json` offset `366`, length `246`.

Correction overlay: `overlays/cube.json`, SHA-256 `55bf43e2f0dfce5ea6c01ac86b7b4885567b610e7cfa4057a66a51d0386ba9b1`, material offset `14`, length `79`.

| Attachment | Left offset | Right offset | Bytes | Changed px | Mean abs | Max abs |
|---|---:|---:|---:|---:|---:|---:|
| `albedo` | 0 | 0 | 230400 | 1551 | 3.062956 | 194 |
| `normal` | 230400 | 230400 | 230400 | 0 | 0.000000 | 0 |
| `position` | 460800 | 460800 | 460800 | 0 | 0.000000 | 0 |
| `object_id` | 921600 | 921600 | 230400 | 0 | 0.000000 | 0 |
| `depth` | 1152000 | 1152000 | 230400 | 0 | 0.000000 | 0 |
| `final` | 1382400 | 1382400 | 230400 | 1551 | 1.597943 | 107 |

Changed albedo bounds: `x=83, y=77, w=47, h=33` (1551 pixels).

## Raw pixel probes

| Probe | XY | Left offset | Right offset | Left bytes | Right bytes | Changed |
|---|---:|---:|---:|---|---|---|
| `first_visible_albedo` | 83,77 | 98892 | 98892 | `e83d2947` | `3d8ceb52` | yes |
| `first_visible_normal` | 83,77 | 329292 | 329292 | `0980af00` | `0980af00` | no |
| `first_visible_position` | 83,77 | 658584 | 658584 | `13bb1cb106ae003c` | `13bb1cb106ae003c` | no |
| `first_visible_object_id` | 83,77 | 1020492 | 1020492 | `01000000` | `01000000` | no |
| `first_visible_depth` | 83,77 | 1250892 | 1250892 | `8f20a83e` | `8f20a83e` | no |
| `first_visible_final` | 83,77 | 1481292 | 1481292 | `dc8d8fff` | `85b9eaff` | yes |
| `bbox_center_nearest_albedo` | 106,93 | 119464 | 119464 | `e83d2947` | `3d8ceb52` | yes |
| `bbox_center_nearest_normal` | 106,93 | 349864 | 349864 | `af80f600` | `af80f600` | no |
| `bbox_center_nearest_position` | 106,93 | 699728 | 699728 | `f2b8b9293a33003c` | `f2b8b9293a33003c` | no |
| `bbox_center_nearest_object_id` | 106,93 | 1041064 | 1041064 | `01000000` | `01000000` | no |
| `bbox_center_nearest_depth` | 106,93 | 1271464 | 1271464 | `1562933e` | `1562933e` | no |
| `bbox_center_nearest_final` | 106,93 | 1501864 | 1501864 | `c26a58ff` | `6a9ac3ff` | yes |

## Acceptance rule

This record is accepted only when the original material hash, both capture manifests, both raw view hashes, and every offset in the tables above still match the exported `SHA256SUMS`.
