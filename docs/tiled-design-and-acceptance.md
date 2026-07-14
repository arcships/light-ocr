# light-ocr Tiled Detection 技术设计与验收规格

状态：Implementation in progress；Core/Node、八张 locked 语料、独立 oracle 与发布候选门禁已实现，四平台受审基线与 0.2.0 registry evidence 尚未完成，不构成已发布能力<br>
Authority：`DetectionStrategy::tiled` 的算法、公开 API、runtime contract、语料、报告、四平台门槛与发布条件<br>
依赖：[高分辨率内存优化设计](memory-optimization.md) · [C++ API](native-api.md) · [Node-API 设计](napi-design.md) · [对齐与质量验证](parity-testing.md)

## 1. 结论与发布边界

`tiled` 是高分辨率小文字优先的显式检测策略。它把原图拆成有重叠的检测窗口，只让一个 tile 的 detection tensor、ORT output 和 workspace 同时存活；所有候选恢复到原图坐标后执行一次确定性去重、一次全局 reading-order sort 和一次流式 recognition。

第一版公开边界固定如下：

- `bounded/960` 继续是默认策略；升级 package 不会把现有调用静默切换到 `tiled`。
- `tiled` 只能在 engine creation 时显式选择，不能按单次请求切换。
- C++ Core 是分块、合并和排序的唯一实现；Node-API 只映射类型、错误、diagnostics 和生命周期。
- `tiled-v1` 的 tile/overlap/merge 参数由 bundle 的版本化 runtime contract 固定，首版不提供用户可调旋钮。
- C++ enum、Node types、normalized-config schema、四平台 prebuild 和六个 npm packages 必须在同一次 lockstep minor release 中出现。
- 在第 14 节全部通过前，README、已发布 npm 类型/运行时都不得宣称支持 `tiled`，也不得使用隐藏环境变量提前开放；candidate 源码中的 additive API 只用于完成验收。

本文是第二阶段 tiled 实现的 authority。`memory-optimization.md` 继续说明整体内存背景；两份文档冲突时，tiled 的算法和验收以本文为准。

## 2. 目标、非目标与术语

### 2.1 目标

- 在 2048² small-text、dense 与 tile-boundary 场景保留比 `bounded/960` 更高的原始像素尺度。
- detection 峰值受一个 tile 限制，不形成完整 2048² float tensor，也不同时保留所有 tile outputs。
- 跨 tile 的同一文字只产生一个最终 box/line；相邻但不同的文字不得被误合并。
- 合并结果与 tile 执行顺序无关；同一输入、bundle、线程设置和 contract 重复运行得到稳定的文字、数量和 reading order。
- 最终 recognition 始终从原图 crop，并继续沿用 batch 1 streaming 生命周期。
- C++ 与 Node.js 暴露相同的策略身份、effective contract、错误和诊断计数。
- Linux x64 glibc、Windows x64、macOS arm64/x64 都有独立进程的 absolute peak 与 warm latency 基线。

### 2.2 非目标

- 不把 `tiled` 改成默认策略，也不删除 `bounded` 或 `upstream_exact`。
- 不在 `tiled-v1` 公开自定义 tile side、overlap、merge threshold 或 reading-order 参数。
- 不实现 encoded-image/PDF 解码、跨图片 batch、GPU Execution Provider 或多 engine 自动调度。
- 不承诺 running inference 的硬取消；现有 AbortSignal 语义保持不变。
- 不用 tile 并发换取速度。一个 engine 的 tile detection 按顺序执行，避免峰值和 ORT oversubscription 失控。
- 不把 upstream 整图结果当作唯一 ground truth；质量门槛使用独立标注。

### 2.3 术语

| 术语 | 定义 |
| --- | --- |
| Tile | 原图中的闭开矩形 `[x, x + width) × [y, y + height)`；不复制原始 BGR payload。 |
| Artificial edge | tile 边缘中不与原图边缘重合的部分。 |
| Original edge | 与 `x=0`、`y=0`、`x=imageWidth` 或 `y=imageHeight` 重合的真实图片边缘。 |
| Raw candidate | 单个 tile 完成 DB postprocess 后、尚未跨 tile 去重的候选，包含 quad、DB score 与来源身份。 |
| Boundary candidate | quad 的轴对齐外接框距任一 artificial edge 不超过 contract margin 的候选。 |
| Representative | 去重时保留下来的原始候选；`tiled-v1` 不平均或拼接多个 quad。 |
| Detection pass | 一次 tile preprocess + detection inference + DB postprocess。 |

所有坐标在恢复到全图空间后使用 `float` public representation；重叠面积、IoU、IOS 和排序比较使用 `double` 中间值。

## 3. `tiled-v1` Runtime Contract

### 3.1 固定参数

| 字段 | `tiled-v1` 值 | 含义 |
| --- | ---: | --- |
| `tileSide` | `1280` | tile 的最大宽和高；可被 detection stride 32 整除。 |
| `minimumOverlap` | `128` | 相邻 tile 的最小重叠；末端锚定可能形成更大的实际 overlap。 |
| `dimensionMultiple` | `32` | detection tensor 宽高向上对齐单位。 |
| `artificialBoundaryMargin` | `32` | 判定 boundary candidate 的原图像素距离。 |
| `mergeIouThreshold` | `0.50` | convex quad IoU 达到该值视为重复。 |
| `mergeIosThreshold` | `0.80` | intersection / smaller-area 达到该值视为重复。 |
| `maxDetectionTiles` | `100` | bundle hard ceiling；reduced limits 只能降低。 |

`minimumOverlap` 是下限而不是“每对 tile 恰好重叠 128”。例如 2048 轴上的两个 1280 tile 起点为 `0` 和 `768`，实际 overlap 为 `512`；这样保证最后一个 tile 锚定原图末端且没有窄尾 tile。

### 3.2 Bundle schema

首次支持 tiled 的 bundle 把 normalized config 升级到 schema `1.2`，并增加：

