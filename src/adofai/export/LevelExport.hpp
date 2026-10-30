#pragma once

#include <cstdint>
#include <string>
#include <vector>

// 谱面导出：把**已加载**的 LevelData 写成四种容器之一。
//
//   Format::Adofai     明文 JSON `.adofai`
//   Format::AdofaiXz   xz 压过的同一份 JSON `.adofai.xz`      ┐ 这三种都要求
//   Format::AdofaiZst  zstd 压过的同一份 JSON `.adofai.zst`    ┘ ADOFAI_LIB_ARCHIVE=ON
//   Format::Adocao     列式二进制 `.adocao`（archive 的 packLevel）
//
// 为什么 `.adocao` 也在这里：它本来就是"导出"这条链上的目标格式，只是它的打包器按依赖倒置
// 住在 archive 里（archive → core 是允许的方向，export → archive 同理）。
//
// 四者共用同一个保真契约（见 export/LevelJsonWriter.hpp 的 writeJson）：**只写库建模的字段**。
// 未知 eventType / 未建模的 settings 键 / decorations 在**解析**那一步就没了，导出无法凭空造回来。

namespace adofai {

struct LevelData;

namespace exporter {

enum class Format : uint8_t {
    Adofai = 0,
    AdofaiXz,
    AdofaiZst,
    Adocao,
};

// "adofai" / "adofai.xz" / "adofai.zst" / "adocao"（不含点，给日志和 CLI 用）
const char* formatName(Format f);
// ".adofai" / ".adofai.xz" / ".adofai.zst" / ".adocao"
const char* formatExtension(Format f);

// 按后缀认格式（大小写不敏感；长后缀优先，所以 `x.adofai.xz` 不会被认成 `.adofai`）。
// 认不出返回 false —— 库不替调用方猜"没有后缀就当明文"。
bool formatFromPath(const std::string& path, Format& out);

// 这个构建里这个格式写得出来吗？Adofai 恒真；其余三种要求 archive（压缩库 + `.adocao` 打包器）。
// 不可用时 writeTo/exportLevel 会返回 false 并给出明确原因，绝不静默换格式。
bool formatAvailable(Format f);

struct Options {
    uint32_t xzPreset  = 6;    // 0..9，默认与 `xz -6` 同档
    int      zstdLevel = 19;   // 1..22，默认与 `.adocao` 段级压缩同档
    // 写完立刻**自己解回来比对**。四种格式都做：
    //   `.adofai`  → 重新解析写出的 JSON，比建模指纹（`exportOnly` 那条便宜的路）；
    //   `.xz`/`.zst` → 用 archive 的读方解回来，逐字节比源 JSON；
    //   `.adocao`  → `unpackLevel` 解回来，比建模指纹。
    // 默认开：这是"绝不写出自己读不回来的文件"的唯一机械保证，代价是编码/解析时间大约翻倍
    // （解码比编码快得多，实测小谱可忽略，GB 级谱才明显）。关掉请自担。
    bool     verify = true;

    // ── `.adocao` 打包参数（透传给 archive 的 `PackOptions`；只在 ARCHIVE=ON 时生效）──
    // 为什么要暴露：默认的 `Auto` 会**同时**跑 zstd-19 与 lzma 9|EXTREME、取更小的那个 ——
    // 大谱上是两倍压缩时间 + lzma 编码器约 674 MB 内存。要压上千万层那种规模，得能单挑
    // 一个编码器（`Zstd` 快、`None` 只测纯序列化）。
    enum class PackCompress : uint8_t { Auto = 0, Zstd, Lzma, None };
    PackCompress packCompress    = PackCompress::Auto;
    int          packLevel       = 0;       // 0 = 各编码器默认（zstd 19 / lzma 9|EXTREME）
    bool         packCheckpoints = false;   // `Derived` 段：每 4096 层一份续算快照
    // 写 **v7 事件段**（EventSchema/EventColumns，且**不去重**：v6 段已有的类型也写进去）。
    // 开了它，产出的 `.adocao` 才能被**无损**回写成 `.adofai`（`exporter::writeV7Json`）——
    // 连装饰物、未知 eventType、未建模的 settings 键都不丢。代价是文件里事件存两份。
    // 需要读谱时 `level.preserveRawJson = true`；没有原始切片时这一段会被跳过（不报错）。
    bool         packV7          = false;
};

struct Report {
    Format   format          = Format::Adofai;
    uint64_t bytes           = 0;   // 产物的字节数（= out.size()）
    uint64_t jsonBytes       = 0;   // ① JSON 文本的字节数（Adocao 为 0）
    uint64_t compressedBytes = 0;   // ② 压缩帧的字节数（Adofai 为 0）
    uint64_t angleCount      = 0;
    uint64_t actionCount     = 0;
};

// 只序列化，不落盘。out 覆盖写；失败时 err 里是原因（中文，面向人读的日志/CLI）。
bool writeTo(Format f, const LevelData& level, std::vector<uint8_t>& out, std::string& err,
             const Options& opt = Options(), Report* report = nullptr);

// 序列化 + 落盘。落盘是"先写 `<path>.tmp` 再改名"：中途失败不会留下半截文件冒充成品。
//
// ⚠️ **别在 `LevelData::releaseMemory()` 之后导出**：那个函数会把 `actions`（以及 `pathData`）
// 放掉，而这两样正是本模块要写的东西 —— 导出的文件会**少掉全部事件**，而且因为输入自己已经
// 空了，`Options::verify` 也检测不出来（它比的是"输入 vs 导出物"，两边都空）。
// 顺序应当是"加载 → 导出（或先把要的东西另存一份）→ releaseMemory"。
bool exportLevel(const LevelData& level, Format f, const std::string& path, std::string& err,
                 const Options& opt = Options(), Report* report = nullptr);

// 按后缀选格式（认不出就报错，不猜）。
bool exportLevel(const LevelData& level, const std::string& path, std::string& err,
                 const Options& opt = Options(), Report* report = nullptr);

}  // namespace exporter
}  // namespace adofai
