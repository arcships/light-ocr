# Changelog

This file records user-visible changes to `light-ocr`. Published artifact details and immutable hashes remain in [`docs/releases/`](docs/releases/).

## [Unreleased]

### Added

- Added PP-OCRv6 Tiny and Medium preview facades with the same Node API, TypeScript types, CLI schema, and `OcrError` identity as the stable Small package. Their commands are `light-ocr-tiny` and `light-ocr-medium`; only Small owns `light-ocr`.
- Added exact, hash-locked Tiny and Medium model bundles. Tiny discloses its 49-language coverage and missing Japanese support through README, `modelProfile`, and CLI info.
- Added the private `0.1.1` server preview to the N2 composition, exact-pinned to Small `0.4.0`.

### Changed

- Cut Node distribution over to the model-free `@arcships/light-ocr-runtime`; native loading, engine lifecycle, CLI parsing, EXIF handling, types, and facade bundle resolution now have one source under `packages/`.
- Moved the Core version authority to `VERSION`, allowing a version-only change to use quick workspace CI instead of triggering the complete native matrix.
- Changed npm releases to independent package versions. Small `0.4.0` reuses the immutable Small model `0.3.4`; runtime and preview packages begin at `0.1.0`.
- Reduced release work to six required native builds, one Tiny/Medium model assembly, one npm pack per package, and focused installed OCR smoke. Core, sanitizer, fuzz, provider qualification, and model conversion no longer rerun inside release.
- Kept Tiny and Medium on `next`; promotion moves only the native/runtime/Small stable closure.

Candidate details: [npm 0.4.0 N2 record](docs/releases/npm-0.4.0.md).

## [0.3.4] - 2026-07-22

### Fixed

- Fixed the native Node-API option contract so the documented `region` option reaches the existing ROI implementation for both `recognize` and `detect`, including valid zero-based `x` and `y` coordinates.
- Extended the existing package smoke to execute a real `light-ocr recognize --region` command, covering CLI argument parsing, encoded-image decoding, native option admission, model inference, and text output in one focused check.

## [0.3.3] - 2026-07-22

### Fixed

- Fixed npm facade assembly to include the declared `light-ocr` bin, its executable script, and the required Node shebang. The package smoke now installs and executes `node_modules/.bin/light-ocr info --version`, preventing a source-only CLI from passing release validation again.
- Changed the reserved `detect --crop` flag from a silent no-op to a fail-closed `unsupported_capability` response until crop bytes are implemented.

### Changed

- Split publication and promotion into single-purpose workflows. `npm release` now rejects an already-published facade version before starting the six-platform build; `npm promote` alone may update `latest` from the original immutable release artifact.
- Replaced the ten-job pull-request fan-out with one representative Linux Core/Node/offline gate; the full six-platform, sanitizer, oracle, and WebGPU suites now run once after changes reach `main`.
- Removed release work that produced no additional evidence: live oracle setup now runs only on Linux x64 where its acceptance tests execute, TypeScript declarations compile once, every platform installs on Node 22 while Node 24 compatibility is checked once on Linux x64, tiled OCR runs once, and registry checks no longer repeat the same tiled contract.

### Notes

- npm `0.3.2` is immutable and its facade tarball omitted the CLI bin even though the source and changelog contained it. `0.3.3` is the corrective patch release; the `0.3.2` artifact remains recorded for provenance.

## [0.3.2] - 2026-07-22

### Added

- Added the `light-ocr` CLI bin with three subcommands: `recognize` (default OCR), `detect` (detection-only, no recognition), and `info` (engine/version diagnostics). The CLI is pure JavaScript (CJS, zero-dependency) and ships as a `bin` entry in the facade package.
- Added a versioned `DocumentResult` envelope (`schemaVersion: 1`) wrapping `OcrResult` with stable line IDs (`L0`, `L1`, …) and detection IDs (`D0`, `D1`, …) for future Layout region association.
- Added `--schema-version 1` to request an exact output schema; unsupported versions return `invalid_argument`.
- Added `--region x,y,w,h` ROI support: pageSpace axis-aligned rectangle, adapter-layer pixel crop after EXIF correction, box coordinates offset back to full pageSpace. Out-of-bounds or partially-intersecting regions return `invalid_argument` without clamping.
- Added EXIF orientation correction for JPEG images: a self-contained C++ EXIF parser reads the orientation tag and applies the pixel transform before recognition. `--no-exif` disables correction; `appliedTransforms` records the applied state.
- Added `detect` subcommand support in Core: `Engine::detect()` reuses the detection stage and skips recognition. `DetectionResult` carries boxes (Quad + score), image dimensions, model bundle ID, and timing.
- Added `applyExif` and `region` options to `RecognizeOptions` (Node API + TypeScript types).
- Added an Agent Skill at `.agents/skills/local-ocr/SKILL.md` with scenario-driven workflows, decision flow, and exit code reference.
- Added stable exit codes (64–72) mapped to `OcrErrorCode`, with flag validation ordered before input reading so parameter errors surface first.
- Added a Windows delay-load hook (`win_delay_load_hook.cpp`) so the native addon loads under Electron and other renamed Node-API hosts, not just `node.exe`.