```json
{
  "schemaVersion": "1.2",
  "resourceLimits": {
    "maxDetectionTiles": 100
  },
  "runtimeProfiles": {
    "tiled": {
      "contractVersion": "tiled-v1",
      "tileSide": 1280,
      "minimumOverlap": 128,
      "dimensionMultiple": 32,
      "dimensionMultipleRounding": "ceil_resize",
      "artificialBoundaryMargin": 32,
      "tileOrder": "row_major",
      "merge": {
        "iouThreshold": 0.5,
        "intersectionOverSmallerThreshold": 0.8,
        "scope": "different_overlapping_tiles",
        "geometry": "select_representative",
        "selectionOrder": [
          "not_artificial_boundary",
          "higher_db_score",
          "farther_from_artificial_boundary",
          "lower_tile_ordinal",
          "lower_candidate_ordinal"
        ]
      },
      "recognition": "once_after_global_merge"
    }
  }
}
```

验证规则：

- schema `1.0`/`1.1` bundle 没有 tiled capability；对其请求 `tiled` 返回 `unsupported_capability`。
- schema `1.2` 必须完整包含上述字段并拒绝未知枚举、非有限 threshold、`tileSide % 32 != 0`、`minimumOverlap == 0`、`minimumOverlap >= tileSide`、margin 超过 overlap，以及超过 Core hard ceiling 的 tile count。
- manifest 的 `coreCompatibility.minimum` 必须提升到首次实现 tiled 的 Core 版本；bundle ID 必须变化，即使 ONNX bytes 没变。现有 manifest 结构足以登记新 normalized config 时，manifest 自身的 schema version 不必随之改号。
- `runtimeDefaults.detection` 仍为 `bounded/960`；`runtimeProfiles.tiled` 是 opt-in profile，不是新默认值。
- `normalized-config.json`、manifest inventory 和 `SHA256SUMS` 共同锁定 contract。修改任何语义字段都必须创建新的 contract version、bundle ID 和 npm release，不能只改实现。

首版不接受 `tileSide` 或 `minimumOverlap` 的用户覆盖。这样质量矩阵只证明一个明确 contract，不把未经测试的参数组合误标为公共能力。

## 4. 分块检测算法

### 4.1 轴向规划

对长度 `L`、tile side `S` 和 minimum overlap `O`，轴向起点按以下等价伪代码计算：

```text
plan_axis(L, S, O):
  require L > 0
  require 0 < O < S
  if L <= S:
    return [0]

  stride = S - O
  starts = [0]
  while starts.last + S < L:
    next = min(starts.last + stride, L - S)
    require next > starts.last
    starts.push(next)
  return starts
```

实现必须使用 checked integer arithmetic。规划完成后还要逐项验证：

- 第一个起点为 `0`，最后一个 tile 的末端等于 `L`；
- 所有起点严格递增且没有重复；
- 相邻 tile 没有 gap，overlap 至少为 `O`；
- 每个 tile 长度为 `min(S, L)`，不会产生窄尾 tile；
- `xStarts.size × yStarts.size` 在开始任何 inference 前不超过 effective `maxDetectionTiles`。

二维 tile 按 `y` 起点优先、`x` 起点次优先生成，即 row-major。`tileOrdinal` 从 `0` 连续递增，是 diagnostics、tie-break、报告和测试向量中的稳定身份。对 2048² 输入，两个轴的起点均为 `[0, 768]`，一共四个 1280² tile。

### 4.2 单 tile 生命周期

每个 tile 严格顺序执行以下阶段：

1. 从已经完成格式转换的原图 BGR `cv::Mat` 建立 ROI view；ROI 不复制原始像素。
2. 复用 `bounded` detection preprocess，effective max side 固定为 `tileSide=1280`，维度继续使用向上对齐 32 的规则。短边小于模型下限时仍使用现有 minimum-short-side 逻辑，但最终 tensor 的宽和高都不得超过 1280。
3. 执行一次 detection inference。
4. 使用 tile 的原始 ROI 宽高完成 DB postprocess，并为每个通过阈值的候选保留 DB score。
5. 把 quad 加上 `(tileX, tileY)` 恢复到全图坐标，按原图边界 clip，并附加来源、边界和排序元数据。
6. 把紧凑的 raw candidate 移入全局候选集合；在进入下一个 tile 前销毁本次 tensor、ORT outputs、resized image 和 postprocess workspace。

允许在 engine 生命周期内复用容量，但不得同时保留两个 tile 的 input/output buffers。对单 tile 图片，仍走同一套 profile 和 diagnostics，使 `tiled-v1` 只有一条可验证语义路径。

### 4.3 候选元数据

Core 内部的 tiled raw candidate 至少包含：

```cpp
struct TiledCandidate {
  Quad global_quad;
  double db_score;
  std::uint32_t tile_ordinal;
  std::uint32_t candidate_ordinal;
  TileRect source_tile;
  ArtificialEdgeMask nearby_artificial_edges;
  double distance_to_nearest_artificial_edge;
};
```

`candidateOrdinal` 是 DB postprocess 在该 tile 中接受候选的稳定顺序。DB score 必须来自实际用于 box filtering 的 score，不能在 diagnostics 关闭时丢弃或重算。

只有 tile 边缘不与原图对应边缘重合时才设置 artificial-edge bit。使用 quad 的轴对齐外接框计算到四条 tile 边的距离；距离小于等于 `artificialBoundaryMargin` 即为 boundary candidate。与原图边界重合的边永远不是 artificial edge，不能因为贴近图片外沿而降低候选优先级。

### 4.4 计数与失败原子性

`maxDetectionCandidates` 在 tiled 下是整张图片所有 detection passes 的总上限，不是每 tile 上限。effective candidate ceiling 取 Core hard limit、bundle resource limit、`detection.postprocess.maxCandidates` 和调用方 reduced limit 的最小值。DB postprocess 必须暴露截断前 contour count；检查到的 contours 和接受的 raw candidates 都用 checked counter 汇总。超过 ceiling 时整次调用返回 `resource_limit_exceeded`，不得静默截断、返回部分 OCR 结果或继续 recognition。

同理，任一 tile preprocess、inference、postprocess 或坐标恢复失败，整次调用失败。对外只有完整结果或错误，不存在“前三个 tile 成功”的部分成功状态。

