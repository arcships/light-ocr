# light-ocr Core Decisions

Status: Active decision record  
Authority: resolved architectural and product choices for the native Core milestone  
Requirements: [requirements.md](requirements.md)

## 1. How to use this record

This file records choices that affect more than one companion document. It prevents an implementation detail from silently changing product behavior.

Statuses are:

- **Accepted**: implementation and tests must follow the decision.
- **Deferred**: explicitly outside the Core milestone.
- **Bootstrap required**: the decision rule is fixed, but a generated lock or report must be committed before acceptance.

Changing an accepted decision requires updating affected documents and rerunning their gates.

## 2. Accepted decisions

### D001 — Complete the C++ core before adapters

Status: Accepted  
Decision: The current milestone ends at a complete C++17 detection-and-recognition core plus validation evidence. N-API and language packaging are not deliverables.  
Reason: The OCR algorithms, model contract, safety limits, and parity behavior need one authoritative implementation before adapter lifecycle and scheduling are introduced.  
Consequence: Adapter-friendly boundaries are retained, but no adapter code may become a completion dependency.

### D002 — Use the official PP-OCRv6 small ONNX pair

Status: Accepted  
Decision: The default bundle uses `PP-OCRv6_small_det_onnx` and `PP-OCRv6_small_rec_onnx` with the immutable identities in [model-bundle.md](model-bundle.md).  
Reason: Official ONNX artifacts exist for both mandatory stages and provide the smallest initial deployment target.  
Consequence: Tiny and medium variants require separate bundle IDs and complete quality, parity, and performance evaluation.

### D003 — Detection and recognition only

Status: Accepted  
Decision: Text-line orientation is represented as a capability but is unavailable in the first bundle. Enabling it returns `unsupported_capability`. Document orientation, unwarping, layout, table, and formula stages are outside scope.  
Reason: This makes missing behavior explicit while keeping the result and capability contracts extensible.  
Consequence: The core must never silently ignore an orientation request.

### D004 — Pin PaddleOCR v3.7.0 as the behavioral oracle

Status: Accepted  
Decision: Differential behavior is locked to PaddleOCR v3.7.0 revision `b03f46425e8ff4442b268ce449e3eef758146cd4`, the same ONNX bytes, and explicit configuration.  
Reason: A named model family is insufficient to reproduce preprocessing and postprocessing.  
Consequence: Upstream `main`, mutable defaults, and locally installed PaddleOCR versions are not valid release evidence.

### D005 — Make normalized JSON the runtime configuration authority

Status: Accepted  
Decision: Official `inference.yml` files remain provenance evidence, while the core consumes only a validated `normalized-config.json`.  
Reason: Official entry points contain different effective defaults, and YAML parser behavior should not affect runtime results.  
Consequence: Every effective parameter is explicit, hashed, and covered by the oracle snapshot. Unknown or inconsistent values fail bundle validation.

### D006 — Use synchronous, bounded engine execution

Status: Accepted  
Decision: `recognize` is synchronous. One engine admits one call and rejects additional concurrent admission; separate engines may run concurrently.  
Reason: The core should not own an invisible queue or impose a scheduling policy on future hosts.  
Consequence: Future asynchronous adapters own input retention, worker pools, backpressure, cancellation policy, and engine lifetime.

### D007 — Expose a C++17 source contract, not a stable ABI

Status: Accepted  
Decision: Public headers use only the C++ standard library and hide backend types, but the milestone does not promise C ABI or cross-toolchain C++ ABI compatibility.  
Reason: This is sufficient for in-repository integration while the semantic contract stabilizes.  
Consequence: Validation packages must be built for each Tier 1 target; consumers cannot mix arbitrary compilers or standard libraries.

### D008 — Accept decoded pixels only

Status: Accepted  
Decision: The C++ Core boundary accepts validated `GRAY8`, `RGB8`, `BGR8`, or `RGBA8` memory views. Host adapters may offer bounded decoding before crossing that boundary; the Node adapter supports in-memory JPEG/PNG. Documents and other formats are decoded by the caller.
Reason: Decoding greatly expands format, security, dependency, and platform scope without improving the OCR algorithms.  
Consequence: Fixtures cross the C++ boundary as raw pixels, parity compares identical decoded bytes, and adapter decoders require their own dependency, security-limit, and format tests.

