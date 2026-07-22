# light-ocr CLI 设计草案

Status: Draft（2026-07-21，落地前细化设计）<br>
Authority: N1 CLI、结果契约、ROI、detect-only 出口与 Agent Skill 的实现与契约规范<br>
Requirements: [requirements.md](requirements.md)<br>
Roadmap: [roadmap.md §5 N1](roadmap.md)<br>
Architecture: [architecture.md](architecture.md)

本草案是 [roadmap §5](roadmap.md) 引用的「落地前细化设计」。它不替代 [decisions.md](decisions.md) 中的 D106 决策记录，而是 D106 的前置设计材料。所有标记 **D-N1-x** 的条目为待维护者确认的开放决策。

## 1. 范围与现状

N1 的目标是让普通用户和 Agent 无需编写 Node.js 集成代码，即可从本地图片获得稳定文本、置信度和坐标，并建立 PDF、Layout 和多模型都能复用的版本化结果契约。

### 1.1 现有可复用基础

对照当前源码（`0.3.1`），N1 的底层能力已经具备：

| 能力 | 现状 | 来源 |
| --- | --- | --- |
| `createEngine` / `OcrEngine.recognize(RawImage)` / `recognizeEncoded(Uint8Array)` / `info` / `close` | 已发布 | [packages/runtime/src/index.d.ts](../packages/runtime/src/index.d.ts) |
| `OcrResult.lines[].box`（4 点 quad）、`imageWidth/Height`、`modelBundleId`、`timingUs`、`diagnostics` | 已发布 | 同上 |
| `EngineInfo.execution`（requested/actual provider chain、device、precision、capabilities、selectionTrace） | 已发布 | 同上 |
| `OcrError` / `CoreErrorCode` / `AdapterErrorCode` / `CreationTrace` | 已发布 | 同上 |
| JPEG/PNG 受限 decode（stb_image，带内存预算） | 已发布 | [encoded_image.cpp](../bindings/node/src/encoded_image.cpp) |
| 四平台 prebuild、离线 bundle 解析、provider Auto | 已发布 | [implementation-status.md](implementation-status.md) |

### 1.2 本节点需新增的能力

| 能力 | 现状 | N1 交付 |
| --- | --- | --- |
| `light-ocr` CLI bin | `package.json` 无 `bin` 字段 | 新增 bin + 子命令（recognize/detect/info） |
| `schemaVersion` 与 `DocumentResult` envelope | `OcrResult` 是单图底层契约，无 envelope | CLI 层新增 envelope |
| EXIF orientation 修正 | 全仓 0 匹配，stb 不读 EXIF | 新增 EXIF 解析 + 像素变换 + `appliedTransforms` |
| `--region` ROI | `recognize` 无 region 参数 | 新增输入侧区域约束（recognize/detect 均可带） |
| `detect` 子命令 | Core `Engine` 公共接口仅 `recognize`/`info`/`close`，无 `detect` | Core 新增 `detect()` 公共方法（不新增算法）+ Node 绑定 + `detect` 子命令 |
| Agent Skill | `.agents/skills/` 不存在 | 新建 `.agents/skills/local-ocr/SKILL.md` |

## 2. 分发形态与命令结构

N2 已把 CLI 公共实现迁到 `packages/runtime/src/cli.cjs`，三个 facade 只保留各自的命令名、版本和模型 profile 配置。`@arcships/light-ocr` 仍唯一拥有 `light-ocr`；Tiny/Medium 分别使用 `light-ocr-tiny` / `light-ocr-medium`。实现只用 Node 内置模块，零运行时依赖，符合禁网安装与 `--ignore-scripts` 要求；argv 解析继续使用手写最小 parser（D-N1-2）。

### 2.1 子命令结构

