#pragma once

#include <cstdint>
#include <string>

// 谱面导出（写入方）—— **库自有的模块**，不在单向镜像的白名单里。
//
// 为什么库自己实现而不是等 ADOCAO：本体**刻意不做导出**（它的 `archive/` 只有读方与
// `.adocao` 的 pack，没有任何 `.adofai` 写入方，见 EXPORT.md）。而"解析 → 导出"这条链
// 恰恰是分发库要对外提供的东西：ADOCAO-E（编辑器）、ADOprobe、别的语言的重新实现都在这条链上。
//
// 为什么放在 `src/adofai/export/` 而不是上游：上游不管这件事，放上去等于给主源塞一个
// 它不需要的功能。代价是**这段代码不在镜像的保真对账范围内** —— 所以它自带一条漂移护栏：
// tests/export_roundtrip_test.cpp 断言 `parse(write(parse(t)))` 与 `parse(t)` 在库建模的
// 字段上逐位相同。上游一旦给 LevelData 加字段而这里没跟上，那条测试立刻红（详见 MIRROR.md）。

namespace adofai {

struct LevelData;

namespace exporter {

// LevelData → 明文 `.adofai`（JSON 文本）。
//
// **契约（右逆）**：`parse(writeJson(parse(text)))` 与 `parse(text)` 在"库建模的字段"上
// **逐位相同** —— settings 的 20 个字段、angleData、actions 的 6 列、pathData。这条由
// tests/export_roundtrip_test.cpp 对每个夹具 × 每种格式断言，也是这个模块唯一的一致性定义。
//
// **明确不承诺的**（LevelData 里根本没有这些信息，不是在导出这一步丢的）：
//   * 未知 `eventType` 的 action —— 两条解析路径在**读入时**就丢弃了（`keep = ... type != Other`）；
//   * 未建模的 settings 键、`decorations`、注释、键序、数字字面量的写法（`1e2` / `100`）；
//   * 逐字节回写原文。无损回写要等上游的 `Preserved` 段（ADOCAO TODO ⑦），本库不假装有。
//
// 另外：`pathData` 与 `angleData` **非空即都写**。pathData 型的谱面因此在导出后多带一份
// angleData（体积换保真：重新解析后两个字段都逐位还原）。要体积就用 `.adocao`。
bool writeJson(const LevelData& level, std::string& out, std::string& err);

// 这份 `LevelData` 能不能写成**明文 JSON**？
//
// 2026-10-22 起，五类"富事件"（MoveCamera / RepeatEvents / ColorTrack+RecolorTrack / Flash /
// MoveTrack）**都能**写回 `actions` 了（见 `putRichEvents()`）。
//
// 只剩 **ScaleRadius** 不行：上游解析它时就把结果烘进了 `tiles[i].position`，
// **原始事件一个字段都没留**（没有 floor、没有数值），`.adocao` 也不存它 ——
// 所以这不是"还没做"，是"模型里没有"。遇到它就**明确拒绝**（`why` 里说明原因），
// 绝不静默写出一个少了事件的文件。
bool canExport(const LevelData& level, std::string* why = nullptr);

// "库建模字段"的指纹（FNV-1a-64，逐字段按位喂字节）：
// settings 全体 + angleData + pathData + 每个 action 的 6 列（字符串按**解出来的内容**算，
// 不按池下标 —— 池的形状不是建模内容，理由写在 LevelJsonWriter.cpp 的 modelDigest 里）。
//
// 用途只有一个：往返自检与"这次导出到底丢没丢东西"的证据 —— **不是文件格式的一部分**，
// 别写进产物、也别跨实现比对（换实现就没有同一个指纹）。要跨实现比内容用 `.adocao` 的
// `inputHash`（那个算法写在 EXPORT.md §1.1）。
uint64_t modelDigest(const LevelData& level);

}  // namespace exporter
}  // namespace adofai