### D009 — Support four Tier 1 targets

Status: Accepted  
Decision: macOS arm64, macOS x64, Windows x64, and Linux x64 are completion blockers. Linux arm64 and Windows arm64 are deferred.  
Reason: These targets cover the first intended native integration environments while keeping the acceptance matrix finite.  
Consequence: Each target needs native build and test evidence; cross-compilation alone is insufficient.

### D010 — Require fully offline runtime behavior

Status: Accepted  
Decision: Bundle bytes are supplied to the core; engine creation and recognition perform no download, shell execution, process launch, or implicit filesystem lookup.  
Reason: Reproducibility, privacy, and embedding safety require all runtime inputs to be explicit.  
Consequence: Model acquisition is a controlled bootstrap/release operation, not a runtime fallback.

### D011 — Enforce bundle ceilings and conservative initial limits

Status: Accepted  
Decision: The first bundle sets the resource ceilings in section 11 of [requirements.md](requirements.md). Engine options may reduce but not increase them.  
Reason: Raw dimensions alone do not bound detection candidates, tensor width, or temporary allocation.  
Consequence: Checked arithmetic and pre-allocation limit checks are required at every expanding stage.

### D012 — Keep results geometric and stage-observable

Status: Accepted  
Decision: Results preserve ordered quadrilateral boxes, confidence, UTF-8 text, model identity, and fixed stage timings. Optional diagnostics exclude pixels, tensors, and recognized-text logging.  
Reason: Geometry and stage evidence are required for adapter parity, debugging, and performance attribution.  
Consequence: Later adapters map the contract without flattening boxes or redefining confidence.

### D013 — Bound default detection and stream recognition memory

Status: Accepted; first phase implemented locally, Tier 1 evidence pending

Decision: Follow [memory-optimization.md](memory-optimization.md). The product default uses `bounded` detection with longest side `960`, keeps `4,000` only as the bundle ceiling and explicit `upstream_exact` behavior, changes the effective recognition default to batch `1`, and constructs/crops/infers/decodes/releases one recognition batch at a time. High-resolution accuracy uses an overlap-tiled strategy after its quality gates pass.

Reason: A single 2048×2048 image currently reaches about 0.9 GiB RSS before dense recognition and about 2.1 GiB for a 127-line form. The model weights are only about 30 MiB; full-resolution activations, ORT workspace, output copies, and all-batch materialization dominate memory.

Consequence: Normalized config schema `1.1` distinguishes upstream resize provenance, product runtime defaults, and hard ceilings. Exact and bounded goldens are separate profiles; macOS arm64 absolute RSS gates and API changes are complete locally, while the other Tier 1 baselines and tiled second phase remain before release.

### D101 — Use an asynchronous, bounded Node-API v8 adapter

Status: Accepted; source implementation complete, release matrix pending<br>
Decision: The first Node.js adapter follows [napi-design.md](napi-design.md): raw Node-API with `NAPI_VERSION=8`, Promise APIs, one dedicated FIFO worker per engine, bounded request and snapshot-byte admission, copied raw-pixel inputs, AbortSignal cooperative cancellation, explicit async close, and environment-scoped cleanup. It supports Node.js 22/24 first. The native boundary still receives one local bundle directory; the published JS facade supplies the built-in model package path by default and accepts an explicit `bundlePath` override.<br>
Reason: The synchronous one-call Core needs an adapter-owned scheduling and lifetime boundary that does not block JavaScript, consume libuv's shared worker pool, or permit caller mutation of in-flight image bytes.  
Consequence: Core OCR semantics and errors remain authoritative. Queued abort removes work; running abort discards delivery but does not interrupt inference. Encoded images, zero-copy, hard interruption, runtime model download/update, Electron/Bun, and additional platforms remain separate extensions.

### D105 — Publish a lockstep @arcships package set with a required default model

Status: Accepted; implementation pending<br>
Decision: The public entry is `@arcships/light-ocr`. It has one exact-version normal dependency on `@arcships/light-ocr-model-ppocrv6-small` and exact-version optional dependencies on four platform native packages, as specified in [npm-packaging.md](npm-packaging.md). The model package carries the unpacked, hash-locked PP-OCRv6 small bundle; `createEngine()` uses it without a required `bundlePath`. No package runs install/postinstall downloads or source-build fallbacks.<br>
Reason: Users should perform one npm installation and then create an engine without a second model acquisition step, while avoiding four duplicated copies of the same model across native packages.<br>
Consequence: Six packages release in lockstep. The facade is published last, after the model and native packages pass sterile tarball installation. A separate model-free flavor, multiple model selection, runtime updating and non-npm model mirrors are not v1 completion requirements.

