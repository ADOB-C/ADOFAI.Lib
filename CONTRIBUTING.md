# 维护者说明（CONTRIBUTING）

给**改这个仓库的人**看。只想用库的话看 [README.md](README.md) 就够。

先立一个最重要的规矩，下面所有流程都从它推出来：

> **`src/adofai/**` 里的源码一律不许在库里手改。**

## 1. 这个仓库是单向镜像

**ADOCAO 是主源，本仓库是从它镜像出来的可分发包。**

- **ADOCAO 不依赖本库** —— 本体自带一份自己的实现，发布链路一动不动；
- 所以要改 `src/adofai/**`，得**先在 ADOCAO 里改**，再按下面的流程拷过来；
- 在库里就地改的话下次对齐会被覆盖，而且两边会静默漂移（没人会发现）。

### 为什么不让本体当消费者

ADOCAO 是**天天改**的那一个。若本体依赖本库，每次改本体都要
"改库 → 发 tag → 本体升 pin → 再构建"，**跨一次仓库**；反过来（本体当主源、库定期手动拷）
跨仓库只在同步那一次发生。高频改动方必须自带源码，库是给其他项目的低频分发副本。

代价的另一面要认：**别人在库里发现 bug，得先在 ADOCAO 修、再对齐一次** ——
所以"定期"是节奏不是锁，有人被卡住就当场对齐。

对齐进度记在 [SYNCED_AT](SYNCED_AT)（当前 = ADOCAO `ad09d05`）。

> ⚠️ **ADOCAO 在 2026-10 重写过历史**（清掉误提交的机器绝对路径），所以**旧的 sha 全部失效**。
> 如果你手上还有重写前的 sha，别用。

## 2. 什么算"库自己的文件"

| 可以手改（库自己的） | 不许手改（镜像来的） |
|---|---|
| `CMakeLists.txt`、`src/**/CMakeLists.txt` | `src/adofai/**/*.{hpp,cpp}` |
| `cmake/*.cmake.in`、install/export 规则 | `tests/level_parse_test.cpp` |
| `examples/**`、`tests/check_*.py`、`tests/data/**` | `tests/level_fixtures/**`、`tests/charts/**` |
| `src/adofai/export/**`（**库自有的导出模块**，见下）| 其余全部 `src/adofai/**/*.{hpp,cpp}` |
| `README.md`、`CONTRIBUTING.md`、`MIRROR.md`、`NOTICE`、`SYNCED_AT`、`.github/**` | `tests/gen_*.py` |

镜像进来的是**逐字节副本**：路径与 ADOCAO **完全一致**（`src/adofai/X` ⇔ ADOCAO 的 `X`），
不做任何文本变换 —— 这是"手动对齐便宜"的前提。

### 唯一的例外：`src/adofai/export/**`

`ADOFAI::export`（写 `.adofai` / `.adofai.xz` / `.adofai.zst` / `.adocao`）是**库自己加的**：
ADOCAO 本体刻意不做导出（它的 `--export` 是打拍音 WAV），所以这段代码没有上游对应物，
只能住在这儿。为什么不让本体当它的家见上文的"为什么不让本体当消费者"——反过来同样成立：
**本体不需要它**，塞进主源就是给主源加一个没人用的功能。

它待在 `src/adofai/` 下只是为了 include 根一致（源码树 `src/adofai/export/X` ⇔
装出去 `<adofai/export/X>`）。代价与对策：

* **保真对账要排除它**（脚本见 §3，已带 `grep -v '^src/adofai/export/'`）；rsync 是按目录拷的
  （`core archive audio` …），本来就不会碰它；
* **它必须自己带一条漂移护栏**：`ctest -R export_roundtrip` 比的是
  `parse(原件)` 与 `parse(导出物)` 的建模字段指纹，判据来自解析器而不是测试里另抄的字段表。
  上游哪天给 `LevelData` 加字段、改字段语义，这条测试立刻红 —— 这就是"没有上游对应物"
  这件事的补偿，改 `src/adofai/export/**` 之前先读 `tests/export_roundtrip_test.cpp` 的顶部说明。