N1 面向 Agent，采用子命令结构而非扁平 flag：Agent 先从顶层动词锁定意图，再看该动词下的 flags，互斥约束收窄到子命令内。代价是 `recognize`/`detect`/`info` 三个动词的 help 与退出码需分别维护，但换来 Agent 可逐步理解的入口结构。

```text
light-ocr recognize <path|--stdin> [flags]   # 默认 OCR：detection + recognition
light-ocr detect     <path|--stdin> [flags]   # 只检测：检测框，不识别
light-ocr info       [--model-info|--version] # 诊断，不读图
light-ocr [recognize] <path> [flags]          # 隐式 recognize：不带子命令 = recognize
light-ocr --help [subcommand]                 # 分层 help
```

约束：

- `light-ocr image.png` 隐式等价 `light-ocr recognize image.png`，保持首次使用直觉与向后兼容；
- 每个 flag 只挂在自己相关的子命令下（见 §2.2），互斥约束在子命令内收敛；
- `info` 子命令不接受 path/`--stdin`，不接受 `--format`；`--model-info` 与 `--version` 作为 `info` 的 flags；
- N3 文档入口另起 `light-ocr-document` bin（[roadmap §3.1](roadmap.md)），与 N1 子命令并列，不并入 `light-ocr`。

> **与 [roadmap §5.2](roadmap.md) 的偏离说明**：roadmap 原文写「`--model-info` 和 `--version`」为顶导 flag，本设计改为 `info` 子命令以保持与 `recognize`/`detect` 的结构统一；语义等价。该偏离在 D106 决策中记录。

## 3. CLI 命令面

第一版保持命令面小而稳定（[roadmap §5.2](roadmap.md)）：

```bash
# 默认 OCR（隐式 recognize）
light-ocr image.png --format json
light-ocr image.png --format text
light-ocr image.png --format jsonl

# 显式 recognize（等价于隐式）
light-ocr recognize image.png --format json

# 区域识别（ROI）
light-ocr recognize image.png --region 100,80,640,320 --format json

# stdin
cat image.png | light-ocr recognize --stdin --type image/png --format json

# detect 子命令：只检测，不识别（输出固定 JSON，不暴露 --format）
light-ocr detect image.png
light-ocr detect image.png --crop
light-ocr detect image.png --region 100,80,640,320 --crop

# info 子命令：诊断与版本，不读图（--model-info 与 --version 互斥）
light-ocr info --model-info
light-ocr info --version

# schema 版本（recognize/detect 均可）
light-ocr recognize image.png --schema-version 1 --format json

# 执行后端（与 Node API execution 契约等价）
light-ocr recognize image.png --provider auto --format json
light-ocr detect image.png --provider webgpu
```

### 3.1 各子命令的 flags

每个 flag 只挂在自己相关的子命令下。第一版只暴露 Agent/用户真实高频的入口层选项；provider 内部实现细节（session fallback、cpu partition、precision、detection strategy、max-side）不透传成 CLI flag，由 runtime 按 provider 默认处理。需要这些内部字段时用 Node API 或 `info --model-info`。

#### `recognize`（默认 OCR：detection + recognition）

默认 help 顶部（高频）：

| Flag | 取值 | 语义 |
| --- | --- | --- |
| `<path>` 位置参数 | 文件路径 | 必须与 `--stdin` 二选一 |
| `--stdin` | flag | 从 stdin 读取 bytes；需配合 `--type` |
| `--type` | image/png \| image/jpeg | stdin 的 mediaType |
| `--format` | json \| jsonl \| text | 默认 `json` |
| `--region` | `x,y,w,h` | pageSpace 轴对齐矩形，整数像素；详见 §7 |
| `--no-exif` | flag | 关闭默认 EXIF orientation 修正；详见 §6 |
| `--provider` | auto \| cpu \| apple \| webgpu | 映射 `execution.provider` |
| `--schema-version` | 1 | 请求精确输出 schema；不支持则稳定失败 |

`--help` 第二层（高级）：

