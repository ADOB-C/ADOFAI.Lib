# 镜像说明（本库自己的文件，不镜像）

本库是 **ADOCAO 的单向镜像**（PLAN.md §0/§3）：ADOCAO 是主源，`src/adofai/**` 里的东西
只能通过在 ADOCAO 里改、再拷过来更新。**在库里手改 `src/adofai/**` 是错的** ——
下次对齐会覆盖掉；库里允许改的只有本文件、`CMakeLists.txt`、`src/**/CMakeLists.txt`、
`cmake/*.cmake*`、install/export 规则、`examples/**`、`README.md`、`NOTICE`、`SYNCED_AT`。

**唯一的例外是 `src/adofai/export/**`** —— 见下面 §"库自有的 `export` 模块"。

对齐进度记在 `SYNCED_AT`。已知不在镜像里的上游改动：ADOCAO 的 `AGENTS.md` / `docs/`
（文档，不在白名单里，所以 `SYNCED_AT` 记的 commit 与"最后跑同步命令的 commit"可能差一笔
纯文档提交，这是预期行为，不是漏同步）。

## 镜像的文件（P2 现状）

```
src/adofai/core/level/{ByteSource,JsonCleaner,LevelData,LevelPath}.{hpp,cpp}
src/adofai/core/timeline/*            Timeline / PositionSolver / PlaybackClock / HitsoundTimestampGroup
src/adofai/core/util/*                AssetPaths / DataFile / Logger / Progress / ThreadPool
src/adofai/archive/{LevelArchive.{hpp,cpp},Install.hpp}
src/adofai/audio/{HitsoundManager.{hpp,cpp},AudioEngine.{hpp,cpp},stb_vorbis_impl.cpp}
tests/level_parse_test.cpp  tests/level_fixtures/**  tests/charts/**
tests/gen_level_fixtures.py  tests/gen_render_fixtures.py
```

排除（按 PLAN.md §3 的白名单）：`core/map/**`（map / 出图留在本体）、`app/**`、
`**/CMakeLists.txt`（库自己的构建文件，连镜像目录里的那几个也是库里写的）、
`assets/**`、`glad/**`。

**路径与 ADOCAO 逐字一致**（不再有例外）：上游 2026-10 把 archive 挪到仓库根的 `archive/`，
库这边就是 `src/adofai/archive/`；`core/level/*`、`core/timeline/*`、`core/util/*`、
`tests/*` 也全部按原路径。

## 可选 archive：上游的依赖倒置（P2 因此不再需要任何桩）

PLAN.md §6 把 `core` 排在 P2、`archive` 排在 P3。P2 开始时 core 还硬依赖 archive
（`LevelData.cpp` 直接 include 带 `<lzma.h>`/`<zstd.h>` 的 `LevelArchive.hpp`），当时
库里只能给一个桩实现；**2026-10 上游把这件事做对了**（`d86fd35`），库跟着重同步，
桩已经删掉。现在的形状：

- `core/level/ByteSource.{hpp,cpp}` —— 纯接口：容器枚举、magic 判断、`WindowSource`
  抽象类、`ArchiveBackend` 函数表（`decodeWhole` + `makeWindow`）、`archiveBackend()`。
  **core 不认识 lzma/zstd**，公共头里也没有它们。
- `archive/LevelArchive.{hpp,cpp}` —— 实现；`archive/Install.hpp` 只暴露一行
  `adofai::archive::install()`，刻意不含三方头（app 就只引这个）。
- 注册是显式的：消费者链 `ADOFAI::archive` 并在启动时 `install()` 一次，压缩谱才可用。
  没注册时 core 自己给出明确错误（`Cannot decompress level (xz): 这个构建没有 archive 模块`），
  不会崩溃、也不会静默当明文。
- 于是 `ADOFAI_LIB_ARCHIVE=OFF` 就是**自然结果**：不编那个目录、不生成 `ADOFAI::archive`，
  消费者 link 它是编译期报错（PLAN.md §5 的口径）；core 照常可编、可装、可测。
- **`dc89c14` 又收了一步（pimpl）**：`ArchiveStream` 的三方状态（`lzma_stream` /
  `ZSTD_DStream` / `ZSTD_inBuffer`）搬进了 .cpp 里的 `struct ArchiveStream::Impl`，7 个访问器
  也移出到 .cpp（Impl 在头里是不完整类型，内联访问器没法解引用）。于是
  **`LevelArchive.hpp` 里已经没有任何 lzma/zstd 类型**，`archive/CMakeLists.txt` 里那两个
  压缩库可以从 PUBLIC 降回**真正的 PRIVATE**。实测装出去的公共头里只剩注释提到这两个名字。

