# VFC Replay correction record

This bundle is self-contained. It does not require the workstation session.

- Record: `record.json`
- Screenshot: `frame.ppm`
- Record SHA-256: `4e12a122265f828fd24ea660e2850015bcb83d7c9b80549cecd7f327008ee536`
- Evidence head: `fac91f9c97bd1f2d4e5c34283ef148c4a2d0e3f1ed72087a4bff45295b970938`
- Export scope: `SELECTED_OBJECT OBJ-112`
- Clock offset B-A: `420 ms`
- Missing fragment A: `[17, 21.4] s`

## Independent review

```sh
vfcr-verify record.json
```

The verifier checks field order, run order, frustum visibility, per-run input hashes, the evidence chain, and the record SHA-256. Recompute the screenshot digest with `sha256sum frame.ppm` when available.
