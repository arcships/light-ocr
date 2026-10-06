# npm 0.5.9 Release Preparation

[中文版](npm-0.5.9.md)

Status: preparation only. npm publication, a public tag, artifact hashes and registry promotion are pending. The GitHub Release is a draft; previous release measurements are not reused.

## Versions

NPU implementation: [PR #67](https://github.com/arcships/light-ocr/pull/67), merged. Preparation: [PR #68](https://github.com/arcships/light-ocr/pull/68); the GitHub draft exists. The release source must be the main commit after merging version preparation.

| Package | Prepared version | Planned tags |
| --- | --- | --- |
| Small facade | `0.5.9` | `next`, then `latest` |
| Model-free runtime | `0.1.9` | `next`, then `latest` |
| Eight native packages | `0.5.9` | `next`, then `latest` |
| Document compatibility facade | `0.1.5` | `next` |
| Tiny/Medium preview facades | `0.1.8` | `next` |
| Small model | `0.3.4` | Reuse immutable package |
| Tiny/Medium models | `0.1.0` | Reuse immutable packages |

## Changes

- The default Small package retains its existing approximately 39 MB model promise and immutable model package 0.3.4. No AMD/OpenVINO libraries or compiled NPU contexts enter the default dependency closure.
- Eight base native packages retain Apple/WebGPU/CPU policies. NPU support is separately installed as `@arcships/light-ocr-openvino-linux-x64-gnu@0.5.9` or `@arcships/light-ocr-amdnpu-linux-x64-gnu@0.5.9`.
- These are optional peers, without automatic installation. Explicit `execution.provider` selects the matching separate addon; Auto does not select either NPU. Vendor drivers are required; device inference remains unqualified.
- AMD includes 20 actual BF16 contexts bound to Small 0.3.4 and is substantially larger. Other tiers need their own compiled contexts.

## Release sequence

1. Merge the split-package correction into main.
2. Run `npm release` with version 0.5.9 and publication disabled for the ordinary eight-platform closure.
3. Run `NPU support packages` separately, also with publication disabled, to build the two independent NPU packages.
4. Record final source commits, both runs, exact package sizes and hashes. Base assembly rejects NPU providers and directories.
5. Publish the base closure through its existing next/latest sequence; publish independent support packages through their own workflow under next. Document and previews remain on next.
6. Update the draft target, publication identities and dates before publishing v0.5.9.

## Evidence and rejected candidate

The previous candidate run `37480252872` passed eight-platform build/smoke, but incorrectly bundled both NPUs into Linux x64: 577,384,672 compressed bytes and 1,455,176,275 unpacked bytes. **Do not publish this candidate; rebuild from corrected main.**

Source NPU build run `37469626258` succeeded. All 20 AMD contexts were actually compiled without device inference. Original SDK provenance and deployment archive identity remain locked in `amdnpu-source.lock.json` and `amdnpu.lock.json`; the approximately 508 MiB archive supplies only the independently installed AMD support package.

The 13 new base identities were vacant. Final corrected tarball identities and both support-package builds are recorded after generation.

## Local split-package build evidence

All three addons (base CPU, independent OpenVINO and independent AMD) built and staged separately. Base inventory contains CPU only, no NPU directories, with 33,091,890 native bytes; this local baseline excludes the production WebGPU/PDF attachments. Both independent support tarballs were generated:

| Package | Compressed bytes | Unpacked bytes |
| --- | ---: | ---: |
| `@arcships/light-ocr-amdnpu-linux-x64-gnu` | 548,609,874 | 1,390,267,314 |
| `@arcships/light-ocr-openvino-linux-x64-gnu` | 26,363,501 | 72,478,453 |

These are local packaging artifacts, not the corrected eight-platform release run. No new local tests or device inference were performed.

## Rollback

Restore stable tags using the archived 0.5.8 publication artifact (run `36013862673`): Small/native `0.5.8`, runtime `0.1.8`. Handle compatibility/preview tags separately; never overwrite or delete published package versions.

## AMD deployment artifact

- Archive: `amdnpu-1.8.0-linux-x64-ppocrv6-small-0.3.4-opt-in-v1.tar.gz`
- Size: `532,631,585` bytes (~508 MiB); unpacked SDK inventory: `1,356,635,270` bytes (~1.26 GiB). This substantially increases the Linux x64 npm package size.
- Archive SHA-256: `bee70b00f3254943751b9460628295696d9901f22887f11b0c791191847fb016`
- Artifact set SHA-256: `5df515842656974be5517ae40b408427f0ffdbb63473c1ced1b610f2a0afc8a4`
- Small source recognition SHA-256: `5435fd747c9e0efe15a96d0b378d5bd157e9492ed8fd80edf08f30d02fa24634`
- Original SDK provenance is locked in `tools/npu/amdnpu-source.lock.json`; deployment archive identity is in `tools/npu/amdnpu.lock.json`. The archive is attached to the draft release. Final npm tarball identities are generated separately by the release workflow.

## GitHub Release

Tag: `v0.5.9` (not public while draft).

Name: `双芯入卷，轻舟再发 · Ship Intel NPU and opt-in AMD support in 0.5.9`