`tests/check_archive.py` 把"读得动 / 干净失败"两向都固化成了测试；
`tests/level_parse_test.cpp`（镜像过来的）另外把每个明文 fixture 在内存里压成 xz/zstd 再加载，
要求 13 个节逐位一致 —— 这就是 PLAN §6 里 P3 要的 round-trip。

## 库自有的 `export` 模块（**唯一一处 `src/adofai/**` 不受镜像规矩约束**）

`ADOFAI::export` 写 `.adofai` / `.adofai.xz` / `.adofai.zst` / `.adocao`，源码在
`src/adofai/export/{LevelJsonWriter,LevelEncode,LevelExport}.{hpp,cpp}`。

**为什么在这儿而不是上游。** 这个判断值得写下来，因为它与"单向镜像"那条规矩看起来冲突：

- 上游 ADOCAO **刻意不做导出**。它的 `--export` 是打拍音 WAV（`docs/cli.md`），
  `archive/` 里只有读方加一个给别的工具用的 `packLevel`（`.adocao` 的写方）；
  没有任何 `.adofai` 的写入方，也**没有计划**加 —— 那属于 ADOCAO-E（编辑器）。
- 所以"把谱面写成容器"这件事只可能落在分发库这边，落上去也不是"给主源塞一个没人用的功能"。
- 反方向（把 exporter 放上游）的代价与"为什么不让本体当消费者"是同一条：本体不需要它，
  上游没人跑它，它会在上游变成没有测试、没人知道的死代码。

**代价：它不在保真对账的覆盖里** —— 也就是最容易被上游悄悄改坏的那类代码。
`LevelJsonWriter.cpp` 里那张"每个 eventType 写哪几个键"的映射表，本质上是
`core/level/LevelData.cpp` 里 `buildAction()` + 两条解析路径的**逆**。上游改前者，这里就会错。

**对策是一条判据来自解析器自己的护栏**（而不是在测试里再抄一份字段表 —— 抄一份才是真正会
静默漂移的做法）：

```bash
ctest -R export                     # export_roundtrip + export_independent
```

* `export_roundtrip`：对**每个**夹具 × **每种**格式，断言
  `modelDigest(parse(原件)) == modelDigest(parse(导出物))`。上游哪天给 `LevelData` 加字段、
  或改某个字段的解析语义，导出物就会缺那一项 / 写错那一项 → 重新解析出来的值不同 → 指纹不等 → 红。
  另外它还断言：同输入两次导出逐字节相同；明文导出**老解析路径**（`ADOCAO_FORCE_DOM_PARSE=1`）
  也读得动；`.adocao` 的 `pack(原件)` 与 `pack(导出物)` 逐字节相同；截断/改坏的容器必须失败。
* `export_independent`：用**第三方**实现说话 —— Python 的 `json`（证明写出来的是合法 JSON，
  而不是只有我们那个宽容的解析器认的东西）、Python 的 `lzma`、`zstd` CLI，
  再加一份**在 Python 里另写一遍**的建模投影。

负向对照已验：把 `beatsAhead` 那一行改成不写 → `f19_all_action_types` 的四条路径全部报
"往返后建模字段不一致（指纹 8d0f89dd3a9433f3 vs 8c3586dd39da8b1a）"，连老解析路径那条也红。

**对齐时要记得**：保真对账脚本已排除 `^src/adofai/export/`（见 CONTRIBUTING §3），
rsync 是按 `core archive audio …` 逐目录拷的，本来也不会碰它。

### 顺带发现的一处上游不对称（已写进 EXPORT.md）

`.adocao` 的 `packLevel` 池 = `actionStrTable` 原序 + 末尾追加 settings 用到的字符串，
而 `unpackLevel` 把整张池原样装回 `actionStrTable` —— 于是
`unpack(pack(x)).actionStrTable` **比 `x.actionStrTable` 长**（diag_dict 实测 2 → 8 项）。
多出来的项没有读者（只有 actions 的 `strId` 指向池头），是良性的；但它意味着
"拿 `actionStrTable` 比对来断言 `.adocao` 往返成功"是**错**的判据 —— 我第一版 `modelDigest`
就是这么写的，被导出模块自己的 `Options::verify` 当场抓出来（那正是它默认开的意义）。

## P3 收口：现状与残留

**已经做完并实测过的：**

1. **round-trip / 窗口路径**：`tests/level_parse_test.cpp`（镜像过来的）里本来就带
   `archiveRoundTrip()`、`windowBoundarySelfTest()`、`windowHugeValueSelfTest()`：
   每个明文 fixture 在内存里压成 xz 与 zstd 再加载，要求 13 个节逐位一致，并在 4 KB 半窗、
   `ADOCAO_WINDOW_REQUIRE=1`（不许静默回退）下再走一遍。所以 P3 这一项不用另做 ——
   `ctest -R level_parse_parity` 就是它。
