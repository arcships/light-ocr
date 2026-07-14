# light-ocr 高分辨率内存优化设计

状态：第一阶段已发布；`tiled` 第二阶段 Core/Node 已实现，完整质量与四平台基线验收进行中

Tiled authority：[Tiled Detection 技术设计与验收规格](tiled-design-and-acceptance.md)。本文保留背景与高层方案；分块算法、runtime contract、API、语料、四平台门槛和完成定义以独立规格为准。

适用范围：C++ Core；Node-API 只映射 Core 策略，不重新实现 OCR 逻辑

依据：PP-OCRv6 small、ONNX Runtime CPU、PaddleOCR v3.7.0 revision `b03f46425e8ff4442b268ce449e3eef758146cd4`

## 1. 结论

默认运行策略改为：

- 检测采用 `bounded` 策略，最长边限制为 `960`，保持宽高比并对齐到 32 的倍数。
- `4,000` 继续作为 bundle 兼容性 ceiling，不再代表产品默认输入尺寸。
- 识别默认 batch 从 `8` 降为 `1`，按 batch 即时 crop、normalize、infer、decode、release。
- Core 不得在识别开始前构造全部 crop tensor 或全部 recognition batch。
- 默认路径不得复制完整 ORT 输出；decode 在一个拥有 ORT output 生命周期的内部 view 上完成。
- 大于默认检测尺寸的准确模式使用有重叠分块；整图 `64/min/4000` 只保留为显式 `upstream_exact` 诊断模式。

`bounded + batch=1 + streaming` 是 npm v1 的发布阻断项。`tiled` 在通过边界文字质量门槛后才成为公开能力；在此之前不得把 `upstream_exact` 作为默认高分辨率方案。

## 2. 问题与实测证据

当前 bundle 直接采用模型导出配置：

```text
limitSideLen = 64
limitType = min
maxSideLimit = 4000
recognitionBatch = 8
```

`min` 只保证短边不小于 64。对 2048×2048 输入，它不会缩小图片，因此检测模型接收完整的 `1×3×2048×2048 float32` tensor。

macOS arm64、单进程、单张图片、PP-OCRv6 small 的冷启动实测为：

| 输入 | 当前峰值 RSS | 将输入预缩到 960 后 |
| --- | ---: | ---: |
| 2048×2048 空白图 | 918.8 MiB | 280.5 MiB |
| 2048×2048 轻量内容 | 932.1 MiB | 未单独记录 |
| 2048×2048 密集表单，约 127 个文本框 | 2.08 GiB | 1.03 GiB |

原始 BGR 图片只有 12 MiB，2048² detection input tensor 为 48 MiB。其余峰值主要来自检测 activation、ORT workspace/arena，以及密集页面的 recognition output 和同时存活的批次。

将检测限制到 960 可以解决检测阶段的大部分峰值，但不能单独解决密集文档；识别必须同步改为流式生命周期。

## 3. 上游行为

PaddleOCR 同一仓库中存在两类默认值，不能把任一入口视为唯一事实：

| 上游入口 | 检测默认 | 识别与内存行为 |
| --- | --- | --- |
| legacy Python `tools/infer` | `960/max` | 默认 batch 6；逐 batch 构造；Paddle backend 启用 `enable_memory_optim()` |
| 旧 Android/Paddle Lite demo | `960/max` | 逐文本框识别；直接写 predictor input；Paddle Lite 有图级内存复用 pass |
| 新 PP-OCRv6 Android/ORT SDK | `64/min/4000` | 默认 batch 1；逐 batch crop 和释放；ORT graph optimization |
| PaddleOCR.js | 默认 pipeline 提供 `64/min/4000`；`960/max` 仅是缺字段时的 parse fallback | pipeline 默认 batch 6；recognition 按 batch 循环；session 提供显式 dispose |

官方参考：