| Flag | 取值 | 语义 |
| --- | --- | --- |
| `--score-threshold` | number | `recognitionScoreThreshold`；改它直接影响精度/召回，默认值已校准 |
| `--no-color` | flag | 显式关闭 stderr 彩色；非 TTY 或 `NO_COLOR` 已默认关闭 |

`--quiet`：stderr 只输出错误，不输出进度/警告。归入默认 help 顶部（行为开关，Agent 常用）。

**不暴露为 CLI flag 的字段**（内部固定默认或由 provider 决定）：

- `sessionFallback`：单值枚举（仅 `error`），0.3.0 定为迁移期遗留，CLI 无意义；
- `cpuPartition`：provider qualification 内部细节，按 provider 默认 `allow`；
- `precision`：`fp16` 仅 Apple、WebGPU 只接受 `auto/fp32`，选错直接失败，价值极低；
- `detectionStrategy`：`tiled`/`upstream-exact` 是开发/parity 验证策略，普通用户用 `bounded`，大图自动 tiled；
- `maxSide`：影响精度/速度 tradeoff，内部用校准默认；
- `includeDiagnostics`：`diagnostics` 字段重且面向开发者调试 provider placement，Agent/用户消费不了，需要时用 Node API 或 `info --model-info`。

#### `detect`（只检测：检测框，不识别）

| Flag | 取值 | 语义 |
| --- | --- | --- |
| `<path>` / `--stdin` / `--type` | 同 `recognize` | 输入一致 |
| `--region` | 同 `recognize` | 输入侧区域约束，与 detect 正交可组合 |
| `--no-exif` / `--provider` / `--schema-version` / `--quiet` | 同 `recognize` | 行为与后端一致 |
| `--crop` | flag | 每框附 PNG crop bytes；详见 §9 |
| `--no-color` | 同 `recognize` | 高级，help 第二层 |

`detect` 子命令不暴露 `--format`：detect 输出永远是结构化 JSON（box quad + score + 可选 crop），`text` 格式无意义（无文字可输出）。从结构上消除 `--format text` + `detect` 的失败路径，不靠运行时报错。`--score-threshold` 不在 `detect` 下（detect 不做 recognition，无识别阈值概念；detection 置信度内部用校准默认）。

#### `info`（诊断，不读图）

| Flag | 取值 | 语义 |
| --- | --- | --- |
| `--model-info` | flag | 输出 `EngineInfo` JSON（含 coreVersion、modelBundleId、execution、capabilities、limits） |
| `--version` | flag | 输出 npm/core/model 版本三元组 |

`info` 子命令约束：

- 不接受 `<path>`、`--stdin`、`--type`、`--format` 及任何 OCR/执行 flags；传入报 `invalid_argument`；
- `--model-info` 与 `--version` **互斥**，不可同时传入：`--version` 的三元组是 `--model-info` 的子集（`coreVersion`、`modelBundleId` 已在 `EngineInfo` 内），合并会重复字段；需要完整信息用 `--model-info`，只需快速版本探测用 `--version`。

目录递归、glob、watch mode、交互式 UI、远程 URL 不进入第一版；批量调用先通过 shell + JSONL 组合完成（[roadmap §5.2](roadmap.md)）。

## 4. 分层 help

子命令结构天然分层，Agent 可逐步理解：

1. `light-ocr --help`：列出三个子命令 + 各自一句话职责 + 一个完整示例；
   ```
   Commands:
     recognize <path|--stdin>   Recognize text in an image (default action)
     detect     <path|--stdin>  Detect text regions only, no recognition
     info       [--model-info|--version]  Show engine/version info without reading images
   ```
2. `light-ocr <subcommand> --help`：该子命令的全部 flags 分组（输入/输出/能力/执行/资源/行为）；
3. `light-ocr <subcommand> --<flag>=help`（如 `light-ocr recognize --region=help`）：该 flag 的精确语义、坐标系、失败行为与示例。

