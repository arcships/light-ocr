# @arcships/light-ocr npm Package Design

状态：六包设计与 `0.2.0` lockstep 发布已完成；0.3.0 Apple/WebGPU native superset bundle release candidate 已接入同一流程<br>
更新时间：2026-07-19<br>
Authority：npm 包名、包拆分、依赖关系、内置模型、版本与发布门槛<br>
Node API：[napi-design.md](napi-design.md)<br>
Model contract：[model-bundle.md](model-bundle.md)<br>
Decision：[decisions.md](decisions.md) D105

0.2.0 继续使用本文六包 lockstep 规则，并强校验 schema 1.2、`tiled-v1`、新 bundle ID、minimum package version，以及 native package 中 JPEG/PNG decoder 的 license/SBOM identity。额外的类型、四平台基线和发布证据见 [Tiled Detection 技术设计与验收规格](tiled-design-and-acceptance.md)。这些增量不改变已发布 `0.1.0` 的不可变包内容。

0.3.0 候选不增加第七个包或第二个安装入口。model package 改为
`ppocrv6-small-native-20260719.1` 自包含 superset：所有平台继续使用其中的
ONNX FP32 CPU/WebGPU payload；锁定的 WebGPU FP16 variants 仅作为内部可复现工件保留；
macOS 15+ arm64/x86_64 使用 `open-macos` 策略，均可显式请求 Core ML。`validatedDeviceFamilies` 只标记已有真机证据，不阻塞其他 Mac 的实验兼容。
release workflow 在 Linux 复现内部 WebGPU FP16 ONNX 工件、在 macOS 以哈希锁 Python 3.12 工具链派生固定 Core ML 工件，Linux assemble job 只消费这些已验证 artifacts；公共 WebGPU 只使用 FP32，运行时和 postinstall 都不转换或下载模型。

## 1. 用户契约

v1 的唯一推荐安装入口是：

```bash
npm install @arcships/light-ocr
```

安装完成后，用户不需要另行下载、解压或配置模型：

```ts
import { createEngine } from "@arcships/light-ocr";

const engine = await createEngine();
```

“自带模型”具体表示：

- PP-OCRv6 small 模型作为 `@arcships/light-ocr` 的普通、必需 npm dependency 随安装取得。
- npm 包内直接保存可读取的 bundle 目录，不在首次运行时解压归档。
- `createEngine()` 默认解析该目录并把绝对路径交给现有 native addon。
- engine 创建和识别期间不访问网络，不执行 shell，不运行下载脚本，也不读取 cwd 或用户环境变量来寻找模型。
- `bundlePath` 继续作为显式高级覆盖入口，用于开发、测试或私有 bundle；它不是正常使用的前置配置。

模型会增加 npm 安装流量和磁盘占用，但不会产生安装后的第二次下载。`0.2.0` model tarball 为 26,091,308 bytes，解包后 package 为 31,333,440 bytes；完整 hash 与 registry integrity 见[发布记录](releases/npm-0.2.0.md)。

## 2. 包集合与依赖图

首个 release set 固定为六个公开 scoped packages：

| 包 | 类型 | 内容 | 安装关系 |
| --- | --- | --- | --- |
| `@arcships/light-ocr` | facade | CJS、ESM、TypeScript types、平台与模型解析器 | 用户直接安装 |
| `@arcships/light-ocr-model-ppocrv6-small` | model | 0.2.0 为 CPU bundle；0.3.0 候选为包含同一 FP32 ONNX payload、锁定的内部 WebGPU FP16 派生工件与 Core ML FP16 工件的 `ppocrv6-small-native-20260719.1`、模型 license、可解析 manifest；WebGPU 公共执行 profile 只发布 FP32 | facade 的普通 dependency |
| `@arcships/light-ocr-darwin-arm64` | native | arm64 `.node`、ONNX Runtime dylib、licenses、SBOM、hashes | facade 的 optional dependency |
| `@arcships/light-ocr-darwin-x64` | native | x64 `.node`、ONNX Runtime dylib、licenses、SBOM、hashes | facade 的 optional dependency |
| `@arcships/light-ocr-win32-x64` | native | x64 `.node`、`onnxruntime.dll`、licenses、SBOM、hashes | facade 的 optional dependency |
| `@arcships/light-ocr-linux-x64-gnu` | native | glibc x64 `.node`、ONNX Runtime `.so`、licenses、SBOM、hashes | facade 的 optional dependency |

