# ADOFAI.Lib —— 建库方案（handoff）

> 这份文件是给"在**本目录**（ADOFAI.Lib）里新开的那个 session"看的：它没读过 ADOCAO 那边的对话历史。
> **实现细节、踩过的坑、全部实测数字以 ADOCAO 本体的 `AGENTS.md`（`../ADOCAO/AGENTS.md`）为准**（先读它，
> 尤其"Rendering / 砖块几何 / 关卡加载"三节）。本文件只负责：目标、模块边界、拍板的决定、对齐方式、验收口径。

## 0. 一句话目标

**ADOCAO 是主源，本仓库是从它单向镜像出来的可分发包。**
从 ADOCAO（那个 ADOFAI 播放器 app）里抽出可复用的部分，做成一个**可分发的编译库** ADOFAI.Lib，
供**其他项目**使用：解析 `.adofai` → 时间线/解算 → 打拍音（合成/导出）→ 可选渲染砖块。

- ADOCAO **不依赖**本库（它继续自带一份自己的实现，发布链路一动不动）；
- 本库**每周手动**跟 ADOCAO 对齐一次（清单见 §3；**不写同步脚本、不加 CI 校验**）。

> **为什么不让本体当消费者（这条是拍过板的，别反转）**：ADOCAO 是**天天改**的那一个。若本体依赖本库，
> 每次改本体都要"改库 → 发 tag → 本体升 pin → 再构建"，**跨一次仓库**；而反过来（本体当主源、库每周手动拷）
> 跨仓库只发生在每周那一次。高频改动方必须自带源码；库是给"其他项目"的低频分发副本。
> 代价的另一面要认：别人在库里发现 bug，得先在 ADOCAO 修、再对齐一次 —— 所以"每周"是节奏不是锁，
> 有人被卡住就当场对齐（手动流程随时能做）。

**下游（2026-10 在盘上核实过）**：目标拓扑是 `ADOCAO → 本库 → ADOCAO-E / ADOprobe / ADOCAV…`。

| 下游 | 现状 | 它会要本库的什么 |
|---|---|---|
| `../ADOCAO-E` | 只有 `docs/design.md`（577 行编辑器设计草案，按 v6.0.0-Alpha1 的上游写的），未接触构建系统 | `core` + **`render`（ON）** + `audio`：草案里明确"复用 adocao_render / adocao_audio"；它自己新增可写文档模型与增量重算，不属于本库 |
| `../ADOprobe` | 空目录 | 未知 |
| `ADOCAV` | **不在盘上**（ADOCAO 的 AGENTS.md 里 `../ADOCAV/` 那句已过期）| 曾经是"播放器与 ADOCAV 的共同地基"里的那个地基 |

结论：**目前没有活的消费者** —— 所以本库的公共 API 现在怎么定都是自由的（没有兼容包袱）；但**第一个真实消费者
很可能就是 ADOCAO-E，它也是 `render` 模块的第一次实战验收**。除此之外只靠本库自己的 `examples/headless` 兜底。

## 1. 已经拍板的决定（不要再问，除非有实测理由推翻）

| 决定 | 内容 |
|---|---|
| 单源方向 | **ADOCAO → 本库，单向**。本库的源码目录不许手改（§3 的硬规则）|
| 对齐方式 | **每周手动**，在**本目录**的 session 里做；只更新 `SYNCED_AT` 记录对齐到的 commit，不写脚本、不加校验 CI |
| 消费方式 | `install` + `find_package(ADOFAI.Lib CONFIG REQUIRED)` **主推**；`add_subdirectory` / FetchContent **兜底**（`PROJECT_IS_TOP_LEVEL` 守卫）|
| 公共 API 命名 | 一律 `adofai::` —— **改名先在 ADOCAO 里做掉**（§4.1），这样拷过来的源码是逐字副本、手动对齐最省事 |
| lzma / zstd | **私有内嵌**（不导出 target、不装头、符号 hidden），只通过 `ADOFAI::archive` 使用 |
| 模块清单 | `core` / `archive` / `audio`（设备播放可选拆 `audio_device`）/ `render`（**默认 OFF**）；**没有 map** |
| map / 出图 | **不进库**：`core/map/**` + `app/MapExport.*` 留在本体 —— 无头一条命令出图是本体拉流量的功能 |
| 许可 | Apache-2.0（ADOCAO 从 v0.5.1 起就是它）|