Agent 与脚本应使用 `info --model-info`、`info --version` 和固定 `--schema-version`，不解析 help 文本。

## 5. stdout/stderr 严格分离

这是 N1 的硬约束（[roadmap §5.2](roadmap.md)），不可回退：

- **stdout**：只承载机器结果（json / jsonl / text 机器输出、`--model-info` 的 JSON、`--version` 的版本串）。成功时 stdout 必须可被 `JSON.parse` 或确定管道消费；
- **stderr**：日志、warnings、diagnostics 的人类文本、进度、`--include-diagnostics` 的人类可读摘要、usage error 提示；
- 任何混合 stdout 的人类提示一律视为缺陷；
- `--quiet` 只压制 stderr 的非错误输出，不改 stdout 契约；
- `--no-color` 控制 stderr ANSI；CI 环境检测（`NO_COLOR`、非 TTY）默认关闭彩色，D-N1-8 确认检测策略。

文本格式（`--format text`）按行输出识别文本，stdout 仍只含文本行，不带坐标/置信度；需要坐标必须用 `--format json`。

## 6. 坐标与 EXIF

### 6.1 坐标词汇表（N1 冻结，后续只扩展）

沿用 [roadmap §3.3](roadmap.md)：

| 术语 | 定义 |
| --- | --- |
| `sourceSpace` | encoded source 方向修正前的固有坐标，仅用于记录 identity 与 transform |
| `pageSpace` | 所有 v1 `line.box`、Layout box、`--region` 的 canonical space；左上原点，x 向右 y 向下，单位为方向修正后 pixel |
| `appliedTransforms` | source→page 有序变换记录：EXIF orientation、crop、raster scale |

### 6.2 EXIF orientation

[roadmap §5.5](roadmap.md) 要求对 encoded JPEG 默认应用可验证的 EXIF orientation 修正，修正后图片定义为 `pageSpace`，结果记录完整 `appliedTransforms`。

实现约束：

- stb_image 不解析 EXIF，需在 decode 后、送入 Core 前新增独立 EXIF 解析（JPEG APP1 segment）与像素变换。实现方式在 D-N1-5 决策（自带最小 EXIF 解析器 vs 换用支持 EXIF 的 decode 库）；
- PNG 不含 EXIF orientation（可含 `eXIf` chunk，首版按无变换处理）；
- `--no-exif` 关闭修正，`appliedTransforms` 仍记录「未应用」状态；
- raw-pixel API（`recognize(RawImage)`）继续由调用者负责方向，传入像素直接定义 `pageSpace`；
- 修正后的 `imageWidth/Height` 与 `line.box` 全部落在 `pageSpace`。

### 6.3 appliedTransforms 结构

```json
"appliedTransforms": {
  "exifOrientation": 6,
  "exifApplied": true,
  "sourceWidth": 4000,
  "sourceHeight": 3000,
  "pageWidth": 3000,
  "pageHeight": 4000
}
```

## 7. ROI 语义

[roadmap §5.3](roadmap.md)：ROI 是输入侧的区域约束，不是 Layout 替代品。

- 第一版只接受位于方向修正后完整有效页面 `pageSpace` 内的轴对齐矩形 `--region x,y,width,height`（整数像素）；
- 在进入完整 OCR pipeline 前限制检测/识别范围；
- 返回的 quad 坐标重新映射到完整有效页面的 `pageSpace`（不是 ROI 局部坐标）；
- 非法、空或越界区域返回 `invalid_argument`，**不隐式 clamp**；部分相交也返回 `invalid_argument`（[roadmap §3.3](roadmap.md)）；
- ROI 仍受像素、临时内存、candidate 和 timeout 上限约束；
- `--region` 与 `detect` 子命令可组合：限制检测范围 + 只输出检测框。