## 5. 全局候选合并与阅读顺序

### 5.1 可比较范围

跨 tile 合并只比较同时满足以下条件的候选对：

- 来自不同 tile；
- 两个 source tile 在原图空间有面积大于零的 overlap；
- 两个 quad 的轴对齐外接框相交。

同一 tile 内的两个候选不由 tiled merge 合并；它们已经受 DB postprocess 自身语义约束。实现可以使用 tile-overlap 索引和空间桶减少 pair 数量，但优化前后必须产生完全相同的代表集合和顺序。

### 5.2 重复判定

quad 先规范为顺时针凸四边形；面积和 intersection 使用 `double` 计算。无效、非有限、自交或面积小于 geometry epsilon 的 quad 在 postprocess 阶段按现有错误策略拒绝，不进入 merge。

对候选 `A`、`B`：

```text
intersection = area(A ∩ B)
iou = intersection / (area(A) + area(B) - intersection)
ios = intersection / min(area(A), area(B))
duplicate = iou >= 0.50 OR ios >= 0.80
```

threshold 是包含边界的 `>=` 比较。IOS 用来捕获一个 tile 在 artificial edge 截短、另一个 tile 保留完整文字框的情况；它不能绕过“不同且重叠 source tiles”的 scope 限制。

### 5.3 代表选择与确定性 NMS

所有 raw candidates 先按以下 tuple 做稳定优先级排序：

1. 非 boundary candidate 优先；
2. DB score 高者优先；
3. 距最近 artificial edge 更远者优先；
4. `tileOrdinal` 小者优先；
5. `candidateOrdinal` 小者优先。

然后按该顺序执行 greedy NMS：候选若与任一已保留且在可比较范围内的 representative 构成 duplicate，则被 suppress；否则成为 representative。`tiled-v1` 不做 quad averaging、union、拼接或 score averaging，最终 box 和 DB score 必须来自一个真实候选。

该规则有意不使用连通分量合并。例如 `A` 与 `B` 重复、`B` 与 `C` 重复但 `A` 与 `C` 不重复时，只要 `A` 优先，`C` 仍可保留，避免一条长文本链把相邻行传递性吞并。测试必须锁定这个行为。

### 5.4 全局排序与 recognition

merge 完成后只对 representatives 调用一次现有 `sort_reading_order`。禁止先在各 tile 内输出、再串接 tile 结果；否则同一水平行跨过 tile 时 reading order 会随 tile 顺序变化。

排序后的 boxes 从原始完整图片 crop，按现有 recognition batch planner 分组，并保持 batch 1 streaming 默认值。recognition 不对每 tile 重复运行，也不从 resized tile crop。最终 `OcrResult.lines`、rejected lines 和 confidence 的顺序全部以这次全局排序为准。

“没有重复行”的验收同时检查几何和文本：同一 ground-truth line 不能匹配多个输出，输出中也不能出现由同一个跨边界文字产生的重复 normalized text。相邻内容相同但 ground-truth 身份不同的两行必须保留，不能只按字符串去重。

## 6. 资源限制、错误与确定性

### 6.1 资源模型

`ResourceLimits` 增加 `max_detection_tiles`，hard default 为 `100`；Node 对应 `maxDetectionTiles`。bundle contract 可以给出不高于 Core hard ceiling 的值，调用方的 `reduced_limits` 只能继续降低。effective limit 取三者最小值。

一次 tiled 调用可以同时持有：

- 一份完成 pixel-format 转换的全图 BGR；
- 一个 tile 的 detection tensor、ORT outputs 和 postprocess workspace；
- 全图 raw candidates 的紧凑元数据；
- 一个 streaming recognition batch 及其 crops；
- 用户显式请求的 diagnostics。

不得持有完整 2048² float detection tensor、全部 tile outputs 或全部 recognition tensors。`max_temporary_bytes` 继续约束 Core 可计量的临时分配，但它不覆盖 ORT、OpenCV allocator 和进程 runtime 的所有内存，因此第 11 节另设独立进程 absolute peak hard gate。

图片宽高、像素数、tile count、candidate count、tensor elements、字节数和坐标 offset 的所有乘加都必须 checked。10000×10000 虽受 `max_pixels=40,000,000` 先行限制，但 tile planner 仍不得依赖该默认值来避免整数溢出。

### 6.2 参数和错误

| 条件 | 错误码 | 对外语义 |
| --- | --- | --- |
| bundle 不含 `tiled-v1` profile | `unsupported_capability` | 当前 bundle 不能创建 tiled engine。 |
| `detection.max_side` 或 request `detectionMaxSide` 与 tiled 同时提供 | `invalid_argument` | `tiled-v1` 不接受用户 side override。 |
| tile/candidate/tensor/temp limit 超限 | `resource_limit_exceeded` | 不返回部分结果。 |
| bundle contract 字段缺失或非法 | `invalid_model_bundle` | bundle validation 在 engine creation 失败。 |
| quad 非有限、坐标恢复溢出或 merge geometry 失败 | `postprocess_failed` | 当前图片失败，不继续 recognition。 |
| ORT/OpenCV/内部异常 | 现有稳定错误映射 | C++ boundary 和 Node promise 均不泄漏异常。 |

具体错误 message 可补充参数和值，但应用只能依赖稳定 error code。Node 的 `code`、`message`、`detail` 必须与已有错误映射方式一致。

### 6.3 可重复性

下列内容属于 contract，而不是实现建议：

- tile 始终 row-major 串行执行；
- raw candidates 使用稳定来源 ordinal；
- score、boundary distance 和 overlap 的非有限值直接失败；
- threshold equality、tie-break 与 greedy NMS 次序固定；
- 全局 reading-order sort 继续使用稳定排序；
- engine 的 ORT thread 设置和现有 serialized concurrency mode 不变。

同一 binary、bundle、输入 bytes、engine options 和线程设置连续运行 10 次，最终 line count、text sequence、quad serialization 和 representative 来源身份必须逐字节一致；timing 与 RSS 不参与一致性比较。

