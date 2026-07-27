# npm 0.5.3 候选记录

状态：已废弃，未完成 stable promotion<br>
日期：2026-07-26

`0.5.3` 是一次发布到 `next` 的 N3 候选，不是完整的 stable release。其
Small/runtime/native 候选制品已经进入 npm registry，因此这些版本号不可复用；
但 stable `latest` 依赖闭包仍停留在 Small `0.4.0`、runtime `0.1.0` 和 native
`0.4.0`。

该候选存在以下阻塞问题：

- README 将默认安装得到的 `0.4.0` 描述成尚未 promoted 的 `0.5.3`；
- PDF 能力被并入 stable facade，违反默认包无安装脚本、可离线安装的合同；
- 发布 smoke 使用 `--ignore-scripts`，没有安装 PDF renderer，也没有渲染真实 PDF；
- Document integration tests 是空占位；
- GitHub Release 被提前标记为 Latest，正文也错误宣称已经完成 promotion；
- `doctor` 输出稳定 hostname hash，不符合 roadmap 的无稳定设备标识要求。

修复由 `0.5.4` 承接。`0.5.0`–`0.5.3` 的 registry bytes 保持不可变，不覆盖、
不重发；GitHub Release 改为 prerelease/superseded，以免继续误导用户。

关联决策：[D108 与 D109](../decisions.md)。