## 3. 对齐流程

```bash
# 0) 上游必须是干净的：git -C ../ADOCAO status --short  应输出为空
cd ../ADOCAO && git log -1 --oneline      # 记住要对齐到的 commit

# 1) 按白名单拷（排除 core/map、app、各目录的 CMakeLists）
cd ../ADOFAI.Lib
# --exclude='map/' 是**必须的**：上游 core/map/**（map / 出图）不属于本库（PLAN.md §3），
# 而 core/ 这一条 rsync 会把整个子树拖进来。2026-10 那次同步就真踩了，进库前才发现。
# --delete：上游会删文件（删过 assets/shaders/icon.*、删过 core/util/ThreadPool.*），
# 不带它就会在库里留一堆"上游已经没有"的孤儿源文件。
for d in core archive audio render glad; do
  rsync -a --delete --exclude='.DS_Store' --exclude='CMakeLists.txt' --exclude='map/' \
    ../ADOCAO/$d/ src/adofai/$d/
done
rsync -a --delete --exclude='.DS_Store' ../ADOCAO/assets/shaders/ assets/shaders/
rsync -a --delete --exclude='.DS_Store' ../ADOCAO/tests/level_fixtures/ tests/level_fixtures/
rsync -a --delete --exclude='.DS_Store' ../ADOCAO/tests/charts/ tests/charts/
# 镜像来的测试源码与夹具生成脚本（逐字节副本）。
# 这份清单要跟着上游 tests/ 里"库也想要的那些"走：新增的 .cpp 记得加进来，否则它们会
# 在库里编译不到、也就不会被挂成用例（`ilytx` / `adocao_window` 这类就是这样来的）。
for f in level_parse_test.cpp lzma_mt_compat_test.cpp \
         tile_geometry_test.cpp tile_expansion_test.cpp geom_probe_test.cpp \
         adocao_columns_test.cpp adocao_diagnostics_test.cpp \
         adocao_blocked_test.cpp adocao_checkpoints_test.cpp adocao_window_test.cpp \
         angle_store_test.cpp bit_span_test.cpp floor_window_cache_test.cpp \
         timeline_anchor_test.cpp cull_index_test.cpp cull_cost_probe.cpp ilytx_test.cpp \
         limits_test.cpp position_offset_test.cpp raw_pcm_test.cpp hz_grid_test.cpp \
         kick_chord_test.cpp timeline_ab_dump.cpp tl_dump.cpp tl_full_dump.cpp tl_trail_dump.cpp \
         gen_level_fixtures.py gen_render_fixtures.py gen_adocao_charts.py gen_ilytx_fixtures.mjs; do
  cp ../ADOCAO/tests/$f tests/
done

# 2) 把新 commit 写进 SYNCED_AT（ADOCAO_COMMIT / _FULL / _DATE）

# 3) 自检
```

**保真对账**（镜像文件必须与上游逐字节一致；`src/adofai/X` 对应上游仓库根的 `X`）：

```bash
# 两边都用 git 定位根目录，别把本机路径写进命令（那正是这条规矩要防的东西）
LIB=$(git -C /path/to/ADOFAI.Lib rev-parse --show-toplevel)
cd /path/to/ADOCAO
tmp=/tmp/blob_up; fail=0; n=0
while read -r f; do
  case "$f" in src/adofai/*) up=${f#src/adofai/};; *) up=$f;; esac
  git cat-file blob "$(git rev-parse HEAD:$up)" > "$tmp" && n=$((n+1))
  cmp -s "$tmp" "$LIB/$f" || { echo "DIFF $f"; fail=1; }
done < <(cd "$LIB" && git ls-files src/adofai tests \
          | grep -vE 'CMakeLists\.txt$|tests/check_|tests/export_roundtrip_test\.cpp$|tests/data/'
          | grep -v '^src/adofai/export/')      # 库自有的导出模块：上游没有对应物，见 §2
echo "$n 个文件 → $([ $fail -eq 0 ] && echo 一致 || echo 有差异)"
```

