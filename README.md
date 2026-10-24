# ADOFAI.Lib

A Dance of Fire and Ice 关卡（`.adofai`）的 **C++20 静态库**：

```
解析 .adofai  →  时间线 / 位置解算  →  打拍音（合成 / 导出）  →  可选：渲染砖块
                                                              ↘  导出谱面（.adofai / .xz / .zst / .adocao）
```

用于把 ADOFAI 的关卡逻辑嵌进你自己的程序 —— 播放器、分析工具、批量转换、编辑器……
都不需要抄一遍解析器和时间线解算。

- **语言 / 构建**：C++20，CMake ≥ 3.20
- **平台**：macOS / Linux / Windows
  —— Windows 上请用 **MinGW/GCC**（本库 CI 的 windows 那格就是 MSYS2 mingw64）。
  **MSVC 不在支持范围**：`core/level/JsonScan.hpp`（上游 2026-10-21 的决定）只用 GCC/Clang 的宏，
  且 `ctz32` 直接用了 `__builtin_ctz` —— MSVC 会**编译失败**（不是"退化成标量"）。
  用预编译好的 `.a` 不受影响；从源码构建（`add_subdirectory`）才需要 MinGW
- **许可**：Apache-2.0
- **仓库**：[ADOB-C/ADOFAI.Lib](https://github.com/ADOB-C/ADOFAI.Lib)

## Features

- **解析** `.adofai`：明文，`.adofai.xz` / `.adofai.zst` 压缩容器，本项目的
  **`.adocao` 二进制容器**（v7：列按 4096 元素**分块**、块内自选字典/常量/差分/位打包，
  每 4096 层一份续算快照，外加**自描述事件库** EventSchema/EventColumns —— v6 会丢的类型
  （装饰物、未知 eventType）也能无损存下；实测把 19.58 MB 的谱压到 6 KB 级 —— 逐位无损、
  坏数据响亮失败，格式见上游 `docs/adocao-format.md`），以及 Re_ADOJAS 的 **`.ilytx`**（xz(tar(ilybin))）
- 容器一律按 **magic** 识别，不看扩展名（改过名/抹掉扩展名照样能读）
- **时间线**：逐层起始时刻、时长、BPM 传播、中旋砖（`angleData = 999`）、SetSpeed/Twirl
- **相机系统**：MoveCamera / RepeatEvents 的富事件 → 关键帧 → 采样（`core/camera/`，
  纯计算不碰 GL），也提供 `camera` / `camera_timeline` / `camera_zoom` 三条金标测试
- **场景组装**：`render/LevelScene`（相机 / 剔除 / 绘制顺序的**无 GL 部分**）——
  2026-10-23 从本体 app 挪进 `render/`，所以库这边也能直接用
- **砖色与闪屏**：trackStyle 的 fill/stroke 派生、ColorTrack/RecolorTrack 的补间、
  Flash 的全屏闪屏（`core/level/TileColor`、`render/` 的 `flash` 着色器）
- **解析的结构扫描走 SIMD**：`core/level/JsonScan.hpp` 用整块掩码找结构字节（引号/括号），
  四级后端 NEON(16B) / AVX2(32B) / SSE2(16B) / **无宏时纯标量**（与向量分支逐字符等价）。
  **SIMD 是编进 `libadofai_core.a` 的**（不需要你做任何事，`loadFromFile` 就是快的）；
  那个头也装出去，方便你在**自己的** TU 里复用同一套原语 —— 但它是镜像来的内部工具头，
  接口**不承诺稳定**（要用就钉住版本，见 [VERSIONING.md](VERSIONING.md)）
- **大谱不靠蛮力**：角度可换**紧凑存储**（字典 + 位打包，MYC 实测 54 MB → 4.8 MB）、每砖结构
  可按块物化（`tilePositionAt`）、剔除可用**粗块 AABB 索引**（`render/CullIndex`）——
  三者都是"丢掉密集数组之后照样逐位正确"，各有独立的逐位对拍用例
- **位置解算**：任意时刻的双星位置，与游戏里同一套解算
- **打拍音**：合成 / 混音 / WAV 导出（忠实度对齐 `HitSoundGenerator`）
- **导出谱面**：`.adofai`（明文 JSON）、`.adofai.xz`、`.adofai.zst`、`.adocao`
  —— 四种格式一个入口，按后缀选；保真契约见下面的[导出谱面](#导出谱面)
- **资产查找**：可配置搜索根 + 可选 zip；库本身零产品布局假设
- **可选模块**：设备播放、砖块渲染都是独立 target，默认不引入它们的重依赖

## Quick Start

```bash
git clone <这个仓库> && cd ADOFAI.Lib
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build          # 默认 42/42
```

跑一遍"解析 → 时间线 → 解算 → 导出打拍音"（一行 GL 都不需要）：

```bash
./build/examples/headless/adofai_headless <谱面.adofai> /tmp/out.wav [音色目录]
```

顺手把它转成别的容器（后缀决定格式）：

```bash
./build/examples/headless/adofai_headless <谱面.adofai> --export /tmp/out.adofai.xz
```

## 用法

### 作为库

```bash
cmake --install build --prefix /some/prefix
```

```cmake
find_package(ADOFAI.Lib CONFIG REQUIRED)
target_link_libraries(app PRIVATE ADOFAI::core)      # 只要解析 + 时间线
# 或 ADOFAI::archive（加 .xz/.zst/.adocao 读写）、ADOFAI::audio（加打拍音）、
#    ADOFAI::export（加四种格式的写出）
```

### 解析 + 时间线 + 解算

```cpp
#include "core/level/LevelData.hpp"     // 公共头的 include 根是 include/adofai/
#include "core/timeline/PositionSolver.hpp"
#include "core/timeline/Timeline.hpp"

adofai::LevelData level;
if (!level.loadFromFile("level.adofai")) return 1;     // 也有 loadFromBuffer / loadFromString

adofai::Timeline timeline;
timeline.build(level, /*exportOnly=*/true);            // exportOnly：只要时间线，不建渲染用的东西

double total = timeline.totalDuration();
for (size_t i = 0; i < level.tiles.size(); i++) {
    glm::dvec2 red, blue;    // glm 由 PositionSolver.hpp 带出（它是本库的公共依赖）
    adofai::PositionSolver::positionAt(timeline, timeline.tileStartTimes()[i], red, blue);
}
```

### 压缩谱面（`.adofai.xz` / `.adofai.zst`）

链 `ADOFAI::archive`，并在启动时注册一次：

```cpp
#include "archive/Install.hpp"      // 只暴露 install()，不含 <lzma.h>/<zstd.h>
adofai::archive::install();
```

没链 archive、或忘了调 `install()` 时：明文照常解析，压缩容器**干净失败**
（`load failed` + "这个构建没有 archive 模块"），不会崩溃、也不会静默当成明文。

### 导出打拍音

```cpp
#include "audio/HitsoundManager.hpp"

adofai::HitsoundManager hs;
hs.init("assets/hitsounds");        // 音色目录（内含 Kick.wav / SnareAcoustic2.wav …）
hs.setVolume(100.0f);
hs.preSynthesize(timeline.getHitsoundTimestampGroups(), (float)timeline.totalDuration());
hs.writeWav("out.wav");             // 也可以 buffer() 交给设备播放
```

### 导出谱面

链 `ADOFAI::export`（它自己会把 core 带进来；archive 开着的话也一起带上）：

```cpp
#include "export/LevelExport.hpp"

adofai::exporter::Report rep;
std::string err;
if (!adofai::exporter::exportLevel(level, "out.adocao", err, adofai::exporter::Options(), &rep))
    std::fprintf(stderr, "导出失败：%s\n", err.c_str());
```

格式按**后缀**认（大小写不敏感、长后缀优先）。不想依赖文件名就用显式重载
`exportLevel(level, Format::AdofaiXz, path, err)`；只要字节不落盘用
`writeTo(Format::Adocao, level, bytes, err)`；先问一句能不能写用 `formatAvailable(f)`。

| 格式 | 内容 | 需要 |
|---|---|---|
| `.adofai` | 明文 JSON | 只要 `ADOFAI::export` |
| `.adofai.xz` / `.adofai.zst` | 同一份 JSON 的整帧压缩（xz preset 6 / zstd level 19，zstd 帧**带内容校验和**）| 还要 `ADOFAI_LIB_ARCHIVE=ON`（压缩库在那一格里，库不重复拉一份）|
| `.adocao` | 列式二进制容器（体积最小的那个）| 同上（打包器 `packLevel` 在 archive 里）|

`ADOFAI_LIB_ARCHIVE=OFF` 时 `ADOFAI::export` 仍然在，但只出得了明文 `.adofai`；
另三种格式会返回 `false` 并把原因写进 `err`（`formatAvailable()` 也能先问）。

三条默认保证：

- **写出来的一定读得回来** —— `Options::verify`（默认开）用完**各自的读取方**解回来比对：
  `.adofai` 重新解析写出的 JSON 比建模指纹、`.xz`/`.zst` 解回来逐字节比源 JSON、
  `.adocao` 解回来比建模指纹。不等就报错、**不落盘**。
- **确定性** —— 同一份输入两次导出逐字节相同（单线程压缩、padding 写 0、字符串池顺序固定）；
- **原子落盘** —— 先写 `<path>.tmp` 再改名，中途失败不会留半截文件冒充成品。

#### 保真契约

导出写的是**库建模的那些字段**，不是原文的逐字节回写：

* settings 的 20 个字段（含 `"song"`）、`angleData`、`pathData`、actions 的 6 列
  （floor / eventType / speedType+bpmMultiplier 或 beatsPerMinute / positionOffset+justThisTile /
  hitsound+hitsoundVolume / duration / trackDisappearAnimation、beatsBehind、beatsAhead、trackAnimation）
  —— 这些**逐位往返**，由 `ctest export_roundtrip` 对全部夹具 × 四种格式断言；
* **未知 `eventType` 的 action 会没有** —— 两条解析路径在**读入时**就丢弃它们，不是导出丢的；
* 未建模的 settings 键、`decorations`、注释、键序、数字字面量的写法（`1e2` 与 `100`）都不保留；
* `pathData` 型的谱面导出后会**同时**带一份 `angleData`（保真优先于体积；要体积就用 `.adocao`）；
* **带 v7 事件段的 `.adocao` 可以"无损回写"成 `.adofai`**：这时导出走库自己的
  `exporter::writeV7Json`（`src/adofai/export/LevelV7Json.cpp`）—— 按 schema 把**原始事件 JSON**
  原样吐回来，连装饰物、未知 `eventType`、以及 settings 里我们没建模的键都不丢
  （实测 `"settings": {"bpm": 150}` 原样保留，不会被补成 29 个建模键）。要用这条，读谱前
  `level.preserveRawJson = true`、打包时
  `PackOptions{withV7Events=true, v7IncludeLegacy=true}`。
  这份实现是**库自有的**：上游把它自己的同名能力当成测试工具（先降级成 `adocao debug json`、
  再挪进 `tests/`），**不提供导出功能**，所以库不依赖它。
* **五类"富事件"也能写回去了**：MoveCamera / RepeatEvents / ColorTrack+RecolorTrack / Flash /
  MoveTrack 在 2026-10-18 起搬进了富结构（`cameraEvents` 等），导出模块按各自 `order` 把它们
  写回 `actions` —— 字段级逐位往返（含 ease 名字、relativeTo、disabled/flags、`"Start+3"` 这种
  层引用、透明度/缩放的 ×100 归一化），由 `ctest export_roundtrip` 对五个夹具逐一钉住。
* ⚠️ **唯一的例外是 ScaleRadius**：上游解析它时就把结果烘进 `tiles[i].position`、**原始事件一个
  字段都没留**（`.adocao` 也不存它）。这种谱 `exportLevel` 会**明确拒绝**写明文
  （`canExport()` 可以先问），并说明原因 —— 绝不静默写出一个少了事件的文件。

要**无损回写**（连未知成员一起保留）现在做不到 —— 那要等上游的 `Preserved` 段
（[ADOCAO](https://github.com/ADOB-C/ADOCAO) 的 TODO ⑦）。`.adocao` 的完整格式规范
（含那段的设计）在 [EXPORT.md](EXPORT.md)。

⚠️ **别在 `releaseMemory()` 之后导出**：它会放掉 `actions` 与 `pathData` —— 正是要写的东西。
那时导出的文件会**少掉全部事件**，而且 `verify` 也看不出来（它比的是"输入 vs 导出物"，两边都空）。
顺序是"加载 → 导出 → 再 `releaseMemory()`"。

## 模块

| target | 默认 | 内容 | 依赖 |
|---|---|---|---|
| `ADOFAI::core` | ON | 解析（明文）、时间线、位置解算、资产查找、日志、线程池 | glm、RapidJSON |
| `ADOFAI::archive` | ON | `.adofai.xz` / `.adofai.zst` + 4 MB 滑窗流式解压 | core；内嵌 lzma/zstd |
| `ADOFAI::export` | ON | 导出谱面：`.adofai` / `.adofai.xz` / `.adofai.zst` / `.adocao` | core；archive 开着时还要它 |
| `ADOFAI::audio` | ON | 打拍音合成 / 混音 / WAV 导出 | core。**不需要 miniaudio** |
| `ADOFAI::audio_device` | OFF | 设备播放（`adofai::AudioEngine`）+ OGG 解码 | audio；引入 miniaudio + stb |
| `ADOFAI::render` | OFF | 砖块渲染（`TileMesh` / `TileShape` / glad）| core + glad + GLFW + 平台 GL |

**关掉的模块不会生成 target** —— link 它是**编译期**报错，而不是运行期才发现。
`render` 关闭时，配置阶段不会 `find_package(OpenGL/glfw)`，公共头里也不会出现 GL 类型。

## 构建选项

| CMake 选项 | 默认 | 说明 |
|---|---|---|
| `ADOFAI_LIB_ARCHIVE` | ON | `.adofai.xz` / `.adofai.zst` / `.adocao` 的**读写**（`ADOFAI::archive`）|
| `ADOFAI_LIB_EXPORT` | ON | 四种格式的**写出**（`ADOFAI::export`）。OFF = 完全没有导出 API |
| `ADOFAI_LIB_AUDIO` | ON | 打拍音合成 / 导出（`ADOFAI::audio`）|
| `ADOFAI_LIB_AUDIO_DEVICE` | OFF | 设备播放（`ADOFAI::audio_device`，引入 miniaudio + stb）|
| `ADOFAI_LIB_RENDER` | OFF | 砖块渲染（`ADOFAI::render`，引入 glad + GLFW + 平台 GL）|
| `ADOFAI_LIB_ASSET_ZIP` | ON | 资产 zip 支持（私有链 miniz）。OFF = 只从磁盘找资产 |
| `ADOFAI_LIB_BUNDLE_DEPS` | ON | 内嵌 lzma/zstd；OFF = 用系统库（`find_package(LibLZMA/zstd)`）|
| `ADOFAI_LIB_BUILD_TESTS` / `_EXAMPLES` | 顶层时 ON | |
| `ADOFAI_LIB_INSTALL` | 顶层时 ON | install / export 规则 |

### Build Dependencies

依赖经 FetchContent 拉取并钉版本：glm 1.0.1、RapidJSON、miniz 3.0.2、xz v5.6.3、
zstd v1.5.6；设备模块另加 miniaudio 0.11.22、stb。

**离线 / 无网络**时指向本地已有的源码树：

```bash
cmake -B build \
  -DFETCHCONTENT_SOURCE_DIR_GLM=/path/to/glm \
  -DFETCHCONTENT_SOURCE_DIR_RAPIDJSON=/path/to/rapidjson \
  -DFETCHCONTENT_SOURCE_DIR_MINIZ=/path/to/miniz \
  -DFETCHCONTENT_SOURCE_DIR_LZMA=/path/to/xz \
  -DFETCHCONTENT_SOURCE_DIR_ZSTD=/path/to/zstd \
  -DFETCHCONTENT_SOURCE_DIR_MINIAUDIO=/path/to/miniaudio \
  -DFETCHCONTENT_SOURCE_DIR_STB=/path/to/stb
```

#### 作为库使用时你需要提供什么

- **glm** —— `PlaybackClock.hpp` / `PositionSolver.hpp` 里的 `glm::dvec2`。随本库装进前缀，
  包配置的 `find_dependency(glm)` 自动接上。
- **liblzma / zstd** —— 只有链 `ADOFAI::archive` 时才有关系。**公共头里没有它们的类型**，
  所以不必配头文件路径，`find_dependency` 会自己接上。
- **RapidJSON** —— `LevelData.hpp` 里有 `<rapidjson/document.h>`（唯一需要你手动给 include
  的依赖，因为它没有可用的 CMake config）：

  ```cmake
  target_include_directories(app PRIVATE /path/to/rapidjson/include)
  ```

### 兜底：`add_subdirectory`

不用 install 也可以把本库当子目录（`PROJECT_IS_TOP_LEVEL` 守卫保证只有本库是顶层项目时才
开测试 / 示例 / install）：

```cmake
add_subdirectory(path/to/ADOFAI.Lib adofai_lib)
target_link_libraries(app PRIVATE ADOFAI::core)
```

## 版本

`ADOFAI_LIB_VERSION`（库自己的语义化版本）与 `ADOFAI_LIB_ADOCAO_COMMIT`（镜像自哪个
上游 commit）是两个不同的东西 —— **后者才是源码的真身份**，每次对齐都会变。

怎么读、怎么钉（tag / 跟 main / 完全可复现）见 [VERSIONING.md](VERSIONING.md)。

## 测试

```bash
ctest --test-dir build
```

**render=OFF 42 条 / render=ON 47 条**（随模块开关增减）：

| 测试 | 验什么 |
|---|---|
| `level_parse_parity` | 快路径解析 vs `cleanJson` + RapidJSON 老路径**逐位一致**；每个 fixture 还会在内存里压成 xz/zstd 再加载比对（含 4 KB 半窗、禁止静默回退）|
| `headless_run` | 示例跑完整条链 |
| `headless_wav_structure` | 导出的 WAV 是结构合法的 16-bit PCM，且不是全零静音 |
| `archive_container` | `ARCHIVE=ON` 读得动 `.xz`/`.zst`；`OFF` 干净失败 |
| `audio_hitsound_mix` | 用现场合成的音色走一遍真混音 |
| `audio_hitsound_rules` | 打拍音铁律的源码护栏（必须饱和加法、不许 Nyquist 去重）|
| `adocao_blocked` / `adocao_checkpoints` / `adocao_window` | `.adocao` v3：分块列的逐位往返（含 4096 边界、-0.0/NaN）+ "同质/字典主导/白噪声列上不许赢"；`Derived` 段的续算快照必须与全谱一次算**逐位**相同；按块重算的 Tile/BPM/起始时刻必须与全量加载逐位相同 |
| `ilytx` | Re_ADOJAS 的 `.ilytx`（xz(tar(ilybin))）与参照谱逐位等价；紧凑模式的 f32 误差；三个坏样本必须干净失败 |
| `angle_store` / `floor_window_cache` / `timeline_anchor` | 角度紧凑存储、每砖按块物化、Timeline 每 4096 层锚点 —— 都是"丢掉密集数组之后与全量逐位相同"，各带反面对照 |
| `release_contract` | `releaseMemory()` 之后 `tileBPMs` / `tileHasTwirl` / `tileHasSetSpeed` 必须逐位不变 —— **图标位是绘制期现算的**，清掉它们图标会静默全消失（上游 app 真踩过）。不碰 GL，CI 上真跑 |
| `style_run` | `trackStyleRunEnd` 的区间不变量（块内无 RecolorTrack 触发时按段返回同一个 style）+ 负面对照；TileMesh 靠它按 run 复用而不是逐砖查 |
| `keyframes` / `tile_animation` / `time_origin` | 逐砖关键帧求值、逐砖时间轴、以及 `separateCountdownTime` 决定的**时间基准**（它错了整条时间线会整体平移）|
| `level_reload` | 关卡对象的**所有权与幂等**：`LevelData` 不可拷贝（缓存所有权）、可安全移动、`loadFromBuffer` 两次 == 一次（actions 不许被追加）、`releaseAngleData()` 之后仍能建窗口缓存 |
| `bit_span` / `cull_index` / `cull_cost` | 位图区间置值、粗块 AABB 索引与全扫逐元素对拍、三种剔除策略的候选数/漏砖数/耗时留痕 |
| `std_includes` | 库自有源码用了 `std::stable_sort`/`std::snprintf`/`std::filesystem`… 就必须**直接** include 对应的头 —— libc++ 常经间接包含（macOS 绿），**MinGW 直接报错**（2026-10-22 windows-default 真炸过）|
| `export_roundtrip` | 四种格式的导出都必须**读得回来且建模字段逐位相同**；同输入两次导出逐字节相同；明文导出还要老解析路径也读得动；`.adocao` 的 pack 与原件的 pack 逐字节相同。**同一条测试就是导出模块的漂移护栏**（上游给模型加字段而导出没跟上 → 红）|
| `export_independent` | 独立验收：Python 的 `json` / `lzma` 说了算（第三方实现），一份**另写的**建模投影对比，magic/头自洽，截断与改坏必须失败 |
| `lzma_mt_compat` | lzma_mt 内存字段的跨版本选择（5.4 起字段改名）——纯编译期，不依赖压缩库 |
| `tile_geometry` / `tile_expansion` / `geom_probe` | 三层几何：五边形不变量 + 调用点护栏 / CPU 逐位 / GPU 逐位。**都要 render=ON** |
| `shader_fallback` | `render/Shaders.hpp` 的内嵌回退 GLSL 必须与 `assets/shaders/*` 逐字相同（8 对，含 GL 3.3 回退路 `tile_cpu.vert`）。**要 render=ON** |

## 状态与上游

这个仓库是 [ADOCAO](https://github.com/ADOB-C/ADOCAO)（本体）的**单向镜像**：本体的源码按
白名单定期拷过来，所以这里的源码始终是本体某个 commit 的逐字节副本（当前对齐点见
[SYNCED_AT](SYNCED_AT)）。**本体不依赖本库**，改镜像源码要先改本体。

模块进度：core / archive / export / audio / audio_device / render **都已可用**。
`export` 与其它模块不同：**它是库自有的**（ADOCAO 刻意不做导出），所以不在镜像白名单里 ——
理由与护栏见 [MIRROR.md](MIRROR.md)。
没有搬的是**像素门槛**那层验收（它要跑本体 app 抓帧，属于本体验收）。

CI：三平台 × 选项矩阵（含 render ON/OFF）全绿；另有一个
[nightly job](.github/workflows/nightly-upstream.yml) 每天拿**未经确认的上游 HEAD** 跑一遍
"镜像 + 构建 + 测试"，让上游的破坏性改动当天就暴露，而不是等到下次对齐。

> 注意：CI 的 runner 没有 GL 上下文，GPU 逐位那一层（`geom_probe`）在那里会打印 `SKIP`。
> workflow 里有专门一步报告它跑没跑 —— 别把"绿"当成"GPU 验过了"。

## Acknowledgements

本库的源码来自 [ADOCAO](https://github.com/ADOB-C/ADOCAO)（本体），按 Apache-2.0 使用；
第三方依赖见 [NOTICE](NOTICE)。

仓库：[github.com/ADOB-C/ADOFAI.Lib](https://github.com/ADOB-C/ADOFAI.Lib)

## License

Apache-2.0 —— 见 [LICENSE](LICENSE)。
