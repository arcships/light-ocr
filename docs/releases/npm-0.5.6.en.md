# npm 0.5.6 Release Record

[中文版](npm-0.5.6.md)

Status: published on 2026-07-31. The stable Small/runtime/native closure is on
both `latest` and `next`; the Document compatibility facade and Tiny/Medium
preview facades remain on `next`.

Release identity:

- PDF fallback implementation:
  [`196ebde`](https://github.com/arcships/light-ocr/commit/196ebdee7c3adead047b485e6a558133bd0263c5)
- Windows release builder fix:
  [`d80a868`](https://github.com/arcships/light-ocr/commit/d80a8681485c470b48699e24fa18661d6a60b718)
- Post-merge baselines:
  [Core 30534418143](https://github.com/arcships/light-ocr/actions/runs/30534418143) and
  [Native WebGPU 30534418001](https://github.com/arcships/light-ocr/actions/runs/30534418001)
- Initial diagnostic rehearsal:
  [30535427156](https://github.com/arcships/light-ocr/actions/runs/30535427156)
  (macOS and Linux passed; Windows exposed the `npx.cmd` resolution issue;
  assembly and publication did not run)
- Complete release rehearsal:
  [30535822947](https://github.com/arcships/light-ocr/actions/runs/30535822947)
  (all six platforms passed with `publish_to_registry=false`)
- Final `main` validation:
  [Core 30599338979](https://github.com/arcships/light-ocr/actions/runs/30599338979)
  and
  [Native WebGPU 30599339015](https://github.com/arcships/light-ocr/actions/runs/30599339015)
- Registry publication and reinstall verification:
  [30599969242](https://github.com/arcships/light-ocr/actions/runs/30599969242)
- Stable dist-tag promotion:
  [30600575756](https://github.com/arcships/light-ocr/actions/runs/30600575756)
- GitHub Release:
  [`v0.5.6`](https://github.com/arcships/light-ocr/releases/tag/v0.5.6)

## User-visible changes

- Fix PDF rasterization for common documents that reference non-embedded
  Chinese fonts. PDFium now renders those pages with the Noto Sans SC fallback
  font carried by the current native npm package before passing the resulting
  pixels to OCR.
- Ship the font, its OFL license, the PDFium addon, and the matching shared
  library in each platform package. The supported installation works with
  `npm install --ignore-scripts`; customer installation and runtime perform no
  font download or native compilation and do not require a system Chinese
  font.
- Pin `NotoSansSC-Regular.otf` at `8,331,336` bytes with SHA-256
  `faa6c9df652116dde789d351359f3d7e5d2285a2b2a1f04a2d7244df706d5ea9`.
- Preserve the existing public API and schema. The image, PDF, and multi-page
  entry points in `@arcships/light-ocr` are unchanged.
- Keep the immutable `0.5.5` packages available, but do not recommend them for
  workloads that require reliable rendering of PDFs with non-embedded Chinese
  fonts.

## Published version closure

| Maturity | Package | Published version | Final tags |
| --- | --- | ---: | --- |
| stable | `@arcships/light-ocr` | `0.5.6` | `latest`, `next` |
| stable | `@arcships/light-ocr-runtime` | `0.1.6` | `latest`, `next` |
| stable | six native platform packages | `0.5.6` | `latest`, `next` |
| compatibility | `@arcships/light-ocr-document` | `0.1.2` | `next` |
| preview | `@arcships/light-ocr-tiny` | `0.1.5` | `next` |
| preview | `@arcships/light-ocr-medium` | `0.1.5` | `next` |

No model received a new version. Small continues to use `0.3.4`, while Tiny
and Medium continue to use `0.1.0`. The publication workflow rebuilt or
retrieved model tarballs for offline installation testing and reused an
immutable registry version only after confirming identical integrity.

## Six-platform native artifacts

The following values come from the complete rehearsal's
`release-manifest.json`. Each delta uses the published `0.5.5` tarball as its
baseline. A customer installs only the one native package selected for the
current platform.

| Platform package | Compressed bytes | Delta from 0.5.5 | Unpacked bytes | SHA-256 |
| --- | ---: | ---: | ---: | --- |
| `darwin-arm64` | 22,889,542 | +7,227,406 | 56,356,978 | `ae325aaf9a3ff79cd184a862ea36687fa32227bf5a048136f02591626ce16342` |
| `darwin-x64` | 24,853,681 | +7,229,542 | 62,109,821 | `4f3d2f9cb6fba776181e327e7321787d91b516c0a39497d8ebf2ad56bf6112cc` |
| `linux-arm64-gnu` | 20,453,485 | +7,227,462 | 40,261,084 | `6dfbedc1637ff17aa13117925ca08f80b07367d62216f90a05b7614bff038078` |
| `linux-x64-gnu` | 27,033,591 | +7,226,822 | 59,390,897 | `eb67edb08db86998c8b232c42df90eda47c0b1a30311f78e1f7f5023f0c22a15` |
| `win32-arm64` | 16,663,753 | +7,232,623 | 30,985,476 | `0ad4e90cadd230405c244a309f9dbaac33b371ae9e6a4868d7eaf31c5962723d` |
| `win32-x64` | 30,387,143 | +7,232,239 | 63,070,260 | `bcbd5b92526f0a640db53154f6482ee0c58a4a9d3c559c5ecac0611f2b21a2dd` |

## Release gates

- [x] Post-merge six-platform Core, sanitizers, fuzzer, and oracle
- [x] Linux and Windows Native WebGPU contracts
- [x] npm registry availability for all 11 new package identities
- [x] PDFium addon rebuilt from the pinned source on all six platforms
- [x] Fallback font and OFL license present in all six native packages
- [x] Complete closure installed on all six platforms in offline mode with
      installation scripts disabled
- [x] Real image OCR on all six platforms
- [x] Non-embedded `STSong-Light` mapped to `Noto Sans SC` on all six platforms
- [x] Non-empty PDF raster and end-to-end `中文测试` OCR on all six platforms
- [x] Candidate tarball manifest, byte counts, SHA-256 values, and npm integrity
- [x] Publish the immutable candidate to `next` with
      `publish_to_registry=true`
- [x] Reinstall from the npm registry and verify integrity
- [x] Promote the stable Small/runtime/native closure to `latest`
- [x] Create the `v0.5.6` GitHub Release

## Publication result

1. The final `main` SHA passed the complete Core and Native WebGPU workflows.
2. [Release run 30599969242](https://github.com/arcships/light-ocr/actions/runs/30599969242)
   repeated the six-platform build and offline smoke suite, published the
   immutable package set to `next`, then reinstalled it from the npm registry
   and verified integrity, image OCR, PDF OCR, and network-disabled operation.
3. [Promotion run 30600575756](https://github.com/arcships/light-ocr/actions/runs/30600575756)
   moved only Small `0.5.6`, runtime `0.1.6`, and the six native `0.5.6`
   packages to `latest`.
4. The [`v0.5.6` GitHub Release](https://github.com/arcships/light-ocr/releases/tag/v0.5.6)
   records the public release.

Tiny, Medium, and Document are not part of stable promotion and remain on
`next`. No external user validation or adoption evidence blocks this release.
Every blocking gate is satisfied by the source, candidate tarballs, real
six-platform runners, and npm registry identity.

## GitHub Release

Release name:

`墨字归真，六境同明 · Restore Chinese PDF rendering in 0.5.6`

Highlights:

- Fix PDF rasterization when documents reference common non-embedded Chinese
  fonts.
- Bundle a checksum-pinned Noto Sans SC fallback font and its OFL license in
  every supported native package.
- Keep installation and runtime fully self-contained: no postinstall download,
  system-font requirement, GitHub access, or local compilation.
- Preserve the existing image, PDF, and multi-page APIs without schema changes.
- Add about 7.23 MB compressed and 8.34 MB unpacked to the one native package
  selected for the customer's platform.

Versions:

- `@arcships/light-ocr@0.5.6`
- `@arcships/light-ocr-runtime@0.1.6`
- six native platform packages at `0.5.6`
- Document compatibility facade `0.1.2` under `next`
- Tiny/Medium preview facades `0.1.5` under `next`
- unchanged Small `0.3.4` and Tiny/Medium `0.1.0` model packages

Verification:

- pre-release dry-run:
  <https://github.com/arcships/light-ocr/actions/runs/30535822947>
- release workflow:
  <https://github.com/arcships/light-ocr/actions/runs/30599969242>
- stable promotion:
  <https://github.com/arcships/light-ocr/actions/runs/30600575756>