ROI 的实现位置在 D-N1-4 决策（adapter 层在 decode 后裁剪像素，还是 Core 在 detection 前接收 region）。倾向 adapter 层裁剪：保持 Core 边界不变，EXIF 修正后裁剪天然落在 `pageSpace`，与坐标契约一致。

## 8. detect 子命令

[roadmap §5.4](roadmap.md)：detection 在 Core 中本就是独立 stage。此出口只把已有能力暴露为公共入口，不新增算法，不改 recognition 语义。

- `detect` 子命令仅运行 detector，输出检测框（与 OCR `line.box` 相同的 `pageSpace` quad 契约），不触发 recognition；
- `detect --crop` 可选返回每个区域的 PNG crop，与检测框 index 对齐，便于喂给下游模型、版面分析、计数或 redaction；
- 与 ROI 互补不重叠：ROI 是输入侧区域约束（`--region`，recognize/detect 均可带），detect 是输出侧能力裁剪；`detect image.png --region 100,80,640,320 --crop` = 只在该矩形内检测并附 crop；
- 不是 Layout 替代：只给原始检测框，不附加 region label、阅读顺序或语义分类。

实现要求：Core `Engine` 当前公共接口仅 `recognize`/`info`/`close`，**无 detection-only 公共方法**。需在 Core 新增虚函数 `Engine::detect(ImageView, DetectOptions) -> Result<DetectionResult>`，内部复用现有 detection stage，跳过 crop+recognition。这是 N1 里唯一的 Core 改动，必须在 D-N1-3 锁定方法签名、返回结构（box quad + score，可选 crop bytes）与 CoreErrorCode 映射后再实现。Node addon 增加 `detect` 绑定，JS `OcrEngine.detect()` 对称暴露。

## 9. 结果契约（schemaVersion=1）

[roadmap §3.2](roadmap.md)：`OcrResult` 继续作为单张已解码图片的底层语义契约，CLI 在其外部增加文档级 envelope。

### 9.1 DocumentResult envelope（v1）

```json
{
  "schemaVersion": 1,
  "source": {
    "kind": "image",
    "mediaType": "image/jpeg",
    "identity": { "path": "image.png" },
    "appliedTransforms": { "exifOrientation": 6, "exifApplied": true, "..." : "..." }
  },
  "pages": [
    {
      "index": 0,
      "width": 3000,
      "height": 4000,
      "coordinateSpace": "pageSpace",
      "structure": "ocr-order",
      "lines": [
        {
          "id": "L0",
          "text": "...",
          "confidence": 0.987,
          "box": [ { "x": 0, "y": 0 }, { "x": 100, "y": 0 }, { "x": 100, "y": 30 }, { "x": 0, "y": 30 } ]
        }
      ],
      "modelBundleId": "ppocrv6-small-...",
      "timingUs": { "..." : "..." }
    }
  ]
}
```

`detect` 子命令输出下 `pages[0].lines` 替换为 `detections[]`，`structure: "detect"`：

```json
"detections": [
  {
    "id": "D0",
    "score": 0.92,
    "box": [ { "x": 0, "y": 0 }, { "x": 100, "y": 0 }, { "x": 100, "y": 30 }, { "x": 0, "y": 30 } ],
    "crop": "base64...（仅 --crop 时出现）"
  }
]
```

`--crop` 的 PNG crop bytes 以 base64 编码出现在每个 detection 的 `crop` 字段，与检测框 `id`/index 对齐。crop 编码方式（base64 vs 二进制 sidecar）在 D-N1-3 锁定。

### 9.2 版本与稳定性

- `schemaVersion` 是整数主版本。兼容新增保持 v1；删除、重命名或语义变化必须增加版本；
- `--schema-version 1` 请求精确 schema，不支持返回 `invalid_argument`（不静默降级）；
- 结果本身始终携带 `schemaVersion`；
- `OcrLine.id` 是 CLI envelope 新增的稳定 line ID（`L{index}`），用于未来 Layout region 关联与 Markdown 追溯；底层 `OcrResult.line` 不变。

