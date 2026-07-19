# light-ocr Model Bundle

Status: normalized schema 1.2 / `tiled-v1` published in npm `0.2.0`; manifest schema 1.2 native provider superset implemented in the 0.3.0 source candidate<br>
Authority: model identity, bundle schema, normalized configuration, integrity, and licensing  
Requirements: [requirements.md](requirements.md)

## 1. Initial bundle decision

The first Core bundle is:

```text
bundleId: ppocrv6-small-onnx-20260714.2
family: PP-OCRv6
detection: PP-OCRv6_small_det_onnx
recognition: PP-OCRv6_small_rec_onnx
text-line orientation: unavailable
```

The immutable `ppocrv6-small-webgpu-20260719.1` candidate preserves the
published FP32 payload and adds hash-locked ONNX FP16 variants. The final
platform-independent npm candidate, `ppocrv6-small-native-20260719.1`, extends
that bundle with FP16 Core ML packages and a macOS-wide open compatibility
policy. The reviewed M4 device list is evidence metadata, not a runtime
allow-list.

PP-OCRv6 tiny is a future independent bundle. PP-OCRv6 medium is architecture-compatible but is not a release target until an official ONNX artifact is pinned and validated.

## 2. Upstream snapshot

The bundle records these upstream identities:

| Item | Immutable revision |
| --- | --- |
| PaddleOCR v3.7.0 | `b03f46425e8ff4442b268ce449e3eef758146cd4` |
| Detection model repository | `28fe5895c24fd108c19eb3e8479f4ab385fbfc62` |
| Recognition model repository | `b8f84f0b80c529de40b4fbb3544b84fa7233a513` |

Model repositories:

