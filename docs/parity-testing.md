# light-ocr 对齐、语料与质量验证

状态：已实现；macOS arm64 本地证据已通过，Tier 1 CI 证据待运行  
范围：Python oracle、raw-pixel 语料、全阶段 checkpoint、容差、例外和质量基线  
需求：[requirements.md](requirements.md)  
模型：[model-bundle.md](model-bundle.md)

## 1. 目标和边界

对齐测试回答的是：C++ Core 是否在相同 raw pixels、相同 ONNX bytes、相同配置、相同线程设置下复现选定的 PP-OCRv6 v3.7.0 行为。

它不是模型准确率声明。模型质量由独立的 ground-truth report 记录；首 bundle 只建立基线，不设置事后发明的通过阈值。

## 2. Oracle 身份

`oracle/oracle.lock.json` 固定：

- CPython 3.11.11。
- PaddleX 3.7.0。
- ONNX Runtime 1.22.0 CPU EP。
- OpenCV contrib Python 4.10.0.84。
- NumPy 2.1.3。
- pyclipper 1.3.0.post6。
- PaddleOCR v3.7.0 revision `b03f46425e8ff4442b268ce449e3eef758146cd4`。
- 六个采用行为的官方源码文件及 Git blob SHA-1。

`oracle/requirements.lock` 对所有 Python 分发包使用 exact version 和 hash。安装命令必须带 `--require-hashes`。

Oracle 不调用会自动下载模型的高层入口。`oracle/oracle.py` 直接读取本地已验签 bundle，创建两个持久 ONNX Runtime session，并逐阶段实现经官方源码核对的 resize、normalize、DB、sort/crop、recognition batch 和 CTC decode。

## 3. 已核对的官方行为

官方 v3.7.0 各入口的默认值并不相同。本 bundle 明确选择：

- detection resize：`limitSideLen=64`、`limitType=min`、`maxSideLimit=4000`。
- DB：threshold `0.3`、box threshold `0.6`、unclip ratio `1.5`、fast score。
- detection/recognition 都以 BGR 字节进入 normalize。
- recognition：高 48、基础宽 320、最大宽 3200、batch 8。
- 18,708 个 YAML 字符后只追加一个 ASCII space，得到 18,709 个非 blank 字符；class 0 是 CTC blank。
- sort/crop 使用 10 px row band、cubic perspective、replicate border，长宽比至少 1.5 时逆时针旋转 90°。

这些值全部来自 bundle 的 `normalized-config.json`，C++ 和 oracle 均不依赖隐式默认值。

## 4. 语料

真实文件布局是：

```text
corpus/
  sources.lock.json
  contracts.json
  ground-truth.lock.json
  parity-exceptions.json
  goldens.lock.json
  goldens/<fixture-id>.json
  fixtures/<fixture-id>/
    fixture.json
    pixels.bin
```

`pixels.bin` 是比较边界，encoded source 只用于可重复生成。每个 fixture 都记录尺寸、stride、pixel format、pixel SHA-256、rights、provenance 和 tags。

当前有 14 个 raw BGR fixtures：

- 6 个项目生成：blank、英文数字、日文横排/旋转竖排、繁体中文混排、低对比透视。
- 8 个来自 PaddleOCR v3.7.0 的固定 Git blob：展示文字、识别样例、登机牌、书页、园区标牌、XFUND 表单和验证码/手写场景。

覆盖项包括简体、繁体、日文、英文、数字、标点、混排、横排/竖排、dense/sparse、small text、低对比、光照不均、透视、旋转、手写、display/artistic/industrial 和 blank。`corpus/contracts.json` 另外把 1×1、最大接受尺寸、bad stride、truncated、oversized、malformed bundle、unsupported capability 和 invalid tensor 映射到具体测试；`light_ocr_contract_manifest_verify` 会拒绝失效的测试名或 corpus revision。

Noto CJK JP/TC 字体和官方图片均在 `corpus/sources.lock.json` 固定 URL、revision/blob、字节数、SHA-256 与许可；`corpus/generate_corpus.py` 会先复核再生成全部 14 个 raw-pixel fixtures，并校验目录集合。当前 corpus revision 为 `20260714.1`，从空输出目录重建后与提交语料逐字节一致。

## 5. 不可变 stage goldens

`corpus/goldens/` 保存 14 个 canonical JSON oracle records，总计约 6 MiB；`goldens.lock.json` 固定每个文件的字节数/SHA-256、bundle manifest、oracle environment lock 和 `oracle.py` source SHA-256。

```bash
# 仅在有意创建新 corpus/oracle revision 时生成
.cache/oracle-venv/bin/python oracle/generate_goldens.py \
  --bundle models/generated/ppocrv6-small-onnx-20260713.1

# 正常验收只做逐字节再生验证
.cache/oracle-venv/bin/python oracle/generate_goldens.py \
  --bundle models/generated/ppocrv6-small-onnx-20260713.1 \
  --verify
```

`run_parity.py` 默认只与锁定 golden 比较；`--live-oracle` 仅用于诊断。CI 将“oracle 逐字节重现 golden”和“native 对比 golden”作为两个独立步骤，因此比较不会静默重写预期结果。

## 6. Stage probe