依赖方向只有两层：

```text
@arcships/light-ocr
├── required: @arcships/light-ocr-model-ppocrv6-small
└── optional, exactly one selected by host
    ├── @arcships/light-ocr-darwin-arm64
    ├── @arcships/light-ocr-darwin-x64
    ├── @arcships/light-ocr-win32-x64
    └── @arcships/light-ocr-linux-x64-gnu
```

模型包不能是 optional dependency。否则 `--omit=optional`、平台筛选或安装器的可选依赖容错会让一次“成功安装”缺少默认模型，违背主包契约。native 包必须是 optional dependencies，因为同一机器只需要一个平台产物。

v1 不提供无模型的 `core`/`lite` 入口，也不允许用户单独拼装 facade、native 和 model 版本。以后若确有服务端瘦包需求，应新建独立产品入口，不能削弱 `@arcships/light-ocr` 的开箱即用保证。

## 3. Facade package

发布清单的关键字段如下：

```json
{
  "name": "@arcships/light-ocr",
  "version": "0.2.0",
  "license": "Apache-2.0",
  "type": "commonjs",
  "main": "./js/index.cjs",
  "module": "./js/index.mjs",
  "types": "./js/index.d.ts",
  "exports": {
    ".": {
      "types": "./js/index.d.ts",
      "import": "./js/index.mjs",
      "require": "./js/index.cjs"
    }
  },
  "files": ["js/", "README.md", "LICENSE", "NOTICE"],
  "engines": { "node": "^22.0.0 || ^24.0.0" },
  "dependencies": {
    "@arcships/light-ocr-model-ppocrv6-small": "0.2.0"
  },
  "optionalDependencies": {
    "@arcships/light-ocr-darwin-arm64": "0.2.0",
    "@arcships/light-ocr-darwin-x64": "0.2.0",
    "@arcships/light-ocr-linux-x64-gnu": "0.2.0",
    "@arcships/light-ocr-win32-x64": "0.2.0"
  },
  "publishConfig": { "access": "public" }
}
```

所有内部 dependency 使用 exact version，不用 `^`、`~`、tag 或 workspace range。源码树中的 `bindings/node/package.json` 暂时保留 `private: true` 防止误发布；release staging 生成的清单必须移除 `private`，保留 `Apache-2.0`，并补全 provenance 信息后才能发布。

Facade 同时导出 CJS、ESM 和 `.d.ts`，但两种模块格式共享同一个 CommonJS native loader 和同一份 environment-scoped addon 实例。native 子路径、模型物理路径和内部 cancel 方法都不作为稳定公共 API。

## 4. 默认模型 package

模型 package 的发布布局固定为：

```text
@arcships/light-ocr-model-ppocrv6-small/
  package.json
  bundle/
    manifest.json
    normalized-config.json
    SHA256SUMS
    det/...
    rec/...
    LICENSES/...
  README.md
  LICENSE
  NOTICE
```

它是纯数据 package，不包含项目运行代码。`package.json.exports` 只开放一个可定位的文件：

```json
{
  "name": "@arcships/light-ocr-model-ppocrv6-small",
  "version": "0.2.0",
  "license": "Apache-2.0",
  "files": ["bundle/", "README.md", "LICENSE", "NOTICE"],
  "exports": {
    "./bundle/manifest.json": "./bundle/manifest.json"
  },
  "publishConfig": { "access": "public" }
}
```

Facade 的共享 CommonJS loader 使用 `require.resolve("@arcships/light-ocr-model-ppocrv6-small/bundle/manifest.json")` 取得物理 manifest 路径，再以其父目录作为 `bundlePath`；不能基于 `process.cwd()`。package 存放解包后的十个 bundle 文件，而不是 `.tar`；Core 的安全 directory loader 和 hash 验证因而保持不变。

