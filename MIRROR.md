# 镜像说明（本库自己的文件，不镜像）

本库是 **ADOCAO 的单向镜像**（PLAN.md §0/§3）：ADOCAO 是主源，`src/adofai/**` 里的东西
只能通过在 ADOCAO 里改、再拷过来更新。**在库里手改 `src/adofai/**` 是错的** ——
下次对齐会覆盖掉；库里允许改的只有本文件、`CMakeLists.txt`、`src/**/CMakeLists.txt`、
`cmake/*.cmake*`、install/export 规则、`examples/**`、`README.md`、`NOTICE`、`SYNCED_AT`。

**唯一的例外是 `src/adofai/export/**`** —— 见下面 §"库自有的 `export` 模块"。

对齐进度记在 `SYNCED_AT`。已知不在镜像里的上游改动：ADOCAO 的 `AGENTS.md` / `docs/`
（文档，不在白名单里，所以 `SYNCED_AT` 记的 commit 与"最后跑同步命令的 commit"可能差一笔
纯文档提交，这是预期行为，不是漏同步）。

## 镜像的文件（当前形状）

镜像的是**上游这五个目录的整棵树**（除各目录自己的 `CMakeLists.txt`）：

```
src/adofai/core/**      level / timeline / util（含这轮新增的 AngleStore.hpp、FloorWindow.*、
                        Scheduler.*、Profile.hpp、CountLimits.hpp …）
src/adofai/archive/**   LevelArchive + `.adocao` v3（Columns/Blocked/Writer/Reader/Checkpoints/
                        Window/Diagnostics）+ `.ilytx`（Ilybin/Ilytx）+ Install.hpp
src/adofai/audio/**     HitsoundManager / AudioEngine / stb_vorbis_impl.cpp
src/adofai/render/**    TileMesh / TileShape / Shaders + 纯头文件 BitSpan.hpp / CullIndex.hpp
src/adofai/glad/**      gl_core.{hpp,cpp}
assets/shaders/**       与 render 的内嵌回退 GLSL 必须逐字相同（check_shader_fallback.py 守着）
tests/*.cpp|*.py|*.mjs  上游的测试源码里"库也想要"的那些（清单在 CONTRIBUTING §3）
tests/level_fixtures/** tests/charts/**  夹具与验收谱（含这轮新增的 9 个 `.ilytx`）
```

**排除**（按 PLAN.md §3）：`core/map/**`（map / 出图留在本体）、`app/**`、
`**/CMakeLists.txt`（库自己的构建文件，连镜像目录里的那几个也是库里写的）、
`core/map/**` 之外的 `assets/**`（只镜像 shaders）。

> ⚠️ **`core/map/**` 是两个坑，不是一个**：它既在"排除清单"里，又正好在 `rsync core/` 的
> 源里面 —— 所以 rsync 必须带 `--exclude='map/'`。2026-10 那次同步真踩了（`LevelMap.cpp` /
> `PngBand.*` / `PngStream.*` 一次性进了库），现在 CONTRIBUTING、AGENTS、nightly 三处的
> rsync 都带了这个排除。
>
> 同样必须带的是 `--delete`：上游会删文件，而"库里留着上游已经没有的源文件"是最容易静默
> 烂掉的状态。已经发生过两次 —— 删 `assets/shaders/icon.{frag,vert}`（图标并入 tile.vert 的
> 几何展开）、删 `core/util/ThreadPool.{hpp,cpp}`（线程池换成 Scheduler）。

### 2026-10 那次大同步（74 个提交）里值得记住的结构变化

* **`.adocao` v2 → v3**：列从"一列到底"改成**分块容器**（每 4096 元素一块、块内自选编码），
  并新增 `Derived` 段（每 4096 层的续算快照）。版本门禁仍然是唯一闸门（`kVersion`），
  库侧的 `check_export.py` 现在**从 `AdocaoFormat.hpp` 读 `kVersion` 对账**，不再写死数字
  （写死会每次升版假红，而从源码读仍然抓得住"写方与读方不是同一个版本"）。
* **`core/util/ThreadPool.*` 被删，换成 `Scheduler.*` + header-only `Profile.hpp`**：
  镜像下来的文件换了名字，**库自己的 `core/CMakeLists.txt` 必须同步改**（那是库写的文件，
  rsync 不会碰）。