2. **`ADOFAI_LIB_BUNDLE_DEPS=OFF`（用系统库）**：已实测通过（macOS + 本机）：
   `find_package(LibLZMA)` 走 CMake 自带 Find 模块、`find_package(zstd)` 走上游的
   `zstdConfig.cmake`（brew 的 zstd 带），配置/构建/测试全绿（0 警告，4/4）。
   CI 里有 `system-deps` 那一格（ubuntu 装 `liblzma-dev libzstd-dev`）。
3. **符号**：实测三个数（macOS/arm64，`nm`）：
   * `libadofai_core.a` 里 **0 个** lzma/ZSTD 符号（解耦的直接证据）；
   * 我们自己的 target 都设了 `CXX_VISIBILITY_PRESET hidden`，所以 `.a` 里 120 个
     `adofai::` 导出符号全部不可见；
   * 装出去的 `ADOFAI::core` / `ADOFAI::archive` 的 `INTERFACE_LINK_LIBRARIES` 里
     **没有任何压缩库**（只有 `ADOFAI::core` ← `ADOFAI::archive` 这一条内部依赖）。
   注意"消费者最终二进制里有 ~234 个 lzma/ZSTD 符号"是**正常且无法在静态库层面避免**
   的：内嵌的 liblzma/libzstd 被静态链进去了，它们的符号在最终可执行文件里可见。
   这不影响 API 承诺（我们没有导出任何压缩类型/函数），要更严就只能改成动态库或
   `-exported_symbols_list`（会引出别的复杂度，不值得）。

**还剩下的：**

1. ~~公共头带 lzma/zstd 头~~ —— **已解决**（上游 `dc89c14` 的 pimpl，见上）。现在
   `archive/CMakeLists.txt` 里 lzma/zstd 是 PRIVATE，而 `level_parse_test.cpp` 因为**直接调用
   压缩 API 造测试数据**（`lzma_easy_buffer_encode` / `ZSTD_compress`）仍需自己链它们 ——
   库的 `tests/CMakeLists.txt` 就是这么写的（与上游一致）。
2. ~~`ADOFAI_LIB_ASSET_ZIP=OFF` 做不到~~ —— **已解决**（上游 `dc89c14`）：`DataFile.cpp`
   的 zip 那段加了 `#ifdef ADOCAO_HAVE_MINIZ`，库侧对应 `ADOFAI_LIB_ASSET_ZIP`（定义
   `ADOFAI_HAVE_MINIZ`）。实测 OFF：core 的 `.a` 里 **0 个 `mz_` 符号**、连 miniz 都不拉、
   ctest 4/4（zip 寻址退化成"只走 dataDir / 搜索根 / 直接路径"）。
