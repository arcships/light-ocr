# Changelog

This file records user-visible changes to `light-ocr`. Published artifact details and immutable hashes remain in [`docs/releases/`](docs/releases/).

## [0.3.0] - Unreleased

### Added

- Added an opt-in Direct Core ML provider for the `0.3.0` source candidate. Apple Silicon routes FP16 detection and shorter recognition shapes through the Neural Engine envelope, with wider recognition shapes on the GPU.
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

- The current source candidate defaults to descriptor-driven Auto selection. Explicit providers are strict single-backend requests, and the legacy `sessionFallback: "cpu"` value returns `invalid_argument`.
- Production bundles use `devicePolicy: "open-macos"` for Apple Silicon: M1–M3 and later Apple Silicon are not blocked by the current evidence list. The npm runtime descriptor does not expose Apple on macOS x64.
- Real-device performance data currently comes from one Apple M4 Max runner. The evidence contract classifies it under the `Apple M4` device family for `deviceValidated`; this is not a claim that every M4 SKU was measured separately. Other Macs report `deviceValidated: false`; experimental compatibility is available, but no performance number is promised until that hardware family is reviewed.
- Heavy model conversion, Compute Plan placement, performance, cache, and lifecycle qualification remain local real-device work. Ordinary CI stays limited to cross-platform builds, contracts, and lightweight tests and does not require paid runners.
- The macOS arm64 Core ML provider is merged on `main` but is not included in the published `0.2.0` npm packages. The planned `0.3.0` distribution keeps the existing six-package installation shape.
- Native WebGPU compatibility and performance are evidenced on the named NVIDIA/Linux and AMD/Windows systems. Other devices may use the open compatibility path but do not inherit these performance numbers.
- The Linux and Windows qualification reports both passed 164/164 mechanical Gates. Their reviewed report and artifact-set hashes are bound into the production runtime lock, so ordinary `0.3.0` release staging now accepts the exact qualified payloads.

Full evidence and methodology: [Apple device acceleration](docs/apple-device-acceleration.md), [Linux device acceleration](docs/linux-device-acceleration.md), [Windows device acceleration](docs/windows-device-acceleration.md), [implementation status](docs/implementation-status.md), the accepted Apple baseline [`apple-fp16-mixed-20260715.2`](contracts/apple-provider-baselines.json), and the checked-in WebGPU qualification reports.

## [0.2.0] - 2026-07-14

- Added opt-in deterministic `tiled-v1` detection for dense and high-resolution images.
- Added bounded in-memory JPEG/PNG decoding through Node.js `recognizeEncoded()`.
- Published the six-package npm release for Node.js 22/24 on macOS arm64/x64, Linux x64 glibc, and Windows x64.

See the immutable [npm 0.2.0 release record](docs/releases/npm-0.2.0.md).

## [0.1.0] - 2026-07-14

- Published the first PP-OCRv6 Small native and Node.js release with offline model installation, raw-pixel recognition, prebuilt Tier 1 native packages, and no runtime Python process.

See the immutable [npm 0.1.0 release record](docs/releases/npm-0.1.0.md).