### 9.3 JSONL 分页语义

[roadmap §3.3](roadmap.md)：

- 单张图片表现为 `pages[0]`；JSONL 对图片输出一条 page record（即 envelope 内的单个 page）；
- 每条 page record 带 document identity、page index 和 `status`（`ok` / `error`）；
- 中途取消或失败时，已完成记录保持有效，stderr 给出终态，进程返回非零 exit code；
- `--format text` 不使用 JSONL，按行输出纯文本。

## 10. 退出码表

稳定 exit code，区分输入、能力、资源、模型和内部错误（[roadmap §5.2](roadmap.md)）。映射现有 `OcrErrorCode`，最终码在 D-N1-7 锁定：

| exit | 类别 | 触发 code | 含义 |
| --- | --- | --- | --- |
| 0 | 成功 | — | 正常输出 |
| 64 | usage | —（参数解析失败、缺文件、互斥 flag 冲突） | 命令行用法错误 |
| 65 | 输入 | `invalid_argument` | ROI 越界、不支持 flag 组合、`--schema-version` 不支持 |
| 66 | 输入 | `invalid_image` / `unsupported_pixel_format` | 图片不可解码或不支持格式 |
| 67 | 能力 | `unsupported_capability` | `--detect-only` 时 rec 不可用等 |
| 68 | 模型 | `invalid_model_bundle` / `unsupported_model` / `model_integrity_failed` | bundle 损坏或 hash 不符 |
| 69 | 资源 | `resource_limit_exceeded` | 超像素/内存/timeout 上限 |
| 70 | 环境/包 | `runtime_initialization_failed` / `package_load_failed` / `unsupported_platform` / `bundle_io_failed` / `adapter_unavailable` / `package_corrupt` | runtime/包加载失败 |
| 71 | 运行 | `inference_failed` / `postprocess_failed` | 推理或后处理失败 |
| 72 | 内部 | `internal_error` / `environment_closing` / `queue_full` | 内部错误或引擎已关闭 |

D112 Auto 创建期可跳过原因（`adapter_unavailable` 等）通过 `creationTrace` 在 stderr 报告，最终仍按对应 exit code 退出。exit code 表一旦发布即为 stable surface，后续只允许新增码，不重排现有码（[roadmap §2.6](roadmap.md)）。

## 11. Agent Skill

[roadmap §5.6](roadmap.md)：仓库内 `.agents/skills/local-ocr/SKILL.md`，是 CLI 的薄工作流层，不实现识别/坐标/schema 逻辑。内容至少覆盖：

- 何时使用 OCR，而不是让多模态模型猜测小字；
- 如何选择全文、ROI、text、JSON 与 diagnostics；
- 如何处理低置信度、空结果、超限和 unsupported capability；
- 如何只读取必要页面或区域，避免无界批处理；
- 如何引用文字及坐标，避免把推断写成 OCR 原文；
- 可执行 CLI 示例和小型验证脚本。

验证稳定后再打包为可安装 Plugin；本地文件 OCR 暂不需要 MCP server（[roadmap §13](roadmap.md)）。

## 12. Agent 友好性 checklist

- stdout 始终可被 `JSON.parse`（`--format json`）或逐行消费（`--format jsonl`/`text`）；
- 退出码与错误类别一一对应，Agent 可据此决定重试/放弃/换输入；
- `info --model-info` 与 `info --version` 不读图、不触发 model load 之外的计算，可安全探测；
- `--schema-version` 固定输出，不随默认漂移；
- 任何 failure 路径 stderr 给出 `OcrError.code` + 人类消息 + 可选 detail，stdout 为空；
- ROI 越界、空图、unsupported capability 均稳定失败，不产出伪结果。

## 13. 实施顺序

建议按依赖与风险递增分步交付，每步可独立验证：