> `grep -v` 那两行就是"哪些文件不该在这里出现"的完整清单：库自己的文件（各目录的
> `CMakeLists.txt`、`tests/check_*`、`tests/export_roundtrip_test.cpp`、`tests/data/`）
> 与库自有的 `src/adofai/export/**`。
> 少排一个，对账就会稳定报一条假 DIFF；多排一个，某个镜像文件就不再被验 —— 宁可一条条列出来。

> 注意：**别用 `git show <sha>:<path> | cmp - file`** —— 我踩过，它会给出**假阳性**
> （把一致的文件报成差异）。`git cat-file blob` 落到临时文件再 `cmp` 才可靠。

**机器绝对路径自检**（照 ADOCAO 的 AGENTS.md，2026-10 真出过事：352 个提交里 50 个带着
`/Users/<用户名>/Documents/Charts/...`，最后只能 `git filter-repo` 重写历史）：

```bash
git grep -nE "/Users/[A-Za-z0-9._-]+/|/home/[A-Za-z0-9._-]+/" -- . ':!build'   # 应为空
```

**本机资源一律用环境变量或 `~/` 相对形式**，别写死绝对路径：

- 谱面目录：`$ADOCAO_CHARTS`，默认 `~/Documents/Charts`；
- 展开 `~/` 的现成写法见 ADOCAO `scripts/capture-gate.sh` 里的 `abs()`（它按 `$HOME` 展开，
  这样清单里可以写 `~/…` 而不泄露用户名）。

**构建 + 测试**（改完至少跑默认那格）：

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --parallel
ctest --test-dir build
```

## 4. 提交前要跑的矩阵

各模块开关会互相影响，所以别只跑默认配置。本地等价命令（`$ALL` 是离线源的
`-DFETCHCONTENT_SOURCE_DIR_*`，见下）：

| 配置 | 关键参数 | 期望 |
|---|---|---|
| 默认 | — | ctest **27/27** |
| 无 archive | `-DADOFAI_LIB_ARCHIVE=OFF` | **13/13**（与 archive 强相关的用例按设计跳过）；前缀里零 lzma/zstd；`ADOFAI::export` 只剩明文 `.adofai`（另三种格式明确报不可用）|
| 系统库 | `-DADOFAI_LIB_BUNDLE_DEPS=OFF` | **27/27**（走 `find_package(LibLZMA/zstd)`）|
| 无资产 zip | `-DADOFAI_LIB_ASSET_ZIP=OFF` | **27/27**，core 里零 `mz_` 符号 |
| 无 audio | `-DADOFAI_LIB_AUDIO=OFF` | **23/23** |
| 全开 | `-DADOFAI_LIB_AUDIO_DEVICE=ON` | **27/27**（会引入 miniaudio + stb）|
| render=ON | `-DADOFAI_LIB_RENDER=ON` | **31/31**（引入 glad + GLFW + 平台 GL；三层几何）|
| 无导出 | `-DADOFAI_LIB_EXPORT=OFF` | **25/25**（`ADOFAI::export` 不存在，link 它是编译期报错）|

> 计数会随用例增减变，**它只是"这一刻该看到多少条"的对照表**，不是判据；判据永远是
> "`ctest` 全绿 + 该跳过的都打印了跳过原因"。本地一次跑全 8 格：`bash build/matrix.sh`
> （产物在 `build-*/`，已 gitignore）。

CI 里这些格都在 Linux 跑（[.github/workflows/build.yml](.github/workflows/build.yml)），
另两平台只跑默认配置。

**离线构建**（CI 之外常用，省一次下载）：

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DFETCHCONTENT_SOURCE_DIR_GLM=/path/to/glm \
  -DFETCHCONTENT_SOURCE_DIR_RAPIDJSON=/path/to/rapidjson \
  -DFETCHCONTENT_SOURCE_DIR_MINIZ=/path/to/miniz \
  -DFETCHCONTENT_SOURCE_DIR_LZMA=/path/to/xz \
  -DFETCHCONTENT_SOURCE_DIR_ZSTD=/path/to/zstd
```