### Changed

- The `detect` subcommand does not expose `--format`; its output is always structured JSON with `structure: "detect"` and `detections[]` instead of `lines[]`.
- `info --model-info` and `info --version` are mutually exclusive (version triple is a subset of EngineInfo).
- CLI flag surface restricted to Agent/user-facing options; provider-internal fields (`sessionFallback`, `cpuPartition`, `precision`, `detectionStrategy`, `maxSide`, `includeDiagnostics`) are not exposed as CLI flags.

### Notes

- This is the first release with the N1 CLI entry point (roadmap §5). The CLI, EXIF, ROI, and detect features are newly implemented; Agent task eval and npm install smoke validation follow per roadmap §5.7.

## [0.3.1] - 2026-07-21

### Added

- Added CPU-only prebuilt native runtimes for Linux arm64 (glibc) and Windows arm64, expanding the npm package set from four to six platform builds. Both new packages use the same PP-OCRv6 Small model and CPU result contract as the existing platforms.
- Added `@arcships/light-ocr-linux-arm64-gnu` and `@arcships/light-ocr-win32-arm64` as optional dependencies of the facade package, selected automatically by the Node.js loader on matching hosts.
- Added `ubuntu-24.04-arm` and `windows-11-arm` GitHub-hosted runners to the core CI and the npm release pipeline so both new builds compile, test, and pack on real arm64 hardware.

### Changed

- Extended the CMake ONNX Runtime CPU staging to select `runtimes/linux-arm64/native` and `runtimes/win-arm64/native` from the pinned NuGet package based on `CMAKE_SYSTEM_PROCESSOR`, instead of assuming x86_64.
- Made the npm release package-count invariant dynamic so adding platforms no longer requires editing a hard-coded count.

### Notes

- WebGPU acceleration remains limited to Linux x64 and Windows x64. The official ONNX Runtime WebGPU Plugin EP 0.1.0 does not ship a Linux arm64 binary, and Windows arm64 WebGPU has not passed the real-device Provider Gate required for a production-qualified release. Both arm64 packages are CPU-only in this version.

## [0.3.0] - 2026-07-19

### Added

- Added an opt-in Direct Core ML provider. Apple Silicon routes FP16 detection and shorter recognition shapes through the Neural Engine envelope, with wider recognition shapes on the GPU.
- Added experimental macOS 15+ Core ML compatibility on `arm64`. The macOS x64 package remains CPU-only after its release smoke test did not reproduce the locked OCR result through Core ML.
- Added per-provider and per-session execution diagnostics, including configured provider chain, device family, operating system, precision, model/cache identity, qualification identity, a structured Auto creation trace, and `deviceValidated` evidence status.
- Added a self-contained Apple model bundle, deterministic Core ML derivation, offline compiled-model cache, cross-process cache locking, bounded recognition-function caching, and descriptor-driven platform Auto selection.
- Added self-contained Native WebGPU execution on Linux x64 glibc/Vulkan and Windows x64/D3D12 with ONNX Runtime 1.24.4, the official WebGPU Plugin EP 0.1.0, hash-verified runtime descriptors, offline staging, and CPU as the final Auto candidate.
- Added an FP32 WebGPU product profile with an explicit bounded CPU partition for `Concat`, `Gather`, and `Slice`. `cpuPartition: "forbid"` fails closed before session creation.

### Changed

- Changed the default execution provider from CPU to descriptor-driven Auto. Explicit providers remain strict single-backend requests.
- Added `auto` and `webgpu` to the Node.js `ExecutionProvider` union and `automatic`/`webgpu` to the C++ enum.
- Added structured creation traces to `EngineInfo.execution` and creation errors. The legacy `sessionFallback: "cpu"` value now returns `invalid_argument`; only Auto can advance to another provider during engine creation.
- Reserved WebGPU FP16 derivations as internal locked artifacts. The public `0.3.0` WebGPU API accepts only `precision: "auto" | "fp32"`; `fp16` remains available for Apple/Core ML.

### Performance

- Qualified the FP16 mixed Core ML path on one Apple M4 Max (16-core CPU, 128 GB RAM, macOS 26.5.1) against the same-machine `cpu_fast` profile, which uses up to 12 intra-op threads. Each locked workload used 5 warm-ups and 3 independent sets of 30 measured runs:

  | Locked workload | CPU warm P50 | Apple warm P50 | Speedup | OCR process CPU-time reduction |
  | --- | ---: | ---: | ---: | ---: |
  | `generated-hello-123` | 19.774 ms | 8.599 ms | 2.300× | 95.91% |
  | `paddleocr-xfund-form` | 943.627 ms | 331.011 ms | 2.851× | 97.67% |