## 2. 模块图与文件归属（下面是 ADOCAO 里的**真实文件名**）

```
ADOFAI::core      ← core/level/{JsonCleaner,LevelData,LevelPath,ByteSource}.{hpp,cpp}
                    core/timeline/*            （Timeline / PositionSolver / PlaybackClock / HitsoundTimestampGroup）
                    core/util/*                （Logger / Progress / ThreadPool / DataFile / AssetPaths，见 §4.2）
                    ByteSource = 容器种类 + magic 判断（sniffLevelArchive）+ `WindowSource` 接口 + `ArchiveBackend` 钩子
                    公共依赖：glm（header-only；PlaybackClock/PositionSolver 的公共头就带 glm::dvec2）
                    禁止：glad / GLFW / imgui / miniaudio / tinyfiledialogs / 平台头
                    （原 core 纯度规则 `scripts/check-core-purity.sh`，库化后升级成 API 承诺）
ADOFAI::archive   ← archive/{LevelArchive.hpp,LevelArchive.cpp,Install.hpp}   （.adofai.xz / .zst + 4 MB 滑窗）
                    实现 core 的 `WindowSource`/`ArchiveBackend`（依赖倒置：core 一个 lzma/zstd 符号都不引）
                    私有内嵌 lzma v5.6.3 / zstd v1.5.6（FetchContent；连"不编 xz 那 4 个 CLI"的设置一起拷）
                    `Install.hpp` 不带 `<lzma.h>`；`LevelArchive.hpp` 也已 **pimpl**（状态在 .cpp 的
                    `ArchiveStream::Impl` 里）→ 链 `ADOFAI::archive` 不必配 lzma/zstd 头/库路径。
                    ⚠️ 但 `tests/level_parse_test.cpp` **自己**在用 lzma/zstd API 造压缩样本
                    （`lzma_easy_buffer_encode`/`ZSTD_compress`），它的 include 与链接依赖不能删
ADOFAI::audio     ← audio/{HitsoundManager.*, stb_vorbis_impl.cpp}      依赖 miniaudio
                    audio/AudioEngine.* → 可选模块 ADOFAI::audio_device（默认 OFF）
ADOFAI::render    ← render/* + glad/*                     默认 OFF
                    render/TileGeometryReference.cpp 编成**不安装**的 adofai_tile_reference，只给测试/工具用
不镜像            → core/map/** + app/** + 各目录自己的 CMakeLists.txt（库自己写）
```

模块边界在**库这边用 CMake 的文件列表**表达（不要求 ADOCAO 改目录结构）—— 这样拷过来的文件路径保持原样，
对照 `git log` 也容易。代价：库要维护每个模块的文件清单，**上游新增 .cpp 时记得加进清单**（手动流程的固有风险，
见 §3 末尾）。

## 3. 每周手动对齐（不写脚本、不加 CI）

**规则：源码目录一律不许在库里手改。** 镜像进来的东西（`core/ audio/ render/ glad/ assets/shaders/`
对拍测试 / `tools/tile-geometry-lab`）只能通过在 ADOCAO 里改再拷过来更新。库里只允许改这些"库自己的文件"：
`CMakeLists.txt`、`cmake/*.cmake.in`、install/export 规则、`examples/**`、`README.md`、`SYNCED_AT`。

**白名单（初版，按需增删）**

```
core/level/**  core/timeline/**  core/util/**
archive/**      audio/**        render/**        glad/**
assets/shaders/**               tools/tile-geometry-lab/**
tests/{level_parse_test.cpp,tile_geometry_test.cpp,tile_expansion_test.cpp,geom_probe_test.cpp}
tests/level_fixtures/**  tests/charts/**  tests/gen_level_fixtures.py  tests/gen_render_fixtures.py
排除：core/map/**（本体拉流量）、app/**、**/CMakeLists.txt、tests/capture_states.txt（那是本体像素门槛用的）
```