`light_ocr_stage_probe` 是测试专用程序，不属于公共 C++ API。一次记录包含：

- 输入尺寸、bundle ID，以及 detection/recognition ONNX 的实际 input/output 名称。
- detection input/output 的 shape、float32 little-endian SHA-256 和固定 sample positions。
- threshold bitmap SHA-256、contour candidate count。
- 每个 DB candidate 的 score、initial quad、unclip polygon、expanded quad、restored quad 和最终过滤决策。
- 排序后的 boxes。
- crop 尺寸、BGR8 SHA-256，以及比较时使用的 base64 pixels。
- recognition batch membership、input/output shape、SHA-256、samples。
- CTC selected indices/probabilities、text、confidence。
- 最终 ordered lines。

生产 `Engine::recognize` 不启用 DB trace，也不会返回 tensors 或 pixels。

## 7. 比较门槛

`oracle/compare.py` 使用以下固定规则：

| Checkpoint | 门槛 |
| --- | --- |
| detection input/output shape 与 bytes | exact SHA-256 |
| threshold bitmap、candidate count、filter decision | exact |
| 一般 DB candidate score | absolute difference ≤ `1e-5` |
| DB candidate geometry | 对应点最大距离 ≤ `0.01` map pixel |
| final line count/order/text | exact |
| final box | IoU ≥ `0.98` 且对应角点距离 ≤ `2 px` |
| crop | exact SHA-256；否则 max channel diff ≤ `3` 且 mean diff ≤ `0.05` |
| recognition batch membership/shape | exact |
| recognition input/output values | exact SHA-256；否则固定 samples max diff ≤ `0.02` |
| CTC selected indices 与 text | exact |
| recognition confidence | absolute difference ≤ `0.001` |

Crop 的 bounded path 用于不同 OpenCV 二进制在 cubic interpolation 上的少量取整差异；报告始终保留 exact-hash 状态、最大值、均值和受影响值数量。

## 8. 例外 PX-0001

`corpus/parity-exceptions.json` 当前只有一个窄范围例外：

- fixture：`paddleocr-book-page`
- checkpoint：`detectionCandidates[129].score`
- score absolute difference ≤ `0.013`
- 强制 invariant：两边 decision 必须完全等于 `below_box_threshold`

实测 C++ 与 Python OpenCV 的 `minAreaRect` 角点约差 `0.00006 px`，恰好跨过官方 `astype(int32)`/fillPoly 遮罩边界，令一个已拒绝候选的 score 相差 `0.0122227603`。其余 331 个已观察 score 继续使用 `1e-5`；例外不会放宽候选决策、最终 box 或最终 text。

例外在 bundle、OpenCV、编译器或 DB 实现变化时到期。报告列出 `appliedExceptionIds`。

## 9. 执行

单 fixture：

```bash
.cache/oracle-venv/bin/python oracle/run_parity.py \
  --native-probe build/preset-release/bin/light_ocr_stage_probe \
  --bundle models/generated/ppocrv6-small-onnx-20260713.1 \
  --fixture corpus/fixtures/generated-hello-123/fixture.json
```

完整语料和质量报告：

```bash
ctest --test-dir build/preset-release --output-on-failure -L acceptance
```

报告默认位于 build tree 的 `reports/parity/` 和 `reports/quality/`。报告只记录 probe 文件名与 SHA-256，不嵌入 workspace 绝对路径。

## 10. 当前结果

2026-07-14 在 macOS arm64、Apple Clang 21、Release、单线程 ORT 下：

- 全阶段对齐：14/14 fixtures passed。
- 应用例外：仅 `PX-0001`。
- 质量 ground truth：10 fixtures；9 个 exact line match。
- CER：`1 / 104 = 0.0096153846`。
- 独立 detection 标注：10 个 quadrilateral；IoU 阈值 `0.5` 下 `10 TP / 0 FP / 0 FN`，precision/recall/Hmean 均为 `1.0`。
- 唯一文字差异：期望日文间隔号 `・`，模型输出中文间隔号 `·`。

这是首 bundle 的文字、detection 和端到端基线，不是一般化产品准确率声明。生成语料的框来自渲染布局几何，官方图片的框由项目按可见文字区域维护；两者都不取自 OCR 输出。`corpus/ground-truth.lock.json` 将每份标注与 raw pixel SHA-256 绑定，`light_ocr_ground_truth_verify` 会校验文本/框数量、有限且顺时针的图像内四边形、标注 hash 和实际像素 hash。后续扩大标注集或修改标注必须提升 corpus revision、重锁并重建报告。

## 11. 更新规则

模型 bytes、normalized config、PaddleOCR oracle revision、OpenCV、Clipper/pyclipper、ORT 或相关算法变化时，必须：

1. 在不修改预期结果的情况下重新运行全阶段比较。
2. 对所有差异做 checkpoint 级 root-cause analysis。
3. 复核并重新批准或删除每个例外。
4. 模型变化时重做 quality 与 performance report。
5. 只有预期产品行为确实改变时才提升 corpus revision；禁止为了让 CI 变绿直接覆盖 golden/ground truth。

四个 Tier 1 平台的最终结果仍必须由 `.github/workflows/core.yml` 的真实 CI run 证明；本机 macOS arm64 结果不能替代其他平台。
