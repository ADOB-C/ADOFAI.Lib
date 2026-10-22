# AGENTS.md

给**在这个仓库里干活的 agent** 看的导航。先读这里，省 token。

## 这个仓库是什么

`ADOFAI.Lib` —— 从 ADOCAO（本体，`../ADOCAO`）**单向镜像**出来的可分发包：
解析 `.adofai` → 时间线/解算 → 打拍音 → 可选渲染 → 可选导出。6 个模块（core / archive /
**export** / audio / audio_device / render），其中 render 与 audio_device 默认 OFF，
export 是**库自有**的（上游刻意不做导出，所以它没有对应物）。

## 三条硬规矩

1. **`src/adofai/**` 一律不许手改。** 它是逐字节镜像，改了下次对齐就覆盖、而且两边静默漂移。
   要改就先在 `../ADOCAO` 改、提交，再按 [CONTRIBUTING.md](CONTRIBUTING.md) 的流程拷过来。
   库自己的文件是：`CMakeLists.txt`、`src/**/CMakeLists.txt`、`cmake/*.cmake*`、
   `examples/**`、`tests/check_*.py`、`tests/data/**`、文档、`.github/**`，
   以及**唯一的例外 `src/adofai/export/**`** ——
   那是库自有的导出模块（ADOCAO 刻意不做导出，所以它没有上游对应物，
   靠 `ctest -R export` 自己守漂移；细节见 [CONTRIBUTING.md](CONTRIBUTING.md) §2）。
2. **入库文件里绝不出现机器绝对路径**（ADOCAO 为此重写过 352 个提交的历史）。
   本机资源走环境变量或 `~/` 相对形式。提交前自检（应为空）：
   ```bash
   git grep -nE "/Users/[A-Za-z0-9._-]+/|/home/[A-Za-z0-9._-]+/" -- . ':!build'
   ```
3. **各模块默认值是一条承诺，不是开关。** render/audio_device OFF 时：不 `find_package`
   它们要的库、装出去的公共头里没有它们的类型、target 集合里没有它们（消费者 link 是**编译期**
   报错）。改动 install/export 后必须验一遍 OFF 那侧。

## 环境（这台机器上真会踩的）

- **本机 ADOCAO 在 `../ADOCAO`**，是主源。对齐前先 `git -C ../ADOCAO status --short`（应为空）。
- **离线构建**：本机 `../ADOCAO/build/_deps` 里有 glm / rapidjson / miniz / xz / zstd / glfw /
  miniaudio / stb 的源码，可以直接指过去省下载：
  ```bash
  cmake -B build -DCMAKE_BUILD_TYPE=Release \
    -DFETCHCONTENT_SOURCE_DIR_GLM=../ADOCAO/build/_deps/glm-src \
    -DFETCHCONTENT_SOURCE_DIR_RAPIDJSON=../ADOCAO/build/_deps/rapidjson-src \
    -DFETCHCONTENT_SOURCE_DIR_MINIZ=../ADOCAO/build/_deps/miniz-src \
    -DFETCHCONTENT_SOURCE_DIR_LZMA=../ADOCAO/build/_deps/lzma-src \
    -DFETCHCONTENT_SOURCE_DIR_ZSTD=../ADOCAO/build/_deps/zstd-src \
    -DFETCHCONTENT_SOURCE_DIR_GLFW=../ADOCAO/build/_deps/glfw-src
  ```
- **沙箱里 `gh` 要设 `XDG_CACHE_HOME`**：默认往 `~/.cache/gh` 写会 `operation not permitted`，
  于是 `gh run view --log` 全失败：
  ```bash
  XDG_CACHE_HOME=/tmp/gh-cache gh run view <id> --log
  ```
- **往工作区外写会被拒**（`~/Projects/` 直接被拒）。备份/中间产物放工作区内（`build/` 是忽略的）。
- **保真对账别用 `git show <sha>:<path> | cmp - file`** —— 它会给**假阳性**（把一致的文件报成差异）。
  用 `git cat-file blob` 落到临时文件再 `cmp`，脚本见 CONTRIBUTING.md。

## 每次对齐要跑的（每周清单）

1. `git -C ../ADOCAO log -1 --oneline`（工作区必须是干净的）；
2. 按 [CONTRIBUTING.md](CONTRIBUTING.md) 第 3 节的白名单从**上游 HEAD 快照**镜像（`git archive`，
   **不是**直接 rsync 工作区 —— 上游经常有未提交的改动，直接 rsync 会把它们拽进库里），更新 `SYNCED_AT`；
   rsync 那几条**必须带 `--exclude='map/'` 与 `--delete`**：前者挡上游的 `core/map/**`
   （出图，不进库；2026-10 踩过一次），后者处理"上游删了文件"（删过 `assets/shaders/icon.*`、
   `core/util/ThreadPool.*`）；
