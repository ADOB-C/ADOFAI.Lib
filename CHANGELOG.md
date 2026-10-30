# 变更日志

只记**库**的变更。镜像进来的 `src/adofai/**` 天天在动，那部分看仓库根的
[SYNCED_AT](SYNCED_AT)（当前对齐到哪个 ADOCAO commit）；两个版本号的区别见
[VERSIONING.md](VERSIONING.md)。

## v0.1.0

第一个打 tag 的版本。库自身的 API 在 0.x 阶段**不做兼容承诺**，但镜像内容是可复现的：
tag 的 commit 里带着当时的 `SYNCED_AT`。

### 模块与默认值

六个模块，其中三个默认关：

| 模块 | 默认 | 说明 |
|---|---|---|
| `ADOFAI::core` | ON | 解析 / 时间线 / 解算 / 砖色 / 打拍音 |
| `ADOFAI::archive` | ON | `.adofai.xz` / `.adofai.zst` / `.adocao` / `.ilytx` 容器 |
| `ADOFAI::export` | ON | 写 `.adofai` / `.adofai.xz` / `.adofai.zst` / `.adocao`（**库自有**）|
| `ADOFAI::audio` | ON | `miniaudio` + `stb_vorbis` |
| `ADOFAI::audio_device` | **OFF** | 真播设备 |
| `ADOFAI::render` | **OFF** | OpenGL / glad / GLFW / 三层几何 |

OFF 那侧是一条承诺：不 `find_package`、公共头里没有它的类型、装出来的 target 里没有它。

### 导出

* 四种格式，都走同一条原子落盘（`.tmp` → rename）；
* **`Options::verify` 默认开** —— 写完立刻自己解回来验，绝不产出"自己读不回来"的文件；
  `.adofai` 比建模指纹，`.adocao` 比"再打一次包是否逐字节相同"，压缩格式比解压后逐字节相同；
* **不可表示的内容明确拒绝**（`canExport()` 可以先问）而不是静默丢：目前只剩 `ScaleRadius`
  —— 上游解析它时就把结果烘进了砖位、原始事件一个字段都没留；
* 五类"富事件"（MoveCamera / RepeatEvents / ColorTrack+RecolorTrack / Flash / MoveTrack）与
  `MultiPlanet`、`PositionTrack` 的粘性三态都能写回；
* **带 v7 事件段的 `.adocao` 可以无损回写成 `.adofai`**（库自己的 `writeV7Json`）：
  连装饰物、未知 `eventType`、未建模的 settings 键都不丢；验收是"容器 → 明文 → 重打包
  逐字节相同"；
* `.adocao` 的打包参数可调：`packCompress` / `packLevel` / `packCheckpoints` / `packV7`。

### 测试

`ctest` 默认 **47** 条；render=ON **53** 条；无 archive **29** 条。其中几条是**防漂移**的：

* `export_roundtrip` —— 每种格式的往返 + 确定性 + 损坏/截断负向对照 + `canExport` 对照；
* `export_independent` —— 用 Python 独立验一遍（第三方 `lzma` / `zstd` 解开、`.adocao` 头、
  每类事件条数、settings 集合）；
* `std_includes` —— 库自有源码用了 `std::stable_sort` / `std::snprintf` / … 就必须直接 include
  对应的头（libc++ 会经别的头间接带进来，MinGW 直接报错，真踩过两次）。

### 已知限制

* `ScaleRadius` 不能写明文（上游没留原始数据）；
* 第三方依赖的头与库**会进消费者前缀**（`ARCHIVE`/`RENDER` 为 ON 时）；根因是静态库不传递依赖，
  试过的机制记在 [MIRROR.md](MIRROR.md)；
* 像素门槛（50 状态逐字节）没搬 —— 属本体验收；
* 上游不支持 MSVC，本库跟随（Windows 用 MinGW / clang-cl 那条路）。
