# npm 0.5.7 Release Record

[中文版](npm-0.5.7.md)

Status: pre-release validation complete. No candidate version has been written
to the npm registry; the current stable Small/runtime/native closure remains at
`0.5.6`. The six-platform rehearsal and manifest audit are green, so the
release may proceed.

Release identity:

- Implementation and release-gate hardening:
  [PR #59](https://github.com/arcships/light-ocr/pull/59)
- Merged `main` commit:
  [`1a26d53`](https://github.com/arcships/light-ocr/commit/1a26d53b97c4260bce2af6badeebd9cb3fe48484)
- Final `main` validation:
  [Core 30986015180](https://github.com/arcships/light-ocr/actions/runs/30986015180)
  and
  [Native WebGPU 30986015218](https://github.com/arcships/light-ocr/actions/runs/30986015218)
- Six-platform release rehearsal:
  [30986812237](https://github.com/arcships/light-ocr/actions/runs/30986812237)
  (`publish_to_registry=false`)
- Registry publication and reinstall verification: pending
- Stable dist-tag promotion: pending
- GitHub Release: pending

## User-visible changes

- Fix native loading after Electron, notarization, or another downstream macOS
  packaging pipeline re-signs the payload and thereby changes its size and
  SHA-256. `light_ocr_node.node` and the ONNX Runtime dylib may now be re-signed
  with the same Developer ID as the host application.
- Keep exact descriptor bytes/SHA-256 matching as the primary integrity gate.
  Only when that gate diverges on macOS does the loader check the Mach-O magic,
  run `codesign --verify --strict`, and require the artifact TeamIdentifier to
  equal the host process TeamIdentifier. A both-ad-hoc host/artifact pair is
  also accepted.
- Continue to return `package_load_failed` for different TeamIdentifiers,
  unsigned mutations, non-Mach-O files, malformed descriptors, and every
  descriptor mismatch on Linux or Windows. Native inventory and the post-load
  ABI contract remain unchanged.
- Require no Electron configuration, environment variable, postinstall hook,
  certificate keychain, or added dependency. `codesign` is used only for
  verification. Because anyone can reproduce an ad-hoc signature, that branch
  is explicitly limited to macOS cases where both the host and artifact are
  ad-hoc signed.
- Preserve every public API and schema, including image, PDF, multi-page, CLI,
  and provider behavior.

## Candidate version closure

| Maturity | Package | Candidate version | Target tags |
| --- | --- | ---: | --- |
| stable | `@arcships/light-ocr` | `0.5.7` | `next`, then `latest` after verification |
| stable | `@arcships/light-ocr-runtime` | `0.1.7` | `next`, then `latest` after verification |
| stable | six native platform packages | `0.5.7` | `next`, then `latest` after verification |
| compatibility | `@arcships/light-ocr-document` | `0.1.3` | `next` |
| preview | `@arcships/light-ocr-tiny` | `0.1.6` | `next` |
| preview | `@arcships/light-ocr-medium` | `0.1.6` | `next` |

No model receives a new version. Small remains at `0.3.4`, while Tiny and
Medium remain at `0.1.0`. The publication workflow rebuilds or retrieves these
immutable model tarballs for complete offline installation testing and reuses
a registry version only after confirming identical integrity.

## Six-platform native candidates

The following values come from the rehearsal's `release-manifest.json`. Each
delta uses the published `0.5.6` tarball as its baseline. A customer installs
only the one native package selected for the current platform.

| Platform package | Compressed bytes | Delta from 0.5.6 | Unpacked bytes | SHA-256 |
| --- | ---: | ---: | ---: | --- |
| `darwin-arm64` | 22,889,540 | -2 | 56,356,978 | `55f5539456e8da241d8101c7ca5f015b9ab130d77758b5a99552f3b5467c9554` |
| `darwin-x64` | 24,853,682 | +1 | 62,109,821 | `18975175b52da136d00e740f3a55ba4aab582732a80390b6c21f5b7053f0e51e` |
| `linux-arm64-gnu` | 20,453,508 | +23 | 40,261,084 | `ffb45dbff2f585010dade1978465b7e5b521c4da01db4dbc955cb20b27d889b7` |
| `linux-x64-gnu` | 27,033,573 | -18 | 59,390,897 | `a3bb20608d785a054b34c90a579f695a72648dae5db91e321167648b97c2e2c4` |
| `win32-arm64` | 16,661,617 | -2,136 | 30,977,284 | `7bbb501cfef7909ec319218c57fd5ed732aa619260ddbf5c39649c7005e3dc3c` |
| `win32-x64` | 30,386,121 | -1,022 | 63,065,652 | `84804a4fa8db6657361d59a2645c6140b00e48de2063a5dab182156ee4fb2d0e` |

## Signing-policy verification

- PR CI runs real `codesign` integration tests under the default Node host on
  a GitHub macOS runner, covering pristine artifacts, unsigned mutations,
  non-Mach-O payloads, different signers, and malformed descriptor records.
- The release workflow copies and ad-hoc signs setup-node's Node executable,
  then runs the same tests again to deterministically exercise the positive
  both-ad-hoc host/artifact path.
- A manual Developer ID signed-host check used TeamIdentifier `3AA79YWT4C`:
  the pristine artifact passed, an unsigned mutation was rejected, and an
  artifact re-signed ad-hoc was rejected as a different signer. If no matching
  Developer ID is present in the runner keychain, the same-team positive test
  explicitly skips instead of opening keychain UI or manufacturing evidence.

## Release gates

- [x] PR Linux native build and workspace Node tests
- [x] Post-merge six-platform Core, sanitizers, fuzzer, and oracle
- [x] Linux and Windows Native WebGPU contracts
- [x] Real signing-policy tests under the default macOS Node host
- [x] Positive both-ad-hoc test under an ad-hoc re-signed Node host
- [x] Developer ID signed-host pristine/tamper/different-signer manual checks
- [x] Malformed bytes/SHA-256 descriptor rejected before signature fallback
- [x] npm registry availability for all 11 new package identities
- [x] Native and PDFium addon rebuilt from pinned sources on all six platforms
- [x] Complete closure installed offline with installation scripts disabled on
      all six platforms
- [x] Real image OCR and non-embedded Chinese-font PDF OCR on all six platforms
- [x] Candidate manifest, byte counts, SHA-256 values, and npm integrity audited
- [ ] Immutable candidates published to `next` with
      `publish_to_registry=true`
- [ ] Registry reinstall, integrity, and network-disabled runtime verification
- [ ] Stable Small/runtime/native closure promoted to `latest`
- [ ] `v0.5.7` GitHub Release created

## Publication and rollback sequence

1. Run the `npm release` rehearsal only from the `main` SHA that passed final
   validation. Download `light-ocr-npm-0.5.7` and audit package count,
   versions, bytes, SHA-256 values, and npm integrity in the manifest.
2. After merging this record, rerun the same six-platform workflow with
   `publish_to_registry=true`. It publishes new package identities only to
   `next`, then reinstalls from the registry and executes offline image/PDF
   smoke tests.
3. Only after the publication run succeeds, promote Small `0.5.7`, runtime
   `0.1.7`, and the six native `0.5.7` packages to `latest` from that run's
   immutable artifact. Document, Tiny, and Medium are not part of stable
   promotion and remain on `next`.
4. If a blocking issue appears after promotion, do not overwrite or delete an
   immutable version. Use the archived `0.5.6` release artifact from run
   `30599969242` to restore the stable tags to Small/native `0.5.6` and runtime
   `0.1.6`.

## GitHub Release draft

Release name:

`同印随舟，重签安渡 · Support re-signed macOS artifacts in 0.5.7`

Highlights:

- Allow downstream macOS packaging pipelines to re-sign the native addon and
  ONNX Runtime dylib with the same Developer ID as the host application.
- Keep descriptor bytes/SHA-256 as the primary integrity gate and use strict
  code-signature equivalence only for a mismatched macOS Mach-O.
- Accept identical TeamIdentifier signatures or the explicitly documented
  both-ad-hoc development case; reject different identities, unsigned
  mutations, non-Mach-O payloads, and malformed descriptors.
- Require no Electron configuration, postinstall hook, keychain access, or new
  dependency, while preserving every public API and non-macOS behavior.

Versions:

- `@arcships/light-ocr@0.5.7`
- `@arcships/light-ocr-runtime@0.1.7`
- six native platform packages at `0.5.7`
- Document compatibility facade `0.1.3` under `next`
- Tiny/Medium preview facades `0.1.6` under `next`
- unchanged Small `0.3.4` and Tiny/Medium `0.1.0` model packages

Verification:

- implementation and PR checks:
  <https://github.com/arcships/light-ocr/pull/59>
- final Core:
  <https://github.com/arcships/light-ocr/actions/runs/30986015180>
- final Native WebGPU:
  <https://github.com/arcships/light-ocr/actions/runs/30986015218>
- pre-release dry-run:
  <https://github.com/arcships/light-ocr/actions/runs/30986812237>
- release workflow: pending
- stable promotion: pending
