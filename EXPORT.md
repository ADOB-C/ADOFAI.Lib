# 怎么写 `.adocao` 导出（写入方实现指南）

> **先用现成的**：本库自带写入方 `ADOFAI::export`（源码在 `src/adofai/export/`），
> `.adofai` / `.adofai.xz` / `.adofai.zst` / `.adocao` 四种格式一个入口 —— 用法与保真契约
> 见 [README](README.md#导出谱面)。本文是给**要自己重新实现一遍**的人看的：ADOCAO-E（编辑器导出）、
> ADOprobe（工具转换）、或者别的语言里的实现。它同时就是**格式规范**。
>
> 参考实现：
> * `.adocao` 的读写方在上游 ADOCAO 的 `archive/AdocaoFormat.hpp`、`AdocaoColumns.{hpp,cpp}`、
>   `AdocaoWriter.{hpp,cpp}`、`AdocaoReader.{hpp,cpp}` —— **这几个文件已经镜像进本库**，
>   可以直接调 `adofai::adocao::packLevel()` / `unpackLevel()`（要对齐到更新的版本见
>   [CONTRIBUTING.md](CONTRIBUTING.md)）；
> * `.adofai`（明文 JSON）的字段映射在本库的 `src/adofai/export/LevelJsonWriter.cpp`。
>
> 为什么这事落在库里而不是上游：ADOCAO 本体**刻意不做导出**（它的 `--export` 是打拍音 WAV，
> 不是谱面容器；`archive/` 里只有读方加一个给别的工具用的 `packLevel`）。所以"把谱面写成
> 容器"要么用本库，要么照本文自己写。

## 0. 一句话

`.adocao` 是**列式二进制容器**：**只存输入**（角度 + 事件），**每列按它的取值基数选编码**
（字典 / 游程 / 差分 / 位打包 / 常量 / 原始），大段再各套一层 zstd。**逐位无损**，
坏数据必须**明确失败**（绝不静默给半截数据）。

为什么值得：同一张谱（"Won't You" 的 677 万层 MYC）

| | 大小 |
|---|---|
| 明文 `.adofai`（JSON）| 611.18 MB |
| `xz -6` 直接压那份 JSON（文本路线的最好成绩）| 2.13 MB |
| **`.adocao`** | **6,120 B** |

—— 因为二进制**不是"压过的文本"**，而是同一份结构的**另一种表示**：JSON 里那些字段名、
十进制数字、每条都重复的 `"eventType"` 在列式表示里根本不存在。

## 1. 文件布局

```
Header(76 B) │ SectionEntry × sectionCount(40 B each) │ 填充到 8 B │ 段载荷…
```
所有整数**小端、定宽**；段偏移 **8 B 对齐**；填充字节**写 0**（这样同一份输入两次导出逐字节相同）。

### 1.1 Header（76 B）

| 偏移 | 类型 | 字段 | 说明 |
|---|---|---|---|
| 0 | char[4] | `magic` | `"ADO1"`（`41 44 4F 31`）|
| 4 | u16 | `version` | 现在 **2**。**唯一的兼容闸门**（读取方看到别的值必须拒绝，不许猜）|
| 6 | u16 | `flags` | 写 0 |
| 8 | u16 | `sectionCount` | |
| 10 | u16 | `reserved` | 写 0 |
| 12 | u64 | `fileSize` | 整个文件的字节数（读取方拿它做截断检测）|
| 20 | u32 | `headerCrc` | 见 §4 的覆盖规则 |
| 24 | u8[20] | `writerCommit` | 写入方 git commit 的**原始 20 字节**：构建期由 `git rev-parse HEAD` 注入（CMake 里只给 `adocao_archive` 目标加 `-DADOCAO_GIT_COMMIT=<40 位十六进制>`，写入方解码成 20 字节）。拿不到 git（tarball / 离线）就写全 0。它记的是**构建**的 commit，不是打包那一刻的 |
| 44 | u8[32] | `inputHash` | **内容指纹**：前 8 字节 = `FNV-1a-64`（小端），后 24 字节写 0 |

**内容指纹的准确算法**（照这个算就能逐字节对上）：

* `FNV-1a-64`：`h = 1469598103934665603`（offset basis）；对每个字节 `h ^= b; h *= 1099511628211`；
* 覆盖范围 = **按写入顺序**的每一段：先段 id（1 字节），再该段的**未压缩**内容（`rawSize` 字节）；
* 所以它与压缩器、与构建无关 —— 同一份谱在任何实现里都得到同一个指纹（去重 / 溯源用）；
* 结果小端写进前 8 字节，后 24 字节写 0；读取方**不看**它，也**不许**当兼容闸门。

> 注意上一条的后果：**跨构建**两次导出会在这 20 字节上不同，所以"同一输入两次导出逐字节相同"
> 这个性质要在**同一构建内**理解（`archiveRoundTrip` 断言的就是后者）。
>
> 实测补充（读方的一个良性不对称，写别的东西的人也常踩）：**读回来的 `LevelData.actionStrTable`
> 比原来长**。因为写入方的池是"`actionStrTable` 原序 + 末尾追加 settings 用到的字符串"，
> 而读取方把整张池原样装进 `actionStrTable` —— 于是读方多出 settings 那几项。它们**没有任何
> 读者**（只有 actions 的 `strId` 指向池头），所以无害；但别拿 `actionStrTable` 的**长度/内容**
> 当往返是否成功的判据，也别把它算进内容指纹（本库的 `modelDigest` 因此只算每个 action
> **解出来的字符串**）。

> 这两个溯源字段**只作溯源**，**绝不作兼容闸门** —— 版本门禁只认 `version`；
> 没有 git 的构建写全 0 并标 unknown，**不影响可读性**。

### 1.2 SectionEntry（40 B）

`id(u8) │ codec(u8) │ flags(u16) │ offset(u64) │ compSize(u64) │ rawSize(u64) │ elemCount(u64) │ crc32c(u32)`

* `offset`：相对**文件头**的字节偏移（≥ 头 + 段表，8 的倍数）；
* `compSize` / `rawSize`：段**在文件里**的字节数 / 段**解码后**的字节数；
* `elemCount`：这一段的元素个数（角度数 / 事件数；其余段写 0）；
* `crc32c`：覆盖该段 `compSize` 字节（即**存储字节**，压缩后那份）。
  算法是 **CRC32C（Castagnoli，多项式 0x82F63B78）** —— 不是 zlib 的 CRC32 ✗。
  信任锚：`crc32c("123456789") == 0xE3069283`。

### 1.3 段

| id | 段 | 内容 |
|---|---|---|
| 1 | `Settings` | 定长记录，字段顺序见 §3（**最容易写错的地方**）|
| 2 | `AngleData` | **一列** double（列编码见 §2）|
| 3 | `Actions` | `u32 columnCount(=6)` + 每列 `u64 byteLen + bytes`（见 §3.2）|
| 4 | `StringPool` | `u32 count` + 每项 `u32 len + bytes`；**`v[0]` 必须是空串** |
| 5 | `PathData` | `u64 len + bytes`（仅在非空时写）|
| 6 | `Preserved` | **保留，不要写**（未识别成员的原始字节，留给"无损回写"用）|
| 7 | `Derived` | **保留，不要写**（预计算/检查点）|

段的 `codec` 字段：`0 = Raw`、`16 = 整帧 zstd`、`17 = 整帧 xz`。
**未知的段 id 读取方会跳过**（前向兼容），但写入方只该写上面这些。

## 2. 列编码（段内自描述）

列头（20 B）：`codec(u8) │ bits(u8) │ flags(u16) │ count(u64) │ aux(u64)`
—— `aux` 是字典项数（`Dict`/`Rle` 用），其余编码写 0。

| codec | 值 | 载荷 |
|---|---|---|
| `Raw` | 0 | 定宽原始：double/int64 每项 8 B，u32 每项 4 B |
| `Dict` | 1 | `aux × 8 B`（double/int64）或 `aux × 4 B`（u32）字典 + **位打包**的下标 |
| `DeltaVarint` | 2 | 首值 `zigzag(varint)`，其后每项 `zigzag(相邻差)` 的 varint |
| `Const` | 3 | 一个值（8 B 或 4 B）|
| `BitPack` | 4 | 位打包（位宽 = `ceil(log2(max+1))`）|
| `Rle` | 6 | 字典（`aux × 8/4 B`）+ `u64 runCount` + 每游程 `(varint 下标, varint 长度)` |
| `DeltaRleVarint` | 7 | `u64 runCount` + 每游程 `(varint zigzag(差分), varint 长度)` |

约定：
* **位打包是低位在前**（第 i 项从第 i·bits 位开始，1..32 位/值）；
* varint 是**无符号 LEB128**；zigzag 是有符号那个；
* **`DeltaRleVarint` 的游程是对"差分流"取的**（"这个差分连续出现 len 次"）——
  解码要**累加 len 次**，不是把同一个值重复 len 次。写反了不会崩，只会体积不对。
* 上表的顺序不是优先级：**每个候选都算出来，取实测字节数最小的那个**。
  然后再**解码回来逐位比对**，不等就退回 `Raw` —— 这样任何输入都不会编错，最坏只是不省。

## 3. 各段怎么写

### 3.1 `Settings` 定长记录（字段顺序照抄）

```
i32 version │ f32 bpm │ f32 offset │ i32 countdownTicks │ f32 zoom │ f32 rotation │
u16 relativeTo │ f32 position[0] │ f32 position[1] │ u16 hitsound │ f32 hitsoundVolume │
u16 trackColor │ u16 secondaryTrackColor │ u16 backgroundColor │ u8 stickToFloors │
u16 planetEase │ u16 trackDisappearAnimation │ u16 trackAnimation │ f32 beatsBehind │ f32 beatsAhead
```

* 所有 `u16` 都是**字符串池下标**（`0` = 空串）；
* `f32` 按**位模式**原样写（float 必须逐位无损）；
* 未知 settings 键与今天两条解析路径的行为一致：只读已知字段（无损回写靠 `Preserved` 段，尚未启用）。

### 3.2 `Actions`（列式，6 列，顺序固定）

`floor(int64) │ type(u32) │ strId(u32) │ flag(u32) │ val1(u32) │ val2(u32)`

* `count` 对这 6 列都是**事件总数**；
* `val1` / `val2` 是 **float 的位模式**当 u32 存（必须逐位无损）；
* **`strId` / `val1` / `val2` 是"按类型稀疏"的列**（实测省 33.7%），列内容 =
  `u32 typeMask │ u64 usedCount │ 一列`：
  * `typeMask` 的 bit `t` = "类型 `t` 的事件在这列里出现过非默认值"（**自描述**，不靠语义硬编码）；
  * 列里**只**放掩码命中那些事件的值，**顺序不变**；
  * 读取方按事件顺序走：命中就取下一个值，否则取 `0`。
    这样事件顺序与语义**逐位不变**，而 99.6% 是 `Twirl` 的那张谱不必为 `val1/val2` 付下标钱。
* 另外注意**只写输入**：事件的 `floor/type/strId/flag/val1/val2` 就够了；
  **砖的位置 / 朝向 / 每层 BPM / 时间线都是派生的**，一律**不要**写进文件（它们由读取方重算）。

### 3.3 段级压缩

* 对每个段：**≥ 256 B** 才值得压；用 zstd（建议 level 19）压一次，
  **压不小就存原样**（`codec = 0`）；
* 压缩后的字节就是 `compSize` 的那份 —— `crc32c` 覆盖它，`rawSize` 记原始长度；
* **不要对已经是 v2 的文件再压一次**：段本身已经是 zstd 帧，再套一层只是多几个帧头
  （实测 6,120 B → 9,795 B，反而更大）。

## 4. 校验和（两条覆盖规则）

* **段**：`crc32c` 覆盖**存储字节**（`compSize` 字节）—— 读取方**先校验再解压**，
  解压后还要核对长度 == `rawSize`；
* **头**：`headerCrc` 覆盖 **Header 的前 24 字节（`headerCrc` 自身按 0 参与）+ 整张段表**。
  写成"只盖 Header"是不行的：段表也得能被查出损坏。

## 5. 写入方必须保证的不变式

1. **逐位无损**：double 按位模式进字典；float 按位模式进 u32 列；解码结果与输入 `memcmp` 相同。
2. **确定性**：同一份输入两次导出**逐字节相同**。为此：
   字符串池的追加顺序固定（`actionStrTable` 原序 + settings 字符串按 §3.1 的字段顺序追加）；
   填充字节写 0；zstd 用固定 level（单线程的那套 API，别用带 worker 的）。
3. **小端 + 定宽**；长度/偏移/计数一律 u64（**从格式层消除 2^31 边界**）。
4. **版本闸门**：`version` 变了就拒绝。不要用 `writerCommit`/`inputHash` 当闸门。
5. **坏数据明确失败**：截断、crc 不符、未知 codec、字典下标越界、游程总数与 `count` 不符 ——
   全部返回失败，绝不"尽力而为"。

## 6. 怎么算写对了

写入方自己**无法**证明自己是对的（这话是经验）。最低验收：

0. **先看本库现成的那套**（`ctest -R export` 就能跑）：
   * `export_roundtrip` —— 四种格式 × 全部夹具：导出 → 重新解析 → 建模字段指纹必须逐位相同；
     同输入两次导出逐字节相同；明文导出还要**老解析路径**也读得动；`.adocao` 的
     `pack(原件)` 与 `pack(导出物)` 逐字节相同；末尾一组负向对照（截断/改坏必须失败）。
     它同时是导出模块的**漂移护栏**（上游给 `LevelData` 加字段而导出没跟上 → 立刻红）；
   * `export_independent` —— 用**第三方**实现说话：Python 的 `json` 解出来的必须是合法 JSON
     （自己的解析器比 JSON 宽容，所以"自己能读回来"不等于"是合法 JSON"）、Python 的 `lzma`
     解出来的 `.xz` 必须与明文逐字节相同、`zstd` CLI 同理、`.adocao` 头字段自洽；
     另有一份**在 Python 里另写一遍**的建模投影用来对拍。
1. **往返**：拿你的读取方对同一个输入解析 → 与"读明文 JSON"得到的数据**逐位相同**。
   ADOCAO 的做法：把 `angleData / actions / settings / tiles / tileBPMs / tileHitsoundVolumes …`
   共 13 个节各做 FNV 摘要比对，**0 处不一致**才算过；并且额外验一次
   "`.adocao` 出的图和明文 JSON 出的图**逐字节相同**"。
2. **可复现**：同一输入导出两次，两份文件 `cmp` 相同。
3. **负向对照**（这几条必须**失败**）：截断文件、改坏任意段的载荷、把 `version` 改掉、
   把某列 `count` 改大、把字典下标改成越界值。任何一条"竟然成功"都说明校验没到位。
4. **给自己写一个 `--codec-report`**：打印每列选中的编码、元素数、朴素字节 / 编码后字节、
   字典项数、位宽。这是唯一能让你看出"某列选错了编码"的手段。

## 7. 别做的事

* ✗ 不存派生数据（位置/朝向/BPM/时间线）；
* ✗ 不对 v2 文件二次压缩；
* ✗ 不手改镜像里的 `src/adofai/**`（本库是单向镜像：先在 ADOCAO 改、提交，再按
  `CONTRIBUTING.md` 对齐）。**唯一的例外是 `src/adofai/export/**`** —— 那是库自有的导出模块，
  没有上游对应物，改它不受镜像规矩约束（但要跑 `ctest -R export`，那是它的护栏）；
* ✗ 不用 `writerCommit` / `inputHash` 做兼容判断；
* ✗ 不发明新的段 id / codec 值而不更新 `version`（读取方会按未知段跳过、按未知 codec 失败）。
