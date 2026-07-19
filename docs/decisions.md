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

Status: Accepted for Perf-1A；Apple provider implementation complete locally with open macOS compatibility<br>
Decision: The following records the D111 implementation-era contract; D112 supersedes its default and whole-session cross-backend fallback semantics. `EngineOptions.execution` owns the stable provider policy. The default remains `cpu` with `sessionFallback=error`, `cpuPartition=allow`, `performanceHint=latency`, and `precision=auto`. The 0.3.0 source union adds `apple`: Direct Core ML FP16 routes Apple Silicon detector and recognition widths through the ANE/MLCPU envelope, sends recognition widths above 1600 to FP16 GPU, and uses all-GPU execution when CPU partitions are forbidden. Production schema 1.1 payloads require macOS 15, batch 1, bounded/960 detection and `devicePolicy=open-macos`; arm64 and x86_64 are accepted. Intel Mac has no ANE and therefore uses Core ML CPU+GPU with `cpuPartition=allow`; strict GPU policy remains Apple-Silicon-only. `validatedDeviceFamilies` and public `deviceValidated` distinguish reviewed M4 evidence from experimental compatibility without blocking M1–M3, later Apple Silicon, or Intel users. `sessionFallback=cpu` is a whole-session creation fallback with a stable reason; runtime inference never retries. Unsupported provider, precision, partition, fallback, or performance combinations return `invalid_argument` rather than being ignored. `EngineInfo.execution.sessions` reports detection and recognition independently, including requested provider, configured chain, device family/OS, device validation status, effective precision, shape policy, model identity/hash, runtime/provider version, cache status, qualification ID, and fallback. Per-call recognition diagnostics add function bucket and compute unit. The legacy aggregate `executionProvider` remains as a compatibility field while callers migrate. Qualification applies the cache-aware 3/30-second provider cold-start ceiling to the locked `generated-hello-123` canary; larger workloads retain their full first-page time as a separate observation because it also includes content-dependent detection and function loading.<br>
Reason: Apple ANE/GPU routing and other accelerators require per-stage selection and truthful fallback evidence. Freezing the neutral contract first lets backends vary without duplicating the OCR pipeline or describing provider registration as device placement.<br>
Consequence: The Core owns a backend-neutral `InferenceSession` boundary with ONNX Runtime CPU and Objective-C++ Direct Core ML implementations. The Apple model package is a self-contained superset of the CPU bundle; compiled models are cached offline by package hash + OS build + hardware identity under a cross-process lock. All 91 recognition functions have reviewed M4 placement evidence, while runtime inputs round up to 20 locked weighted width buckets under an LRU ceiling of 20. Other Macs can run early and report failures; their performance is not advertised until community or maintainer evidence is reviewed. WebGPU, DirectML, OpenVINO, CUDA, QNN, provider `auto`, and throughput profiles remain unavailable in the implementation covered by D111. D112 supersedes D111 only for future cross-backend creation selection and whole-session fallback semantics.

### D112 — Use platform-aware Auto with creation-time ordered fallback

Status: Implemented in the current source candidate; platform release evidence pending<br>
Decision: `provider=auto` is the only mode that may try more than one backend. During `Engine` creation it resolves a versioned, platform-specific candidate list, attempts each candidate atomically in order, selects the first candidate whose detector and recognizer sessions both initialize, and then freezes that backend for the engine lifetime. Every valid released policy is non-empty and ends in `cpu`; the target policy is macOS `apple → cpu`, Windows x64 `webgpu → cpu`, and Linux x64 glibc `webgpu → cpu`. A candidate may enter a **released** policy only when its runtime and artifacts are declared by the platform runtime descriptor, physically bundled in that release set, and accepted for the descriptor's compatibility range by the platform Gate. Until all three conditions hold, that released policy omits the accelerator; a CPU-only release therefore resolves Auto to `[cpu]` rather than reporting a synthetic accelerator failure.<br>

The platform runtime descriptor is the package-private immutable authority for candidate construction. It records schema version, Auto policy ID/version and ordered provider IDs; each provider entry records bundled artifacts and hashes, runtime/provider ABI, platform/architecture, compatibility-manifest identity and qualification ID. Release staging generates it from the files actually staged and fails if declarations and payload disagree. The facade/native loader reads this descriptor from the selected platform package; the model manifest does not own runtime capability, and runtime code never discovers candidates by scanning the host.<br>

D112 is implemented in the current source candidate and changes the C++ and Node default from `cpu` to `auto` together; published `0.2.0` binaries retain their documented D111 behavior. Explicit `provider=cpu|apple|webgpu|…` bypasses the Auto list and attempts only the named backend. A known provider that is not delivered for the current platform fails pre-attempt validation as `unsupported_capability`; it never falls through to CPU and does not fabricate a D112 creation reason. `sessionFallback` is retained only as a migration field until removal: `error` is the sole valid value for both Auto and explicit modes, while `sessionFallback=cpu` always returns `invalid_argument`. This prevents the legacy field from acquiring a second meaning under Auto.<br>

