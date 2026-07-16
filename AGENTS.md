# 项目协作规则

## 版本管理

- 新建版本时采用保守的语义化版本策略。默认只递增最后一位补丁版本，即从 `x.x.y` 更新为 `x.x.(y+1)`。
- 不得仅因功能增加或常规迭代就主动递增主版本或次版本。只有项目已经明确制定并满足相应的大版本或次版本发布规划时，才可递增前两位版本号。

## 标题风格

- 新建 Commit、Pull Request 或 Issue 时，标题必须同时包含**中文古诗风格短句**和**英文标题**；固定使用 `中文古诗短句 · English title` 的顺序与分隔符。
- 中文部分应措辞凝练、富有意象与节奏；英文部分应简洁、自然，并准确概括同一项改动或问题。两部分都不得为装饰性占位，维护者应能仅凭标题判断主题。
- Commit 继续遵循 Conventional Commits，格式为 `<type>(<scope>): <中文古诗短句> · <English title>`；`scope` 可按改动范围省略。
- Pull Request 与 Issue 的格式为 `<中文古诗短句> · <English title>`，不添加 Conventional Commit 前缀。
- 示例：
  - Commit：`docs(apple): 芯光入卷，双语共书实绩 · document Apple performance results`
  - Pull Request：`芯光入卷，双语共书实绩 · Document Apple performance results`
  - Issue：`寒窗偶暗，缓存待明 · Investigate cold-cache startup`