1. **CLI bin 骨架 + 子命令 dispatch + 基本识别**：`recognize`（含隐式默认）、文件/stdin → `--format json|text`、`info --model-info`/`info --version`、stdout/stderr 分离、exit code 表（不含 ROI/detect/EXIF）。复用现有 `recognizeEncoded`； ✅ 完成
2. **schemaVersion=1 envelope**：`DocumentResult`/page/`line.id`、JSONL 单页 record、`--schema-version`； ✅ 完成
3. **EXIF orientation 修正 + appliedTransforms**（D-N1-5 实现方式）； ✅ 完成（JS 解析器 + C++ 像素变换 + `applyExif` option）
4. **ROI `--region`**（D-N1-4 实现位置）； ✅ 完成（adapter C++ 层裁剪 + box offset 重映射）
5. **`detect` 子命令**：Core `Engine::detect()` 公共方法 + Node 绑定 + `detect [--crop]`（D-N1-3 签名）； ⬜ 进行中
6. **Agent Skill** `.agents/skills/local-ocr/SKILL.md`； ✅ 完成
7. **验收**：Tier 1 CLI smoke、schema snapshot、20 个 Agent task eval。 ⬜ 部分（52 单元测试，端到端待 native build）

第 5 步是唯一的 Core C++ 改动，风险最高，单独评审。

## 14. 验收与退出条件

沿用 [roadmap §5.7](roadmap.md)：

- Tier 1 平台 Node.js 22/24 均通过 `npm install` 后 CLI smoke；
- CJS、ESM、Node API 和 CLI 对同一输入返回语义一致的结果；
- JSON/JSONL 使用 committed schema 和 snapshot 测试；
- stdin、文件路径、ROI、EXIF、退出码和 stderr/stdout 分离有测试；
- 禁网、sterile cwd、`--ignore-scripts` 安装继续通过；
- 至少 20 个 Agent task eval 覆盖全文、指定区域、低置信度和错误恢复；
- Agent eval 至少 18/20 通过，且任何失败不能把推断内容伪装成 OCR 原文；
- 一个不熟悉内部架构的读者能只凭 README/SKILL 完成首次 OCR。

## 15. 本节点不做

- 目录递归、glob、watch mode、交互式 UI（[roadmap §5.2](roadmap.md)）；
- 任意多边形 ROI、多个 ROI 合批、仅对已知 line crop 执行 recognition（[roadmap §5.3](roadmap.md)）；
- Layout region label、阅读顺序、语义分类（`detect` 子命令不做，属 N4）；
- MCP server（[roadmap §13](roadmap.md)）；
- 字符级或词级坐标（recognition contract 以文字行为单位）。

## 16. 待决策项（D-N1）

以下在进入实现前需维护者确认，确认后提炼为 [decisions.md](decisions.md) 的 D106。已确认项不再列出（CLI 分发形态 = runtime 共享实现 + facade bin；`--region` = 整数像素；`--no-color`/`--quiet` 默认行为 = 非 TTY 或 `NO_COLOR` 自动关闭彩色）。

- **D-N1-2** argv parser：手写最小 zero-dependency parser，还是引入轻量零依赖库。
- **D-N1-3** Core `Engine::detect()` 公共方法签名与 `DetectionResult` 结构（box quad + score），以及 `--crop` 的 PNG crop 编码方式（base64 内联 vs 二进制 sidecar）与 `CoreErrorCode` 映射。
- **D-N1-4** ROI 实现位置：建议 adapter 层（EXIF 修正后、送 Core 前裁剪像素），保持 Core 边界不变。
- **D-N1-5** EXIF 解析实现方式：自带最小 JPEG APP1 EXIF 解析器（零依赖，与 stb 风格一致），还是换用支持 EXIF orientation 的 decode 路径。
- **D-N1-7** exit code 表最终映射（§10），含 D112 Auto 创建期可跳过原因的 stderr 报告格式。