**每次对齐的步骤**

1. `cd ../ADOCAO && git log -1 --oneline` —— 记住要对齐到的 commit；
2. 照白名单把源码同步过来（`cp -R` / `rsync` 一行自己敲就行），注意上面那几个排除项；
3. 把该 commit 写进 `SYNCED_AT`；
4. `cmake -B build -DADOFAI_LIB_RENDER=ON && cmake --build build && ctest --test-dir build`
   以及 `cmake -B build-off -DADOFAI_LIB_RENDER=OFF && cmake --build build-off`
   （后者是"默认配置真的不需要 GL"的验证）；
5. `cmake --install build --prefix <临时前缀>`，再另建一个工程 `find_package(ADOFAI.Lib)` 编一遍 `examples/headless`；
6. `git commit -m "sync: ADOCAO @ <sha>"`（需要时再打 tag）。

两条容易踩的地方：

- 如果出现"库里非得改点源码才编得过" —— 说明该**先在 ADOCAO 里改**（§4），别在库里就地改：下次对齐会覆盖掉。
- 手动流程的固有风险是**漏文件 / 忘加清单**。想自查就用 git 对一眼：
  `git -C ../ADOCAO diff <SYNCED_AT> -- <白名单前缀>`，看上游这段时间动了哪些库该跟的文件。

## 4. 为了让"每周手动对齐"便宜，先在 ADOCAO 里做的几件事（顺序上先做，属于 P1）

1. **`adofai::` 命名空间**：ADOCAO 自己先把 `LevelData` / `Timeline` / `PositionSolver` / `TileMesh` 等包进
   `adofai::`（一次性机械改动，验收靠 §6 那三条门槛）。这样拷过来的源码就是**逐字副本**，库里不需要任何
   "改名"变换 —— 否则每周手动重放一遍文本变换，迟早漂移。
2. **资产查找去硬编码**：`core/util/DataFile.cpp` 现在 `#include "miniz.h"` 且把 `ADOCAO-data.zip` 写死，
   被 `render/Shader.cpp`（找 GLSL）与 `audio/HitsoundManager.cpp`（找打拍音 WAV）共用。
   在 ADOCAO 里改成"可配置搜索根 + 可配置 zip 名"（默认值仍是本体现在用的那个）；库里再用选项
   `ADOFAI_LIB_ASSET_ZIP` 决定要不要 zip 支持（ON 私有链 miniz / OFF 只走磁盘）。
3. **模块边界稳定**：`archive` 的源码今天在 `core/level/LevelArchive.*`，纯合成与设备播放同在 `audio/` ——
   保持现状即可（库用文件清单划模块），但**新文件要放对目录**，免得白名单越来越难写。

## 5. "render 默认 OFF" 是一条**承诺**，不只是开关

`ADOFAI_LIB_RENDER=OFF`（默认）时必须做到：

1. 配置阶段**不** `find_package(OpenGL/glfw)`、不拉 GLFW；
2. 装出去的公共头里**不出现**任何 GL 类型；
3. 装出来的 target 集合里**没有** `ADOFAI::render` —— 消费者 link 它应当是**编译期**报错，而不是运行时炸；
4. CI 有一条 OFF 矩阵（三平台），保证"默认配置真的不需要 GL"。

反过来的事实也要知道：**render 的公共头确实暴露 GL 类型**（`Planet.hpp` / `PlanetTrail.hpp` / `Shader.hpp` /
`TileMesh.hpp` 里有 `GLuint` 等），所以 render=ON 时 **glad 的头必须跟着一起装/导出**。

> **进度**：P1 已完成（见下表）。下一步 P2 = 在**本目录**建库骨架，把 `core` 从 ADOCAO 拷过来。
> 对齐起点用 ADOCAO 的 `dc89c14`（已 push、CI 三平台全绿；`SYNCED_AT` 就写它）。

## 6. 骨架、阶段、验收

