# ADOFAI.Lib

A Dance of Fire and Ice 关卡（`.adofai`）的**可分发编译库**：

```
解析 .adofai  →  时间线 / 位置解算  →  打拍音（合成 / 导出）  →  可选：渲染砖块
```

C++20，静态库，`CMake ≥ 3.20`。Apache-2.0。

**平台**：macOS / Linux / Windows（CI 三平台都编，见 [.github/workflows/build.yml](.github/workflows/build.yml)）。

---

## 30 秒上手

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

作为**库**用（install 之后另建工程）：

```bash
cmake --install build --prefix /some/prefix
```

```cmake
find_package(ADOFAI.Lib CONFIG REQUIRED)
target_link_libraries(app PRIVATE ADOFAI::archive)   # 或 ADOFAI::core / ADOFAI::audio
```

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

---

## 模块

| target | 默认 | 内容 | 依赖 |
|---|---|---|---|
| `ADOFAI::core` | ON | 解析（明文 `.adofai`）、时间线、位置解算、资产查找、日志、线程池 | glm、RapidJSON |
| `ADOFAI::archive` | ON | `.adofai.xz` / `.adofai.zst` 容器 + 4 MB 滑窗流式解压 | 单向依赖 core；内嵌 lzma/zstd（PRIVATE）|
| `ADOFAI::audio` | ON | 打拍音合成 / 混音 / WAV 导出（`HitsoundManager`）| 单向依赖 core。**不需要 miniaudio** |
| `ADOFAI::audio_device` | **OFF** | 设备播放（`AudioEngine`）+ OGG 解码（stb_vorbis）| 单向依赖 audio；引入 miniaudio + stb |
| `ADOFAI::render` | **OFF** | 砖块渲染（`TileMesh` / `TileShape` / glad）| 还没搬过来（见文末"进度"）|

### 两条"默认 OFF"是承诺，不是开关

**`render` OFF**：配置阶段不 `find_package(OpenGL/glfw)`、装出去的公共头里不出现 GL 类型、
装出来的 target 集合里没有 `ADOFAI::render` —— 消费者 link 它是**编译期**报错，不是运行期炸。

**`archive` / `audio` OFF**：同理不生成对应 target。而且 core 与 archive 已经是**依赖倒置**的：

- core 只认 `core/level/ByteSource.hpp` 那套接口（容器枚举、magic 判断、`WindowSource`、
  `ArchiveBackend` 钩子）；
- 真正的解压由 archive 通过 `adofai::archive::install()` 注册进来。

所以 `ADOFAI_LIB_ARCHIVE=OFF` 不需要任何"桩"：core 照常可编可装可测，只是压缩谱在运行期
给出明确错误。实测 `ARCHIVE=OFF` 时前缀里**零 lzma/zstd 痕迹**（464 文件 vs 默认 556）。

---

## 压缩谱面（`.adofai.xz` / `.adofai.zst`）

按 **magic** 识别，不看扩展名。启用只需链 archive 并在启动时注册一次：

```cpp
#include "archive/Install.hpp"      // 只暴露 install()，不含 <lzma.h>/<zstd.h>
adofai::archive::install();
```

没链 archive、或忘了调 `install()` 时：明文照常解析，压缩容器**干净失败**（`load failed` +
"这个构建没有 archive 模块"），不会崩溃、也不会静默当成明文去解析。

> `archive/LevelArchive.hpp` 是给对拍测试用的实现头（它零三方依赖，但只在你确实要直接
> 构造 `ArchiveStream` 时才需要）。日常使用引 `archive/Install.hpp` 就够。

---

## 打拍音

```cpp
#include "audio/HitsoundManager.hpp"

adofai::HitsoundManager hs;
hs.init("assets/hitsounds");        // 音色目录（内含 Kick.wav / SnareAcoustic2.wav …）
hs.setVolume(100.0f);
hs.preSynthesize(timeline.getHitsoundTimestampGroups(), (float)timeline.totalDuration());
hs.writeWav("out.wav");             // 也可以 buffer() 交给设备播放
```

两条**产品铁律**（`AGENTS.md` 的 "Hitsounds" 有完整来由，别放宽）：