模型 package 的 payload `license` 是 bundle 已验证的 `Apache-2.0`，并包含 PaddleOCR 固定 revision 的完整 license 和 model notice。该纯数据 package、项目 facade 和 native packages 均按 Apache-2.0 发布。model package 还必须写入机器可读元数据：bundle ID、schema version、PaddleOCR revision、det/rec revision 和生成源 lock digest。

Facade 在创建 engine 前读取 manifest，并核对 `bundleId` 是否等于本 release set 预期值；Core 随后验证 `SHA256SUMS`、manifest inventory 和每个 payload SHA-256。npm lockfile integrity 保护 package tarball，Core hash 保护安装后的模型内容，两层校验不能互相替代。

## 5. Native platform packages

每个 native package 只包含一个平台组合，使用 npm 的平台筛选字段。例如 Linux 包：

```json
{
  "name": "@arcships/light-ocr-linux-x64-gnu",
  "version": "0.2.0",
  "license": "Apache-2.0",
  "main": "./native/light_ocr_node.node",
  "exports": { ".": "./native/light_ocr_node.node" },
  "os": ["linux"],
  "cpu": ["x64"],
  "libc": ["glibc"],
  "files": ["native/", "licenses/", "sbom.spdx.json", "artifact-hashes.json", "README.md", "LICENSE", "NOTICE"],
  "engines": { "node": "^22.0.0 || ^24.0.0" },
  "publishConfig": { "access": "public" }
}
```

macOS 包使用 `os: ["darwin"]` 和对应 `cpu`；Windows 包使用 `os: ["win32"]`、`cpu: ["x64"]`。package root 只导出 `native/light_ocr_node.node`，facade 的共享 CommonJS loader 因而可直接 `require(packageName)`。macOS/Linux 动态库使用相对 loader path，Windows DLL 与 `.node` 放在固定相邻目录。package 不得依赖消费者预装 ONNX Runtime、OpenCV 或编译工具链。

Facade 只按固定映射加载 package：

| `process.platform` | `process.arch` | package |
| --- | --- | --- |
| `darwin` | `arm64` | `@arcships/light-ocr-darwin-arm64` |
| `darwin` | `x64` | `@arcships/light-ocr-darwin-x64` |
| `win32` | `x64` | `@arcships/light-ocr-win32-x64` |
| `linux` + glibc | `x64` | `@arcships/light-ocr-linux-x64-gnu` |

未知组合以 `unsupported_platform` 拒绝 `createEngine()`。已支持组合但 native package 缺失时，以 `package_load_failed` 拒绝，并提示重新安装且不要使用 `--omit=optional`；不能静默源码编译或在线下载二进制。开发环境仍可显式设置 `LIGHT_OCR_NODE_BINARY`，但 published README 不把它当作生产配置。

### 5.1 硬件加速的分发约束

当前源码已经为 Linux x64 glibc 与 Windows x64 实现 Native WebGPU qualification payload：Linux package 自带 ORT Core 1.24.4 与 official WebGPU plugin 0.1.0；Windows 还自带 plugin 所需的 `dxcompiler.dll`、`dxil.dll`。schema 2 runtime descriptor 从实际 staging 文件生成，逐文件记录 bytes/SHA-256、provider library、ORT/plugin ABI、qualification identity 与 `webgpu → cpu` Auto policy；共享 loader 拒绝缺失、额外、hash 漂移或 symlink payload，并从 sterile cwd 加载。当前 lock 仍为 `development-pending-device-validation`，因此这些制品只允许 `--qualification-build` staging，不能进入普通 npm release。