Node 的 AbortSignal 语义不因 tiled 改变：排队任务可以在进入 Core 前取消；已经开始的同步 Core 调用可以让 JS promise 进入 rejected 状态，但首版不承诺中断正在运行的 tile inference。不得在文档或类型中把 tile 边界描述成 hard-cancel checkpoint。

## 7. C++ 与 Node.js 公开 API

### 7.1 C++ Core

下一次 minor release 对 public types 做以下 additive change：

```cpp
enum class DetectionStrategy {
  bounded,
  tiled,
  upstream_exact,
};

struct ResourceLimits {
  // existing fields...
  std::uint32_t max_detection_tiles = 100;
};

struct TiledDetectionInfo {
  std::string contract_version;  // "tiled-v1"
  std::uint32_t tile_side = 1280;
  std::uint32_t minimum_overlap = 128;
  std::uint32_t artificial_boundary_margin = 32;
  float merge_iou_threshold = 0.50f;
  float merge_ios_threshold = 0.80f;
};

struct Capabilities {
  bool detection = true;
  bool recognition = true;
  bool textline_orientation = false;
  bool tiled_detection = false;
};

struct EngineInfo {
  // existing fields...
  std::string normalized_config_schema_version;  // "1.2"
  DetectionStrategy detection_strategy = DetectionStrategy::bounded;
  std::uint32_t detection_max_side = 960;
  std::optional<TiledDetectionInfo> tiled_detection;
};
```

`EngineOptions::detection.strategy=tiled` 是唯一选择入口。`DetectionOptions::max_side` 在 tiled 下必须为空；`RecognizeOptions::detection_max_side` 同样非法。`EngineInfo::detection_max_side` 对 tiled 表示一次 detection pass 的最大 tensor side，即 `1280`，不是可接受图片的最大边。图片上限仍由 `limits.max_width`、`max_height` 和 `max_pixels` 给出。

现有 `model_bundle_schema_version` 继续表示 manifest schema；新增 `normalized_config_schema_version` 才报告本规格的 `1.2`。Node 对应保留 `modelBundleSchemaVersion` 并新增 `normalizedConfigSchemaVersion`，不得把两者合并成一个含糊的版本字段。

当 bundle 声明并通过 `tiled-v1` 验证时，`capabilities.tiled_detection=true`。只有 tiled engine 的 `info.tiled_detection` 有值；bounded/upstream engine 保持 `nullopt`，避免调用方把 bundle 能力和当前 engine 的 effective strategy 混为一谈。

### 7.2 Node.js package

`@arcships/light-ocr` 的 `.d.ts` 与 runtime object 同步增加：

```ts
export type DetectionStrategy = 'bounded' | 'tiled' | 'upstreamExact';

export interface ResourceLimits {
  // existing fields...
  readonly maxDetectionTiles: number;
}

export interface TiledDetectionInfo {
  readonly contractVersion: 'tiled-v1';
  readonly tileSide: 1280;
  readonly minimumOverlap: 128;
  readonly artificialBoundaryMargin: 32;
  readonly mergeIouThreshold: 0.5;
  readonly mergeIosThreshold: 0.8;
}

export interface EngineInfo {
  // existing fields...
  readonly normalizedConfigSchemaVersion: string;
  readonly capabilities: {
    readonly detection: boolean;
    readonly recognition: boolean;
    readonly textlineOrientation: boolean;
    readonly tiledDetection: boolean;
  };
  readonly detectionStrategy: DetectionStrategy;
  readonly detectionMaxSide: number;
  readonly tiledDetection?: TiledDetectionInfo;
}
```

使用方式：

```ts
import { createEngine } from '@arcships/light-ocr';

const engine = await createEngine({
  detection: { strategy: 'tiled' },
});

console.log(engine.info.tiledDetection?.contractVersion); // tiled-v1
```

JS facade、raw Node-API addon 和 C++ Core 都必须拒绝 `{ strategy: 'tiled', maxSide: ... }`。facade 不得在 addon 或 bundle 不支持时回退到 `bounded`，也不得根据图片大小自动改策略。unknown-property rejection、Promise error mapping、queue limits、copied raw-pixel admission、AbortSignal 和 `close()` 生命周期维持现有设计。

加入 `maxDetectionTiles` 后，`reducedLimits` 仍采用“提供即包含全部字段”的严格对象规则；遗漏新字段或出现未知字段都以 `invalid_argument` 拒绝，不能给旧对象静默补 `100`。

### 7.3 版本与兼容

- public enum/type 增加发生在 `0.1.x` 之后的下一次 minor release；本文不预先硬编码具体版本号。
- facade、model package 和四个 native packages 使用相同版本并 lockstep 发布。
- 新 facade 在解析旧 bundle 时仍可创建 bounded/upstream engine，但显式 tiled 请求稳定失败为 `unsupported_capability`。
- 旧 facade 不会认识新的 `'tiled'` 字符串；这是选择新能力时的预期类型/参数失败，不提供字符串别名。
- `bounded/960` 的默认构造、result shape 和现有 goldens 不因 additive fields 改变。

## 8. Diagnostics、Timing 与报告格式

### 8.1 Diagnostics

C++ 增加逐 pass shape 和 merge 计数：

```cpp
struct DetectionPassShape {
  std::uint32_t tile_ordinal = 0;
  std::uint32_t x = 0;
  std::uint32_t y = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t tensor_width = 0;
  std::uint32_t tensor_height = 0;
  std::uint32_t contour_candidates = 0;
  std::uint32_t raw_candidates = 0;
};

struct Diagnostics {
  // existing fields...
  std::uint32_t raw_detection_boxes = 0;
  std::uint32_t suppressed_duplicate_boxes = 0;
  std::uint32_t max_live_detection_pass_buffers = 0;
  std::vector<DetectionPassShape> detection_passes;
};
```

Node 使用 `rawDetectionBoxes`、`suppressedDuplicateBoxes`、`maxLiveDetectionPassBuffers`、`detectionPasses[]` 和 camelCase shape fields。现有聚合字段在 tiled 下定义为：

