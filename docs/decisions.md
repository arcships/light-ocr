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
Decision: The public boundary accepts validated `GRAY8`, `RGB8`, `BGR8`, or `RGBA8` memory views. Encoded images and documents are decoded by the caller.  
Reason: Decoding greatly expands format, security, dependency, and platform scope without improving the OCR algorithms.  
Consequence: Fixtures cross the C++ boundary as raw pixels, and parity compares identical decoded bytes.

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

### D101 — Use an asynchronous, bounded Node-API v8 adapter

Status: Accepted; source implementation complete, release matrix pending  
Decision: The first Node.js adapter follows [napi-design.md](napi-design.md): raw Node-API with `NAPI_VERSION=8`, Promise APIs, one dedicated FIFO worker per engine, bounded request and snapshot-byte admission, copied raw-pixel inputs, AbortSignal cooperative cancellation, explicit async close, and environment-scoped cleanup. It supports Node.js 22/24 first and uses local caller-supplied model bundles only.  
Reason: The synchronous one-call Core needs an adapter-owned scheduling and lifetime boundary that does not block JavaScript, consume libuv's shared worker pool, or permit caller mutation of in-flight image bytes.  
Consequence: Core OCR semantics and errors remain authoritative. Queued abort removes work; running abort discards delivery but does not interrupt inference. Encoded images, zero-copy, hard interruption, model download/mirror integration, Electron/Bun, and additional platforms remain separate extensions.

## 3. Deferred decisions

### D102 — Public native SDK and ABI policy

Status: Deferred  
Deferred items: C ABI, shared-library naming, symbol versioning, long-term ABI compatibility, external installation layout, and consumer package managers.

### D103 — Additional model capabilities

Status: Deferred  
Deferred items: PP-OCRv6 tiny/medium, orientation models, document preprocessing, layout, table, formula, and accelerator Execution Providers.

Each is a separately versioned capability or bundle and requires its own compatibility and resource policy.

### D104 — External distribution operations

Status: Deferred  
Deferred items: public download destination, signing, macOS notarization, Windows code signing, package retention, and end-user support policy. Internal immutable storage is sufficient for Core acceptance.

## 4. Bootstrap and evidence records

These are not unresolved semantic choices. Their current completion state is tracked in [implementation-status.md](implementation-status.md):

| Record | Repository authority | Current state |
| --- | --- | --- |
| Native dependency lock | `models/deps.lock.json` | Complete locally; Tier 1 use awaits CI evidence |
| Toolchain/CI identity | `.github/workflows/core.yml` plus generated build manifest | Configured; remote run evidence pending |
| Bundle lock and archive | `models/bundles.lock.json`, generated bundle and USTAR checksum | Hash complete; controlled mirror pending |
| Oracle environment lock | `oracle/oracle.lock.json`, `oracle/requirements.lock` | Complete and locally exercised |
| Corpus manifest | `corpus/sources.lock.json`, `contracts.json`, fixture manifests | Complete for current 14-fixture parity corpus; clean regeneration and contract evidence are verified |
| Benchmark declaration/report | `oracle/run_benchmark.py`, generated benchmark report | Local reference run passed; controlled CI report pending |
| Ground-truth report | `corpus/ground-truth.lock.json`, generated quality report | Complete local first-bundle text/detection baseline with pixel-bound independent boxes |

The C++ implementation is complete, but the milestone cannot be declared release-complete until the Pending items above have immutable evidence.