- [legacy Python 960/max 默认值](https://github.com/PaddlePaddle/PaddleOCR/blob/b03f46425e8ff4442b268ce449e3eef758146cd4/tools/infer/utility.py#L59-L65)
- [Python Paddle backend memory optimization](https://github.com/PaddlePaddle/PaddleOCR/blob/b03f46425e8ff4442b268ce449e3eef758146cd4/tools/infer/utility.py#L390-L413)
- [Python recognition 分 batch 执行](https://github.com/PaddlePaddle/PaddleOCR/blob/b03f46425e8ff4442b268ce449e3eef758146cd4/tools/infer/predict_rec.py#L586-L696)
- [旧 Android 长边 960 resize](https://github.com/PaddlePaddle/PaddleOCR/blob/b03f46425e8ff4442b268ce449e3eef758146cd4/deploy/android_demo/app/src/main/cpp/ocr_ppredictor.cpp#L108-L160)
- [新 Android 默认配置](https://github.com/PaddlePaddle/PaddleOCR/blob/b03f46425e8ff4442b268ce449e3eef758146cd4/deploy/ppocr-android/ppocr-sdk/src/main/java/com/paddle/ocr/PaddleOCRConfig.kt)
- [新 Android 流式识别](https://github.com/PaddlePaddle/PaddleOCR/blob/b03f46425e8ff4442b268ce449e3eef758146cd4/deploy/ppocr-android/ppocr-sdk/src/main/java/com/paddle/ocr/engine/OCREngine.kt#L80-L145)
- [PaddleOCR.js pipeline 默认值](https://github.com/PaddlePaddle/PaddleOCR/blob/b03f46425e8ff4442b268ce449e3eef758146cd4/paddleocr-js/packages/core/src/pipelines/ocr/default-config.ts#L29-L50)
- [PaddleOCR.js `960/max` parse fallback](https://github.com/PaddlePaddle/PaddleOCR/blob/b03f46425e8ff4442b268ce449e3eef758146cd4/paddleocr-js/packages/core/src/models/det.ts#L84-L140)
- [PaddleOCR.js recognition batch loop](https://github.com/PaddlePaddle/PaddleOCR/blob/b03f46425e8ff4442b268ce449e3eef758146cd4/paddleocr-js/packages/core/src/models/rec.ts#L114-L156)
- [Python 可选切片检测](https://github.com/PaddlePaddle/PaddleOCR/blob/b03f46425e8ff4442b268ce449e3eef758146cd4/tools/infer/predict_system.py#L76-L107)

本项目的产品默认值以宿主内存安全和经过验证的质量门槛为准。模型 YAML 仍是算法来源和 `upstream_exact` 证据，但不直接决定 npm 默认资源策略。

## 4. 目标与非目标

### 4.1 目标

- 单张 2048² 图片不能因为默认策略形成完整 2048² detection tensor。
- 峰值随当前 batch 上界增长，不随整页文本框总数线性增长。
- 空白、稀疏和密集页面都有绝对峰值 RSS 报告。
- 默认结果与使用相同运行参数的 Python oracle 对齐。
- 高分辨率准确模式保留小文字，不依赖一次性整图推理。
- 同一 engine 多次调用后没有无界 RSS 增长。

### 4.2 非目标

- 不把 30 MiB 权重大小当作进程内存承诺。
- 不承诺用 `max_temporary_bytes` 精确限制 ORT 内部 workspace。
- 不在第一阶段引入 GPU Execution Provider、量化模型或替换推理引擎。
- 不用简单关闭 ORT arena 代替尺寸和生命周期优化。
- 不以降低质量门槛或删除密集页面测试换取较低内存数字。

## 5. 检测策略

### 5.1 `bounded`：默认

默认配置：

```text
strategy = bounded
limitType = max
maxSide = 960
dimensionMultiple = 32
```

规则：

1. 短边小于 64 时先等比例放大到 64；若放大后长边会超过 960，则仍以长边 960 为准。
2. 长边大于 960 时等比例缩小到 960；位于 `[64, 960]` 范围内的普通输入不做整体缩放。
3. `bounded` 的两个结果维度都向上对齐到 32 的倍数；该规则避免 190→142→128 这类向下对齐造成的小字质量回退，同时不会突破 960，因为 960 本身可被 32 整除。
4. 坐标按实际 resize ratio 恢复到原图。
5. `max_detection_side=4000` 仍作为 hard ceiling，不能改变默认 960。

示例：

| 原图 | 默认 detection tensor 尺寸 |
| --- | --- |
| 800×180 | 800×192，按现有倍数规则对齐 |
| 225×46 | 320×64，保留小图检测能力 |
| 1280×190 | 960×160，bounded 向上对齐 |
| 2048×1024 | 960×480 |
| 2048×2048 | 960×960 |
| 5000×2500 | 960×480 |

### 5.2 `tiled`：高分辨率准确模式

初始参数固定为：

```text
strategy = tiled
tileSide = 1280
tileOverlap = 128
```

规则：

1. 宽和高都不大于 1280 时退化为单 tile。
2. 大图按 1280×1280 窗口切分，相邻窗口重叠 128；最后一个窗口锚定图像末端。
3. 每个 tile 独立执行 detection，不放大，最长边不超过 1280，并对齐到 32 的倍数；坐标增加 tile offset 后恢复到全图空间。
4. 触碰人工 tile 边界、但未触碰原图边界的候选标记为边界候选。
5. 重复框用 polygon IoU `>= 0.50` 或 intersection-over-smaller-area `>= 0.80` 合并；优先保留 DB score 更高、距离人工边界更远的候选。
6. 合并后只执行一次全局 reading-order sort 和 recognition。

这些阈值是初始实现参数，不是永久模型事实。任何修改都必须通过 tile-boundary corpus、small-text ground truth 和 `upstream_exact` 对照。

### 5.3 `upstream_exact`：显式诊断模式

该模式使用 bundle 的 `64/min/4000` 行为，只用于：

- stage parity 和上游差异定位；
- 创建或更新 oracle goldens；
- 用户明确接受高内存占用的受控诊断。

它不是 npm 默认能力，也没有低内存承诺。公开适配器若暴露该模式，名称必须包含 `upstreamExact`，不能使用含糊的 `highQuality`。

## 6. 识别流式化

默认 recognition batch 改为 `1`，bundle ceiling 保持 `8`。显式 batch `2..8` 是吞吐优先选项，不改变内存安全检查。

单次请求按以下生命周期执行：

1. detection 完成并得到全局有序 boxes。
2. 保存轻量 box/index 元数据，不预先保存全部 crop tensor。
3. 为当前 batch 创建 crop。
4. resize、normalize 并构造当前 batch input。
5. 运行 recognition。
6. 直接在仍存活的 ORT output 上完成 CTC decode。
7. 将解码结果写回原始 box index。
8. 释放 ORT output、batch input 和 crop，再进入下一 batch。

不允许以下当前行为继续存在：

- 在 inference 前构造 `std::vector<RecognitionBatch>` 的全部 tensor payload；
- 让所有 crop 一直存活到全部 recognition input 完成；
- 把完整 ORT output 再复制到同尺寸 `std::vector<float>` 后才 decode。

完成后，密集页面的 Core-owned recognition memory 应近似：

```text
one batch crops + one batch input + one ORT output + one batch decode state
```

而不是所有文本行的总和。

## 7. 目标 Core 配置契约

第一阶段已实现的公开契约为：

```cpp
enum class DetectionStrategy {
  bounded,
  upstream_exact,
};

struct DetectionOptions {
  std::optional<DetectionStrategy> strategy;
  std::optional<std::uint32_t> max_side;
};

struct EngineOptions {
  // Existing thread and threshold fields remain.
  DetectionOptions detection;
  std::optional<std::uint32_t> recognition_batch_size;  // effective default: 1
};
```

`DetectionStrategy::tiled` 已进入 0.2.0 candidate headers；tile side、overlap 和 merge threshold 不作为用户旋钮，而由 schema 1.2 bundle 的 `tiled-v1` contract 固定。它尚未随 npm 版本发布。

约束：

- `bounded.max_side` 可降低，不得超过 bundle `max_detection_side`。
- `upstream_exact` 只能在 engine creation 时选择；单个请求不能把一个 bounded engine 升级成 exact。
- request option 可以进一步降低 detection max side，也可以在 `1..effective batch ceiling` 内选择 recognition batch；两者都不能提高 engine ceiling，request 也不能把 bounded engine 升级成 exact。
- `EngineInfo` 返回 effective strategy、side 和 batch，报告不得只显示 bundle ceiling。
- Node-API 后续只做枚举和字段映射；默认仍由 Core 决定。

normalized bundle 必须分开记录：

- `sourceDetectionResize`：官方 YAML 的 `64/min/4000`，用于 provenance 和 exact oracle。
- `runtimeDefaults.detection`：本项目默认的 `bounded/960`。
- `resourceLimits.maxDetectionSide`：兼容性 hard ceiling `4000`。

三者不能再复用同一个“limit”字段表达不同语义。

schema `1.1` 还明确记录 `minimumShortSide=64` 与 `dimensionMultipleRounding=ceil`；旧 schema `1.0` 继续解释为 `upstream_exact`，不被静默改写。

## 8. ONNX Runtime 内存策略

优先完成尺寸限制和识别流式化，再进行以下 A/B 实验：

- CPU arena enabled/disabled；
- memory pattern enabled/disabled；
- intra-op threads 1/2/4；
- ORT output owning view 与当前 output copy；
- session close 后 allocator trim 只作为测试测量归一化，不作为生产释放保证。

选择规则：

- 默认值由 warm latency、absolute peak RSS、steady RSS 三者共同决定。
- 关闭 arena 如果只降低调用后的 retained RSS、却显著增加下一次 latency，不自动胜出。
- 任何 ORT 选项都不得改变 tensor contract 或最终结果。
- 不把 `malloc_trim` 放入生产 `recognize` 热路径。

## 9. Parity 与质量

验证分成两个 profile：

### 9.1 `upstream_exact`

- 保留现有 `64/min/4000`、batch 8 stage goldens。
- 用于证明算法 primitive、模型 bytes 和上游来源没有漂移。
- 不作为产品默认内存或性能基线。

### 9.2 `bounded_default`

- Python oracle 显式使用 `960/max` 和相同 recognition batch。
- 重新生成 detection shape、box、crop、recognition batch 和 final-result 报告。
- 原有 ground-truth fixtures 不得出现未批准的 CER、line count、reading order 或 detection Hmean 回退。
- 新增至少三类 2048² fixtures：空白、稀疏小文字、密集表单。

当前本地证据：两套 profile 均通过 14/14 全阶段 parity；`bounded_default` 在 10 个独立 ground-truth fixtures 上达到 10/10 exact、CER `0`、detection precision/recall/Hmean `1.0`。这只是锁定语料基线，不是一般化准确率声明。

### 9.3 `tiled`

本节仅保留质量方向；可执行 fixture matrix、merge 判定、oracle、报告与 hard gates 见 [Tiled Detection 技术设计与验收规格](tiled-design-and-acceptance.md)。

- 新增文字跨水平边界、垂直边界、四 tile 交点和原图边缘的 fixtures。
- 不得产生重复行。
- 与 `upstream_exact` 对应结果的文字、顺序和置信度执行差异报告。
- polygon merge 阈值必须锁入 normalized config 或明确的版本化 runtime contract。

改变默认 resize 会改变 goldens。实施时必须创建新的 bundle/profile lock 与独立 golden 目录，不能覆盖旧报告后假装行为未变；raw pixels 和 ground truth 未变化时不虚增 corpus revision，只有语料或标注变化才提升它。

## 10. 内存与性能验收

所有峰值以独立进程测量；报告同时包含原图尺寸、effective detection shape、文本框数、recognition batch shapes、线程数和策略。

macOS arm64 首个实现门槛：

| 场景 | Core 峰值 RSS 门槛 |
| --- | ---: |
| 2048² 空白，`bounded/960` | `<= 384 MiB` |
| 2048² 密集表单，约 127 框，batch 1 streaming | `<= 640 MiB` |
| 800×180 现有 smoke | 不高于当前绿色 CI peak 的 `1.10x` |

跨平台要求：

- Linux x64、Windows x64、macOS x64/arm64 都保存绝对峰值，不用一个平台的 bytes 作为其他平台硬等值。
- 每个平台的 release baseline 一经接受，后续增长超过 `15%` 即失败。
- 5 次 warmup 后 10 次 lifecycle 的现有 `32 MiB total / 8 MiB per cycle` 增长门槛保持不变。
- queue depth 0 的 Node-API 单请求绝对峰值不得超过 `1088 MiB`；同平台 Core 峰值差作为跨进程 observation 保存，排队 snapshot 预算另行报告。
- 性能仍与使用相同 resize、batch、线程和 pixels 的 Python oracle 比较；当 runtime binary 或采样进程不可直接比较时，按 requirements 保存为 observation，并以各实现受审 baseline 的 `15%` 回归门槛为准。

`640 MiB` 是第一阶段发布上限，不是目标值。完成 output zero-copy 和 ORT A/B 后应争取将密集场景压到 `512 MiB` 以下。

当前 macOS arm64 Release 原生 CTest 实测：2048² 空白图 `318.8 MiB`（0 框），2048² xfund 密集表单 `400.5 MiB`（116 框）；两者分别低于 `384 MiB` 与 `640 MiB` 门槛，密集场景也低于 `512 MiB` 目标。正式 CTest 由独立进程 `light_ocr_memory_gate` 生成报告，不依赖 Python；`oracle/run_memory_gate.py` 用于交叉检查 benchmark JSON。

## 11. 实施顺序

1. **已完成**：增加独立进程 memory benchmark，固化 2048/960 证据。
2. **已完成**：recognition 改为逐 batch crop/input/infer/decode/release，默认 batch 改为 1。
3. **已完成**：用 owning ORT output view 替代完整 output copy。
4. **已完成**：增加 `DetectionStrategy`，实现默认 `bounded/960`，保留显式 `upstream_exact`。
5. **已完成**：更新 normalized config schema、bundle ID/revision、C++/Node 类型和 `EngineInfo`。
6. **已完成（本地）**：建立 `bounded_default` Python oracle 与独立 goldens，完成 quality/parity 回归。
7. **进行中**：`tiled-v1` planner、顺序 detection、全局 merge、Core/Node contract 与资源工具已实现；tile-boundary corpus、独立 oracle 和四平台 accepted baseline 待完成。
8. 执行 ORT arena/memory-pattern/thread A/B，锁定最终 backend 选项。
9. 在四个平台保存 absolute peak、steady RSS、latency 和 release baselines。

步骤 2、4、6 已完成。步骤 7 的代码主链路已经在本机通过 2048 blank/dense/boundary 验证，但完整质量与四平台证据未完成；在独立规格的 checklist 全绿前不公开 `tiled`，也不对原尺寸小文字准确模式作承诺。

## 12. 被拒绝的替代方案

- **只压缩或量化权重**：30 MiB 权重不是 0.9–2.1 GiB 峰值的主要来源。
- **只把 `maxTemporaryBytes` 调低**：它不覆盖 ORT activation/workspace，不能形成真实进程上限。
- **只关闭 ORT arena**：可能降低 retained RSS，但完整 2048 detection activation 仍然存在。
- **继续 batch 8，只降低 detection 尺寸**：960 密集表单当前仍约 1.03 GiB。
- **默认 `upstream_exact` 并写警告**：Node 常驻进程仍会承担不可接受的峰值。
- **无重叠硬切图**：会切断跨边界文字并造成漏检或重复识别。
- **直接把 Android demo 当作内存证明**：新版 Android 同样使用 `64/min/4000`，现有 benchmark 记录调用前后 PSS，但没有 2048 peak gate。

## 13. 完成定义

第一阶段发布完成需要同时满足：

- public default 的 effective detection shape 对 2048² 为 960²；
- recognition 代码路径不存在全 batches tensor materialization；
- default batch 为 1，显式 batch 2..8 有独立峰值报告；
- `upstream_exact` 与 `bounded_default` 两套 parity 都可重建；
- 四平台 absolute peak 和 lifecycle growth 通过；
- 2048 small-text 和 dense ground truth 通过；
- 文档、`EngineInfo`、C++ API、Node types 和实际代码一致。

公开 `tiled` 还必须满足：

以下四项的精确定义与逐项 checklist 见 [Tiled Detection 技术设计与验收规格 §14](tiled-design-and-acceptance.md#14-完成定义)。

- 2048 small-text、dense、水平/垂直边界、四 tile 交点和原图边缘 ground truth 通过；
- 合并后没有重复行，reading order 稳定；
- 四平台 tiled absolute peak 和 latency 基线已保存；
- `DetectionStrategy::tiled`、Node types 与版本化 runtime contract 同时发布。