```
ADOFAI.Lib/
  CMakeLists.txt                  # options + add_subdirectory + install/export + CTest 守卫
  cmake/ADOFAI.LibConfig.cmake.in # find_dependency(glm) / (render ON 时) GLFW+OpenGL
  include/adofai/…                # 公共头（core/archive/audio；render 仅 ON 时）—— 用 install(DIRECTORY) 从镜像目录取
  src/                            # ← 镜像来的源码（路径与 ADOCAO 一致）
  third_party/{lzma,zstd}         # FetchContent 钉版本，PRIVATE + hidden
  examples/headless/              # 消费者验收：解析 + 建时间线 + 导出打拍音 WAV（不碰 GL）
  SYNCED_AT                       # 对齐到的 ADOCAO commit（§3 手动维护）
```

| CMake 选项 | 默认 | 说明 |
|---|---|---|
| `ADOFAI_LIB_ARCHIVE` | ON | xz/zstd 容器 |
| `ADOFAI_LIB_AUDIO` | ON | 打拍音合成 + 解码 |
| `ADOFAI_LIB_AUDIO_DEVICE` | OFF | miniaudio 设备播放 |
| `ADOFAI_LIB_RENDER` | **OFF** | TileMesh 等；见 §5 |
| `ADOFAI_LIB_ASSET_ZIP` | ON | 资产 zip 支持（私有链 miniz）；OFF = 只走磁盘 |
| `ADOFAI_LIB_BUNDLE_DEPS` | ON | 内嵌 lzma/zstd；OFF = 用系统库 |
| `ADOFAI_LIB_BUILD_TESTS` / `_EXAMPLES` | 顶层时 ON | |

target 名：`ADOFAI::core` / `ADOFAI::archive` / `ADOFAI::audio` / `ADOFAI::render`（+ 内部 `adofai_tile_reference`）。

| 阶段 | 做什么 | 验收 |
|---|---|---|
| P1 ✅ **已完成**（2026-10，ADOFAO 端 `9dd2e3c` + `d86fd35` + `a1261c7` + `dc89c14`）| **在 ADOCAO 里**做了 §4 的 1、2 两步：core/audio/render 44 个文件包进 `adofai::`（app/tests 侧 25 个 .cpp 加 `using namespace adofai;`）、新增 `core/util/AssetPaths.{hpp,cpp}`（库默认零产品名），ADOCAO 的产品知识集中到 `app/AssetSetup.cpp`；并把解压从 core 里**解耦**出去（core 只留 `ByteSource` 接口，实现搬进独立的 `archive/` 模块，`main()` 顶部 `adofai::archive::install()`）| 三条门槛全绿（ctest 4/4、`ADOCAO_TILE_EXACT=1` 2/2、**像素门槛 50/50**）|
| P2 | 本库骨架 + `core` 模块 + install/export + `examples/headless` + 三平台 CI | 库里 ctest 绿、consumer 两条路径都能编、render ON/OFF 都能配 |
| P3 | `archive`（含 lzma/zstd FetchContent 与"不编 xz CLI"的设置）| 同上 + 压缩谱 round-trip 测试 |
| P4 | `audio`（打拍音两条铁律的注释与逐样本对拍测试一起镜像）| 同上 |
| P5 | `render`（默认 OFF；CPU/GPU 逐位 + 几何测试一起镜像；shader 资产与内嵌回退一起镜像）| 同上 + render ON/OFF 两个矩阵 |
| P6 | 收尾：README（消费示例）、版本策略、每周清单写进本库自己的 `AGENTS.md` | — |

**P1 在 ADOCAO 里跑这三条**（"改名 / 资产 API 没改坏"的硬门槛；P2 之后 ADOCAO 不受本库影响，所以只在 P1 需要）：

```bash
cd ../ADOCAO
./build.sh && ctest --test-dir build                       # 4/4
ADOCAO_TILE_EXACT=1 ctest --test-dir build -R 'tile_expansion|geom_probe'   # 严格逐位
BASE=<基线目录>   # 改动前先存一次：bash scripts/capture-gate.sh store --out "$BASE"
ADOCAO_GATE_PY=<带 Pillow 的 python3> bash scripts/capture-gate.sh check \
    --against "$BASE"                                      # 50/50 逐字节
```