* **`LevelData` 多了 `angleAt()/angleCount()/tileCount()/tilePositionAt()`**，以及
  `AngleStore`（角度字典 + 位打包）与 `FloorWindowCache`（`tiles` 按块物化）。上游的口径是
  「**运行期读角度一律走 `angleAt()`**、别再直接碰 `tiles[i]`」。库自有的导出模块因此改成了
  访问器口径 —— 见上一节与下面那条实测。
* **新用例的链接差异**：`floor_window_cache` 与 `cull_cost_probe` 会无条件调
  `archive::install()`，所以 `ARCHIVE=OFF` 时它们必须跟着跳过（与 `level_parse_parity` 同口径），
  否则配置阶段就会报 "ADOFAI::archive not found"。

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

### 第二次对齐（52fc91d）时护栏抓到的两件事

1. **上游把角度改成了可选紧凑存储**（`releaseAngleData()` 会把 `angleData` 直接 `swap` 掉，
   值只剩 `AngleStore` 里的字典 + 位打包下标）。导出模块原本直读 `level.angleData` ——
   现在改成 `angleCount()` / `angleAt()`（上游 LevelData.hpp 的口径：「运行期读角度一律走
   `angleAt()`」）。`export_roundtrip` 为此加了一段：把每个输入尽量 `releaseAngleData()` 成功，
   再断言「压缩前后指纹相同」「压缩后导出的 `.adofai` 重新解析回来指纹仍相同」。
   实测 `angles360` 与 `smoke_level` 压缩成功、`diag_glide` 因为字典主导被上游自己拒绝
   （1024 个取值 → 10 bit/层 + 8 KB 字典 = 9.25 B/层 > 8 B/层）。
2. **`.adocao` 的 `packLevel` 仍直读密集数组**（上游没走 `angleAt()`）。所以"角度已压缩"
   的 `LevelData` 打 `.adocao` 会写出一列空角度 —— 导出模块现在**提前明确拒绝**并说明原因，
   而不是等 `verify` 报一句含糊的"指纹不一致"。

### 第三次对齐（afcaa7d）暴露的护栏**盲区**（已补）

上游给 `LevelData::Settings` 加了 `songFilename`（`.adofai` 的 `"song"`）。当时：
`modelDigest` 里没有它、Python 那侧的"导出物键集合"检查也只数**我们自己的**清单 ——
于是"导出把 `song` 整条丢掉"这件事，**两条护栏都看不见**。这是"没有上游对应物"这种模块
最危险的失败方式：静默、且正好落在护栏的缝里。

补了两处，一窄一宽：

* **窄**：`modelDigest` / `putSettings` / Python 投影都加上 `songFilename`（写 `.adofai` 时
  键名是 `"song"`，解析器还接受 `songFilename` 当回退）。负向对照已验：写方漏写 `song` →
  `.adofai` 的自检当场报"重新解析后建模字段不一致（指纹 cb1225… vs 603a8b…）"。
* **宽（真正的防漂移闸）**：`check_export.py` 现在断言「元件里出现的每个 settings 键，要么被
  建模、要么在 `SETTINGS_NOT_MODELED` 的显式清单里」。上游下次加字段、夹具带上它 → 红 →
  逼你做一个决定（建模，或记进清单并说明为什么丢得起）。负向对照已验：把 `artist` 从清单里
  注释掉，立刻红并指名那个键。
  —— 它不完美（只有夹具里真的出现那个键才会响），但它是**不需要反射**能做到的最强形式，
  而且比"在测试里再抄一份字段表"强得多：抄的那份永远不会因为你忘了更新它而变红。

### 第四次对齐（3f73ca2）：镜像区一个文件都没动，但它点出一个**库侧缺口**

上游那一笔是纯 `app/` 的（事件图标在实机里全消失 —— `releaseMeshTemporaries()` 在网格建完
就清了 `tileHasTwirl`/`tileHasSetSpeed`）。本库没有 `app/`、也没有像素门槛，所以这个 bug 本身
**在库里看不见也不可能出现**。但它依赖的**契约是镜像代码里的**：

