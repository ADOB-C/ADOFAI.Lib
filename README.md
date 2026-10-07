# ADOFAI.Lib

A Dance of Fire and Ice 关卡（`.adofai`）的 **C++20 静态库**：

```
解析 .adofai  →  时间线 / 位置解算  →  打拍音（合成 / 导出）  →  可选：渲染砖块
```

用于把 ADOFAI 的关卡逻辑嵌进你自己的程序 —— 播放器、分析工具、批量导出、编辑器……
都不需要抄一遍解析器和时间线解算。

- **语言 / 构建**：C++20，CMake ≥ 3.20
- **平台**：macOS / Linux / Windows
- **许可**：Apache-2.0
- **仓库**：[ADOB-C/ADOFAI.Lib](https://github.com/ADOB-C/ADOFAI.Lib)

## Features

- **解析** `.adofai`：明文，以及 `.adofai.xz` / `.adofai.zst` 容器（按 magic 识别，不看扩展名）
- **时间线**：逐层起始时刻、时长、BPM 传播、中旋砖（`angleData = 999`）、SetSpeed/Twirl
- **位置解算**：任意时刻的双星位置，与游戏里同一套解算
- **打拍音**：合成 / 混音 / WAV 导出（忠实度对齐 `HitSoundGenerator`）
- **资产查找**：可配置搜索根 + 可选 zip；库本身零产品布局假设
- **可选模块**：设备播放、砖块渲染都是独立 target，默认不引入它们的重依赖

## Quick Start

```bash
git clone <这个仓库> && cd ADOFAI.Lib
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build          # 默认 6/6
```

跑一遍"解析 → 时间线 → 解算 → 导出打拍音"（一行 GL 都不需要）：

```bash
./build/examples/headless/adofai_headless <谱面.adofai> /tmp/out.wav [音色目录]
```

## 用法

### 作为库

```bash
cmake --install build --prefix /some/prefix
```

```cmake
find_package(ADOFAI.Lib CONFIG REQUIRED)
target_link_libraries(app PRIVATE ADOFAI::core)      # 只要解析 + 时间线
# 或 ADOFAI::archive（加 .xz/.zst）、ADOFAI::audio（加打拍音）
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

## 模块

| target | 默认 | 内容 | 依赖 |
|---|---|---|---|
| `ADOFAI::core` | ON | 解析（明文）、时间线、位置解算、资产查找、日志、线程池 | glm、RapidJSON |
| `ADOFAI::archive` | ON | `.adofai.xz` / `.adofai.zst` + 4 MB 滑窗流式解压 | core；内嵌 lzma/zstd |
| `ADOFAI::audio` | ON | 打拍音合成 / 混音 / WAV 导出 | core。**不需要 miniaudio** |
| `ADOFAI::audio_device` | OFF | 设备播放（`adofai::AudioEngine`）+ OGG 解码 | audio；引入 miniaudio + stb |
| `ADOFAI::render` | OFF | 砖块渲染（`TileMesh` / `TileShape` / glad）| core + glad + GLFW + 平台 GL |

**关掉的模块不会生成 target** —— link 它是**编译期**报错，而不是运行期才发现。
`render` 关闭时，配置阶段不会 `find_package(OpenGL/glfw)`，公共头里也不会出现 GL 类型。

## 构建选项

| CMake 选项 | 默认 | 说明 |
|---|---|---|
| `ADOFAI_LIB_ARCHIVE` | ON | `.adofai.xz` / `.adofai.zst`（`ADOFAI::archive`）|
| `ADOFAI_LIB_AUDIO` | ON | 打拍音合成 / 导出（`ADOFAI::audio`）|
| `ADOFAI_LIB_AUDIO_DEVICE` | OFF | 设备播放（`ADOFAI::audio_device`，引入 miniaudio + stb）|
| `ADOFAI_LIB_RENDER` | OFF | 砖块渲染（未提供）|
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

**render=OFF 7 条 / render=ON 11 条**（随模块开关增减）：

| 测试 | 验什么 |
|---|---|
| `level_parse_parity` | 快路径解析 vs `cleanJson` + RapidJSON 老路径**逐位一致**；每个 fixture 还会在内存里压成 xz/zstd 再加载比对（含 4 KB 半窗、禁止静默回退）|
| `headless_run` | 示例跑完整条链 |
| `headless_wav_structure` | 导出的 WAV 是结构合法的 16-bit PCM，且不是全零静音 |
| `archive_container` | `ARCHIVE=ON` 读得动 `.xz`/`.zst`；`OFF` 干净失败 |
| `audio_hitsound_mix` | 用现场合成的音色走一遍真混音 |
| `audio_hitsound_rules` | 打拍音铁律的源码护栏（必须饱和加法、不许 Nyquist 去重）|
| `lzma_mt_compat` | lzma_mt 内存字段的跨版本选择（5.4 起字段改名）——纯编译期，不依赖压缩库 |
| `tile_geometry` / `tile_expansion` / `geom_probe` | 三层几何：五边形不变量 + 调用点护栏 / CPU 逐位 / GPU 逐位。**都要 render=ON** |
| `shader_fallback` | `render/Shaders.hpp` 的内嵌回退 GLSL 必须与 `assets/shaders/*` 逐字相同。**要 render=ON** |

## 状态与上游

这个仓库是 [ADOCAO](https://github.com/ADOB-C/ADOCAO)（本体）的**单向镜像**：本体的源码按
白名单定期拷过来，所以这里的源码始终是本体某个 commit 的逐字节副本（当前对齐点见
[SYNCED_AT](SYNCED_AT)）。**本体不依赖本库**，改镜像源码要先改本体。

模块进度：core / archive / audio / audio_device / render **都已可用**。
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
