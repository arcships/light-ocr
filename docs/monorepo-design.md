# light-ocr Monorepo 设计

状态：已接受用于 N2 阶段 1（2026-07-22）。
受众：维护者、贡献者。本文定义 monorepo 的目录结构、包依赖关系、迁移路径和约束，不替代各包自身的实现设计。

## 1. 动机

Roadmap §3.1 规划了 7+ 个 npm 包（runtime、light-ocr、tiny、medium、document、layout、model-*），加上 server-side OCR 服务后达到 8+ 个。当前 `bindings/node/` 单包结构无法支撑跨包改动（如 runtime 提取后 tiny/medium/document 都需要同步更新）。Monorepo 用 npm workspaces 统一管理这些包，保持一次 install、一次 lint、一次 test 的开发体验。

## 2. 目录结构

```
light-ocr/
├── package.json              # root workspace config（private, workspaces）
├── packages/
│   ├── runtime/              # @arcships/light-ocr-runtime
│   │   ├── package.json
│   │   ├── src/
│   │   └── test/
│   ├── light-ocr/            # @arcships/light-ocr（small 默认，从 bindings/node 迁移）
│   │   ├── package.json      # 含 "bin": { "light-ocr": "./src/cli.cjs" }
│   │   ├── src/
│   │   └── test/
│   ├── light-ocr-server/     # @arcships/light-ocr-server（REST API + Docker）
│   │   ├── package.json
│   │   ├── Dockerfile
│   │   ├── .dockerignore
│   │   ├── src/
│   │   └── test/
│   ├── light-ocr-tiny/       # @arcships/light-ocr-tiny（N2）
│   ├── light-ocr-medium/     # @arcships/light-ocr-medium（N2）
│   ├── light-ocr-document/   # @arcships/light-ocr-document（N3）
│   ├── light-ocr-layout/     # @arcships/light-ocr-layout（N4）
│   └── model-*/              # @arcships/light-ocr-model-*（纯数据包）
├── native/                   # native addon 源码与 CMake（从 bindings/node 拆出 JS 后的残留）
│   ├── CMakeLists.txt
│   └── src/
│       └── addon.cpp
├── src/                      # C++ Core（不变）
├── docs/
├── tests/                    # 跨包集成测试与 corpus
├── tools/
└── contracts/                # 公共契约（cli-design.md §2.5 的 contracts/）
    ├── cli/
    │   ├── flags.json5
    │   ├── envelope.schema.json
    │   ├── exit-codes.json
    │   └── fixtures/
    └── errors/
        └── ocr-error-codes.json
```

### 2.1 关键设计决策

- **`native/` vs `packages/native/`：** native addon 不是独立的 npm 用户入口包，其预编译产物通过 platform-specific optional dependency 被 `runtime` 引用。`native/` 放在根目录，与 `src/`（C++ Core）平级，保持 CMake 构建路径简洁。
- **`contracts/`：** 多个包共享的 flag 定义、schema、exit code 表、golden fixtures。单一来源，由各包的构建/测试脚本读取。
- **`packages/light-ocr/` 的 bin：** roadmap §3.1 规定 `light-ocr` bin 只属于默认 small 包。tiny/medium 若有 CLI，bin 命名为 `light-ocr-tiny` / `light-ocr-medium`。
- **`packages/light-ocr-server/`：** 唯一携带 Dockerfile 的包。它是一个 npm 包（可发布到 registry），也是一个可独立构建的 Docker 镜像。

## 3. 包依赖关系

```text
@arcships/light-ocr-runtime
├── native addon（platform-specific optionalDependencies）
├── express? 否 — runtime 不含 HTTP 层
└── 不默认携带模型

@arcships/light-ocr
├── exact: @arcships/light-ocr-runtime
├── exact: @arcships/light-ocr-model-ppocrv6-small
├── bin: light-ocr
└── 唯一代表"开箱即用"的默认入口

@arcships/light-ocr-server
├── exact: @arcships/light-ocr（默认 small 模型）
├── express, multer
├── Dockerfile（基于 node:22-trixie-slim）
└── 部署制品：Docker 镜像

@arcships/light-ocr-tiny / -medium
├── exact: @arcships/light-ocr-runtime
├── exact: 对应模型包
└── 与 light-ocr 共享相同 JS API + 类型

@arcships/light-ocr-document（N3）
├── exact: @arcships/light-ocr-runtime（或接受注入的 engine factory）
├── PDF renderer（S3 接受分支）
└── 不强制依赖特定杯型

@arcships/light-ocr-layout（N4）
├── exact: @arcships/light-ocr-runtime
├── exact: Layout 模型包
└── 不拥有 CLI bin
```

### 3.1 Server 的依赖方向

Server 依赖 `@arcships/light-ocr`（默认 small），而不是 `runtime`。这确保 server 开箱即用：一个 `docker run` 就能跑 OCR，不需要用户另外装模型。

未来可选支持通过环境变量切换模型杯型（例如 `MODEL_PACKAGE=@arcships/light-ocr-medium`），但默认保持 small。

## 4. 工具链

### 4.1 选择：npm workspaces

当前项目使用 npm，零额外工具迁移成本。

```jsonc
// 根 package.json
{
  "private": true,
  "workspaces": [
    "packages/*"
  ]
}
```

- `npm install` 在根目录执行，自动为所有 `packages/*` workspace 安装依赖并创建 symlink；`native/` 是 CMake 源码目录，不伪装成 npm workspace。
- `npm test --workspaces` 运行所有包的测试。
- `npm publish --workspace packages/light-ocr` 发布单个包。