> `TileMesh::deriveRange()` 在**绘制期**按可见窗口现从 `tileHasTwirl[i]` / `tileHasSetSpeed[i]` /
> `tileBPMs[i]/[i-1]` 派生每砖图标位 —— 所以 `releaseMemory()` **必须留着这三张表**。

这句话在 `LevelData.hpp` 的字段注释和 `releaseMemory()` 的注释里都有，但**没有任何测试**盯它。
后果：上游哪天为了省内存给 `releaseMemory()` 加一句 `tileHasTwirl.clear()`，本库每个消费者都会
静默丢图标 —— 与上游那次一模一样的失败方式（不崩不报错，`i < size()` 把条件变成恒假）。

补了库自有的 `tests/check_release_contract.cpp`（不碰 GL，CI 上真跑）：三张表在 `releaseMemory()`
之后逐位不变、`tileCount()` 不变、文档说会放掉的东西真的放掉了、重复调用安全；外加一句
"这些输入里必须真的有 Twirl/SetSpeed 层"防测试空转（实测累加 14 处图标）。
负向对照已验：给 `releaseMemory()` 临时插一句 `tileHasTwirl.clear()` → 两条断言红。

**同一笔里的另一类问题（shader 路径悬空）本库结构上不会犯**：`app/LevelScene.cpp` 把
`resolveAsset(...).c_str()` 绑到 `const char*`，临时 `std::string` 当场析构 → 路径悬空 →
一直走内嵌回退串（所以改 `assets/shaders/*.vert` 全无效果）。本库这边：
`render/Shader.cpp` 的 `compile(vertPath, fragPath, …)` 路径是**调用方**传进来的；
而 `tests/check_shader_fallback.py` 机械保证"内嵌回退串 == assets/shaders 下的文件"
逐字相同。于是"路径坏了退回内嵌串"这个失败是**无害的**（渲染结果一样）。
真正让上游那笔变成静默 bug 的是"路径坏了 **+ 回退串滞后**"，而后半截在库里被那道护栏堵住了。

### 第五次对齐（9ac56ec，110 个提交）：模型跑到了导出模块前面

上游把**五类事件搬进了富结构**：`cameraEvents`（MoveCamera）、`repeatEvents`、`colorEvents`
（ColorTrack/RecolorTrack）、`flashEvents`、`moveTrackEvents` —— 外加 `ScaleRadius`（只把结果
烘进 `tiles[i].position`，**原始事件都不留**）。`Settings` 也多了 8 个字段（砖色模型 + `pitch`
+ `legacyCamRelativeTo`）。

对 `.adocao` 没影响：它有第 9~13 段专门存前五类（ScaleRadius 除外，那是上游的设计）。
但**`.adofai` 的 JSON 没有这几类事件的表示** —— 导出会静默丢掉它们。实测：
`tests/charts/camera_basic.adofai`（MoveCamera ×2）与 `neon_alpha.adofai`（ColorTrack ×2）
在改动前是**静默丢事件、测试全绿**的（把 `canExport` 强制改成 `true` 复现过：0 处失败）。

处理（本轮已完成）：
1. `Settings` 的 8 个新字段进写方与 `modelDigest`（漏一个就是静默降级，`songFilename` 的教训）；
2. 新增 `canExport()`：五类富事件或 ScaleRadius 非空时**明确拒绝**写明文，`err` 里列出
   "是哪几类、各多少条"，并指向 `.adocao`。三条 JSON 路径都先过这道闸；
3. `.adocao` 的自检从"比 `modelDigest`"改成 **"再打一次包，字节必须完全相同"** —— 判据
   **字段无关**：unpack 丢了任何一项，第二次 pack 就一定不同。这比逐字段枚举强（枚举那套
   每加一类事件就要跟一次，漏了就是"自检绿灯但数据已经丢了"）；
4. 测试按两种情况断言：能表示 → 往返指纹；不能表示 → 必须**失败且原因指名道姓**。
   另加一条 `canExport` 与模型现状的对照（"说能写、但谱里含富事件" → 红），
   防它自己漂。