## 7. 必须跟着镜像走的"产品铁律"（不带走，库使用者就会改坏）

- **打拍音**：每个命中都要混音（不许抄 `ADOFAI_HitSound` 的 Nyquist 去重）；16 位累加、**每次相加都 clamp**。
  这是忠实度要求，不是性能取舍。
- **精度**：一切时间与位置用 `double`；`Tile::position` 不许量化；`angleData` 不许量化到 ≥1e-4 的格子
  （`Timeline.cpp` 里 `delta < 0.0001 → 整圈` 是语义阈值）。
- **砖的绘制顺序**：`m_drawOrder` 复刻的是历史顺序（深度 24 bit 下相邻砖会撞同一档深度），不许换容器/加 `reserve`/
  改排序；图标必须在**拖尾之前**画（旧顺序把拖尾擦掉是 bug，已修）。
- **几何位精确**：VS 与 `TileShape::expand()` 必须逐字同结构（显式 `fma`、`rp(a,b)=fma(a,b,0)`、负系数 `-(rp(w,m))`）；
  改几何必须三层测试全绿（CPU 逐位 / GPU 逐位 / 像素）。
- **对拍口径**：两个逐位测试默认按**几何尺度容差 1e-5 砖**；`ADOCAO_TILE_EXACT=1` 才要求逐位
  （"逐位"是在特定机器+编译器上钉的，换编译器/驱动有 1 ULP 级差异，8-bit 看不出来）。
- **shader 一致性**：`render/Shaders.hpp` 的内嵌回退 GLSL 必须与 `assets/shaders/*` 逐字相同
  （ADOCAO 里由 `scripts/check-shader-fallback.sh` 机械校验，镜像时要一起带）。

## 8. ADOCAO 侧现状（对照用，2026-10）

- 模块与规模：`core` 4120 行 / `audio` 907 / `render` 2364 / `glad` 334 / `app` 4326。
- target：`adocao_core`（→ 无）、`adocao_audio`（→ core）、`adocao_render`（→ core + glad）、`adocao_glad`（→ glfw）；
  `app/` 直接 `target_link_libraries` 这几个静态库；**今天没有任何 install/export 规则**。
- 测试：`tests/level_parse_test.cpp`（解析对拍）、`tile_geometry_test.cpp`（中旋五边形 + 调用点护栏）、
  `tile_expansion_test.cpp`（CPU 逐位，3.3e7 坐标）、`geom_probe_test.cpp`（GPU 逐位，4.7e7 坐标，无显示时 SKIP）；
  `tests/charts/` 是生成出来的验收谱（129,600 个形状那个是全 (sa,ea) 覆盖）。
- 脚本：`build.sh`（增量）、`check-core-purity.sh`、`check-cli-help.sh`、`check-shader-fallback.sh`、
  `capture-gate.sh`（50 状态像素门槛）、`release.sh`、`push-ci.sh`。
- `LevelData` 已有干净的嵌入入口：`loadFromBuffer(const char*, size_t)` / `loadFromFile` / `loadFromString`；
  mmap 的 `FileMap` 留在 app（带平台头），所以 `core` 能保持"禁平台头"。

## 9. 非目标

- **不许让 ADOCAO 依赖本库**：那会把"改本体"变成跨仓库流程（改库 → 发 tag → 本体升 pin → 构建）。
  本体是高频改动方，库是低频分发副本，方向不能反（理由见 §0 开头那段引用块）。
- **本库不许出现对 ADOCAO 仓库的构建期依赖**（单向镜像；反向只允许"读它的源码做对照"）。
- **不写同步脚本、不加"与上游逐字相同"的校验 CI** —— 对齐是每周手动的事（§3）。
- 不做 GUI / 向导 / 启动器（`imgui`、`tinyfiledialogs` 不进库；`miniz` 仅为 §4.2 的资产 zip 私有使用）。
- 不做 map / 出图（见 §1，本体拉流量）。
- 不重新引入 GPU compute culling（本体 2.0.0 起已删，别再加回来）。
