# Python 资产盘点与精简评估

Status: Active review record
Scope: 仓库内全部 60 个 Python 文件（`oracle/`、`tools/`、`tests/python/`、`corpus/`）
Goal: 搞清每个文件的职责、是否被 CI 引用、以及如何在不削弱质量门禁的前提下降低 Python 在语言识别中的占比

## 1. 背景：为什么写这份文档

`light-ocr` 的核心是 C++17 OCR 引擎（PP-OCRv6），但仓库内 Python 行数（13516）超过了 C++（`.cpp` 10977 + `.hpp` 1331 = 12308）。GitHub Linguist 据此把项目识别为 Python，而 [.gitattributes](../.gitattributes) 里**没有任何 `linguist-*` 配置**——这是误判的直接原因。

这份文档既给出"删代码/挪代码"的方案，也给出"不删一行只改 Linguist 配置"的方案，让维护者自己权衡。

## 2. 总量盘点

统计口径：`git ls-files '*.py'`（已提交到仓库的真实文件，排除 `.cache/`、`build/`、`__pycache__/` 等本地产物）。

| 目录 | 文件数 | 行数 | 职责 |
| --- | ---: | ---: | --- |
| `tools/` | 22 | 7872 | CI 发布工具链：bootstrap、模型转换、打包、资格门禁、npm 发布编排 |
| `oracle/` | 24 | 3803 | PP-OCRv6 参考实现 + 对照比对/质量/golden 校验套件（CTest acceptance） |
| `tests/python/` | 13 | 2484 | 上述 Python 工具链的 unittest 单测 + fixture 工厂 |
| `corpus/` | 2 | 617 | 一次性语料生成器（产物已 check-in） |
| **合计** | **61** | **14776** | |

对照：C++（`.cpp` 39 个 + `.hpp` 24 个）共 **12308 行**。Python 多出约 2400 行，其中绝大部分来自 `tools/`。

## 3. 引用关系核心结论

判断"是否能删"的依据不是直觉，而是**是否被 CI 引用**。三个关键机制：

1. **`tests/python/test_*.py`（13 个文件）全部被 CI 调用** — [core.yml:108](../.github/workflows/core.yml) 的 `python3 -m unittest discover -s tests/python`，其中 6 个还被 [webgpu-native.yml:41-47](../.github/workflows/webgpu-native.yml) 显式门禁。3 个 `*_fixtures.py` 被这些 test 显式 import。
2. **`oracle/*.py` 大多数接入 CTest**，不是被 workflow 直接 `python oracle/xxx.py` 调用，而是由 [CMakeLists.txt:285-399](../CMakeLists.txt) 注册为 `acceptance` 测试，在 [core.yml:149](../.github/workflows/core.yml) 用 `ctest -L acceptance` 跑。判定 oracle/ 的引用必须查 CMake，不能只看 workflow YAML。
3. **`bindings/node/package.json` 的 scripts 只有一个 `node --test`**，与 Python 完全无关。所以"被引用"等价于"被 CI yml 或 CMake 引用"。

## 4. 分目录精简方案

### 4.1 corpus/ — 高置信度可归档（−617 行）

| 文件 | 行 | 现状 | 处置 |
| --- | ---: | --- | --- |
| [corpus/generate_tiled_corpus.py](../corpus/generate_tiled_corpus.py) | 412 | 一次性生成 `corpus/tiled-v1/fixtures/*`（8 个），产物已 check-in，生成器在 CI/CMake 零引用 | 归档到 `docs/corpus/` 或 `examples/`，保留源码供重建 |
| [corpus/generate_corpus.py](../corpus/generate_corpus.py) | 205 | 一次性生成 `corpus/fixtures/*`（14 个），同上 | 同上 |

这两个脚本带 `cv2`/`PIL`/`numpy` 重依赖，是推高 Python 占比的帮凶。产物（fixture 目录）必须留，生成器已"功德圆满"。

### 4.2 tools/ — 混合处置（潜在 −1235~−3048 行）

#### ⛔ 初判"可归档"但实测会被 CI 测试 import —— 必须保留

初版分析（基于 workflow yml 的 grep）曾判定下列文件"CI 零引用、可归档"。**复核 Python import 后推翻该结论**：这些模块被 `tests/python/` 下的单测直接 `from tools... import`，而单测经 `core.yml:108` 的 `unittest discover` 在 CI 中运行。归档会立即 ImportError 破坏 CI。

| 文件 | 行 | import 证据（CI 活跃） |
| --- | ---: | --- |
| [tools/webgpu/qualify.py](../tools/webgpu/qualify.py) | 1235 | [test_webgpu_qualification.py:18](../tests/python/test_webgpu_qualification.py) `from tools.webgpu import qualify`；[webgpu_report_fixtures.py:13](../tests/python/webgpu_report_fixtures.py) 同样 import。两者均被 core.yml:108 + webgpu-native.yml:46 跑。 |
| `tools/apple/` 4 个 gate | — | [test_apple_qualification.py:9-15](../tests/python/test_apple_qualification.py) `from tools.apple import accept_qualification, collect_qualification, fallback_gate, package_bundle, performance_gate`。该测试被 core.yml:108 跑。 |