1. **每个命中都要混音** —— 不许抄 `ADOFAI_HitSound` 的 Nyquist 去重（实测那会让 95% 的
   输出采样变化，等于换了个音色）；
2. **16 位累加，每次相加都 clamp**（`HitSoundGenerator` 语义）。float + 软限幅试过并删除，
   94.5% 采样会变，响的段落成畸变平台、轻的段落掉电平。

这两条由 `tests/check_hitsound_rules.py` 做**源码级护栏**：混音内核必须出现饱和加法
（`vqaddq_s16` / `_mm_adds_epi16` / 标量 clamp），且不许出现非饱和加法
（`vaddq_s16` / `_mm_add_epi16`）或去重逻辑。负向对照验过：换成非饱和加法会红、插一行
去重会红。

设备播放（默认 OFF）：链 `ADOFAI::audio_device` 后用 `adofai::AudioEngine`。它只在真的
要出声时才需要，`ADOFAI::audio` 的合成/导出完全用不到它 —— 这也是它默认 OFF 的原因。

---

## 构建选项

| CMake 选项 | 默认 | 说明 |
|---|---|---|
| `ADOFAI_LIB_ARCHIVE` | ON | `.adofai.xz` / `.adofai.zst`（`ADOFAI::archive`）|
| `ADOFAI_LIB_AUDIO` | ON | 打拍音合成/导出（`ADOFAI::audio`）|
| `ADOFAI_LIB_AUDIO_DEVICE` | OFF | 设备播放（`ADOFAI::audio_device`，引入 miniaudio + stb）|
| `ADOFAI_LIB_RENDER` | **OFF** | 砖块渲染（`ADOFAI::render`，未搬）|
| `ADOFAI_LIB_ASSET_ZIP` | ON | 资产 zip 支持（私有链 miniz）。OFF = 只从磁盘找资产 |
| `ADOFAI_LIB_BUNDLE_DEPS` | ON | 内嵌 lzma/zstd；OFF = 用系统库（`find_package(LibLZMA/zstd)`）|
| `ADOFAI_LIB_BUILD_TESTS` / `_EXAMPLES` | 顶层时 ON | |
| `ADOFAI_LIB_INSTALL` | 顶层时 ON | install / export 规则 |

依赖经 FetchContent 拉取并钉版本（glm 1.0.1、RapidJSON、miniz 3.0.2、xz v5.6.3、
zstd v1.5.6；设备模块另加 miniaudio 0.11.22、stb）——**与 ADOCAO 用的是同一批版本**。

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

---

## 消费方式

**主推 install + `find_package`**（上面 30 秒上手那段）。

**兜底 `add_subdirectory` / FetchContent**：`PROJECT_IS_TOP_LEVEL` 守卫保证只有本库是顶层
项目时才开测试/示例/install。

### 公共依赖

装出去的公共头会引用几个第三方头，大部分自动接上，只有 RapidJSON 要消费者自己动手：

- **glm** —— `PlaybackClock.hpp` / `PositionSolver.hpp` 里的 `glm::dvec2`。随本库装进前缀，
  包配置里 `find_dependency(glm)` 自动接上。
- **liblzma / zstd** —— 只有链 `ADOFAI::archive` 时才有关系。**公共头里已经完全没有它们的
  类型**（`ArchiveStream` 的三方状态做成了 pimpl），所以消费者不必配压缩库的头文件路径；
  `find_dependency(LibLZMA/zstd)` 会自己接上。只链 `ADOFAI::core` 时完全不涉及它们。
- **RapidJSON（唯一需要自己动手的）** —— `LevelData.hpp` 里有 `<rapidjson/document.h>`。
  它**只是被 include**：没有任何 rapidjson 类型出现在公共接口里。上游那份
  `RapidJSONConfig.cmake.in` 缺少只有它自己构建时才会生成的 `RapidJSON-targets.cmake`，
  所以 `find_package(RapidJSON)` 找不到它。消费者自己给 include 即可：

  ```cmake
  target_include_directories(app PRIVATE /path/to/rapidjson/include)
  ```

### 版本策略

生成的 `<adofai/version.hpp>` 里有两个不同的东西，别混：