**做完了（2026-10-22）**：五类富事件现在都写回 `actions`（`putRichEvents()`，各自按 `order`
升序）。字段名与取值口径全部照 `LevelData.cpp` 的 `jsonRead*Event()` 反着写：
ease 走 `easeName()`、relativeTo / flashStyle / plane 走名字、`startTile` 用 `"Start+3"` 这类
字符串、透明度与缩放按 ×100 归一化（写成 `(double)v*100.0`，让 ×100→÷100 精确回到原 float）。
`modelDigest` 也把这五类加进了指纹（**只算输入字段**：`order` 会被重新编号，
`trigger`/`durSec`/`lo`/`hi` 是解析后才算的派生值，算进去就永远不等）。

两个实现细节值得记：
* **缺分量的位置要写 `null`**（不是 0）：`MoveTrack.positionOffset` 的 `kMoveOffX/kMoveOffY` 是
  **逐分量**的位，写 `0` 会让解析侧多置一个位 → 指纹不等。
* `RepeatEvent` / `FlashEvent` **没有 `order` 字段**，用下标/floor 当序即可（同类型内部有序就够，
  跨类型先后解析侧不关心）。

**仍然不行的只有 ScaleRadius**：上游只把结果烘进砖位、原始事件没留，任何实现都还不了原。

### 第六次对齐（868ed24，30 个提交）：core 里第一次出现架构 intrinsic

`core/level/JsonScan.hpp` 是 core 里第一个用架构 intrinsic 的文件（`<arm_neon.h>` /
`<immintrin.h>` / `<emmintrin.h>`）。本机是 arm64，走 NEON；x86 上走 SSE2/AVX2；
一个宏都不成立时**整段向量代码消失**、落回与向量分支逐字符等价的标量状态机。
header-only、零 CMake 改动、无新依赖。

库这边的结论（**一开始判错了，已改正**）：

* 起先我把 `level/JsonScan.hpp` 从安装集里排除了，理由是"装出去的公共头应零架构依赖"。
  **那个理由是错的**：这个头**只被 `LevelData.cpp` include**，没有任何公共头引它 ——
  装不装都不影响消费者间接包含到什么，排除它只是**单方面砍掉了下游复用的可能**。
* 而且**性能根本不依赖那个头**：SIMD 路径是编进 `libadofai_core.a` 的。证据（本机 arm64，
  `otool -tv` 数 `LevelData.cpp.o`）：`cmeq.16b` ×44、`and.16b` ×190、`addv.8b` ×38 ——
  向量的逐块分类确实在里面。所以消费者 `loadFromFile` 拿到的已经是 SIMD 路径，
  与头装不装无关。
* 于是恢复安装。口径改成：**装出去，但它是内部工具头**（`adofai::jsonscan`，
  镜像自上游、接口随上游变动、不承诺稳定）。要用就按 VERSIONING.md 钉版本。
* 上游的 `check-core-purity.sh` 也过（intrinsic 不算"平台头"）—— 两边口径一致：
  core 里允许 intrinsic，本库不额外设限。

**后端覆盖情况（诚实版）**：NEON 由 arm64 macOS 那格跑、SSE2 由 x86 的 Linux/Windows 那格跑，
两边都会跑同一批解析对拍（`level_parse_parity` 比快路径与 RapidJSON 老路径逐位一致），
所以"向量 == 标量语义"在**这两种后端上**是被 CI 钉住的。
**标量兜底那条只会在异架构（arm32 / 老 x86 且无 SSE2）上生效，没有 CI 覆盖** ——
它是纯标量实现，风险低，但不是"验过"。

同一批里还有两个纯算法提交（`findTileIndex` 二分、已有序时跳过 `stable_sort`），与 intrinsic
无关，原样镜像。另外 `Settings` 多了一个 `separateCountdownTime`（决定**时间基准**），
`FastAction` 多了 `strId2`（AnimateTrack 的 `trackAnimation` 字符串，之前只留了个 flag）——
导出模块两处都跟上了：前者进写方与指纹，后者现在写**事件自己的**字符串而不是借 settings。

### 第七次对齐（596138c）：`JsonScan` 去掉了 MSVC 分支 —— 一个要盯的回归

上游那笔 `4908e56 refactor(level): JsonScan 去掉 MSVC 分支，只认 GCC/Clang 的宏`，理由写得很清楚：
"不为 MSVC 做优化……不保留 `_M_*` 分支和 `<intrin.h>` / `_BitScanForward` 那套不可验证的路径
（Windows 侧统一用 MinGW/GCC）"。