> **方法论教训**：判断"是否被 CI 引用"不能只 grep workflow yml 里的 `python xxx.py` 调用，必须同时检查 `tests/python/` 里的 `from tools... import` 语句——`unittest discover` 会让所有 `test_*.py` 在 CI 跑，它们的 import 即真实引用。`tools/webgpu/qualify.py` 与 `tools/apple/*` 的 gate 模块都是这种情况。

apple/ 其余 4 个 gate（`qualify_models`/`quality_gate`/`cache_concurrency_gate`/`capture_identity`）虽未被测试直接 import，但与被测的 4 个 gate 通过相对 import 耦合（如 `fallback_gate` import `collect_qualification`），整组归档会牵连被测模块。**整组保留。**

#### 🟡 可去重（不删功能，预计 −300~−450 行）

最大重复是底层 helper：

- 流式 `sha256(path)` 在 7 处近乎逐字复制：[qualify.py:88](../tools/webgpu/qualify.py)、[npm_release.py:67](../tools/npm_release.py)、[generate_release_metadata.py:26](../tools/generate_release_metadata.py)、[package_model_bundle.py:20](../tools/package_model_bundle.py)、[tiled_release_gate.py:32](../tools/tiled_release_gate.py)、[review_reports.py:53](../tools/webgpu/review_reports.py)、[apple/package_bundle.py:21](../tools/apple/package_bundle.py)。
- `canonical_json` 在 3 处重复，`read_json`/`write_json` 在 4 处重复。
- `apple/package_bundle` 与 `webgpu/package_bundle` 流程同构（inventory→manifest→SHA256SUMS），可抽公共基类。

**前置条件**：`tools/` 当前没有 `__init__.py`（非 import 包），跨文件复用靠 `sys.path` hack。需先补 `tools/__init__.py` + `tools/common/__init__.py`，再抽 `tools/common/{hashing,jsonio,bundle}.py`。预计省 300-450 行。

#### 🟢 保留核心（活跃 CI，勿动）

`npm_release.py`(1188)、`bootstrap_models`/`bootstrap_dependencies`/`package_model_bundle`/`run_offline_check`(775)、`generate_release_metadata.py`(549)、`webgpu/{build_runtime,convert_models,package_bundle,review_reports}.py`(1830)、`tiled_release_gate.py`(400)、`apple/{convert_models,package_bundle}.py`(482)。

### 4.3 oracle/ — 保留为主（−65~−480 行）

`tiled_*` 系列**不是一次性实验**——接入 4 个 CTest 测试 + 2 个 workflow，有独立语料和 [docs/tiled-design-and-acceptance.md](tiled-design-and-acceptance.md)，是高分辨率分块识别的一等特性，整体保留。

#### 🔴 可删

| 文件 | 行 | 证据 |
| --- | ---: | --- |
| ~~[oracle/generate_smoke_corpus.py](../oracle/generate_smoke_corpus.py)~~ | 65 | 孤儿。生成的 `generated-hello-123` 与 [corpus/generate_corpus.py:199](../corpus/generate_corpus.py) 完全重复，且该 fixture 的 provenance 记录的 generator 是后者。全仓零引用（含 Python import 复核）。✅ **已删除**。 |

#### 🟡 可归档（脱离 CI，仅文档/manual）

| 文件 | 行 | 证据 |
| --- | ---: | --- |
| [oracle/run_memory_gate.py](../oracle/run_memory_gate.py) | 120 | 不在 CTest/workflow。真正的 CI 门禁是 native CTest 二进制 `light_ocr_memory_gate`（[CMakeLists.txt:244](../CMakeLists.txt)），本脚本仅是 benchmark JSON 的交叉检查包装器，文档 [build-and-release.md:210](build-and-release.md) 有提及。 |
| [oracle/run_benchmark.py](../oracle/run_benchmark.py) | 89 | 不在 CTest/workflow，仅文档 manual 命令。tiled 版已在 CI；单遍延迟门被 parity+quality 覆盖。若一并归档 `benchmark.py`(208) 则共减 297 行。 |

> B 档置信度只到"中"：维护者可能在做发布报告时手动跑。删/归档前请确认。

#### 🟢 保留核心

接入 CTest `-L acceptance` 的 20 个文件全部保留（详见各文件的 CTest 注册：`test_compare`、`generate_goldens`、`verify_ground_truth`、`tiled_ground_truth`、`generate_tiled_goldens`、`verify_contracts`、`run_parity`、`run_corpus`、`run_quality`、`run_tiled_*` 等）。

