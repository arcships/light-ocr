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
- AMD BF16 recognition backend, model compiler, SDK importer and packaging are implemented. AMD never participates in Auto. Builds containing a matching AMD SDK and compiled models can explicitly select `execution: { provider: "amdnpu" }` or `--provider amdnpu`.
- The default release inputs contain no AMD vendor SDK or actual compiled contexts. Do not claim that the default npm package can execute AMD NPU inference. Supply a `npu-release-sdks` artifact containing `amdnpu/` to include the real SDK/models, or follow the [NPU build guide](../npu-runtime-release.md). Record whether the final release includes AMD.
- Hardware qualification reports are optional for NPU shipping. Unvalidated devices remain `deviceValidated: false`; inventory, hashes, model binding, licenses and existing base-runtime checks remain enforced.
- NPU packages use runtime descriptor 2.1; other packages retain 2.0. Windows, arm64 and musl retain their existing provider routes. Model versions, the `>=22.0.0` Node floor and public API contracts are unchanged.

## Release sequence

1. Merge preparation and select main with Core `0.5.9`.
2. Run `npm release` with `version=0.5.9` and `publish_to_registry=false`. Intel SDK assembly is automatic on Linux x64. An optional `npu_sdk_run_id` supplies additional AMD native libraries and all 20 compiled contexts; AMD stays opt-in.
3. Review eight-platform builds, offline image/PDF smoke and the tarball manifest. Record the source SHA, run ID, 13 new package identities and artifact hashes.
4. Publish to `next` with `publish_to_registry=true` and record registry reinstall results.
5. Run `npm promote` with the publication run ID and `tag=latest`; promote only Small/runtime/native. Document and preview facades stay on `next`.
6. Update the draft release target to the final source commit, attach the manifest/artifact links, and fill publication dates and both release records before publishing `v0.5.9`.

Preparation does not publish npm packages, promote tags or publish the GitHub draft. PR #67 CI is implementation evidence, not the eight-platform 0.5.9 release result. All 13 new package identities are vacant (the complete manifest also reuses two preview model packages). Core/Node were reconfigured and built with version 0.5.9; version closure, lockfile and script syntax checks passed. No new local tests were run; artifact integrity and publication runs remain pending. No NPU hardware qualification is scheduled.

## Rollback

Restore stable tags using the archived 0.5.8 publication artifact (run `36013862673`): Small/native `0.5.8`, runtime `0.1.8`. Handle compatibility/preview tags separately; never overwrite or delete published package versions.

## GitHub Release

Tag: `v0.5.9` (not public while draft).

Name: `双芯入卷，轻舟再发 · Ship Intel NPU and opt-in AMD support in 0.5.9`