- `detectedCandidates`：所有 passes 的 contour candidates 总数；
- `rawDetectionBoxes`：通过 DB postprocess 的 raw candidate 总数；
- `suppressedDuplicateBoxes`：merge suppress 的数量；
- `acceptedBoxes`：merge 后交给 recognition 的 representative 数量；
- `detectionInputWidth/Height`：所有 passes 中各自最大 tensor width/height，用于向后兼容；逐 pass 真值以 `detectionPasses` 为准。
- `maxLiveDetectionPassBuffers`：测试 instrumentation 观察到同时存活的 detection pass buffer set 最大值；tiled 必须为 `1`。

必须满足 `rawDetectionBoxes - suppressedDuplicateBoxes == acceptedBoxes`。diagnostics 只包含尺寸、坐标、计数、来源 ordinal 和稳定 warning，不包含输入像素、probability map、tensor values 或识别到的额外隐私副本。

### 8.2 Timing

`Timing` 增加 `detection_merge_us`，Node 映射为 `timingUs.detectionMerge`。三个 detection 原有阶段分别累计所有 tile 的 preprocess、inference 和 DB postprocess；`detectionMerge` 只覆盖全图坐标候选的索引、overlap 计算和 NMS；`cropAndSort` 从全局 reading-order sort 开始，继续包含 recognition crop/planning。

每段时间只计入一个 bucket，`total` 从请求进入 Core 到完整结果组装结束。bounded/upstream 的 `detectionMerge` 固定为 `0`。CI 校验所有阶段不大于 `total`，并允许未归类的调度/分配开销，所以不要求阶段和严格等于 `total`。

### 8.3 机器可读运行报告

每个验收运行输出 schema `light-ocr-tiled-run-report/1.0` JSON，至少包含：

```json
{
  "schema": "light-ocr-tiled-run-report/1.0",
  "identity": {
    "gitCommit": "...",
    "coreVersion": "...",
    "packageVersion": "...",
    "bundleId": "...",
    "manifestSchemaVersion": "1.0",
    "normalizedConfigSchemaVersion": "1.2",
    "contractVersion": "tiled-v1",
    "normalizedConfigSha256": "..."
  },
  "platform": {
    "os": "...",
    "arch": "...",
    "runner": "...",
    "cpu": "...",
    "compiler": "...",
    "node": "...",
    "onnxRuntime": "...",
    "intraOpThreads": 1,
    "interOpThreads": 1
  },
  "fixture": {
    "id": "tiled-small-text-2048",
    "pixelsSha256": "...",
    "width": 2048,
    "height": 2048
  },
  "tilePlan": [],
  "counts": {
    "passes": 4,
    "contours": 0,
    "rawBoxes": 0,
    "suppressedDuplicates": 0,
    "acceptedBoxes": 0,
    "outputLines": 0
  },
  "result": {
    "orderedTextSha256": "...",
    "orderedQuadsSha256": "...",
    "representativeSourcesSha256": "..."
  },
  "latencyUs": {
    "warmups": 5,
    "samples": 10,
    "median": 0,
    "p95": 0,
    "inferenceOnlyMedian": 0
  },
  "memoryBytes": {
    "metric": "peak_rss_or_peak_working_set",
    "absolutePeak": 0
  },
  "gates": []
}
```

`tilePlan` 逐项记录 ordinal、ROI、tensor shape 和候选计数；`gates` 对每个门槛记录 measured、limit 和 pass/fail，不能只给总布尔值。报告必须使用稳定 key/order-independent canonical hash，禁止写入绝对本地路径、原始图片或环境 secret。

PR artifacts 放在构建目录；经 review 接受的基线进入 `contracts/tiled-platform-baselines.json`。仓库只提交小型 JSON、fixture metadata 和 lock files，不提交重复的 profiler dump。

## 9. Ground-truth 语料矩阵

### 9.1 固定语料

新增独立的 `corpus/tiled-v1/`，所有 fixture 都是 2048×2048 raw pixels，并有人工可审阅的渲染规格、逐行文本、quad、reading-order index、像素 SHA-256 和生成器版本：

| Fixture ID | 最低内容 | 必须证明 |
| --- | --- | --- |
| `tiled-small-text-2048` | 至少 32 行，主要字高 12–24 px，含中英数字 | 不依赖 960 缩放仍能识别小字，line/text/order 全匹配。 |
| `tiled-dense-2048` | 至少 100 个独立文字框，行距覆盖窄/常规两档 | 高候选量不丢行、不重复、不破坏 reading order。 |
| `tiled-horizontal-boundary-2048` | 至少 8 行跨越 horizontal artificial edges，覆盖正交与轻微旋转 | 上下 tile 对同一行只保留一个完整 representative。 |
| `tiled-vertical-boundary-2048` | 至少 8 行跨越 vertical artificial edges，覆盖左右截短 | 左右 tile 对同一行只保留一个完整 representative。 |
| `tiled-four-way-intersection-2048` | 至少 4 个框覆盖四 tile 交点，另有交点邻近但不相交的控制框 | 四份候选最多合成一个；邻近独立框不被吞并。 |
| `tiled-original-edges-2048` | 四边和四角均有贴边/被图片裁切的文字 | original edge 不被误标为 artificial edge，框被合法 clip。 |
| `tiled-near-neighbor-2048` | 至少 8 对内容相同或相似的近邻行，部分跨 overlap | 不按字符串去重，IOS 规则不误合并真实近邻。 |
| `tiled-reading-order-2048` | 至少 16 行跨越两个轴，包含同行跨 tile 和多列控制区 | 合并后一次全局排序，顺序与 tile 运行顺序无关。 |

small-text 与 dense 可以复用同一套字体资产，但不能是同一图片的简单别名。boundary fixtures 的目标几何必须由 locked `tiled-v1` planner 计算出来，覆盖 2048 轴的 artificial boundary、四 tile 交点和原图边缘。由于 overlap 有面积，“四 tile 交点”标签固定指四个 source tiles 共同覆盖区域的中心 target，而不是假设无 overlap 的单像素 seam。

### 9.2 Ground truth 生成与锁定

