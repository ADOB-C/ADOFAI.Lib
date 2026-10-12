#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// 整帧压缩（写入方）。头里**没有任何 lzma/zstd 类型** —— 与 archive/LevelArchive.hpp 的
// pimpl 同一个口径：只想调压一下的消费者不必配压缩库的头文件路径，include 本头就够。

namespace adofai {
namespace exporter {

// 这个构建里压缩可用吗？= export 编译时带了 archive（它的 PRIVATE 依赖里有 lzma/zstd）。
// `ADOFAI_LIB_ARCHIVE=OFF` 时为 false，下面两个函数会直接返回失败（err 里写明原因），
// 而不是让链接期报未定义符号 —— 格式是运行期选的（枚举），所以这里也只能是运行期报错。
bool compressionAvailable();

// xz (LZMA2) 整帧编码。preset 0..9，默认 6（与 `xz -6` 同档，也是 EXPORT.md §0 那张
// 体积对照表里"文本路线"用的档位）。单线程、确定性（同一输入两次编码逐字节相同）。
bool encodeXz(const char* data, size_t length, std::vector<uint8_t>& out, std::string& err,
              uint32_t preset = 6);

// zstd 整帧编码。level 1..22，默认 19（与 `.adocao` 段级压缩同一档）。
// 用一次性 `ZSTD_compress`（不带 worker）：确定性，且不引入多线程状态。
bool encodeZstd(const char* data, size_t length, std::vector<uint8_t>& out, std::string& err,
                int level = 19);

}  // namespace exporter
}  // namespace adofai