后续 CoreML、DirectML、OpenVINO、TensorRT、VitisAI、QNN 或其他 provider 也不得改变本节的用户契约：正常用户仍只运行 `npm install @arcships/light-ocr`，不能被要求另装或配置 ONNX Runtime、Windows ML framework runtime、CUDA、TensorRT、OpenVINO、Ryzen AI/VitisAI、QNN SDK、Python 或编译工具链。正常操作系统和硬件 driver 是唯一允许的系统前置条件；Windows official runtime 依赖的 Microsoft Visual C++ 2015-2022 x64 系统 runtime 需作为平台前置条件明确说明。

加速 payload 遵守以下规则：

- facade 继续按 OS/CPU 自动选择 native platform package；provider 探测和选择发生在 native package 内部。
- runtime、EP/plugin、provider-specific model/context、license、SBOM 和 compatibility manifest 必须随同一 exact-version release set 取得。
- npm 不支持按 Intel/NVIDIA/AMD GPU vendor 筛选 optional dependency。一个厂商 payload 如果进入默认 Windows package，所有 Windows x64 安装都会承担其下载和磁盘成本，因此必须独立通过用户加权收益与 package-size Gate。
- provider payload 可以物理包含在 native tarball，也可以由 native package 依赖内部、同版本、同平台 shard；两种形式都必须在一次顶层安装中自动完成，内部 shard 不成为用户安装入口。
- 禁止 install/postinstall 按硬件下载 runtime、现场编译 provider、调用厂商安装器，或从 PATH、注册表和全局 SDK 目录借用未锁定 DLL。
- 发布 Gate 必须在未安装厂商 SDK/runtime 的干净目标系统上，仅使用 npm tarball 和正常硬件 driver 完成离线安装、engine 创建和真实推理。

因此“一个公共入口、完全离线、较小 Windows package、同时获得所有厂商最优性能”不能被当作无成本组合。默认 native package 只接收通过收益、质量、体积、许可和维护 Gate 的 provider；没有通过时保留稳定 CPU/广覆盖 backend，而不是把依赖安装责任交给用户。该规则不改变已发布 `0.2.0` 的六包内容，只约束后续加速 release set。

## 6. 公共 API 与默认解析

目标 TypeScript API 是：

```ts
export type BuiltInModel = "ppocrv6-small";

export interface CreateEngineOptions {
  readonly model?: BuiltInModel;
  readonly bundlePath?: string;
  // existing tuning and queue options remain unchanged
}

export function createEngine(options?: CreateEngineOptions): Promise<OcrEngine>;
```

解析规则按顺序固定：

1. `options` 缺失，或既无 `model` 也无 `bundlePath`：使用 `ppocrv6-small`。
2. `model: "ppocrv6-small"`：使用内置模型 package。
3. `bundlePath`：必须是现有绝对目录，使用调用方 bundle。
4. 同时传 `model` 和 `bundlePath`：以 `invalid_argument` 拒绝，避免优先级歧义。
5. JS facade 解析出绝对目录后才调用 native `createEngine`；native 接口不新增模型发现或 registry 逻辑。

模型 package 缺失、exported manifest 无法解析、package bundle ID 不匹配属于 `package_load_failed`。目录存在但 bundle 内容或 hash 不合法，继续使用 Core 的 `invalid_model_bundle` 或 `model_integrity_failed`。

## 7. 版本策略

六个 package 使用 lockstep SemVer：同一次 release set 的版本完全相同。Facade 对 model/native 始终使用 exact version，因此不能加载另一批次的 Core、ORT 或模型。

模型内容身份与 npm 版本分离：

- npm version 表示 package release set，例如 `0.2.0`。
- bundle ID 表示 OCR 模型与配置身份，例如 `ppocrv6-small-onnx-20260714.2`。
- 任何模型 bytes、normalized config、dictionary 或 manifest 变化都创建新 bundle ID，并发布新的完整 release set。
- 只修改 README 不需要创建新 bundle ID，但仍需要新的 npm version。

首批未完成四平台矩阵时只发布到 `next` tag。所有 release gates 通过后，才允许把同一已验证版本提升为 `latest`；不能用重新打包的同版本覆盖已发布 package。

## 8. 构建与发布流程

仓库不提交第二份 31 MB 模型副本。release staging 从三个已验证来源组装 packages：

