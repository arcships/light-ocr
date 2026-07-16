# Changelog

This file records user-visible changes to `light-ocr`. Published artifact details and immutable hashes remain in [`docs/releases/`](docs/releases/).

## [Unreleased]

### Added

- Added an opt-in Direct Core ML provider for the `0.2.1` source candidate. Apple Silicon routes FP16 detection and shorter recognition shapes through the Neural Engine envelope, with wider recognition shapes on the GPU.
- Added experimental macOS 15+ compatibility for both `arm64` and `x86_64`. Intel Macs use Core ML CPU+GPU because they do not have an Apple Neural Engine; the strict GPU-only profile remains Apple-Silicon-only.
- Added per-provider and per-session execution diagnostics, including configured provider chain, device family, operating system, precision, model/cache identity, stable fallback reason, and `deviceValidated` evidence status.
- Added a self-contained Apple model bundle, deterministic Core ML derivation, offline compiled-model cache, cross-process cache locking, bounded recognition-function caching, and explicit whole-session CPU fallback.

### Performance

- Qualified the FP16 mixed Core ML path on one Apple M4 Max (16-core CPU, 128 GB RAM, macOS 26.5.1) against the same-machine `cpu_fast` profile, which uses up to 12 intra-op threads. Each locked workload used 5 warm-ups and 3 independent sets of 30 measured runs:

  | Locked workload | CPU warm P50 | Apple warm P50 | Speedup | OCR process CPU-time reduction |
  | --- | ---: | ---: | ---: | ---: |
  | `generated-hello-123` | 19.774 ms | 8.599 ms | 2.300× | 95.91% |
  | `paddleocr-xfund-form` | 943.627 ms | 331.011 ms | 2.851× | 97.67% |

- Passed all 14 locked quality fixtures with 99.6484% character similarity to the CPU oracle, 100% detection recall, 99.5508% mean matched IoU, a 0.004349 mean matched confidence difference, and zero critical failures. These are CPU-parity metrics rather than an independent ground-truth accuracy claim, and FP16 output is not byte-identical.
- Recorded a 692.14 MiB peak RSS across the formal warm performance runs and a 25.42 MiB Apple bundle increment. The fixed startup canary took 7.219 s on a first compiled-cache miss and 1.275/1.278 s on hits; the 113-line form's first full page took 53.846 s on a miss and 12.677/12.677 s on hits because first use compiles offline and loads recognition functions on demand.
- Passed the four-process empty-cache race and the same-engine 100-page lifecycle gate. The lifecycle run peaked at 888.11 MiB and finished 27.47 MiB below its post-warm-up baseline, with no sustained resident growth in that run.

### Compatibility and evidence

- CPU remains the default provider. Apple acceleration requires an explicit `provider: "apple"` request and can use `sessionFallback: "cpu"` for a visible whole-session fallback.
- Production bundles use `devicePolicy: "open-macos"`: M1–M3, later Apple Silicon, and Intel Macs are not blocked by the current evidence list.
- Real-device performance data currently comes from one Apple M4 Max runner. The evidence contract classifies it under the `Apple M4` device family for `deviceValidated`; this is not a claim that every M4 SKU was measured separately. Other Macs report `deviceValidated: false`; experimental compatibility is available, but no performance number is promised until that hardware family is reviewed.
- Heavy model conversion, Compute Plan placement, performance, cache, and lifecycle qualification remain local real-device work. Ordinary CI stays limited to cross-platform builds, contracts, and lightweight tests and does not require paid runners.
- The Core ML provider is merged on `main` but is not included in the published `0.2.0` npm packages. The planned `0.2.1` distribution keeps the existing six-package installation shape.

Full evidence and methodology: [Apple device acceleration](docs/apple-device-acceleration.md), [implementation status](docs/implementation-status.md), and accepted baseline [`apple-fp16-mixed-20260715.2`](contracts/apple-provider-baselines.json).

## [0.2.0] - 2026-07-14

- Added opt-in deterministic `tiled-v1` detection for dense and high-resolution images.
- Added bounded in-memory JPEG/PNG decoding through Node.js `recognizeEncoded()`.
- Published the six-package npm release for Node.js 22/24 on macOS arm64/x64, Linux x64 glibc, and Windows x64.

See the immutable [npm 0.2.0 release record](docs/releases/npm-0.2.0.md).

## [0.1.0] - 2026-07-14

- Published the first PP-OCRv6 Small native and Node.js release with offline model installation, raw-pixel recognition, prebuilt Tier 1 native packages, and no runtime Python process.

See the immutable [npm 0.1.0 release record](docs/releases/npm-0.1.0.md).
