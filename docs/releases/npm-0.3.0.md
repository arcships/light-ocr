# npm 0.3.0 发布记录

发布日期：2026-07-19<br>
发布 commit：`7665b15122b9e031c4cca16a528d8739694ec632`

版本：`0.3.0`

协议：Apache-2.0

## 面向使用者的变化

- `createEngine()` 默认从 CPU 改为平台 descriptor 驱动的 Auto：macOS arm64 按 `apple → cpu`，Linux x64 与 Windows x64 按 `webgpu → cpu`，macOS x64 使用 CPU。
- macOS 15+ arm64 新增 Direct Core ML。Apple provider 使用已验证的 FP16 路径；M4 有正式设备证据，其他 Apple Silicon 走开放兼容路径但不继承性能承诺。
- Linux x64/Vulkan 与 Windows x64/D3D12 新增 official Native WebGPU Plugin EP。公开 WebGPU precision 只接受 `auto`/`fp32`，并显式限制 `Concat`、`Gather`、`Slice` 的 CPU partition。
- 六包安装结构不变：一个 facade、一个模型包、四个平台 native 包；安装与首次运行不下载 provider、模型或编译原生代码。
- `engine.info.execution` 新增 session placement、provider/runtime/qualification identity、adapter、precision、`deviceValidated` 和 Auto creation trace。

## API 与兼容性

- Node.js `ExecutionProvider` 增加 `auto`、`apple`、`webgpu`；C++ enum 增加 `automatic`、`apple`、`webgpu`。
- 显式 provider 是严格单后端请求，不会静默回退。只有 Auto 可以在创建阶段按 descriptor 锁定的 typed failure 继续下一个候选。
- 旧 `sessionFallback: "cpu"` 返回 `invalid_argument`；继续使用 `sessionFallback: "error"`，或直接省略让 Auto 管理创建候选。
- Apple FP16 与 WebGPU FP32 使用不同的资格路径。WebGPU `precision: "fp16"` 不属于 0.3.0 公共 API。
- macOS x64 发布 smoke 未复现锁定的 Core ML OCR 结果，因此 0.3.0 descriptor 仅暴露 CPU。

## 性能与质量

所有数字都是表中设备上的同机 CPU 对照，不外推到其他设备或 driver。

| Provider 与记录设备 | 已记录的端到端结果 | 质量与 Gate |
| --- | ---: | --- |
| Apple/Core ML，Apple M4 Max | `HELLO 123` 2.300×；XFUND 2.851× | 14 fixtures 通过锁定的 CPU parity 阈值 |
| WebGPU/Vulkan，NVIDIA RTX 5060 Ti | 14-fixture 聚合 P50 5.698×；单项 3.474×–9.299× | 14/14 与 CPU FP32 字节级一致；164/164 Gate |
| WebGPU/D3D12，AMD Radeon 780M | 14-fixture 聚合 P50 2.436×；单项 1.277×–2.982× | 14/14 与 CPU FP32 字节级一致；164/164 Gate |

Apple 结果对比最多 12 intra-op threads 的 `cpu_fast` profile；`HELLO 123` 与 XFUND 的 warm P50 分别从 19.774/943.627 ms 降至 8.599/331.011 ms，宿主 OCR 进程 CPU time 分别降低 95.91%/97.67%。14 个 fixture 的字符相似度为 99.6484%，detection recall 为 100%，平均 matched IoU 为 99.5508%；这是 CPU parity，不是独立 ground-truth accuracy。

WebGPU 聚合值为锁定 14-fixture corpus 的 `sum(CPU P50) / sum(WebGPU P50)`。两份报告还通过 cold start、native C++、memory、placement、strict rejection 和 repeated-lifecycle Gate；Windows lifecycle 最终比预热后基线低 22.9 MiB。

## 发布与验证证据

