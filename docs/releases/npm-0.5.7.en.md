# npm 0.5.7 Release Record

[中文版](npm-0.5.7.md)

Status: published on 2026-08-05. The stable Small/runtime/native closure is on
both `latest` and `next`; the Document compatibility facade and Tiny/Medium
preview facades remain on `next`.

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
- Registry publication and reinstall verification:
  [30988312627](https://github.com/arcships/light-ocr/actions/runs/30988312627)
- Stable dist-tag promotion:
  [30989501173](https://github.com/arcships/light-ocr/actions/runs/30989501173)
- GitHub Release:
  [`v0.5.7`](https://github.com/arcships/light-ocr/releases/tag/v0.5.7)

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

## Published version closure

| Maturity | Package | Published version | Final tags |
| --- | --- | ---: | --- |
| stable | `@arcships/light-ocr` | `0.5.7` | `latest`, `next` |
| stable | `@arcships/light-ocr-runtime` | `0.1.7` | `latest`, `next` |
| stable | six native platform packages | `0.5.7` | `latest`, `next` |
| compatibility | `@arcships/light-ocr-document` | `0.1.3` | `next` |
| preview | `@arcships/light-ocr-tiny` | `0.1.6` | `next` |
| preview | `@arcships/light-ocr-medium` | `0.1.6` | `next` |

No model received a new version. Small remains at `0.3.4`, while Tiny and
Medium remain at `0.1.0`. The publication workflow rebuilt or retrieved these
immutable model tarballs for complete offline installation testing and reused
the registry versions only after confirming identical integrity.

## Six-platform native artifacts

The following values come from the formal publication run's
`release-manifest.json`, and each npm registry integrity was independently
matched. Each delta uses the published `0.5.6` tarball as its baseline. A
customer installs only the native package selected for the current platform.

| Platform package | Compressed bytes | Delta from 0.5.6 | Unpacked bytes | SHA-256 |
| --- | ---: | ---: | ---: | --- |
| `darwin-arm64` | 22,889,533 | -9 | 56,356,978 | `d8d0b30f1c30c989d8392d438be3b3e06c0b0f3c86a19bb125dfa6732857dad6` |
| `darwin-x64` | 24,853,690 | +9 | 62,109,821 | `4b69cbe6aebf44dd052f37fd7173b386b446ebff040e003278d08a9a6d9a77d6` |
| `linux-arm64-gnu` | 20,453,503 | +18 | 40,261,084 | `81d2f306c247aa21ef18ff881e33df419de8b27c71adf7acf46c50838dd29d7f` |
| `linux-x64-gnu` | 27,033,585 | -6 | 59,390,897 | `6f83bdfbce7500e56bb0c31001a55143e9874257dafe679dcaad406980b72750` |
| `win32-arm64` | 16,661,640 | -2,113 | 30,977,284 | `8c702941470a549ffda07cf0ba9e224824c39d5debf6046446018520c1602f01` |
| `win32-x64` | 30,386,117 | -1,026 | 63,065,652 | `aa2d792a2e476073b39c9d3fc8cdf225695c06b534ce85e640db10c664f25baa` |

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
- [x] Immutable candidates published to `next` with
      `publish_to_registry=true`
- [x] Registry reinstall, integrity, and network-disabled runtime verification
- [x] Stable Small/runtime/native closure promoted to `latest`
- [x] `v0.5.7` GitHub Release created

## Publication result and rollback

1. All 14 jobs in [rehearsal run 30986812237](https://github.com/arcships/light-ocr/actions/runs/30986812237)
   passed. The 13-package manifest passed source SHA, version, uniqueness,
   bytes, SHA-256, shasum, and npm integrity audits without writing to the
   registry.
2. [Release run 30988312627](https://github.com/arcships/light-ocr/actions/runs/30988312627)
   repeated the six-platform build and offline smoke suite, published the new
   package identities to `next`, then reinstalled from the registry and
   verified integrity, image/PDF OCR, and network-disabled operation.
3. [Promotion run 30989501173](https://github.com/arcships/light-ocr/actions/runs/30989501173)
   moved only Small `0.5.7`, runtime `0.1.7`, and the six native `0.5.7`
   packages to `latest`. Document, Tiny, and Medium remain on `next`.
4. The [`v0.5.7` GitHub Release](https://github.com/arcships/light-ocr/releases/tag/v0.5.7)
   targets the formal publication source commit `bff2243`.

For rollback, do not overwrite or delete an immutable version. Use the
archived `0.5.6` release artifact from run `30599969242` to restore the stable
tags to Small/native `0.5.6` and runtime `0.1.6`.

## GitHub Release

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
- release workflow:
  <https://github.com/arcships/light-ocr/actions/runs/30988312627>
- stable promotion:
  <https://github.com/arcships/light-ocr/actions/runs/30989501173>
- GitHub Release:
  <https://github.com/arcships/light-ocr/releases/tag/v0.5.7>