The first Auto surface accepts only the provider-neutral values `sessionFallback=error`, `cpuPartition=allow`, `performanceHint=latency`, `precision=auto`, and no `deviceId`. Precision is resolved inside each candidate. Strict partitioning, throughput tuning, an explicit precision, or a device ID requires an explicit provider; combining any of them with Auto returns `invalid_argument` before a factory call. `cpuPartition` remains a graph-placement constraint on accelerator sessions, while the final CPU candidate is the requested CPU backend rather than a graph partition.<br>

The creation state machine is:

```text
validate request and resolve requested provider
  ├─ explicit provider → atomically create detector + recognizer
  │    ├─ success → freeze selected backend
  │    └─ any failure → destroy partial state; return structured creation error
  └─ auto → resolve policy ID/version and ordered bundled candidates
       → for each candidate
          ├─ both sessions created → record selected; freeze backend; stop
          ├─ skippable creation failure → destroy partial state; record skipped; continue
          └─ fatal creation failure → destroy partial state; record fatal; return error
       → final CPU candidate failure → record fatal; return error with full trace
```

These four stable creation reasons permit Auto to continue only when another candidate remains:

| Reason code | Exact meaning |
| --- | --- |
| `adapter_unavailable` | No compatible device/adapter exists for the candidate on the current host. A provider omitted from the release descriptor is not attempted and does not produce this reason. |
| `model_compute_unsupported` | The candidate device or compute unit cannot create both required sessions for the locked model, shape, precision, and partition policy. |
| `device_memory_insufficient` | During candidate session creation, the provider explicitly reports that required **device** memory cannot be satisfied. Host allocation failure, unknown OOM, `std::bad_alloc`, and resource leakage are not included. |
| `driver_version_unsupported` | The detected driver is outside the version range locked by the bundled compatibility descriptor. |

These reasons are fatal and stop Auto immediately:

| Reason code | Exact meaning |
| --- | --- |
| `package_corrupt` | A descriptor-declared runtime, model, plugin, or required dependency is absent, truncated, or structurally invalid. |
| `artifact_hash_mismatch` | A bundled artifact does not match its locked digest. |
| `provider_abi_mismatch` | Runtime, plugin, addon, or provider ABI versions are incompatible. |
| `internal_assertion_failed` | A product/runtime invariant or internal assertion fails. |
| `unrecoverable_load_failed` | Native loading fails after package integrity and compatibility checks and the failure is not one of the four skippable reasons. |

Failure classification is typed, not inferred from exception text. Descriptor presence, structure, hashes, ABI and driver range are classified by package/preflight validation; provider adapters may emit one of the four continue-eligible reasons only from an explicit provider/device status. Unknown exceptions and host allocation failures such as `std::bad_alloc` are terminal public errors outside the nine D112 reasons; they never become `device_memory_insufficient`. Unclassified native-load failures use `unrecoverable_load_failed`. Attempt status describes control flow, not a second taxonomy: a continue-eligible reason is `skipped` only when another candidate will actually be tried; the same reason on the final CPU candidate is `fatal` because creation terminates. The broad public `ErrorCode` remains separate from `creationReason`; callers decide whether Auto may continue only from a typed D112 reason plus candidate position, never by parsing `message`, `detail`, ORT/Core ML text, or a broad error code.<br>

`Engine::Run` never changes backend. Device loss, driver reset, inference failure, and runtime OOM are returned to the caller; the caller may create a new engine under an explicit policy. Candidates that cannot be safely torn down after a failed attempt, or whose incompatible runtimes cannot be isolated, must not coexist in one Auto list.<br>

Selection observability uses one immutable structured trace with requested provider, optional Auto policy ID/version, ordered candidates, attempts, and optional final selected backend. Each attempt has a provider and `selected | skipped | fatal` status. `selected` carries no failure field; `skipped` carries one of the four continue-eligible `creationReason` values; `fatal` carries any typed reason that terminates creation, or, for a terminal public error outside the D112 taxonomy, a stable `errorCode` with no fabricated reason. A successful `EngineInfo` contains zero or more skipped accelerator attempts followed by exactly one `selected` attempt; explicit success contains one selected candidate and no Auto policy. Engine creation failure has no `EngineInfo`, so the returned C++ creation error and mapped Node `OcrError` carry the same structured `creationTrace`: explicit factory failure contains one terminal `fatal` attempt, and Auto failure always ends in `fatal`, including failure of the final CPU candidate. Pre-attempt request/capability validation errors have no attempts. `fatal` is therefore never fabricated inside a successfully created engine.<br>

