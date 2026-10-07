# 版本策略

## 两个版本号，别混

生成的 `<adofai/version.hpp>` 里有两个东西：

| 宏 | 是什么 | 什么时候动 |
|---|---|---|
| `ADOFAI_LIB_VERSION` | **库自己的**语义化版本（`CMakeLists.txt` 的 `project(VERSION)`）| 发版时 |
| `ADOFAI_LIB_ADOCAO_COMMIT` | 这份源码**镜像自 ADOCAO 的哪个 commit** | **每次对齐**（= 仓库根的 [SYNCED_AT](SYNCED_AT)）|

日常对齐只动后者。这意味着"库版本号相同"**不代表源码相同** —— 两个都打了 tag 的库版本之间，
可能夹着好几次对齐。

## 唯一的真身份是 ADOCAO commit

所以消费方要复现某个确切行为，应该看/记 `ADOFAI_LIB_ADOCAO_COMMIT`，而不是只看 `ADOFAI_LIB_VERSION`：

```cpp
#include <adofai/version.hpp>
static_assert(ADOFAI_LIB_VERSION_MAJOR == 0);
// 想要"对齐到某个上游 commit"的构建，就把这个宏记进你的构建日志
// ADOFAI_LIB_ADOCAO_COMMIT
```

## 库版本号怎么走

现在处于 **0.x**：API 随时可能改（上游 `adofai::` 里的东西本身也在动）。
`ADOFAI.LibConfigVersion.cmake` 用的是 `SameMajorVersion`，也就是：

- `0.y.z` 之间：**不做兼容承诺**，升级时请看 `ADOFAI_LIB_ADOCAO_COMMIT` 的差异；
- 到 `1.0.0` 之后：同一个 major 内保证向后兼容（按语义化版本走）。

发版规则（与 ADOCAO 的 `scripts/release.sh` 同一套命名习惯）：

- 版本号形态：`x.y.z`，预发布用 `-AlphaN` / `-BetaN` / `-RcN`；
- **CMake 的 `project(VERSION)` 只接受纯数字**，所以 CMakeLists 里写去掉后缀的核心号（`0.1.0`），
  git tag 用完整号（`v0.1.0-Beta1`）—— ADOCAO 踩过同样的坑，照抄它的约定；
- 每次发版 tag 应该能对应到一个确定的 `SYNCED_AT`（tag 的 commit 里就有那个文件）。

## 消费方怎么钉

- 想要**稳定**：钉 tag（例如 `v0.1.0`），并在构建日志里记下 `ADOFAI_LIB_ADOCAO_COMMIT`；
- 想要**跟上上游**：跟 `main`，但要接受 `src/adofai/**` 随对齐变化；
- 想**完全可复现**：把 `SYNCED_AT` 里的 commit 记下来，或直接 `git subtree` / vendored 拷贝那份源码。

## 相关文件

- [SYNCED_AT](SYNCED_AT) —— 对齐点（`ADOFAI_COMMIT` / `_FULL` / `_DATE`）
- [cmake/version.hpp.in](cmake/version.hpp.in) —— 生成版本头的模板
- [CONTRIBUTING.md](CONTRIBUTING.md) —— 对齐流程与提交前要跑的矩阵
