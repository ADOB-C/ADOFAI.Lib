#pragma once

#include <string>

// `.adocao`(v7) → `.adofai` 的**无损**回写（库自有实现）。
//
// 为什么库自己写这一份、而不是调上游的：上游把这个能力当成**测试工具**（2026-10-22 它先被
// 从 `adocao` 一级命令降级成 `adocao debug json`，随后又从 `archive/` 挪进了 `tests/` ——
// 只服务于它自己的容器往返验证）。既然它不在 archive 模块里了，库就不能依赖那个符号；
// 而库这边真的需要这条路径：**归档文件里带着全部原始事件时，导出不该退化成"建模过的子集"**。
//
// 输入要求（缺了就明确失败，不猜）：
//   * `LevelData::v7SchemaBytes` + `v7ColumnsBytes` 都非空（= 这份 LevelData 来自带 v7 段的
//     `.adocao`，且打包时用了 `PackOptions{withV7Events=true, v7IncludeLegacy=true}`）；
//   * 打包时若只写了"v6 存不下的类型"（默认去重），回写会缺 v6 段已有的那些事件 ——
//     那是调用方的选择，这里的判据只是"段在不在"。
//
// 输出：`settings` 有 `rawSettingsJson` 就**原样**写回（ADOFAI 的 settings 键远多于我们建模的
// 那 29 个，逐键建模必然丢字段）；`actions` / `decorations` 按 schema 的 `source` 分回去。
// 所以这条路是**逐字节级无损**的：原始事件 JSON 原样吐出，包括未知 eventType。
namespace adofai {

struct LevelData;

namespace exporter {

bool writeV7Json(const LevelData& level, std::string& out, std::string& err);

}  // namespace exporter
}  // namespace adofai