不需要 rush、lerna、turbo 等额外编排工具，当前 8 个包的规模 npm workspaces 完全够用。如果将来需要统一版本发布、changelog 生成，再评估 changesets。

### 4.2 版本策略

采用**独立版本**（independent versioning），每个包有自己的 `version` 字段：

| 包 | 版本锚点 | 说明 |
| --- | --- | --- |
| `runtime` | 与 Core 版本解耦 | 适配层，变化频率低 |
| `light-ocr` | 跟随项目 semver | 默认入口，用户感知的主版本号 |
| `server` | 独立 semver | 部署制品，有自己的 breaking change 周期 |
| `model-*` | 与模型 bundle ID 对齐 | 数据包，模型变更时发新版 |
| `tiny/medium/document/layout` | 各自独立 | 按各自成熟度独立发版 |

约束：
- `light-ocr` 精确锁定 `runtime` 和 `model-*` 的兼容版本（`"@arcships/light-ocr-runtime": "1.2.3"` 精确 pin，不用 `^`）。
- `server` 精确锁定 `light-ocr` 版本。
- 每次 `light-ocr` 发版时，同步检查 `server` 是否需要更新依赖版本。

## 5. 迁移路径

### 5.1 阶段 0：当前状态（不破坏现有结构）

```
bindings/node/          # @arcships/light-ocr，JS + native addon 混合
├── js/                 # JS facade（未来 → packages/runtime/）
├── src/                # native addon（未来 → native/）
├── CMakeLists.txt
└── package.json
```

### 5.2 阶段 1：建立 monorepo 骨架（N2 启动时）

- 创建 `packages/runtime/`，从 `bindings/node/js/` 迁移 JS facade 代码。
- 创建 `packages/light-ocr/`，依赖 `runtime` + 模型包，包含 CLI bin。
- `native/` 保持 addon 源码 + CMake，平台预编译包独立发布。
- `bindings/node/` 标记为 deprecated，保留到确认迁移稳定后删除。
- 此阶段保留 `bindings/node/` 作为 `0.3.x` 发布兼容入口，但只增加一条 workspace 契约检查，不重复运行两套完整原生矩阵；切换发布源之前必须证明 workspace facade 与旧入口的 API、错误类型和真实 OCR 语义一致。

### 5.3 阶段 2：加入 server（N2 完成或 N3 前后）

- `packages/light-ocr-server/` 加入 workspace。
- Dockerfile 使用两阶段构建或从 npm registry 安装依赖。
- 本地开发时 `npm install` 自动 symlink workspace 内的 `light-ocr`，无需先发布。

### 5.4 阶段 3：加入 tiny、medium（N2 GA）

- 两个新包加入 workspace。
- 验证三杯型共享同一 API、类型、测试套件。

### 5.5 阶段 4：document、layout（N3、N4）

- 按各自节点加入。

## 6. Server Docker 构建

Server 是特殊的包：它既作为 npm 包发布，也作为 Docker 镜像分发。

### 6.1 Dockerfile 策略

```dockerfile
# packages/light-ocr-server/Dockerfile
FROM node:22-trixie-slim

WORKDIR /app

# 从 npm registry 安装（生产模式不需要 workspace）
COPY package.json package-lock.json ./
RUN npm ci --omit=dev

COPY src/ ./src/

RUN groupadd -r ocr && useradd -r -g ocr ocr
USER ocr

EXPOSE 3000
ENV EXECUTION_MODE=cpu
ENV QUEUE_CAPACITY=4

CMD ["node", "src/server.js"]
```

### 6.2 本地开发 vs 生产构建

- **本地开发**：`npm install`（workspace 解析为 `packages/light-ocr/` 的 symlink），直接 `node src/server.js`，改动即时生效。
- **生产 Docker 构建**：`npm install` 走 npm registry，拉取已发布的 `@arcships/light-ocr`。镜像构建独立于 monorepo。
- **CI 集成测试**：先 `npm install`（workspace），再 `npm test --workspace packages/light-ocr-server`，验证 server + engine 的端到端行为。

### 6.3 与 docker-compose 的关系

根目录 `docker-compose.yml` 引用 `packages/light-ocr-server/Dockerfile`：

```yaml
services:
  light-ocr-api:
    build:
      context: .
      dockerfile: packages/light-ocr-server/Dockerfile
    ports:
      - "3000:3000"
    environment:
      - EXECUTION_MODE=cpu
```

## 7. 约束

- **根 `package.json` 不包含业务依赖。** 只声明 `workspaces` 和顶层 scripts（lint、test、build 编排）。
- **每个包独立可发布。** `npm publish --workspace <name>` 不依赖 workspace symlink。
- **跨包依赖使用精确版本。** `"@arcships/light-ocr": "0.4.0"`，不用 `^` 或 `~`。
- **`contracts/` 是跨包共享的单一来源。** 各包 CI 从 `contracts/` 生成 flag parser 或验证 golden fixtures，不自行复制 schema。
- **`native/` 不在 npm workspace 中发布为用户包。** 它只作为 `runtime` 的 platform-specific optionalDependency 的构建源。
- **Server 的 Docker 镜像版本与 npm 包版本保持一致。** 每次 `packages/light-ocr-server/package.json` 的 version bump 对应一个 Docker image tag。

## 8. 不做

- 不引入 rush/lerna/turbo/nx 等额外编排工具，除非 npm workspaces 被证明不够用。
- 不把所有包强制统一版本号（lockstep versioning）。server 和 light-ocr 的 breaking change 周期不同。
- 不修改 C++ Core 的源码目录结构。
- 不在 monorepo 迁移完成前向用户承诺 server package 的 API 稳定性。