- Passed all 14 locked quality fixtures with 99.6484% character similarity to the CPU oracle, 100% detection recall, 99.5508% mean matched IoU, a 0.004349 mean matched confidence difference, and zero critical failures. These are CPU-parity metrics rather than an independent ground-truth accuracy claim, and FP16 output is not byte-identical.
- Recorded a 692.14 MiB peak RSS across the formal warm performance runs and a 25.42 MiB Apple bundle increment. The fixed startup canary took 7.219 s on a first compiled-cache miss and 1.275/1.278 s on hits; the 113-line form's first full page took 53.846 s on a miss and 12.677/12.677 s on hits because first use compiles offline and loads recognition functions on demand.
- Passed the four-process empty-cache race and the same-engine 100-page lifecycle gate. The lifecycle run peaked at 888.11 MiB and finished 27.47 MiB below its post-warm-up baseline, with no sustained resident growth in that run.
- Qualified WebGPU FP32 on the locked 14-fixture corpus with byte-identical OCR results against CPU FP32 and 164/164 Gates on each recorded platform:

  | Platform and recorded device | CPU P50 total | WebGPU P50 total | Aggregate speedup | Per-fixture range |
  | --- | ---: | ---: | ---: | ---: |
  | Linux x64 / NVIDIA RTX 5060 Ti / Vulkan | 5,475.623 ms | 961.042 ms | **5.698×** | 3.474×–9.299× |
  | Windows x64 / AMD Radeon 780M / D3D12 | 6,500.853 ms | 2,669.160 ms | **2.436×** | 1.277×–2.982× |

- Passed WebGPU cold-start, native C++, memory, placement, strict rejection, and repeated-lifecycle Gates. The Windows warmup-aware lifecycle run finished 22.9 MiB below its post-warm-up baseline.

### Compatibility and evidence

- Version `0.3.0` defaults to descriptor-driven Auto selection. Explicit providers are strict single-backend requests, and the legacy `sessionFallback: "cpu"` value returns `invalid_argument`.
- Production bundles use `devicePolicy: "open-macos"` for Apple Silicon: M1–M3 and later Apple Silicon are not blocked by the current evidence list. The npm runtime descriptor does not expose Apple on macOS x64.
- Real-device performance data currently comes from one Apple M4 Max runner. The evidence contract classifies it under the `Apple M4` device family for `deviceValidated`; this is not a claim that every M4 SKU was measured separately. Other Macs report `deviceValidated: false`; experimental compatibility is available, but no performance number is promised until that hardware family is reviewed.
- Heavy model conversion, Compute Plan placement, performance, cache, and lifecycle qualification remain local real-device work. Ordinary CI stays limited to cross-platform builds, contracts, and lightweight tests and does not require paid runners.
- The macOS arm64 Core ML provider is included in the published `0.3.0` npm packages. The distribution keeps the existing six-package installation shape; macOS x64 remains CPU-only.
- Native WebGPU compatibility and performance are evidenced on the named NVIDIA/Linux and AMD/Windows systems. Other devices may use the open compatibility path but do not inherit these performance numbers.
- The Linux and Windows qualification reports both passed 164/164 mechanical Gates. Their reviewed report and artifact-set hashes are bound into the production runtime lock, so ordinary `0.3.0` release staging now accepts the exact qualified payloads.

Full evidence and methodology: [Apple device acceleration](docs/apple-device-acceleration.md), [Linux device acceleration](docs/linux-device-acceleration.md), [Windows device acceleration](docs/windows-device-acceleration.md), [implementation status](docs/implementation-status.md), the accepted Apple baseline [`apple-fp16-mixed-20260715.2`](contracts/apple-provider-baselines.json), and the checked-in WebGPU qualification reports.

See the immutable [npm 0.3.0 release record](docs/releases/npm-0.3.0.md).

## [0.2.0] - 2026-07-14

- Added opt-in deterministic `tiled-v1` detection for dense and high-resolution images.
- Added bounded in-memory JPEG/PNG decoding through Node.js `recognizeEncoded()`.
- Published the six-package npm release for Node.js 22/24 on macOS arm64/x64, Linux x64 glibc, and Windows x64.

See the immutable [npm 0.2.0 release record](docs/releases/npm-0.2.0.md).

## [0.1.0] - 2026-07-14

- Published the first PP-OCRv6 Small native and Node.js release with offline model installation, raw-pixel recognition, prebuilt Tier 1 native packages, and no runtime Python process.

See the immutable [npm 0.1.0 release record](docs/releases/npm-0.1.0.md).