- [PP-OCRv6 small detection ONNX at the pinned revision](https://huggingface.co/PaddlePaddle/PP-OCRv6_small_det_onnx/tree/28fe5895c24fd108c19eb3e8479f4ab385fbfc62)
- [PP-OCRv6 small recognition ONNX at the pinned revision](https://huggingface.co/PaddlePaddle/PP-OCRv6_small_rec_onnx/tree/b8f84f0b80c529de40b4fbb3544b84fa7233a513)

Original official archives:

- [Detection archive](https://paddle-model-ecology.bj.bcebos.com/paddlex/official_inference_model/paddle3.0.0/PP-OCRv6_small_det_onnx_infer.tar)
- [Recognition archive](https://paddle-model-ecology.bj.bcebos.com/paddlex/official_inference_model/paddle3.0.0/PP-OCRv6_small_rec_onnx_infer.tar)

The URLs are provenance, not immutable identity. SHA-256 is required.

Bootstrap first uses the official Paddle model-ecology archives. If an archive
request fails at the network layer, it falls back to the `inference.onnx` and
`inference.yml` files in the official PaddlePaddle Hugging Face repository at
the immutable revisions above. Both paths must match the same locked member
byte counts and SHA-256 values; integrity failures never trigger fallback.

## 3. Observed official artifact inventory

The 2026-07-13 bootstrap download produced:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Detection archive | 9,891,840 | `d218f6fbf0f1c23d2161bd6ac7f5eaa6104fa89955c09290497e31008e2618e4` |
| Detection `inference.onnx` | 9,880,512 | `d73e0058b7a8086bbd57f3d10b8bcd4ff95363f67e06e2762b5e814fe9c9410e` |
| Detection `inference.yml` | 885 | `193f435274bf9f0b5f71a929bbfbcf148282df7e633b34e7c373e8f44741b516` |
| Recognition archive | 21,319,680 | `d267ab077a44a0eedb1ea8f8c542d263f211de8e9d7a029bf9fcfff7e5a88fb1` |
| Recognition `inference.onnx` | 21,159,378 | `5435fd747c9e0efe15a96d0b378d5bd157e9492ed8fd80edf08f30d02fa24634` |
| Recognition `inference.yml` | 150,579 | `ab078671bb49f06228eadccd34f1bb501e157f7a047095ffb943ba81512c77d1` |

The values above are bootstrap observations from the official URLs. Release automation downloads, verifies, stages them into the controlled npm model package, and records them again; any byte-count or hash change blocks the release until explicitly reviewed.

## 4. Bundle layout

```text
ppocrv6-small-onnx-20260714.2/
  manifest.json
  normalized-config.json
  det/
    inference.onnx
    inference.yml
  rec/
    inference.onnx
    inference.yml
    dictionary.json
  LICENSES/
    PaddleOCR-Apache-2.0.txt
    MODEL-NOTICE.md
  SHA256SUMS
```

The WebGPU bundle additionally contains:

```text
webgpu/
  det/inference.onnx
  rec/inference.onnx
  provenance.json
```

The native npm superset also contains:

```text
apple/
  detector-fp16.mlpackage/
  recognizer-fp16.mlpackage/
  provenance.json
```

The recognizer is one 91-function MLProgram (`w0320` through `w3200`, step 32),
not 91 resident sessions. The runtime rounds up to one of 20 locked weighted
width buckets and lazily keeps at most 20 selected functions; the detector
accepts the bounded 32–960 range.

`ModelBundle::create` receives the complete directory as immutable in-memory files. It requires every payload named by `manifest.json`, including normalized config, dictionary, both ONNX/YAML pairs, licenses and notice; `SHA256SUMS` is the one permitted external checksum file. Recognition never parses YAML.

## 5. Integrity model

Integrity avoids circular hashes:

1. `manifest.json` contains SHA-256 for every payload except itself and `SHA256SUMS`.
2. `SHA256SUMS` contains hashes for all payload files plus `manifest.json`; it excludes itself.
3. `tools/package_model_bundle.py` creates a deterministic USTAR archive and verifies its identity against `models/bundles.lock.json`.
4. The published `.2` archive is 31,334,400 bytes with SHA-256 `e543b93bc4882f35b1564a71961e5bc55439ede6c2f33b4166acc15e6348712f`.
5. `@arcships/light-ocr-model-ppocrv6-small` must contain the exact unpacked bundle files and record its npm tarball SHA-256/integrity. Publishing that verified package is the v1 controlled redistribution path. `mirror: null` only means the standalone USTAR archive has no separate mirror; it does not trigger runtime download and is not a prerequisite for the npm topology.

Runtime bundle validation verifies:

- Path normalization and uniqueness.
- Required files.
- Complete `SHA256SUMS` coverage, including `manifest.json`.
- Manifest schema `1.0` for CPU-only bundles, legacy `1.1` for an Apple-only payload, or `1.2` for the native provider superset and optional Apple payload, plus Core compatibility. The locked WebGPU FP16 derivation in schema 1.2 is internal in `0.3.0`; public WebGPU execution remains FP32-only.
- Every manifest payload hash.
- Model ID and configuration agreement.
- WebGPU derived-model source binding, output/provenance hashes, conversion ID,
  FP16 tensor type, Extended optimization policy, and the bounded
  `Concat/Gather/Slice` CPU operator contract.
- Tensor contract and dictionary identity.

A hash mismatch returns `model_integrity_failed`. A structurally invalid but correctly hashed bundle returns `invalid_model_bundle`.

## 6. Manifest schema

`manifest.json` has this logical shape:

```json
{
  "schemaVersion": "1.0",
  "bundleId": "ppocrv6-small-onnx-20260714.2",
  "family": "PP-OCRv6",
  "coreCompatibility": {
    "minimum": "0.1.0",
    "maximumMajor": 0
  },
  "upstream": {
    "repository": "https://github.com/PaddlePaddle/PaddleOCR",
    "release": "v3.7.0",
    "revision": "b03f46425e8ff4442b268ce449e3eef758146cd4"
  },
  "capabilities": {
    "detection": true,
    "recognition": true,
    "textlineOrientation": false
  },
  "models": {
    "detection": {
      "id": "PP-OCRv6_small_det_onnx",
      "sourceRevision": "28fe5895c24fd108c19eb3e8479f4ab385fbfc62",
      "modelPath": "det/inference.onnx",
      "configPath": "det/inference.yml",
      "inputRank": 4,
      "outputRanks": [3, 4]
    },
    "recognition": {
      "id": "PP-OCRv6_small_rec_onnx",
      "sourceRevision": "b8f84f0b80c529de40b4fbb3544b84fa7233a513",
      "modelPath": "rec/inference.onnx",
      "configPath": "rec/inference.yml",
      "dictionaryPath": "rec/dictionary.json",
      "inputRank": 4,
      "outputRank": 3
    }
  },
  "normalizedConfigPath": "normalized-config.json",
  "files": {
    "det/inference.onnx": {
      "sha256": "d73e0058b7a8086bbd57f3d10b8bcd4ff95363f67e06e2762b5e814fe9c9410e",
      "bytes": 9880512
    }
  },
  "licenses": ["Apache-2.0"]
}
```

The real manifest lists every payload file. Core `0.1.x` and `0.2.0` accept manifest schema `1.0`; the 0.3.0 source retains legacy Apple-only schema `1.1` and adds schema `1.2`. Schema `1.2` can carry the locked internal WebGPU FP16 derivation and the Apple provider, while the 0.3.0 public WebGPU execution profile remains FP32-only. Normalized configuration evolves independently. Unknown schemas or provider keys are rejected.

### 6.1 WebGPU provider extension

Schema 1.2 adds `providers.webgpu` sub-contract 1.0. It binds:

- conversion ID `onnxruntime-float16-1.24.4-20260719.1`, native float16 graph
  I/O, and Extended graph optimization;
- detector and recognizer model IDs, paths, hashes, and their exact source FP32
  model IDs/hashes;
- the converter provenance file and hash;
- `cpuPartition: "allow-required"` with exactly `Concat`, `Gather`, and `Slice`.

The locked FP16 derivations remain available for reproducibility and provenance
checks, but `0.3.0` does not select them through the public WebGPU API. CPU,
explicit WebGPU, and Auto use the immutable source FP32 models. The graph contract rejects strict
CPU-partition prohibition before ORT session creation.

### 6.2 Apple provider extension

Schema 1.1, or schema 1.2 in the combined native superset, adds a top-level `providers.apple` object. The provider sub-contract is version 1.1 and fixes:

- `minimumMacOS: "15.0"`, `devicePolicy: "open-macos"`,
  `architectures: ["arm64", "x86_64"]`, a non-empty
  `validatedDeviceFamilies` evidence list, and
  `qualificationId: "apple-fp16-mixed-20260715.2"`;
- detector package/model/hash/tensor/shape identities, interactive ANE and
  strict GPU policies, Intel CPU+GPU policy, plus the maximum reviewed MLCPU
  operation envelope;
- recognizer package identity, 32-pixel width multiple, ANE maximum width 1600,
  `w%04u` function mapping, all 91 qualified widths, the locked 20 runtime
  width buckets, and an LRU ceiling of 20 functions;
- every `.mlpackage` member in the normal manifest inventory and a package-level
  inventory hash, so changing either protobuf or weights invalidates the bundle.
- conversion removes the volatile Core ML conversion date, deterministically
  serializes every model protobuf, and replaces package entry UUIDs with stable
  UUIDv5 identifiers before checking the locked package hashes.

`qualifiedMLCPUOperations` is a maximum reviewed envelope on the M4 evidence
device, not a requirement
that every shape use every listed CPU operation. Qualification rejects unknown
or excess MLCPU operations, missing ANE placement below the boundary, any CPU
operation on the strict GPU route, incomplete width coverage, or argmax parity
changes. `validatedDeviceFamilies` does not gate production execution:
`open-macos` permits any listed architecture on macOS 15+, while
`deviceValidated=false` marks hardware without reviewed evidence. The
`validated-only` policy exists for controlled deployments and fallback testing,
but is not used by the npm production bundle.

## 7. Normalized configuration

Runtime code parses only `normalized-config.json`. This prevents YAML-parser differences and silent upstream defaults.

The published 0.1.0 normalized-config schema is `1.1`. It separates `sourceDetectionResize` (`64/min/4000` provenance), `runtimeDefaults.detection` (`bounded/960`), and `resourceLimits.maxDetectionSide` (`4000` ceiling). The published 0.2.0 bundle uses schema `1.2` and adds the locked tiled profile plus `maxDetectionTiles`. Core still accepts schema `1.0` bundles as the legacy `upstream_exact` / batch-8 contract; it never silently assigns new product defaults or tiled capability to an old bundle.

The [Tiled Detection specification](tiled-design-and-acceptance.md) defines normalized-config schema `1.2` and the versioned `tiled-v1` runtime profile. The parser, published `.2` bundle, and old-bundle rejection are implemented; schema `1.1` remains supported only for the immutable 0.1.0 release contract.

The same file also fixes the bundle ceilings:

```json
{
  "resourceLimits": {
    "maxWidth": 10000,
    "maxHeight": 10000,
    "maxPixels": 40000000,
    "maxDetectionSide": 4000,
    "maxDetectionCandidates": 3000,
    "maxDetectionTiles": 100,
    "maxRecognitionBatchSize": 8,
    "maxRecognitionWidth": 3200,
    "maxTemporaryBytes": 536870912,
    "maxConcurrentCalls": 1
  }
}
```

Engine options may replace runtime defaults only within these hard ceilings; reduced resource-limit objects can only lower the ceilings.

### 7.1 Detection

The source provenance and product runtime default are:

```json
{
  "sourceDetectionResize": {
    "limitSideLen": 64,
    "limitType": "min",
    "maxSideLimit": 4000,
    "dimensionMultiple": 32,
    "minimumDimension": 32,
    "scaledDimensionRounding": "truncate_toward_zero",
    "multipleRounding": "half_to_even",
    "maxSideLimitOrder": "before_multiple_rounding",
    "interpolation": "linear"
  },
  "runtimeDefaults": {
    "detection": {
      "strategy": "bounded",
      "maxSide": 960,
      "minimumShortSide": 64,
      "dimensionMultipleRounding": "ceil"
    },
    "recognitionBatchSize": 1
  },
  "detection": {
    "input": {
      "colorOrder": "BGR",
      "tensorLayout": "NCHW",
      "tensorType": "float32"
    },
    "normalize": {
      "scale": 0.00392156862745098,
      "mean": [0.485, 0.456, 0.406],
      "std": [0.229, 0.224, 0.225]
    },
    "postprocess": {
      "algorithm": "DB",
      "threshold": 0.3,
      "boxThreshold": 0.6,
      "unclipRatio": 1.5,
      "maxCandidates": 3000,
      "useDilation": false,
      "scoreMode": "fast",
      "boxType": "quad",
      "minimumBoxSide": 3
    }
  }
}
```

The official model YAML declares model-level DB values `0.2`, `0.45`, and `1.4`. The values above intentionally select the official v3.7 general OCR/Android pipeline behavior. The oracle harness passes them explicitly. No value is obtained from a stage fallback.

`upstream_exact` uses the source half-even rounding. `bounded` raises a short side below 64, caps the long side at its effective maximum, then rounds dimensions upward to 32. Both parity profiles assert the exact sequence; stage code does not choose a different fallback.

### 7.2 Crop and reading order

```json
{
  "geometry": {
    "rowBandPixels": 10,
    "perspectiveInterpolation": "cubic",
    "borderMode": "replicate",
    "tallLineRatio": 1.5,
    "tallLineRotation": "counterclockwise90"
  }
}
```

### 7.3 Recognition

```json
{
  "recognition": {
    "input": {
      "colorOrder": "BGR",
      "tensorLayout": "NCHW",
      "tensorType": "float32",
      "shape": [3, 48, 320],
      "minimumTensorWidth": 320,
      "maximumTensorWidth": 3200,
      "tensorWidthRounding": "truncate_toward_zero",
      "resizedContentWidthRounding": "ceil",
      "batchTensorWidth": "maximum_sample_tensor_width",
      "interpolation": "linear"
    },
    "normalize": {
      "scale": 0.00392156862745098,
      "mean": [0.5, 0.5, 0.5],
      "std": [0.5, 0.5, 0.5],
      "paddingValue": 0.0
    },
    "batch": {
      "maximumSize": 8,
      "sortByWidth": true
    },
    "decode": {
      "algorithm": "CTC",
      "blankIndex": 0,
      "collapseRepeats": true,
      "appendSpaceCharacter": true,
      "confidence": "mean_selected_argmax_probability"
    },
    "defaultScoreThreshold": 0.0
  }
}
```

The recognition YAML embeds 18,708 dictionary entries. The decode character sequence is:

1. Embedded entries in exact YAML order.
2. One appended ASCII space when not already present under the upstream rule.

The bundle generator records the resulting UTF-8 dictionary SHA-256 and class count. Session creation verifies that the model output class dimension equals blank plus the effective dictionary size.

Recognition color order follows the official YAML and pinned Python/JavaScript behavior: BGR bytes are normalized without a channel swap. The v3.7.0 Android `RecPreprocessor` performs an RGB conversion; that known implementation difference is secondary evidence and does not override the oracle.

For a crop ratio `r = width / height`, preprocessing computes the sample tensor width as `truncate(48 * max(320 / 48, r))`, clamps it to 3,200, resizes content to `min(sampleTensorWidth, ceil(48 * r))`, and fills the remaining batch tensor area with normalized zero. A batch tensor uses the largest sample tensor width in that batch.

## 8. Tensor contract

At engine creation:

- Detection input is rank 4, float32, NCHW, three channels.
- Detection output is a supported single probability map with batch alignment.
- Recognition input is rank 4, float32, NCHW, height 48, dynamic width.
- Recognition output is rank 3 in `[batch, time, classes]` order.
- Input and output names are discovered, then recorded in test-only diagnostics and the parity report.

Unexpected ranks, types, channel counts, fixed dimensions, or class counts return `unsupported_model`.

## 9. Bundle generation

The release pipeline:

1. Downloads official archives.
2. Verifies bootstrap or approved upstream hashes.
3. Extracts only expected safe relative paths.
4. Rejects links, devices, path traversal, duplicate entries, and unexpected required-file ambiguity.
5. Generates normalized configuration.
6. Generates the effective dictionary and its hash.
7. Runs oracle configuration export and stage probes.
8. Writes manifest, licenses, and checksums.
9. Runs parity and performance gates.
10. Archives the immutable bundle, stages its exact unpacked files into the npm model package, and optionally mirrors the standalone USTAR for non-npm consumers.

Production recognition performs no download or conversion.

## 10. Licensing

The PaddleOCR repository and official model cards declare Apache-2.0. The bundle includes:

- The exact PaddleOCR license text from the pinned revision.
- Model source URLs and revisions.
- A notice naming detection and recognition artifacts.
- Generation and distribution metadata.

Release automation verifies license metadata again. A missing license record blocks the bundle.

## 11. Update policy

A model or configuration change creates a new bundle ID and requires:

- New hashes and source revisions.
- Full stage-level parity.
- Ground-truth model-quality comparison.
- Performance comparison.
- Language and scenario impact report.
- Rollback availability.

An upstream URL changing content without an approved update fails integrity validation and never silently replaces a released bundle.