生成器只负责可复现渲染和导出设计时几何；期望文本、line identity、reading order 和最终标注 quad 必须单独 review，不能从 Core 输出反向生成。对于抗锯齿带来的 glyph 外沿，标注策略固定为 renderer layout box 加版本化 padding，而不是按一次模型预测手调。

`corpus/tiled-v1/ground-truth.lock.json` 锁定：

- fixture metadata 与 `pixels.bin` hash；
- font/resource 来源、license 和 hash；
- renderer、locale、随机 seed 和生成器 commit；
- 每个 line 的稳定 ID、UTF-8 text、clockwise quad 和 expected order；
- contract version 与 planner test-vector hash。

修改像素、文字、标注、字体、seed、contract 或 generator 均必须更新 corpus revision，并在 PR 中展示 before/after overlay。禁止因为实现未通过就降低标注数量、移动 boundary target 或用新模型输出覆盖 ground truth。

外部真实图片只可作为额外回归样本，必须继续满足现有来源、许可和 hash 锁定要求；第 10 节 hard quality gate 不依赖不可再分发的外部资产。

## 10. 单元、集成、Parity 与质量验收

### 10.1 Planner 与 merge 单元测试

axis planner 至少锁定 `L={1,31,32,1279,1280,1281,2048,4096,10000}` 的 test vectors。必须显式断言：

- `1280 -> [0]`、`1281 -> [0,1]`、`2048 -> [0,768]`；
- 任意生成长度都覆盖 `[0,L)`、无 gap、无重复起点且末端锚定；
- 10000×10000 在默认 contract 下为 9×9、共 81 tiles；
- effective `maxDetectionTiles=80` 对该规划在 inference 前失败；
- `L=0`、invalid contract、乘法/坐标溢出走稳定错误，不 crash。

merge 单元测试至少覆盖：

- IoU/IOS 小于、等于和大于 threshold 的三点；
- 完整框与 artificial-edge 截短框的 IOS duplicate；
- 同一 tile、互不重叠 source tiles 和仅 AABB 相交的非重复候选；
- original edge 不参与 boundary penalty；
- 五级 tie-break 的每一级以及完全相同 score/distance 时的 ordinal；
- `A↔B`、`B↔C`、`A↮C` 的非传递 greedy case；
- 旋转凸 quad、阈值附近浮点、退化/自交/非有限 geometry；
- 相同文字的近邻框不会因为 text 相同而合并；
- 输入候选排列被打乱时，只要来源 ordinal 不变，结果仍一致。

checked-math、geometry 和 merge fuzz tests 使用固定 seed 并保存最小化 regression cases。fuzz 发现的 crash、UB、非确定性或 limit bypass 都是 release blocker。

### 10.2 Core 集成与 Node contract

每个第 9 节 fixture 都要在 Release Core binary 上执行真实 PP-OCRv6 detection + recognition，并断言：

- tile plan、逐 pass tensor shape 和 candidate accounting 与 contract 一致；
- merge 后 box 来自真实 candidate，坐标在原图 geometric edge 范围内；
- recognition crop 可由原图与最终 quad 重建，不引用 tile-resized pixels；
- diagnostics 开关不改变 lines、confidence 或 boxes；
- 连续 10 次的稳定输出满足第 6.3 节；
- resource limit、旧 bundle、非法 side override 和中途 inference failure 均不返回部分结果。

Node.js 22/24 在四个平台重复同一 corpus subset，并逐字段对照 C++ JSON：strategy、effective contract、limits、lines、quads、timing keys、diagnostics counts 和 errors。CJS、ESM、TypeScript compile、AbortSignal、close、GC、worker teardown 与 queue tests 必须同时保持绿色。

### 10.3 独立 oracle 与 parity

新增 `tiled_v1` oracle profile。Python/NumPy/OpenCV 实现从 locked normalized config 读取 planner 与 merge contract，但不调用 C++ planner/merge，也不读取 Core 输出作为 expected；ONNX model bytes、preprocess/postprocess primitives 和 dictionary 继续由现有 hash-locked oracle 管线执行。

stage parity 至少比较：

1. tile plan 与每 pass detection shape；
2. threshold bitmap hash 与 contour count；
3. 带 score/source identity 的 raw candidates；
4. suppressed pair/representative identity；
5. global sorted quads；
6. recognition batch shapes、decoded text/confidence；
7. final ordered result。

坐标和 confidence 沿用现有 parity tolerance；不得为 tiled 添加覆盖整类 boundary fixture 的宽泛 exception。确需 exception 时必须精确到 fixture、stage、candidate/line ID、observed delta、原因和 expiry contract version。

### 10.4 Hard quality gates

第 9 节八张 synthetic fixtures 分别且汇总满足：

- ground-truth line recall `1.0`、output precision `1.0`，polygon match 使用 IoU `>=0.50`；
- 按现有 Unicode normalization 后 CER `0`，line text sequence 与 expected reading order 完全一致；
- 每个 ground-truth line 只匹配一个输出、每个输出只匹配一个 ground-truth line；
- `suppressedDuplicateBoxes` 与独立 oracle 一致，最终 duplicate-line count 为 `0`；
- horizontal、vertical、four-way 和 original-edge fixture 的每个 tagged target 全部通过，不能只靠全图平均值掩盖失败；
- near-neighbor 的每个 distinct line ID 都保留；reading-order fixture 在逆序喂入 raw candidates 后仍得到同一序列。

此外，现有 14 个 corpus fixtures 的 `bounded_default` 与 `upstream_exact` goldens、10 个 ground-truth quality cases和 error contract 必须保持原门槛；实现 tiled 不能用更新旧 golden 来吸收无关回归。

## 11. 四平台内存与性能验收

### 11.1 测试矩阵

以下四个目标都必须用本平台原生 Release binary 和本平台 npm native package 运行，不能用交叉编译产物或模拟器代替：

| OS | Architecture | Core | Node |
| --- | --- | --- | --- |
| Linux glibc | x64 | 独立进程 CLI | Node 22、24 |
| Windows | x64 | 独立进程 CLI | Node 22、24 |
| macOS | arm64 | 独立进程 CLI | Node 22、24 |
| macOS | x64 | 独立进程 CLI | Node 22、24 |

