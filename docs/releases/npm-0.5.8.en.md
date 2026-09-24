# npm 0.5.8 Release Record

[中文版](npm-0.5.8.md)

Status: published on 2026-09-24. The stable Small/runtime/native closure is on
both `latest` and `next`; the Document compatibility facade and Tiny/Medium
preview facades remain on `next`.

Release identity:

- musl platforms and relaxed engines implementation:
  [PR #63](https://github.com/arcships/light-ocr/pull/63)
  (including two independent review rounds)
- musl ONNX Runtime artifacts release:
  [`musl-runtime-1.22.0`](https://github.com/arcships/light-ocr/releases/tag/musl-runtime-1.22.0)
- Version sync commit:
  [`947ee35`](https://github.com/arcships/light-ocr/commit/947ee3518b521a1cf76ba5169ebf4e8ef2e1ee9d0)
- Eight-platform release rehearsal:
  [36012260612](https://github.com/arcships/light-ocr/actions/runs/36012260612)
  (`publish_to_registry=false`)
- Registry publication and reinstall verification:
  [36013862673](https://github.com/arcships/light-ocr/actions/runs/36013862673)
- Stable dist-tag promotion:
  [36018294655](https://github.com/arcships/light-ocr/actions/runs/36018294655)
- GitHub Release: [`v0.5.8`](https://github.com/arcships/light-ocr/releases/tag/v0.5.8)

## User-visible changes

- Added CPU-only prebuilt platform packages for musl Linux,
  `@arcships/light-ocr-linux-x64-musl` and
  `@arcships/light-ocr-linux-arm64-musl`, expanding the platform set from six
  to eight. Alpine hosts get a working engine from
  `npm install @arcships/light-ocr` directly and no longer need to switch to a
  glibc base image.
- Relaxed `engines.node` in every published package from the enumerated
  `^22.0.0 || ^24.0.0` range to the floor `>=22.0.0`. The addon uses the stable
  Node-API ABI, so newer Node major versions (including 26) load the same
  prebuilt binaries without a library release; this also removes the hard Yarn
  install failure on every new Current line. Tier 1 support stays on the 22/24
  LTS lines, expressed by the documented tier matrix and CI smoke, separate
  from the install-time engines floor.
- `@arcships/light-ocr-runtime` exports `platformIdentity()` (declared in the
  TypeScript definitions); facades and tooling resolve the native platform
  package through one glibc/musl-aware identity, and the bundled PDF renderer
  resolves its platform the same way.
- No public API or schema breaks; image, PDF, CLI, and provider behavior on
  glibc platforms is unchanged.

## musl supply chain

- ONNX Runtime 1.22.0 has no official musl prebuilt. This project builds the
  CPU-only shared library from the v1.22.0 source in an `alpine:3.22` container
  (the only patch guards the `execinfo.h` include in `stacktrace.cc` with
  `NDEBUG`; the `backtrace()` body is compiled out in Release anyway), packages
  it in the NuGet layout, and bundles the MIT LICENSE and
  ThirdPartyNotices.txt with the archive.
- Both musl archives are pinned by URL + bytes + SHA-256 in
  `models/deps.lock.json`, under the same supply-chain contract as the glibc
  NuGet package. `.github/workflows/onnxruntime-musl.yml` (workflow_dispatch)
  rebuilds them on demand and attaches them to the `musl-runtime-1.22.0`
  release.
- pdfium-native `0.6.1` ships official `linux-musl-x64/arm64` targets
  (SHA-256 pinned); the PDFium addon inside the musl platform packages is built
  from the same source as glibc platforms.
- The musl platforms are CPU-only: WebGPU/Dawn on musl would need its own
  qualification matrix and is out of scope here, matching the existing
  CPU-only policy for linux-arm64-gnu and windows-arm64.

## Published version closure

| Maturity | Package | Published version | Final tags |
| --- | --- | ---: | --- |
| stable | `@arcships/light-ocr` | `0.5.8` | `latest`, `next` |
| stable | `@arcships/light-ocr-runtime` | `0.1.8` | `latest`, `next` |
| stable | eight native platform packages | `0.5.8` | `latest`, `next` |
| compatibility | `@arcships/light-ocr-document` | `0.1.4` | `next` |
| preview | `@arcships/light-ocr-tiny` | `0.1.7` | `next` |
| preview | `@arcships/light-ocr-medium` | `0.1.7` | `next` |

The model packages are not republished: Small keeps `0.3.4` and Tiny/Medium
keep `0.1.0`. The release pipeline rebuilds or fetches these immutable model
tarballs for the full offline install verification and reuses them only after
the registry integrity matches exactly.

## Eight-platform native artifacts

The table below comes from the release candidate run's `release-manifest.json`
and was verified against npm registry integrity per package. Users install
only the one native package matching their platform.

| Platform package | Packed bytes | Unpacked bytes | SHA-256 |
| --- | ---: | ---: | --- |
| Platform package | Packed bytes | Unpacked bytes | SHA-256 |
| --- | ---: | ---: | --- |
| `darwin-arm64` | 22,889,689 | 56,358,450 | `b073519dd23d3898f47272e79caa6e795c1e8b4210776daf7f52ca78d2283d69` |
| `darwin-x64` | 24,854,005 | 62,111,293 | `77258f6e33cdd8060fa930b35b647dd277ca1a6f928c1fa93f9ae6447508eb43` |
| `linux-arm64-gnu` | 20,453,594 | 40,262,556 | `b1e640f2f22908075e1c639957b7217d5fb983f4770c62f25a4a9fb0b3a61773` |
| `linux-x64-gnu` | 27,033,567 | 59,390,887 | `925d2a2a8097ed2266ab24b285fb2e9dafc17e8912a70d416b39cfb4a0991e26` |
| `linux-arm64-musl` | 22,839,920 | 49,120,781 | `a46665a54fdeebbdbc03bff372873050ccf58a156c84a4e8aa73ab836b4458eb` |
| `linux-x64-musl` | 24,712,425 | 56,327,142 | `6bb8bef38acaf8095b3f1b42f1d8c5394ae086ecb0525ed8b54399ca7e3177fd` |
| `win32-arm64` | 16,669,552 | 31,027,950 | `9a317d2e93fded0def42e0a42e24a7d277e845f062bef5430e4b6bc9b1c9cda9` |
| `win32-x64` | 30,386,113 | 63,065,642 | `0faf1a65b4dd4a7ff965fd99c3c80b619ab93ae63a3154f4bafae7c2856800b5` |

## Release gates

- [x] PR Linux native build and workspace Node tests
- [x] Two independent review rounds (build/supply chain, JS/release pipeline,
      documentation consistency) with verified fixes
- [x] npm registry vacancy check for 15 new package identities
- [x] Six glibc platforms rebuilt from locked sources (native + PDFium addon)
- [x] Both musl platforms rebuilt inside alpine:3.22 from locked sources
      (deps.lock SHA-256 verification, `LIGHT_OCR_TARGET_LIBC=musl` toolchain
      gate, dumpmachine check)
- [x] Eight platforms installed offline with scripts disabled
- [x] Real image OCR and non-embedded CJK font PDF OCR on six glibc platforms
- [x] Real image OCR and built-in PDF OCR on both musl platforms in Alpine
- [x] Candidate tarball manifest, bytes, SHA-256, and npm integrity audit
- [x] Publish immutable candidates to `next` with `publish_to_registry=true`
- [x] Reinstall from the npm registry, verify integrity and offline operation
- [x] Promote the stable Small/runtime/native closure to `latest`
- [x] Create the `v0.5.8` GitHub Release

## Actual release results

1. The [rehearsal run 36012260612](https://github.com/arcships/light-ocr/actions/runs/36012260612)
   finished green across all jobs; the 15-package manifest audit passed and
   nothing was written to the registry.
2. The [publication run 36013862673](https://github.com/arcships/light-ocr/actions/runs/36013862673)
   repeated the eight-platform builds and offline smoke, published the new
   package identities to `next`, and reinstalled from the registry to verify
   integrity, image/PDF OCR, and offline operation. The first publish job
   attempt timed out because npm registry integrity propagation for some
   packages exceeded the 600-second wait; rerunning that single job skipped
   the already-published packages per the idempotent semantics and completed
   the remaining publications, ending green.
3. The [promotion run 36018294655](https://github.com/arcships/light-ocr/actions/runs/36018294655)
   advanced only Small `0.5.8`, runtime `0.1.8`, and the eight native
   `0.5.8` packages to `latest`. Document, Tiny, and Medium stay on `next`
   (the first musl versions carry `latest` automatically per npm rules,
   matching `next`).
4. The [`v0.5.8` GitHub Release](https://github.com/arcships/light-ocr/releases/tag/v0.5.8)
   binds the release source commit and records the public release.

To roll back, do not overwrite or delete published versions; use the archived
`0.5.7` release artifact (run `30988312627`) to restore the stable tags to
Small/native `0.5.7` and runtime `0.1.7`.

## GitHub Release

Release name:

`雪岭轻舟，musl 同渡 · Ship musl platform packages in 0.5.8`