### 4.4 tests/python/ — 全部保留（−0 行）

**这 13 个文件测的是 Python 发布工具链 `tools/*.py` 本身**（锁校验、artifact 装配、descriptor 防篡改、质量门算术），不是端到端调用 C++/Node 产物。全仓库仅 2 处 subprocess（调 cmake 和 npm 各一次，带 `shutil.which` 跳过保护）。

**"改写成 Node.js 测试"不可行**：要迁测试得先重写整个 `tools/` 发布流水线（7872 行），而那些工具本身就是 CI 流水线的组成部分。`bindings/node/test/*.test.cjs` 已覆盖 Node 适配层，与这批 Python 单测职责不重叠。

## 5. 方案对比：删代码 vs 改 Linguist 配置

| 维度 | 方案 A：改 `.gitattributes` | 方案 B：归档 + 删除 | 方案 C：A + B |
| --- | --- | --- | --- |
| 实施成本 | 改 1 个文件，加几行 | `git mv` + 改文档引用 + 补 `__init__.py` | 两者之和 |
| 风险 | 几乎为零 | 复核 import 后，可归档面大幅收窄 | 中等 |
| 减少行数 | 0（Linguist 统计忽略，代码仍在） | 实测仅 generate_smoke_corpus(65) 安全可删 | 同 B |
| 是否影响 CI | 否 | 否（已删除的孤儿经 import 复核确认零引用） | 否 |
| 治本程度 | ✅ 直接解决语言识别误判 | ⚠️ 可删面太小，无法靠删代码反超 C++ | ✅ |
| 代码可维护性 | 不变 | 改善（去重后） | 改善 |

> **修正说明**：初版曾把 `tools/webgpu/qualify.py`(1235) 与 `tools/apple/` 8 个 gate(1413) 计入"方案 B 可归档"，但 import 复核发现它们被 CI 测试直接 import，归档会破坏 CI。实际可安全删除的只有已删除的孤儿脚本。方案 B 对语言占比几乎无贡献，**根治语言误识别应优先用方案 A**。

### 方案 A 的 `.gitattributes` 写法（推荐先做）

```
# 发布/资质/测试工具链是 CI 基础设施，不计入语言识别
tools/**        linguist-detectable=false
oracle/**       linguist-detectable=false
tests/python/** linguist-detectable=false
corpus/**       linguist-detectable=false

# 一次性生成器标记为 generated（即使 detectable 也降权）
corpus/generate_*.py linguist-generated=true
```

> `linguist-detectable=false` 让这些目录不参与语言占比计算，但代码仍在仓库里、CI 照跑。这是 GitHub 官方推荐的做法，一行 CI 都不用改。

详见 [github/linguist - Overrides](https://github.com/github-linguist/linguist#overrides)。

## 6. 落地优先级（按"减行/风险比"排序）

1. ✅ **改 `.gitattributes`**（0 风险，立即修正语言识别）。**已完成**——这是治本动作，GitHub commit+push 后 Linguist 会重新统计，项目将正确显示为 C++。
2. ✅ **删 `oracle/generate_smoke_corpus.py`**（65 行，零引用孤儿，已做 import 复核）。**已完成**。
3. ⛔ ~~**归档 `tools/webgpu/qualify.py`**~~ —— **取消**。import 复核发现被 [test_webgpu_qualification.py](../tests/python/test_webgpu_qualification.py) 直接 import，归档会破坏 CI。
4. ⛔ ~~**归档 `tools/apple/` 资格组**~~ —— **取消**。[test_apple_qualification.py](../tests/python/test_apple_qualification.py) 直接 import 了其中 4 个 gate，整组归档会破坏 CI。
5. **抽 `tools/common/` 去重 sha256/json helper**（300-450 行，机械重构，13 文件受益）。可选，与语言识别无关，纯代码卫生；改动面大需跑测试验证。
6. **corpus 生成器**：`.gitattributes` 已把 `corpus/**` 排除出语言统计，移动它们对语言识别无额外收益且会破坏脚本内相对路径，**保持原位**。

## 7. 不确定项

- `tools/webgpu/qualify.py` 和 `tools/apple/` 的 gate 脚本**确实被 CI 测试 import**（已核实），不能删除/归档；但维护者本地是否还手动跑这些脚本不影响该结论。
- Apple provider 的发布路线图未知。即便 provider 搁置，因 gate 模块被测试 import，也不能简单归档；若要清理需先重构测试或显式 skip 这些测试。
- 本报告行数均来自 `Get-Content | Measure-Object -Line` 实测，与 Linguist 统计口径可能存在差异；最终语言占比以 GitHub 仓库页面为准。`.gitattributes` 配置需 commit 并 push 到默认分支后 Linguist 才会重新计算。