- [完整 dry-run 29694938140](https://github.com/arcships/light-ocr/actions/runs/29694938140)：四平台构建、Windows 生命周期、确定性模型派生、六包 staging、临时 registry，以及 Node.js 22/24 的八组 package tests 全部成功。
- [npm release run 29695763892](https://github.com/arcships/light-ocr/actions/runs/29695763892)：在相同 commit 上复跑同一 release gate，以 npm 11 + provenance 按依赖优先、facade 最后的顺序把六包发布到 `next`，并完成 registry 与禁网复验。
- [npm promotion run 29696646354](https://github.com/arcships/light-ocr/actions/runs/29696646354)：复用 release artifact，逐包核对公开 registry integrity，再按依赖优先、facade 最后提升到 `latest`。
- 最终公开查询确认六包的 `next` 与 `latest` 均指向 `0.3.0`。

发布前不重复运行真机 benchmark；reviewed Apple/Linux/Windows qualification 由 production lock 精确绑定。完整 dry-run 必须保留，因为它同时验证四平台可安装产物、默认 Auto OCR、显式 provider、Node 22/24、registry 和禁网边界。

## 不可变制品

以下数据来自 release run 保存的 `release-manifest.json`；manifest SHA-256 为 `d13e31d100b5b2d50b8ba47275714bbf7d2fa8521d77fdd2acb78b70f71a83b0`，`gitRevision` 为发布 commit，registry 的 `dist.integrity` 已逐包复核一致。

| Package | Tarball bytes | Unpacked bytes | SHA-256 |
| --- | ---: | ---: | --- |
| `@arcships/light-ocr` | 13,564 | 49,111 | `5204dc33615ec0894a39c8ddc89f67e9d1ea0356a658431eb719bad254ce4fcb` |
| `@arcships/light-ocr-model-ppocrv6-small` | 52,529,117 | 73,594,252 | `80048aec12f89348ace1ca84b34318ae251a7d0d47057d4fa36833476e0dccd8` |
| `@arcships/light-ocr-darwin-arm64` | 12,067,180 | 39,987,242 | `8e98b5bf36de7e9cfd17c626a18ecdf6a1134b30eb1da7364a95b500493f6ad0` |
| `@arcships/light-ocr-darwin-x64` | 13,996,604 | 46,021,373 | `2e0273a0364caaf8dc974ee733aa0eb925dcfc15e441314a3695151b76549550` |
| `@arcships/light-ocr-linux-x64-gnu` | 16,306,936 | 44,149,803 | `49dd721256567bf65bd279b3767ad6968f7dfad20f81ecec323322d5cc071ae0` |
| `@arcships/light-ocr-win32-x64` | 19,298,217 | 47,080,383 | `40249eac4280504a30abf32c20e381911749761ccce3c628b2323bd342d34894` |

Registry integrity：

```text
@arcships/light-ocr
sha512-rTKUW08XHPxxRpPCxcyk4G4OOv/uPUu0VWVn2nzAs1PqDkLGQAvGdmiGGOrUAoc4CjbZwMcgwQsK+Zws4ARfXA==

@arcships/light-ocr-model-ppocrv6-small
sha512-vKUzIzsJSb8/nZ7pv9u9hS4lelaPubgmi/kINPMC1aCydBZovTUSV5NpbDkvVq1rmDww2Xg8rPE5ecCQqA1OVQ==

@arcships/light-ocr-darwin-arm64
sha512-+rKbx8Du6V8t6xjCYLK6vYCdkYezFtxt3cQxfSn/+r6hw5Q8imsqNbyLqM4I7G4twfhKTJUOoadfauBm/hVm3Q==

@arcships/light-ocr-darwin-x64
sha512-VJh+GeLGiNPIWQ1yOAvHBhPFVNd6GLORrG72Z0alNf79JAUx0RWD9N1phHifHmxcseT6VgXEbK6q/d1rktUjpw==

@arcships/light-ocr-linux-x64-gnu
sha512-oxvYcvENpdxxfMVoZQWXwoZ1i0DNFkIZoNJR18gAjm6JVK8loq7UzGpeAuvg1uyd89gQqjaWKlRABLP0DQb9wQ==

@arcships/light-ocr-win32-x64
sha512-1Aih7zeUNfmgLUGwWtJwAQ916QPQjOUS32i8s/v5ijhD38xQRJQ5+WuKiIAstZwsD5RQKxlXxwo6sWMMqv6H8w==
```