hard memory/latency cases 至少包含 `tiled-small-text-2048`、`tiled-dense-2048` 和 boundary 集合中 raw candidate 数最多的一张。每个报告记录 runner image、CPU、内存、compiler、Core/addon/package/bundle commit、ORT/OpenCV 版本、线程和所有 fixture hashes。

### 11.2 Absolute peak memory

每个 case 启动一个全新进程，create engine 后只执行一张目标图片，再正常 close。Linux/macOS 记录进程 lifetime peak RSS，Windows 记录 PeakWorkingSetSize；采样器还要保留阶段 heartbeat，防止短时峰值被轮询遗漏。

第一版 hard gates：

- 四个平台的 Core `absolutePeak <= 1 GiB`；
- queue depth 0 的 Node 单请求 `absolutePeak <= 1088 MiB`；Node 与 Core 独立进程峰值之差必须保存为 observation，但不作为首版 hard gate，因为两种进程的运行时基线不同；
- diagnostics on 与 off 分别记录；hard gate 取较高者；
- diagnostics 证明最大 detection tensor side `<=1280`，同时存活 detection passes 为 `1`；
- 5 次 warmup 后 10 次 create/recognize/close lifecycle 继续满足现有 `32 MiB total / 8 MiB per cycle` retained-growth 门槛。

`1 GiB` 是发布上限，不是宣传值。README 只能引用四平台实测报告中的具体数字和 fixture，不能把上限写成典型占用。任一平台没有数值、数值为 `null`、进程被 OOM/timeout 杀死或 measurement API 失败，均视为 gate failure。

### 11.3 Latency baseline

每个平台、fixture、Core/Node 组合固定 1/1 ORT threads：先 warmup 5 次，再测量 10 次完整 recognize；报告 median 与 nearest-rank p95。机器被标记为 noisy、CPU 信息变化或 thermal/power 条件不可确认时，结果只能作为 artifact，不能更新 committed baseline。

benchmark 不属于普通 CI 或 npm release preflight。只有首次建立基线、Core/model/ORT/compiler/thread policy/runner class 变化、准备公开新性能数字或调查疑似回归时，才通过显式 `run_benchmark=true` 手动执行。Core 与 Python 必须在同一 runner、相同线程配置和同一 fixture 上按固定次序比较；结果只有在 runner identity 与热/负载条件可审查时才能成为 accepted baseline。

首个 baseline 的 bootstrap 同时满足：

- 所有 10 次调用在 `120 s` hard timeout 内完成；
- Core 与 Python `tiled_v1` oracle 的 warm median、warm p95 和 inference-only median 必须在同机、同 fixture、同 pixels/batch/threads 下保存。当前 Core 使用 NuGet/平台原生 ONNX Runtime，Python oracle 使用 PyPI wheel，三组跨 runtime 比率均标记为 non-blocking observation；只有双方使用相同 ONNX Runtime binary build identity 时才恢复 `1.10x`/`1.15x`/`1.05x` hard gate；
- Node 与 Core 的 median、p95 比率必须保存。当前 qualification 在独立进程中按固定顺序采样，无法把 runner 热状态与 Node-API 开销分离，因此比率是 non-blocking observation；只有改为相同 native runtime、受控交错采样后才启用 `1.10x`/`1.15x` hard gate；
- quality gates 必须先通过，不能通过减少识别框数获得更快 latency。

baseline 经 review 写入 `contracts/tiled-platform-baselines.json` 后，后续同 runner class 的 median、p95 和 absolute peak 任一超过 accepted value 的 `1.15x` 即失败。跨 OS/arch 不互相比较绝对数值；runner class 改变时生成并 review 新 baseline，不能静默覆盖。

`bounded/960` 的 latency 只做信息对照，因为它处理的 detection pixels 和小字 recall 不同，不是 tiled hard performance comparator。

### 11.4 Baseline 文件完整性

committed baseline 每行由以下复合键唯一定位：

```text
contractVersion / bundleId / fixtureHash / os / arch /
runnerClass / core-or-node / nodeVersion / threadConfig
```

值至少包含 sample 数、median、p95、inference-only median、comparator identity、absolute peak、result hashes、报告 artifact digest 和批准该 baseline 的 git commit。CI 对重复键、缺字段、非有限值、fixture/config hash 不匹配和孤立 artifact digest 直接失败。

## 12. CI、npm 发布与回滚

### 12.1 PR 与 release jobs

普通 CI 与 npm release preflight 形成以下依赖链：

```text
contract/schema validation
        ↓
planner + geometry + merge unit/fuzz regressions
        ↓
tiled oracle parity + eight-fixture quality gates
        ↓
four-platform Node 22/24 contract/real-OCR smoke
        ↓
six-package staging + local-registry + offline verification
        ↓
npm publish to next → evidence verification → promote latest

按需 qualification（独立触发）：
Core/Node reports → 四平台矩阵校验 → 人工 review/accepted baseline
```

普通 PR 不读取 `NPM_TOKEN`；release preflight 也不采集 benchmark。性能 qualification 必须由人明确触发，不能因为 push、PR、定时器或普通发布自动运行。触发条件仅限首次基线、性能相关依赖/实现/runner 变化、新性能公开值或疑似回归调查。

quality、memory 与 latency tool 返回非零 exit code 才算 gate，不能只上传一份带 `"passed": false` 的 JSON。四平台 artifacts 由汇总 job 校验 schema、identity、hash、完整矩阵和 threshold 后，才允许进入 package job。

### 12.2 首次 baseline 与变更审批

首次实现没有旧 tiled baseline 时，candidate 仍必须通过 absolute memory、per-call timeout 和全部 quality gates；Core/Python 跨 binary latency、Node/Core 跨进程 latency 与峰值差均作为 observation 保留。CI 生成待审 JSON；reviewer 核对 runner identity、result hash、波动、profiler 证据和四平台完整性后，把各实现自己的 absolute baseline 作为同一 release PR 的受审变更提交。

之后修改 contract、model、ORT、OpenCV、compiler major、thread policy 或 runner class 时必须显式走 baseline requalification。仅因新结果变慢而执行自动“接受当前值”脚本是禁止的。