### D111 — Freeze a provider-neutral execution contract before enabling accelerators

Status: Accepted for Perf-1A; CPU implementation complete, provider qualification pending<br>
Decision: `EngineOptions.execution` owns the stable provider policy. The default remains `cpu` with `sessionFallback=error`, `cpuPartition=allow`, `performanceHint=latency`, and `precision=auto`. A release exposes only providers that are bundled and have passed the Provider Gate; therefore the current TypeScript `ExecutionProvider` union contains only `cpu`. Unsupported provider, device, precision, partition, fallback, or performance combinations return `invalid_argument` rather than being ignored. `EngineInfo.execution.sessions` reports detection and recognition independently, including requested provider, actual configured provider chain, device, effective precision, shape policy, model identity/hash, runtime/provider version, cache status, and session fallback. The legacy aggregate `executionProvider` remains as a compatibility field while callers migrate.<br>
Reason: Apple ANE/GPU routing and other accelerators require per-stage selection and truthful fallback evidence. Freezing the neutral contract first lets backends vary without duplicating the OCR pipeline or describing provider registration as device placement.<br>
Consequence: The Core owns a backend-neutral `InferenceSession` boundary and the ONNX Runtime CPU session is its first implementation. CoreML, DirectML, OpenVINO, CUDA, QNN, `auto`, CPU partition prohibition, and throughput profiles remain unavailable until a provider-specific D111 addendum locks descriptors, distribution, qualification devices, and Gate ceilings. Runtime inference errors never trigger an undeclared CPU retry.

## 3. Deferred decisions

### D102 — Public native SDK and ABI policy

Status: Deferred  
Deferred items: C ABI, shared-library naming, symbol versioning, long-term ABI compatibility, external installation layout, and consumer package managers.

### D103 — Additional model capabilities

Status: Deferred  
Deferred items: PP-OCRv6 tiny/medium, orientation models, document preprocessing, layout, table, formula, and shipping accelerator Execution Providers. The provider-neutral Perf-1A contract is accepted by D111; it does not publish an accelerator.

Each is a separately versioned capability or bundle and requires its own compatibility and resource policy.

### D104 — External distribution operations

Status: Deferred  
Deferred items: standalone non-npm model download destination, signing, macOS notarization, Windows code signing, long-term registry/package retention, and end-user support policy. npm package topology and built-in model behavior are resolved by D105; public `0.1.0` publication and its release gates are recorded in [releases/npm-0.1.0.md](releases/npm-0.1.0.md).

## 4. Bootstrap and evidence records

These are not unresolved semantic choices. Their current completion state is tracked in [implementation-status.md](implementation-status.md):

| Record | Repository authority | Current state |
| --- | --- | --- |
| Native dependency lock | `models/deps.lock.json` | Complete and exercised by the latest Tier 1 CI run |
| Toolchain/CI identity | `.github/workflows/core.yml` plus generated build manifest | Latest six-job Core workflow passed on Linux, Windows, and both macOS architectures |
| Bundle lock and archive | `models/bundles.lock.json`, generated bundle and USTAR checksum | Hash complete; npm model staging and public registry evidence recorded for `0.1.0` |
| Oracle environment lock | `oracle/oracle.lock.json`, `oracle/requirements.lock` | Complete and locally exercised |
| Corpus manifest | `corpus/sources.lock.json`, `contracts.json`, fixture manifests | Complete for current 14-fixture parity corpus; clean regeneration and contract evidence are verified |
| Benchmark declaration/report | `oracle/run_benchmark.py`, generated benchmark report | Local reference run and controlled Linux oracle CI passed; final release report must be regenerated after D013 |
| Ground-truth report | `corpus/ground-truth.lock.json`, generated quality report | Complete local first-bundle text/detection baseline with pixel-bound independent boxes |

The C++ implementation and npm `0.1.0` release evidence are complete. Deferred C ABI/install layout and external distribution operations remain separate future decisions.