**但注释里那句"MSVC 不定义这些宏时会直接落到标量兜底（仍能编译）"是不成立的。**
`ctz32` 的定义在命名空间作用域、**无条件**用 `__builtin_ctz`：

```cpp
inline unsigned ctz32(uint32_t x) { return (unsigned)__builtin_ctz(x); }   // 第 49 行
```

向量后端确实被 `#if` 掉了（MSVC 不定义 `__SSE2__`），可 `ctz32` 是**普通函数定义**——
MSVC 不认 `__builtin_ctz` 这个标识符，即使没人调用也会报错。所以 MSVC 从源码构建
`LevelData.cpp` 会**编译失败**，不是"变慢"。

对库的影响分两种，必须分清：

* **用预编译 `.a` 的消费者**：不受影响 —— `JsonScan.hpp` 只在 `LevelData.cpp` 里被编一次。
* **源码构建（`add_subdirectory` / 自己编）的消费者**：MSVC 会失败。本库 CI 的 windows 那格是
  **MSYS2 mingw64（GCC）**，所以 CI 看不出来。README 的"平台"一行已写明
  "Windows 上请用 MinGW/GCC，MSVC 不在支持范围"。

**建议上游只补一行**（这也是让那句注释变成真话的最小改法）：给 `ctz32` 加回
`#if defined(_MSC_VER)` 的 `_BitScanForward` 分支（4 行），向量后端继续只认 GCC/Clang。
其余部分（`_M_X64` 的 SSE2 分支）确实可以不保留 —— 那只是"MSVC 上少了向量加速"，不致命。

### 第八次对齐（85ca708，32 个提交）：上游有了一个"调试用"的回写工具

`.adocao` 升到 **v7**：自描述的 `EventSchema` + `EventColumns`（外加 EventIndex / 事件窗口 /
直接解码），于是 v6 会整体丢的类型（装饰物、未知 eventType）也能无损存进容器。配套新增
`archive/AdocaoJsonWriter`：`writeLevelJson(lv, out, err)` —— **`.adocao`(v7) → `.adofai` 的无损回写**。

**它不是导出功能。** 上游把一级命令 `adocao json` 主动**降级**成了 `adocao debug json`
（commit `8407089`），`docs/cli.md` 写明"它不是导出功能（我们不做 `.ilytx` 导出，也不把
`.adofai` 回写当产品能力）"；随后（2026-10-22，工作区里进行中）又把它**从 `archive/` 挪进了
`tests/`** —— 只作它自己的容器往返验证。所以"ADOCAO 刻意不做导出"这句仍然成立。

库因此**自己实现了一份**：`src/adofai/export/LevelV7Json.cpp` 的 `exporter::writeV7Json`。
算法与上游那份同源（Schema → EventColumns → 原始事件 JSON → 按 `source` 分回 actions/decorations），
但住在本库里、不依赖一个上游随时会搬走的符号。两处改进：角度走 `angleCount()/angleAt()`
（压缩过角度也写得出来）；settings 缺 `rawSettingsJson` 时退回建模写方（复用 `writeSettingsObject`，
不再抄一遍 29 个键）。

`archive/AdocaoJsonWriter.cpp` 已**从库的构建里摘掉**（文件还在镜像树里，等上游提交那次移动后
`rsync --delete` 会删除它）—— 这样配置期不会因为"找不到源文件"报错。

它跟库自有的 `ADOFAI::export` 是**两个东西**：

| | 上游 `AdocaoJsonWriter` | 库 `ADOFAI::export` |
|---|---|---|
| 输入 | 必须来自带 v7 段的 `.adocao` | **任意** `LevelData`（`.adofai`/`.xz`/`.zst`/`.adocao`）|
| 保真 | **无损**（原始事件 JSON + `rawSettingsJson`）| 建模字段（+ 五类富事件）|
| 输出 | 只有明文 JSON | `.adofai` / `.xz` / `.zst` / `.adocao` |