3. **第三方子项目的 install 规则会污染消费者前缀**（ARCHIVE=ON 时）：`glm` / `liblzma` /
   `libzstd` / `miniz` 的头文件、`.a`、cmake config 都会进前缀，上游那 4 个 CLI
   （`xz`/`xzdec`/`lzmadec`/`lzmainfo`）也照常编、照常装。ARCHIVE=OFF 时前缀是干净的
   （实测 464 文件 —— P4 前是 452 —— 零压缩库痕迹）。

   **为什么必须让 lzma/zstd 进前缀（这是本轮最关键的实测结论）**：`ADOFAI::archive` 是
   静态库，而**静态库不传递依赖** —— `libadofai_archive.a` 里只有调用点，lzma/zstd 的代码在
   它们自己的 `.a` 里。所以消费者必须能拿到那两个库。实测过：只链 `ADOFAI::archive`、
   不提供任何压缩库时，链接直接报 `Undefined symbols: _ZSTD_createDStream / _lzma_code ...`
   （读压缩谱要用到）。于是只有两条路：
   * **A（现状）** 把依赖装进消费者前缀，`ADOFAI.LibConfig.cmake` 里
     `find_dependency(LibLZMA/zstd)` 接回接口 —— 消费者零配置，代价是前缀不干净；
   * **B（未做）** 把 lzma/zstd 的目标文件**合并进我们自己的 `.a`**（`ar` 合库；MSVC 要走
     `lib.exe`）。这样消费者零压缩依赖、前缀也干净 —— 是真正符合 PLAN.md §1
     "私有内嵌"的做法，代价是跨平台合库那套东西（我无法在本机验证 Windows）。

   **在 CMake 4.4.3 上实测过、都不可行的机制**（别再重走）：
   * `cmake_language` 没有 `GET_COMMAND`/`SET_COMMAND`（已查 4.4.3 文档），所以拿不到
     内建 `install` 的引用；`cmake_language(CALL install ...)` 会命中同名 function（递归）。
   * `function(install)` 遮蔽是**全局**的：一旦定义，连我们自己的 `install` 规则也一起失效
     （实测：先遮蔽后调用，规则数为 0）。试过用兄弟目录绕开——不行；试过在父作用域遮蔽再
     `include()` 一个装着我们自己 install 语句的文件——不行；试过用 macro 遮蔽——不行。
     `cmake_language(DEFER ...)` 的写法会把配置挂死。
   * 把第三方子项目的 `CMAKE_INSTALL_PREFIX` 指到丢弃位置——**无效**：父项目的
     `cmake_install.cmake` 在 include 子项目脚本之前会用自己的 `CMAKE_INSTALL_PREFIX`
     （来自 `cmake --install --prefix`）覆盖，烘进去的路径被顶掉。实测结果与改动前逐项一致。
   * 把上游 CLI `EXCLUDE_FROM_ALL` 掉——`install(TARGETS ...)` 仍引用它们，
     `cmake --install` 会去找一个从未生成的可执行文件而整个失败。

   **可行方向（本轮试过，被一个硬问题挡住，未落地）**：把 lzma / zstd / miniz 从
   `add_subdirectory` 换成 `ExternalProject_Add` + 独立前缀（`<prefix>/lzma-ep` 等）。
   实测**确实能让我们的 install 树干净**：前缀里只剩 `adofai/` 与 `glm/`（当时 466 文件 vs
   原来的 554；P4 之后同样口径是 464 vs 556 —— 数字随模块增加而变，重要的是"只剩 adofai/ 与
   glm/"这一点），上游 4 个 CLI 也不再装进来，而且 ExternalProject 不再需要
   `EXCLUDE_FROM_ALL` 那套绕法（用 `--component liblzma_Runtime --component
   liblzma_Development` 精确装库与头，不碰 CLI）。**但挡在一个硬问题上**：
   * **静态库不传递依赖**。`libadofai_archive.a` 里引用 `ZSTD_*` / `lzma_*`，而依赖被装到
     独立前缀、消费者前缀里没有它们 → 消费者链接报未定义符号（实测
     `Undefined symbols: _ZSTD_createDStream ...`），而 `loadFromFile` 读压缩谱必须用到。
     只有 `BUNDLE_DEPS=OFF`（用系统库，走 `find_dependency`）那条路不受影响。
   * 于是要落地必须再做一块：把 lzma/zstd 的目标文件**合并进我们自己的 `.a`**
     （`ar` 合库，跨平台要处理 MSVC 的 `lib.exe`），或者把它们作为私有 target 导出并
     解决可重定位问题。
   * 另一处坑（已实测两种布局）：`FETCHCONTENT_SOURCE_DIR_*` 给出源码树时，ExternalProject 的
     `SOURCE_DIR` 就是那棵树；否则它 clone 到 `<prefix>/src/<name>`。两种布局下头文件路径不同
     （miniz 尤其：`src/miniz_ep/`），写死任一种都会在另一种下编不过。
   * 依赖的 install 命令**必须显式带 `--prefix <INSTALL_DIR>`**：外部 `INSTALL_COMMAND`
     不会自动加（zstd 用默认命令所以有，lzma 用了自定义命令所以第一次什么都没装）。
   * `liblzma.a` 挂在 component **`liblzma_Development`** 上（不是 `_Runtime`），
     install 过滤时要两个都带上。

## CI（`.github/workflows/build.yml`）

三平台：ubuntu（主力矩阵）/ macOS / windows(MSYS2)。Linux 上跑 `ARCHIVE`×`BUNDLE_DEPS`
的组合 + install/find_package 与 add_subdirectory 两条消费者验收；另两平台只跑默认配置
（平台性风险在编译/链接，不在选项组合）。三条已验证过的格子：
`bundled` 4/4、`no-archive` 3/3、`system-deps` 4/4。
**注意：这份 workflow 还没在任何 CI 上跑过**（本地等价命令都验过，但 GH Actions 环境本身未验）。

## 待补的镜像内容

- `tests/{tile_geometry_test.cpp,tile_expansion_test.cpp,geom_probe_test.cpp}`（都要 render，P5）
  与 `tests/capture_states.txt`（本体像素门槛用，按白名单排除）
- `render/**` + `glad/**` + `assets/shaders/**`（P5）
- `tools/tile-geometry-lab/**`（P5，跟着几何走）

## 待补的镜像内容

- `tests/{tile_geometry_test.cpp,tile_expansion_test.cpp,geom_probe_test.cpp}`（都要 render，P5）
  与 `tests/capture_states.txt`（本体像素门槛用，按白名单排除）
- `render/**` + `glad/**` + `assets/shaders/**`（P5）
- `tools/tile-geometry-lab/**`（P5，跟着几何走）