- `ADOFAI_LIB_VERSION` —— 库自己的语义化版本；
- `ADOFAI_LIB_ADOCAO_COMMIT` —— 这份源码镜像自 ADOCAO 的哪个 commit（= 仓库根的
  [SYNCED_AT](SYNCED_AT)）。

每次对齐只动后者。

---

## 测试

```bash
ctest --test-dir build
```

默认配置 **6 条**（各模块开关会增减几条）：

| 测试 | 验什么 |
|---|---|
| `level_parse_parity` | **镜像自 ADOCAO 的对拍测试**：快路径 vs `cleanJson` + RapidJSON 老路径**逐位一致**；每个明文 fixture 还会在内存里压成 xz/zstd 再加载比对（含 4 KB 半窗、禁止静默回退）|
| `headless_run` | 示例跑完整条链 |
| `headless_wav_structure` | 导出的 WAV 是结构合法的 16-bit PCM，且不是全零静音 |
| `archive_container` | `ARCHIVE=ON` 读得动 `.xz`/`.zst`；`OFF` **干净失败**（负向对照）|
| `audio_hitsound_mix` | 链 `ADOFAI::audio` 时用**现场合成**的音色走真混音（不依赖 ADOCAO 的 assets）|
| `audio_hitsound_rules` | 打拍音两条铁律的源码护栏（带负向对照）|

还没搬：`tile_geometry` / `tile_expansion` / `geom_probe` —— 都要 render，随 P5 一起。

CI 见 [.github/workflows/build.yml](.github/workflows/build.yml)：三平台，Linux 上跑 6 格选项
矩阵（含 `no-archive` / `system-deps` / `no-asset-zip` / `no-audio` / `all-modules`），
外加 `find_package` 与 `add_subdirectory` 两条消费者验收。

---

## 它与 ADOCAO 的关系（用之前请先读这段）

**ADOCAO 是主源，本仓库是从它单向镜像出来的可分发包。**

- **ADOCAO 不依赖本库** —— 本体自带一份自己的实现，发布链路一动不动；
- `src/adofai/**` 里的源码**不允许在库里手改**：它只能通过在 ADOCAO 里改、再拷过来更新
  （对齐规则、白名单、已知偏差见 [MIRROR.md](MIRROR.md)）；
- 对齐进度记在 [SYNCED_AT](SYNCED_AT)（当前 = ADOCAO `7022ece`，**2026-10 历史重写后**的新
  sha —— 旧 sha 已失效）。

为什么不做成"本体当消费者"（那看起来更正常）：ADOCAO 是**天天改**的那一个。若本体依赖本库，
每次改本体都要"改库 → 发 tag → 本体升 pin → 再构建"，**跨一次仓库**；反过来（本体当主源、
库定期手动拷）跨仓库只在同步那一次发生。高频改动方必须自带源码，库是给其他项目的低频分发副本。

代价的另一面要认：**在库里发现 bug，得先在 ADOCAO 修、再对齐一次** —— 所以"定期"是节奏不是锁，
有人被卡住就当场对齐。

### 进度

| 阶段 | 内容 | 状态 |
|---|---|---|
| P1 | 在 ADOCAO 里做命名空间 + 资产 API 可配置 | ✅ |
| P2 | 库骨架 + `core` + install/export + `examples/headless` + 三平台 CI | ✅ |
| P3 | `archive` 收口（依赖倒置、pimpl、系统库那条路、round-trip）| ✅ |
| P4 | `audio`（两条铁律的护栏 + 真混音验收）| ✅ |
| P5 | `render`（默认 OFF；三层几何测试一起搬）| ⏳ |

尚未收口的项（都在 [MIRROR.md](MIRROR.md)，含实测与失败记录）：

- **第三方 install 会污染消费者前缀**：`ARCHIVE=ON` 时 glm/lzma/zstd/miniz 的头与 `.a` 也进
  前缀（`ARCHIVE=OFF` 时是干净的）。根因是静态库不传递依赖 —— 读者请先看 MIRROR.md 再动手，
  那里记着几种**试过但不行**的机制。
- `ADOFAI::render` 还没搬。

---

## 许可

Apache-2.0（与 ADOCAO 从 v0.5.1 起一致）。库里的源码来自 ADOCAO，第三方依赖见 [NOTICE](NOTICE)。