```text
bindings/node/js + facade manifest template
models/generated/ppocrv6-small-onnx-20260714.2
reports/release/<platform> native artifacts
                         ↓
dist/npm/<six staging directories>
```

`dist/npm` 是临时生成目录，不是源码 authority。打包器必须使用 `files` allowlist，并拒绝 source、test fixture、cache、绝对路径、symlink、额外动态库和未登记文件。

发布顺序必须是原子可恢复的：

1. 生成六个 staging directories，验证 package metadata 和文件 inventory。
2. 对每个目录执行 `npm pack --dry-run`，再生成 `.tgz` 并记录 filename、bytes、SHA-256 和 npm integrity。
3. 把六个 `.tgz` 放入一次性本地 npm registry；在无仓库文件可见的临时目录只安装 facade，验证 exact dependency graph、平台筛选和真实模型。
4. 先发布 model package 和四个 native packages 到 `next`。
5. 确认五个依赖都可读取且 metadata 正确后，最后发布 facade 到 `next`。
6. 完整 release evidence 归档后再移动 dist-tag。

`@arcships` scope 必须已由发布账号或组织控制。scoped package 的发布清单固定 `publishConfig.access: public`，发布流程也显式使用 public access，避免 scope 默认私有策略造成误配置。

2026-07-14 的 0.1.0 首发证明发布身份控制 `@arcships` scope；同日 0.2.0 workflow 继续以 npm provenance 发布六包，经独立 promotion job 逐包核对 registry integrity 后提升到 `latest`。

相关 npm 官方行为依据：

- [Creating and publishing scoped public packages](https://docs.npmjs.com/creating-and-publishing-scoped-public-packages/)
- [`package.json`: files、optionalDependencies、os、cpu 与 libc](https://docs.npmjs.com/cli/v11/configuring-npm/package-json/)
- [About scopes](https://docs.npmjs.com/about-scopes/)

## 9. Release gates

一个 npm release candidate 至少满足：

- 四个 native packages 分别在目标 OS/arch 原生构建；Node.js 22/24 加载、真实 OCR、AbortSignal、close、GC 和 worker teardown 全绿。
- `npm pack --dry-run` inventory 与 allowlist 完全一致；package 内没有源码、缓存、测试图像、原始上游 archive 或绝对构建路径。
- Facade 的 ESM、CJS 和 TypeScript compile tests 均通过。
- 从一次性本地 registry 执行 `npm install @arcships/light-ocr` 后，`createEngine()` 不传 `bundlePath` 即完成真实 PP-OCRv6 识别。
- `--ignore-scripts` 安装后行为相同，证明没有 install/postinstall 下载或编译依赖。
- 在网络禁用环境里，对已经安装好的 package 重复 create/recognize/close 成功。
- 模型 package 的 bundle 文件总字节、manifest、`SHA256SUMS` 和 bundle ID 与 `models/bundles.lock.json` 对应生成物一致。
- native package 的 addon、ORT library、artifact hashes、license inventory 和 SPDX SBOM 一致。
- 从干净 release commit 生成并记录六个 npm tarballs 的 SHA-256、registry integrity、dist-tag 和 CI artifact URL。
- 仓库根 `LICENSE`/`NOTICE`、facade/native package 的 SPDX `license` 字段与 Apache-2.0 一致。

## 10. v1 明确不做

- install/postinstall 时从 GitHub、对象存储或 Paddle 官方地址下载模型或二进制。
- 首次 `createEngine()` 时下载、解压或自动更新模型。
- 将模型直接复制进四个 native packages；这会造成四份重复分发。
- 无模型 facade、按需语言包、tiny/medium、orientation 或 GPU packages。
- 源码编译 fallback、`node-gyp` fallback 或消费者系统 ORT fallback。
- Electron、Bun、Deno、Linux musl/arm64、Windows arm64 支持声明。
- Yarn Plug'n'Play/zip archive 兼容声明；v1 release gate 以官方 npm 的物理安装目录为准。