### 12.3 六包发布

首次带 tiled 的 release 遵循现有六包 lockstep 和 exact dependency 规则：

1. 从已通过 gate 的同一 git commit 组装 model、四个 native、facade tarballs。
2. 验证 model package 确实包含 schema `1.2`、`tiled-v1` contract 和新 bundle ID。
3. 在一次性 registry 使用 CJS/ESM、Node 22/24 创建 tiled engine，执行真实 2048 boundary fixture，并在禁网环境复验。
4. 先把 model 和四个 native packages 发布到 `next`，最后发布 facade 到 `next`。
5. 核对 registry integrity、provenance、SBOM、license inventory、类型和 runtime identity 后，才移动 `latest`。

任何一个平台 package 仍是旧 enum、facade types 先于 runtime 发布、bundle contract 缺失，或 registry 安装需要额外下载模型，都阻止整个 release set 提升到 `latest`。

### 12.4 回滚

npm version 不可覆写。发现 tiled 严重问题时：

- 立即停止 promotion；若已经是 `latest`，把 `latest` 移回最后一个已验证版本并 deprecate 问题版本；
- 保留 tarball、baseline 和失败报告作为证据，不删除 release 来制造“从未发生”；
- 发布修复 patch 时保持 public enum/type 可解析；若 tiled 暂时不可安全运行，engine creation 对受影响 bundle 明确返回 `unsupported_capability`，bounded 默认仍可用；
- 语义算法或 threshold 变化必须创建新 contract version 和 bundle ID，不能用 patch 在 `tiled-v1` 名下静默换规则；
- README/status 在修复重新通过完整四平台链前撤下 tiled production-ready 声明。

## 13. 实施顺序

当前实现快照（2026-07-14）：步骤 1–7 已进入源码。八张 2048² fixture 共 196 行在本机 Release Core 上达到 196 TP / 0 FP / 0 FN、CER 0、duplicate line 0，独立 Python planner/merge/stage oracle 与原生候选来源、抑制关系和最终结果对齐；无 token 本地 registry 预检已接入 release workflow，benchmark 已从普通 CI/release preflight 分离为显式按需任务。步骤 8 仍需在确有资格审查需求时运行真实四平台采集、人工 review 与 accepted baseline commit；步骤 9 的公开发布仍是阻断项。

1. **Contract 与 bundle**：实现 schema `1.2` parser/validator、`tiled-v1` normalized profile、capability 和新 bundle identity；先完成 malformed/old-bundle tests。
2. **Planner 与内部数据结构**：实现 checked axis planner、tile identity、candidate score/source metadata 和全局计数；先让纯单元 test vectors 全绿。
3. **顺序 detection pipeline**：把 engine detection 抽成单 pass primitive，加入 ROI view、逐 tile buffer release 和原图坐标恢复；保持 bounded/upstream path goldens不变。
4. **Merge 与全局排序**：实现 convex overlap、scope index、stable priority 和 greedy NMS；通过 threshold、chain、edge 和 fuzz regressions。
5. **Diagnostics/API**：添加 C++ enum/info/limits/timing，再同步 raw Node-API、facade validation、runtime object 和 `.d.ts`；补齐旧 bundle与非法组合错误测试。
6. **Corpus 与 oracle**（完成）：生成并锁定八张 fixtures，独立实现 Python tiled profile，保存 stage reports、独立 goldens、精确 parity exception 与十次确定性/质量报告。
7. **资源与性能工具**（完成）：独立进程 diagnostics on/off memory gate、Core-vs-oracle 与 Node-vs-Core latency runner、报告聚合器、result hashing 和 15% regression 失败测试均已接入；工具只生成 candidate，不自动写 accepted baseline。
8. **四平台 CI**：运行 Core/Node 完整矩阵，review 并提交首批 baseline，验证 15% regression gate 会对故意注入的超限样本失败。
9. **Package 与文档发布**：更新六包 staging、local registry/offline test、README 中英文和 release evidence；所有 gate 全绿后发布 `next`，最后提升 `latest`。

每一步都要以独立可审阅 commit 保持旧默认可构建、可测试；不得先把 README 标成可用，再用后续提交补实现或基线。

## 14. 完成定义

只有以下项目全部打勾，才能把本文状态改为 `Implemented` 并公开 `DetectionStrategy::tiled`：

- [x] schema `1.2`、`tiled-v1`、新 bundle ID/hash 和旧 bundle rejection 已实现并测试。
- [x] axis planner、row-major pass、单 tile buffer lifetime 和 checked limits 通过单元/集成测试。
- [x] DB score/source metadata、IoU/IOS merge、代表选择和全局 reading order 通过独立 oracle parity。
- [x] 2048 small-text、dense、水平边界、垂直边界、四 tile 交点、原图边缘、near-neighbor 与 reading-order ground truth 全部通过。
- [x] 最终 duplicate-line count 为 0，近邻独立行无误合并，连续 10 次结果稳定。
- [x] C++ public enum/info/diagnostics/timing/limits 与 Node runtime/`.d.ts` 同步，非法组合和旧 bundle 得到稳定错误。
- [x] Linux x64 glibc、Windows x64、macOS arm64、macOS x64 的 Core 与 Node absolute peak 均通过 hard gate并保存非空报告。
- [x] 四平台 warm median/p95 基线已受审提交，Node/Core bootstrap observations 已保存，后续各实现 `1.15x` regression gate 有真实失败测试。
- [ ] 现有 bounded/upstream parity、quality、memory、Node lifecycle 和 package tests 无回归。
- [ ] 六个同版本 npm tarballs 通过本地 registry、Node 22/24、真实 tiled OCR、禁网、integrity、SBOM 和 license 检查。
- [ ] release commit、CI artifacts、baseline digest、tarball integrity 和 dist-tag 形成可追溯 evidence。
- [ ] README 中英文、native API、Node design、model bundle、memory design、npm packaging 与 implementation status 已交叉更新。

任一项未完成时，对外准确表述只能是“tiled 设计中/实现中”，不能是“已支持”。
