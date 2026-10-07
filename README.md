# ADOFAI.Lib

A Dance of Fire and Ice 关卡（`.adofai`）的**可分发编译库**：解析 → 时间线/解算 → 打拍音
（合成/导出）→ 可选渲染砖块。

## 它与 ADOCAO 的关系（先读这段）

**ADOCAO 是主源，本仓库是从它单向镜像出来的可分发包。**

- **ADOCAO 不依赖本库** —— 本体自带一份自己的实现，发布链路一动不动；
- `src/adofai/**` 里的源码**不允许在库里手改**：它只能通过在 ADOCAO 里改、再拷过来更新
  （每次对齐的规则、白名单、已知偏差见 [MIRROR.md](MIRROR.md)）；
- 对齐进度记在 [SYNCED_AT](SYNCED_AT)（当前 = ADOCAO `9dd2e3c`）。

为什么不做成本体当消费者（那看起来更"正常"）：ADOCAO 是**天天改**的那一个。若本体依赖本库，
每次改本体都要"改库 → 发 tag → 本体升 pin → 再构建"，**跨一次仓库**；反过来（本体当主源、
库每周手动拷）跨仓库每周只发生一次。高频改动方必须自带源码，库是给其他项目的低频分发副本。
代价的另一面要认：别人在库里发现 bug，得先在 ADOCAO 修、再对齐一次 —— 所以"每周"是节奏不是锁。

## 现状（P2）

| 模块 | 状态 |
|---|---|
| `ADOFAI::core` | ✅ 解析（明文）、时间线、位置解算、工具。**不依赖任何压缩库** |
| `ADOFAI::archive` | ✅ `.adofai.xz` / `.adofai.zst` + 4 MB 滑窗流式解压。**可选**，见下 |
| `ADOFAI::audio` | ✅ 打拍音合成/导出（`HitsoundManager`）—— 纯逻辑，**不需要 miniaudio** |
| `ADOFAI::audio_device` | ✅ 设备播放（`AudioEngine` + stb_vorbis），**默认 OFF**（要 miniaudio）|
| `ADOFAI::render` | ⏳ P5（**默认 OFF**，见下） |

两个默认 OFF 的承诺都不只是开关：

- **`render` OFF**：配置阶段不 `find_package(OpenGL/glfw)`、装出去的公共头里不出现 GL 类型、
  装出来的 target 里没有 `ADOFAI::render`（消费者 link 它是**编译期**报错）。
- **`archive` OFF**：core 照常可编、可装、可测；没有 `ADOFAI::archive`；压缩谱在运行期给出
  明确错误。这靠的是 ADOCAO 2026-10 的**依赖倒置** —— core 只认
  `core/level/ByteSource.hpp` 那套接口，真正的解压由 archive 模块通过
  `adofai::archive::install()` 注册进来。

### 压缩谱面怎么启用

```cpp
#include "archive/Install.hpp"      // 只暴露 install()，不含 <lzma.h>/<zstd.h>
adofai::archive::install();          // 启动时调一次；之后 .adofai.xz / .adofai.zst 就能读
```

没链 archive、或没调 `install()` 时：明文照常解析，压缩容器干净失败（`load failed` +
"这个构建没有 archive 模块"），不会崩溃、也不会静默当成明文。
`examples/headless/` 就是这么做的，`tests/check_archive.py` 两向都测。

## 构建

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build
```

依赖全部经 FetchContent 拉取并钉版本（glm 1.0.1、RapidJSON、miniz 3.0.2、xz v5.6.3、zstd v1.5.6），
与 ADOCAO 用的是同一批版本。**离线或在没有网络的环境里**，用本地已有的源码树：

```bash
cmake -B build \
  -DFETCHCONTENT_SOURCE_DIR_GLM=/path/to/glm \
  -DFETCHCONTENT_SOURCE_DIR_RAPIDJSON=/path/to/rapidjson \
  -DFETCHCONTENT_SOURCE_DIR_MINIZ=/path/to/miniz \
  -DFETCHCONTENT_SOURCE_DIR_LZMA=/path/to/xz \
  -DFETCHCONTENT_SOURCE_DIR_ZSTD=/path/to/zstd