## 5. 消费者验收（改动 install/export 时必做）

`find_package` 与 `add_subdirectory` 两条路都要能编出来：

```bash
cmake --install build --prefix /tmp/prefix
mkdir -p /tmp/cons && cd /tmp/cons
# CMakeLists 里：find_package(ADOFAI.Lib CONFIG REQUIRED) + target_link_libraries(app PRIVATE ADOFAI::archive)
cmake -B build -DCMAKE_PREFIX_PATH=/tmp/prefix \
  -DCMAKE_CXX_FLAGS="-I/path/to/rapidjson/include"     # 消费者自备 RapidJSON
cmake --build build
```

两条容易踩的：

- **消费者链 `ADOFAI::core` 之外还要什么？** 现在 `core`/`archive`/`export`/`audio` 都已
  "公共头零重依赖"，但 `ADOFAI::archive`（以及链了它的 `ADOFAI::export`）需要一个能提供
  lzma/zstd **库**的路径 —— 静态库不传递依赖，见下节。检验办法：只链模块、**不额外 find
  任何压缩库**，能链上才算对。`ADOFAI::export` 的公共头里连 `<lzma.h>`/`<zstd.h>` 都没有。
- `examples/headless/` 里"有没有某模块"是**构建系统用宏告知**的
  （`ADOFAI_HEADLESS_HAVE_ARCHIVE` / `_AUDIO` / `_EXPORT`），**不能用 `__has_include`**：
  模块 OFF 时它的头仍在源码树里，`__has_include` 会为真、链接期报未定义符号。

## 6. 还没收口的事（动手前先看 MIRROR.md）

详细实测与**试过但不行**的机制都记在 [MIRROR.md](MIRROR.md)，这里只列结论：

1. **第三方 install 会污染消费者前缀**：`ARCHIVE=ON` 时 glm/lzma/zstd/miniz 的头与 `.a`
   也进前缀（`ARCHIVE=OFF` 时干净）。根因是**静态库不传递依赖** —— `libadofai_archive.a`
   里只有调用点。两条出路写在 MIRROR.md，其中"把厂商代码合并进我们自己的 `.a`"未做。
2. `ADOFAI::render` 已搬（P5），但**像素门槛那一层（`capture-gate.sh` 的 50 状态逐字节）没搬**：
   它要跑本体 app 抓帧，属于本体验收。库里现在覆盖的是 L0（五边形 + 调用点护栏）、
   L1（CPU 逐位）、L2（GPU 逐位）、以及内嵌回退 GLSL 与 `assets/shaders/*` 的逐字一致。
   另外 CI 的 Ubuntu runner 没有 GL 上下文，`geom_probe` 会打印 SKIP 退 0 —— workflow 里
   专门有一步**报告**它是跑了还是跳过了，别把"绿"当成"GPU 那一层验过了"。
3. RapidJSON 是消费者唯一要手动给 include 的公共依赖。

## 7. 铁律（改动时别碰坏）

- **打拍音**：每个命中都要混音；16 位累加且**每次相加都 clamp**。
  由 `tests/check_hitsound_rules.py` 守着（负向对照已验）。
- **精度**：时间与位置一律 `double`；`Tile::position` 不许量化；`angleData` 不许量化。
- **砖的绘制顺序**：`m_drawOrder` 复刻历史顺序，不许换容器/加 `reserve`/改排序。
- **几何位精确**：改几何必须三层测试（CPU 逐位 / GPU 逐位 / 像素）全绿 —— 这三个测试还没搬。

（完整来由见 ADOCAO 的 `AGENTS.md`，那里是这些结论的原始出处。）