The selected candidate owns a complete detector/recognizer pair. Partial success is never exposed as a mixed backend: if either session fails, all state for that candidate is destroyed before another candidate starts. Detector/recognizer actual provider, device, precision, model and CPU-partition facts remain per-session fields beneath the selection trace.<br>
Reason: Platform users need one deterministic default that uses an accepted accelerator when available without turning runtime failures or damaged packages into silent CPU execution. Restricting fallback to creation-time `auto`, with a closed failure taxonomy, preserves reproducibility and makes every selection auditable.<br>
Consequence: D112 is the single source of truth for cross-backend selection, failure classification, explicit-provider behavior, migration-field validation and selection traces on both success and creation failure. D111 remains authoritative for provider-neutral session diagnostics, graph partitioning, Apple internal routing, and the rule against runtime retry. Platform documents define backend-local qualification and routing but reference D112 instead of redefining fallback. Architecture and implementation-status documents change only after implementation lands.

### D113 — Select Native WebGPU FP16 through immutable model variants

Status: Superseded by D114 for the public `0.3.0` execution contract
Decision: Linux x64 and Windows x64 Native WebGPU use the official ONNX Runtime 1.24.4 Plugin EP 0.1.0 runtime. Explicit `provider=webgpu, precision=fp16` selects deterministic ONNX Runtime float16-derived detector and recognizer models with native float16 graph input/output and Extended graph optimization. The upstream FP32 models remain immutable and continue to serve CPU, explicit WebGPU FP32, and D112 Auto. WebGPU FP16 is therefore opt-in until device evidence justifies a separate Auto-policy change. The WebGPU Plugin EP exposes no provider option that toggles FP16; model tensor types select the precision and Dawn enables `ShaderF16` only on a capable adapter.<br>

The derived payload records its source hashes, converter/tool versions, output hashes, graph-I/O type, blocked-operation policy, and runtime contract. CI regenerates both models byte-for-byte and runs deterministic finite FP16 inference before packaging. Manifest schema 1.2 binds the derived payload to its FP32 sources and permits the Apple payload to coexist in one platform-independent npm model superset.<br>

The current graphs require a bounded CPU partition containing only `Concat`, `Gather`, and `Slice`. `cpuPartition=allow` is the qualified WebGPU contract and ORT profiles must contain no other CPU operator. `cpuPartition=forbid` is still a meaningful fail-closed request: engine creation returns stable `unsupported_capability` before session creation rather than allowing ORT to fail after partial placement. Qualification compares CPU FP32, explicit WebGPU FP32, and explicit WebGPU FP16; it verifies strict rejection, FP16 quality/determinism/performance, placement, memory, cold starts, and lifecycle independently.<br>
Reason: The Linux device report proved useful FP32 acceleration but also proved that three recognition operators cannot satisfy the old all-WebGPU strict expectation. A model-selected FP16 path is the upstream-supported mechanism, preserves the published FP32 bundle identity, and makes the unavoidable CPU partition explicit and reviewable.<br>
Consequence: FP16 results cannot be generalized to Auto or to adapters without `ShaderF16`. Any change to conversion, retained FP32 operations, CPU operator allowlist, graph optimization, or source models creates a new immutable artifact/conversion ID and requires both platform reports again.

### D114 — Ship Native WebGPU FP32 as the `0.3.0` public profile

Status: Accepted

Decision: Linux x64/Vulkan and Windows x64/D3D12 publish Native WebGPU with `precision=auto|fp32`; Auto also selects FP32. `provider=webgpu, precision=fp16` is rejected as `invalid_argument`. The `Precision` enum and TypeScript union retain `fp16` because Apple/Core ML uses it, but FP16 is not a WebGPU compatibility or performance promise in `0.3.0`.<br>

The locked FP16 ONNX derivations remain reproducible internal artifacts so existing manifest provenance and the native superset bundle stay deterministic. Their presence in a bundle does not make them a public execution profile. A future WebGPU FP16 release requires a new decision, explicit API/documentation change, and fresh cross-platform quality and performance evidence.<br>

Reason: The final Linux and Windows FP32 reports each passed 164/164 Gates and all 14 quality fixtures matched the CPU FP32 baseline byte-for-byte. The experimental FP16 run did not meet that release quality bar, so publishing FP32 gives one consistent, qualified product contract across both WebGPU platforms.<br>

Consequence: Qualification, report review, examples, release notes, and performance displays use only `cpu`, WebGPU FP32 `allow`, `strict`, and Auto. The required `Concat`, `Gather`, and `Slice` CPU partition remains explicit; `cpuPartition=forbid` continues to fail closed. No WebGPU FP16 speedup is advertised.

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