3. **保真对账**：所有镜像文件与上游对应路径**逐字节一致**（脚本在 CONTRIBUTING.md）；
4. **本地只跑两格**（默认 + render=ON）。其余 6 格**交给 CI** —— CI 那 10 格把它们全覆盖了，
   本地再跑一遍是重复劳动（8 格全量约 5–6 分钟，两格约 1.5 分钟）：
   ```bash
   ctest --test-dir build            # 默认：36/36
   ctest --test-dir build-render     # render=ON：41/41（含三层几何 + shader 一致性 + 导出）
   ```
   `build-render` 由 `bash build/matrix.sh render` 建（也接受多个配置名，产物在 `build-*/`，
   已 gitignore）。**只有改了 install/export/选项矩阵/CI 本身时**才值得本地多跑几格 ——
   那几类改动坏的恰好是"OFF 那侧"，而 OFF 侧的承诺最容易静默破。
   **加了新用例、或者动了某个用例的 `target_link_libraries` 之后，至少本地跑一次
   `bash build/matrix.sh noarch`**（约 30 秒）。它是**配置期**最容易红的一格：链了
   `ADOFAI::archive` 的新用例会让 `ADOFAI_LIB_ARCHIVE=OFF` 直接 Configure 失败，
   而本地默认格永远看不出来 —— 2026-10-22 就是这么让 CI 红的。
5. 绝对路径自检（见上）；
6. `git push`，然后把 **run 链接报出来就够了 —— 一律不要 `gh run watch`**。
   GitHub 的通知邮件就是"跑完了"的信号（仓库所有者会收到）；守着看是纯空等
   （一轮 3~4 分钟，最慢的永远是 `windows-default`）。红了对方会说，那时才查：
   ```bash
   XDG_CACHE_HOME=/tmp/gh-cache gh run view <id>
   XDG_CACHE_HOME=/tmp/gh-cache gh run view <id> --log | grep -E "FAIL|tests passed"
   ```
   另外：只改 `**.md` / `SYNCED_AT` / `LICENSE` / `NOTICE` 的提交**压根不会触发 CI**
   （workflow 里的 `paths-ignore`）—— 那种推完既没有 run 也没有邮件，不用去找。

## 当前状态（2026-10）

- 对齐点：**ADOCAO `5524ee6`**（见 [SYNCED_AT](SYNCED_AT)）。注意 ADOCAO **重写过历史**，
  2026-10 之前的旧 sha 全部失效。
- P1–P6 完成。**六个模块**：core / archive / **export**（库自有的写方）/ audio /
  audio_device / render。
- 测试：render=OFF **36/36**、render=ON **41/41**；CI 10 格全绿（三平台 + 选项矩阵 + render ON/OFF）。
- **GPU 逐位那一层在 CI 上跑不了**：Ubuntu/macOS runner 都没有 GL 上下文，`geom_probe` 会
  打印 `SKIP` 退 0。workflow 里有一步专门**报告**它跑没跑 —— 别把"绿"当成"GPU 验过了"。
- 还没做：**像素门槛**（`capture-gate.sh` 的 50 状态逐字节）没搬 —— 它要跑本体 app 抓帧，
  属于本体验收，库里不打算做（详见 [CONTRIBUTING.md](CONTRIBUTING.md)）。
- 已知未收口：**第三方 install 会污染消费者前缀**（`ARCHIVE=ON`/`RENDER=ON` 时 glm/lzma/zstd/
  miniz/glfw 的头与库也进前缀；对应的 OFF 组合下前缀是干净的）。根因是静态库不传递依赖；
  **试过但不行**的机制全记在 [MIRROR.md](MIRROR.md)，动手前先读那一节，别重走。

## 文档地图

| 文件 | 给谁 |
|---|---|
| [README.md](README.md) | 使用者（Features / Quick Start / 用法 / 导出 / 选项 / 依赖）|
| [EXPORT.md](EXPORT.md) | 要**自己实现**导出的人（`.adocao` 格式规范）；也用来说明库内 `src/adofai/export/` 的契约 |
| [VERSIONING.md](VERSIONING.md) | 版本号怎么读、怎么钉（两个版本号的区别）|
| [CONTRIBUTING.md](CONTRIBUTING.md) | 维护者（对齐流程、提交前矩阵、消费者验收）|
| [MIRROR.md](MIRROR.md) | 镜像的细节与**所有实测记录**（含失败机制）|
| [SYNCED_AT](SYNCED_AT) | 对齐点元数据 |
| [PLAN.md](PLAN.md) | 最初的建库方案（阶段划分的出处）|
| [../ADOCAO/AGENTS.md](../ADOCAO/AGENTS.md) | **上游的事实来源** —— 实现细节、踩过的坑、全部实测数字都以它为准 |