```

### 选项

| CMake 选项 | 默认 | 说明 |
|---|---|---|
| `ADOFAI_LIB_ARCHIVE` | ON | `.adofai.xz` / `.adofai.zst` 容器。**OFF 也真的可用**：不编 archive 模块，core 照常，压缩谱运行期明确报错 |
| `ADOFAI_LIB_AUDIO` | ON | 打拍音合成/导出（`ADOFAI::audio`）—— 纯逻辑，不拉 miniaudio |
| `ADOFAI_LIB_AUDIO_DEVICE` | OFF | 设备播放（`ADOFAI::audio_device`）—— 会引入 miniaudio + stb |
| `ADOFAI_LIB_RENDER` | **OFF** | 砖块渲染（P5） |
| `ADOFAI_LIB_ASSET_ZIP` | ON | 资产 zip（私有链 miniz）。**OFF 真的可用**：core 里零 `mz_` 符号、连 miniz 都不拉，zip 寻址退化成只走磁盘 |
| `ADOFAI_LIB_BUNDLE_DEPS` | ON | 内嵌 lzma/zstd；**OFF = 用系统库**（`find_package(LibLZMA/zstd)`，已实测）|
| `ADOFAI_LIB_BUILD_TESTS` / `_EXAMPLES` | 顶层时 ON | |
| `ADOFAI_LIB_INSTALL` | 顶层时 ON | install / export 规则 |

`ADOFAI_LIB_ARCHIVE=OFF` 时消费者只会看到 `ADOFAI::core`（没有 `ADOFAI::archive`，
link 它是编译期报错），而且前缀里不会出现 lzma/zstd —— 因为依赖倒置之后 core 与它们
没有任何关系。这不需要任何"桩"：core 本来就只认接口。

```cmake
target_link_libraries(app PRIVATE ADOFAI::core)       # 只想解析明文谱
# 或
target_link_libraries(app PRIVATE ADOFAI::archive)    # 还要 .xz/.zst（archive 会带进 core）
```

## 消费方式

**主推**：install + `find_package`。

```bash
cmake --install build --prefix /some/prefix
```

```cmake
find_package(ADOFAI.Lib CONFIG REQUIRED)
target_link_libraries(app PRIVATE ADOFAI::core)
```

```cpp
#include "core/level/LevelData.hpp"     // 公共头的 include 根是 include/adofai/
#include "core/timeline/Timeline.hpp"
```

**兜底**：`add_subdirectory` / FetchContent（`PROJECT_IS_TOP_LEVEL` 守卫保证只有顶层才开
测试/示例/install）。

### 公共依赖

装出去的公共头会引用几个第三方头，大部分是自动接上的、只有 RapidJSON 要消费者自己动手：

- **glm** —— `PlaybackClock.hpp` / `PositionSolver.hpp` 里的 `glm::dvec2`。跟着本库一起装进
  前缀，包配置里的 `find_dependency(glm)` 自动接上，**不用做额外的事**。
- **liblzma / zstd** —— 只有链 `ADOFAI::archive` 时才有关系。**公共头里已经完全不含它们的
  类型**（上游把 `ArchiveStream` 的三方状态做成了 pimpl，`LevelArchive.hpp` 零三方依赖），
  所以消费者**不需要**配压缩库的头文件路径 —— 包配置里的 `find_dependency(LibLZMA/zstd)`
  会自己把它们接上。内嵌模式（默认）下它们随本库装进前缀；`-DADOFAI_LIB_BUNDLE_DEPS=OFF`
  时改用系统库。**只链 `ADOFAI::core` 的话完全不涉及它们。**
- **RapidJSON（唯一需要自己动手的）** —— `LevelData.hpp` 里有 `<rapidjson/document.h>`。
  注意它**只是被 include**：没有任何 rapidjson 类型出现在公共接口里（全库只有那一处 include）。
  ADOCAO 是把 rapidjson Populate 下来用的，source 树里那份 `RapidJSONConfig.cmake.in` 缺少
  只有它自己构建时才会生成的 `RapidJSON-targets.cmake`，所以 `find_package(RapidJSON)` 找不到它。
  消费者自己把它的 include 给上即可：

  ```cmake
  target_include_directories(app PRIVATE /path/to/rapidjson/include)
  ```

  只有那些 include `LevelData.hpp` 的翻译单元需要它。要让它变自动，得让 rapidjson 以
  `add_subdirectory` 方式带 install 规则进来 —— 见 [MIRROR.md](MIRROR.md) 的待收口项。

## 版本策略

`ADOFAI_LIB_VERSION`（库自己的语义化版本）与 `ADOFAI_LIB_ADOCAO_COMMIT`（镜像自哪个 commit）
是两件事，都在生成的 `<adofai/version.hpp>` 里。每次对齐只动后者。

## 示例与测试

`examples/headless/` 是消费者验收：解析 → 建时间线 → 解算位置 → 导出打拍音 WAV，**一行 GL 都没有**。

```bash
./build/examples/headless/adofai_headless tests/data/smoke_level.adofai /tmp/out.wav
ctest --test-dir build
```

默认配置 6 条测试（各模块的开关会增减几条）：

| 测试 | 验什么 |
|---|---|
| `level_parse_parity` | **从 ADOCAO 镜像来的对拍测试**：快路径 vs `cleanJson` + RapidJSON 老路径逐位一致；每个明文 fixture 还会在内存里压成 xz/zstd 再加载比对（这就是 round-trip）。仅 `ARCHIVE=ON` |
| `headless_run` | 示例能跑完整条链 |
| `headless_wav_structure` | 导出的 WAV 是结构合法的 16-bit 单声道 PCM，且不是全零静音 |
| `archive_container` | `ARCHIVE=ON` 时 `.xz` 读得动；`OFF` 时**干净失败**（负向对照：静默当明文就红）|
| `audio_hitsound_mix` | 链了 `ADOFAI::audio` 时，用**现场合成**的音色 WAV 走一遍真混音（双声道 WAV、非零样本、取值分布不像直流）。不依赖 ADOCAO 的 `assets/hitsounds/` |
| `audio_hitsound_rules` | **源码级护栏**：混音内核必须是饱和加法（`vqaddq_s16`/`_mm_adds_epi16` + 标量 clamp），且不许出现非饱和加法或 Nyquist 去重。负向对照已验 |

还没搬的是 `tile_geometry` / `tile_expansion` / `geom_probe`——三个都要 render 模块，
随 P5 一起，见 [MIRROR.md](MIRROR.md) 的"待补的镜像内容"。

## CI

[.github/workflows/build.yml](.github/workflows/build.yml)：三平台（ubuntu / macOS / windows-MSYS2）。
Linux 上跑 `ADOFAI_LIB_ARCHIVE` × `ADOFAI_LIB_BUNDLE_DEPS` 的三种组合，外加
install + `find_package` 与 `add_subdirectory` 两条消费者验收；另两平台只跑默认配置。
**这份 workflow 尚未在任何 CI 上跑过**（本地等价命令都验过）。

## 许可

Apache-2.0，与 ADOCAO 从 v0.5.1 起的许可一致。库里的源码来自 ADOCAO，第三方依赖见 [NOTICE](NOTICE)。
