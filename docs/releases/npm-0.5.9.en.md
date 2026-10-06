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

- Linux x64 glibc release builds include pinned OpenVINO 2026.4.0 by default. Auto tries Intel NPU before WebGPU and CPU, running detection and 20-bucket recognition when supported. Creation-time device/model/driver exclusions advance to the next provider.
- Linux x64 glibc builds include Ryzen AI 1.8 native deployment libraries and 20 real BF16 recognition contexts bound to Small 0.3.4. AMD stays outside Auto; explicitly select `execution: { provider: "amdnpu" }` or `--provider amdnpu`. Detection uses CPU; STX/KRK devices are the target. Tiny/Medium require separately compiled contexts.
- The workflow fetches the immutable AMD deployment archive automatically. End users need vendor drivers, without installing the full SDK or Python.
- Hardware qualification reports are optional for NPU shipping. Unvalidated devices remain `deviceValidated: false`; inventory, hashes, model binding, licenses and existing base-runtime checks remain enforced.
- NPU packages use runtime descriptor 2.1; other packages retain 2.0. Windows, arm64 and musl retain their existing provider routes. Model versions, the `>=22.0.0` Node floor and public API contracts are unchanged.

## Release sequence

1. Merge preparation and select main with Core `0.5.9`.
2. Run `npm release` with `version=0.5.9` and `publish_to_registry=false`. Both SDKs are prepared automatically on Linux x64. An optional `npu_sdk_run_id` overrides both SDKs and must contain `openvino/` and `amdnpu/`. Draft asset downloads use the workflow GitHub token. AMD stays opt-in.
3. Review eight-platform builds, offline image/PDF smoke and the tarball manifest. Record the source SHA, run ID, 13 new package identities and artifact hashes.
4. Publish to `next` with `publish_to_registry=true` and record registry reinstall results.
5. Run `npm promote` with the publication run ID and `tag=latest`; promote only Small/runtime/native. Document and preview facades stay on `next`.
6. Update the draft release target to the final source commit, attach the manifest/artifact links, and fill publication dates and both release records before publishing `v0.5.9`.

Preparation does not publish npm packages, promote tags or publish the GitHub draft. PR #67 CI is implementation evidence, not the eight-platform 0.5.9 release result. All 13 new package identities are vacant (the complete manifest also reuses two preview model packages). Intel+AMD Core/Node were reconfigured and built with version 0.5.9; version closure, lockfile and script syntax checks passed. No new local tests were run; artifact integrity and publication runs remain pending. All 20 AMD recognition contexts were actually compiled; 20 native libraries and 56 inventory files were imported. No AMD inference or NPU hardware qualification was performed.

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