库这边的处理是**分层**而不是二选一：`writeTo()` 发现 `v7SchemaBytes`/`v7ColumnsBytes` 非空
就走 `writeV7Json`（库自己的无损实现），否则走建模写方 —— 目的是"有全部原始字段时别退化成
建模子集"。上游既不提供导出能力、也不提供这个函数（它已经搬去 `tests/` 了）。`canExport()` 同理 —— 有 v7 段时连 ScaleRadius 都能写
（它本来就在原始事件里），没有 v7 段时才因为"上游没留原始事件"拒绝。

实测（`ctest export_roundtrip` 新增一段）：`preserveRawJson = true` 读谱 →
`pack(withV7Events, v7IncludeLegacy)` → 读回来 → 导出 `.adofai` →
**`UnknownType` 被保留**；而且 settings 原样是 `{"bpm": 150}`（不会被补成 29 个建模键）。

⚠️ **两个 API 细节，踩过才知道**：
* 捕获原始成员要**先** `level.preserveRawJson = true` 再 `loadFromFile` —— 默认是 `false`
  （那是有内存/时间成本的），关了就打不出 v7 段。
* `pack` 的 `withV7Events` 默认**只写"v6 存不下的类型"**（去重）。要能回写就必须
  `v7IncludeLegacy = true`（不去重）。

### 第九次对齐（d4e8219，41 个提交）：TIFF 出图（不进库）、场景组装挪进 render

* **`render/LevelScene.{hpp,cpp}` + `SceneConfig.hpp`**：场景组装（相机 / 剔除 / 绘制顺序的
  **无 GL 部分**）从 `app/` 挪进了 `render/` —— 于是它进了镜像，库的 render 目标要加
  `LevelScene.cpp`（已加）。这是"上游把 app 里的纯逻辑往下沉"的又一步，方向对库有利。
* **TIFF 出图**：`core/map/TiffWriter.{hpp,cpp}` + `tests/tiff_writer_test.cpp`。
  **`core/map/**` 是排除目录**（PLAN §3：出图留在本体），所以这两个都不镜像 —— 库里的
  `tests/` 也就不会挂 `tiff_writer`。这不是漏，是边界。
* `archive/{AdocaoColumns,AdocaoWriter}`、`audio/AudioEngine`、`core/camera/CameraDirector.hpp`、
  `render/{TileGeometryReference,TileMesh.hpp,TileShape.hpp}` 同步；`adocao_blocked_test.cpp` 改了
  断言（分段列**必须**选分块），已跟着拷。

**这轮我自己踩的两个坑（都记在 AGENTS/SYNCED_AT 里）**：

1. **漏拷被修改的测试源码**：只 rsync 了 5 个目录 + 夹具，没跑 `for f in …` 那份清单，于是
   `tests/adocao_blocked_test.cpp` 还是旧的，而 `archive/*` 已经是新的 → `adocao_blocked` 两条
   断言红（"分段列应当选分块"）。**对账会抓到它**（旧副本与 HEAD 逐字节不同 → DIFF），
   所以对账要在跑测试**之前或至少提交之前**跑。
2. `std_includes` 守卫**在 Windows 上自己红了**：脚本最后那句 `print(f"OK: …（…）")` 带中文，
   cp1252 控制台抛 `UnicodeEncodeError`。其它 `check_*.py` 早就有 `sys.stdout.reconfigure`
   前置，这个新脚本漏了。已补，并用 `PYTHONIOENCODING=cp1252` 验过两条输出路径（成功与报错）。

顺带一提：上游这轮加了 `scripts/check-gcc.sh`（推之前用真 GCC 全量语法检查）——
和我的 `std_includes` 守卫是同一类补丁，只是他们走编译器、我走静态扫描。

### 第十次对齐（f64f038，42 个提交）：粘性轨道 / 多星球把 `strId2` 变成了**复用字段**

模型新增两样，都让导出模块的假设失效了一次：

* **`PositionTrack` 的 `stickToFloors` 三态**（0=关 1=开 2=事件没写），上游把它塞进了
  `FastAction::strId2` —— 而 `strId2` 原本只被 `AnimateTrack` 当 `actionStrTable` 的下标用。
* **`MultiPlanet`**：新的事件类型，仍留在 `actions` 里，只有一列（星数 `val1`）。
* 顺带：`tilePositionOffsets` 从"一层一条"变成"一层可多条"（同层 justThisTile 与继承型不再互相覆盖）。

**护栏是怎么报警的**：`.adocao` 往返指纹在 `f24_positiontrack_variants.json` 上不一致
（`82a4f471…` vs `81ce9722…`）。根因在**我的** `modelDigest`：它对所有类型都拿 `actionStr2(a)`
当字符串解，而 PositionTrack 的 `strId2` 是 0/1/2 —— 越界下标退化成空串，而池长度在 JSON 与
`.adocao` 两条路下不同（`.adocao` 的池含 settings 字符串），于是指纹飘了。

三处修：

1. `modelDigest` 改成**按类型**取：AnimateTrack → `actionStr2` 字符串；其余 → `strId2` 数值；
2. `putAction` 的 PositionTrack 补 `stickToFloors`（只有 `strId2 != 2` 才写 —— 写 `false` 也算
   "写过"，写成显式默认会破坏继承语义）；
3. `putAction` 补 `MultiPlanet`（写 `"planets": N`）。**这个类型原先会掉进 `default`，连
   `eventType` 都不写** —— 会产出坏 JSON，只因为还没有夹具带它而一直没暴露。

为了让后两件能被"看见"，`export_roundtrip` 加了一段**手搓 LevelData**：三种 stick 取值 +
一个 `MultiPlanet(3)`，导出再解析，断言三态与星数都原样回来。

顺带一提：`export_independent` 在第二轮先红了 —— 它把 `.adocao` 的版本号**写死成 2**，
而上游升到了 v3（分块列 + `Derived` 段）。那不是库的 bug，但正是"独立实现"该做的事：
它把格式变更逼到台面上。现在改成从 `AdocaoFormat.hpp` 读 `kVersion` 对账 ——
既不写死（升版不假红），也仍然抓得住"写方与读方不是同一个版本"。

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

三平台：ubuntu（主力矩阵，8 格）/ macOS（render=ON）/ windows(MSYS2)。
Linux 的 8 格是 `ARCHIVE` / `EXPORT` / `BUNDLE_DEPS` / `ASSET_ZIP` / `AUDIO` / `AUDIO_DEVICE`
/ `RENDER` 的组合（`bundled` / `no-archive` / `no-export` / `system-deps` / `no-asset-zip` /
`no-audio` / `all-modules` / `render-on`），其中 `bundled` 与 `no-export` 两格还做
install + `find_package` 的消费者验收；`no-export` 那格额外验"模块 OFF 的承诺"（前缀里不许有
`add_library(ADOFAI::export`、export 的公共头、`libadofai_export.a`）。
另有一个 nightly job 每天拿**未经确认的上游 HEAD** 跑一遍"镜像 + 构建 + 测试"。

**本地只跑 `default` + `render` 两格**（约 1.5 分钟），其余 6 格交给上面这 10 个 job ——
8 格全量在本地要 5~6 分钟且每格都从零重编 lzma/zstd/glm，收益只到"CI 复核"为止。
同样的道理，`push` 之后**不守 `gh run watch`**（一轮 3~4 分钟纯等）：把 run 链接报出来就够，
**GitHub 的通知邮件就是"跑完了"的信号**，红了再 `gh run view`。见 [AGENTS.md](AGENTS.md)
每周清单第 4、6 条。

## 不在镜像范围内、且不打算补的

- **`tests/capture_states.txt` + 像素门槛（`capture-gate.sh` 的 50 状态逐字节）**：它要跑本体
  app 抓帧，属于本体验收。库里覆盖的是 L0/L1/L2 三层几何 + 内嵌回退 GLSL 的逐字一致。
- **`app/**`**（ImGui 启动器 / 向导 / 地图导出 / LevelLoader 的窗口化开关都在这里）。
  `font_cjk_test` 因此也没挂（它链 `adocao_imgui`）。
- **`core/map/**`**（map / 出图，本体拉流量）。
- **`tools/tile-geometry-lab/**`**（上游的几何实验台）。
- `kick_chord`（要 `assets/hitsounds`，不在白名单）、`timeline_ab_dump` / `tl_*_dump`
  （上游自己也没挂 add_test，是给 app 用的 dump 工具）。
