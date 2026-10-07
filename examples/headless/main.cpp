// examples/headless —— 库的消费者验收程序（PLAN.md §6）。它只碰 ADOFAI::core（可选加
// ADOFAI::archive），一行 GL 都没有：解析 .adofai（明文，或链了 archive 时的 .xz/.zst 容器）
// → 建时间线 → 解算位置 → 把打拍音时刻导出成 WAV。
//
// 这里刻意"自己手写 WAV"而不链 ADOFAI::audio：audio 模块 P4 才搬过来，而这个例子属于 P2，
// 它要证明的是"core 的公共 API 能独立用起来"。
//
//   headless <level.adofai> [out.wav]        # 不给 out.wav 就只打印，不写文件
//
// 压缩谱面的启用方式（上游 2026-10 的依赖倒置）：链上 ADOFAI::archive，并在启动时调一次
// adofai::archive::install()。没链 / 没调时，明文照常解析、压缩容器给出明确错误。
// 这里**必须**由构建系统告知（examples/headless/CMakeLists.txt 里的
// ADOFAI_HEADLESS_HAVE_ARCHIVE），不能用 __has_include 判断：OFF 时
// archive/Install.hpp 仍在源码树里、只是没编进任何 target，头在而符号不在。
//
// 每层一个点击音（同 ADOFAI_HitSound 的老做法：不带拖尾的双极性脉冲）——
// 不引入任何音频依赖，只用来证明时间线确实算出了逐层的打拍时刻。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#if defined(ADOFAI_HEADLESS_HAVE_ARCHIVE)
#  include "archive/Install.hpp"
#endif

#include "core/level/LevelData.hpp"
#include "core/timeline/PositionSolver.hpp"
#include "core/timeline/Timeline.hpp"

using adofai::LevelData;
using adofai::Timeline;

namespace {

// ── 一个能独立跑的 16-bit 单声道 WAV 写出器（不依赖 audio 模块） ──────────────
bool writeClickTrackWav(const std::string& path,
                        const std::vector<double>& hits,
                        double totalDuration,
                        uint32_t sampleRate = 48000)
{
    const uint64_t frames = static_cast<uint64_t>(totalDuration * sampleRate) + 1;
    std::vector<int16_t> pcm(static_cast<size_t>(frames), 0);

    // 每层一个 8 ms 的衰减点击音；两个相邻命中之间做极性交替，避免中间那段静音里的直流偏移。
    const size_t clickLen = static_cast<size_t>(sampleRate * 0.008);
    for (size_t i = 0; i < hits.size(); i++) {
        const double t = hits[i];
        if (t < 0.0) continue;
        size_t start = static_cast<size_t>(t * sampleRate);
        if (start >= pcm.size()) continue;
        const double sign = (i % 2 == 0) ? 1.0 : -1.0;
        for (size_t k = 0; k < clickLen && start + k < pcm.size(); k++) {
            const double env = std::exp(-static_cast<double>(k) / (sampleRate * 0.0016));
            const double s = sign * env * std::sin(2.0 * 3.14159265358979323846 * 1200.0
                                                   * static_cast<double>(k) / sampleRate);
            int v = static_cast<int>(std::lround(s * 24000.0));
            v = std::clamp(v, -32768, 32767);
            pcm[start + k] = static_cast<int16_t>(
                std::clamp<int>(static_cast<int>(pcm[start + k]) + v, -32768, 32767));
        }
    }

    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;

    const uint32_t dataBytes = static_cast<uint32_t>(pcm.size() * sizeof(int16_t));
    const uint32_t byteRate  = sampleRate * 2;
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };

    std::fwrite("RIFF", 1, 4, f);
    u32(36 + dataBytes);
    std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16);          // fmt chunk 大小
    u16(1);           // PCM
    u16(1);           // 单声道
    u32(sampleRate);
    u32(byteRate);
    u16(2);           // block align
    u16(16);          // bits
    std::fwrite("data", 1, 4, f);
    u32(dataBytes);
    std::fwrite(pcm.data(), 1, dataBytes, f);
    std::fclose(f);
    return true;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <level.adofai> [out.wav]\n", argv[0]);
        return 2;
    }
    const std::string levelPath = argv[1];
    const std::string outWav    = (argc >= 3) ? argv[2] : std::string();

#ifdef ADOFAI_HEADLESS_HAVE_ARCHIVE
    // 注册解压后端：之后 .adofai.xz / .adofai.zst 就能读了。
    // 这是**唯一**需要为压缩谱做的事（core 不认识 archive，只认后端钩子）。
    adofai::archive::install();
    std::printf("archive        : linked (xz/zstd 可用)\n");
#else
    std::printf("archive        : not linked (压缩谱会给出明确错误)\n");
#endif

    LevelData level;
    if (!level.loadFromFile(levelPath)) {
        std::fprintf(stderr, "load failed: %s\n", levelPath.c_str());
        return 1;
    }

    Timeline timeline;
    timeline.build(level, /*exportOnly=*/true);

    const auto& times = timeline.tileStartTimes();
    const double total = timeline.totalDuration();
    const double audioStart = timeline.audioStartOffset();

    std::printf("level          : %s\n", levelPath.c_str());
    std::printf("tiles          : %zu\n", level.tiles.size());
    std::printf("tileStartTimes : %zu\n", times.size());
    std::printf("totalDuration  : %.6f s\n", total);
    std::printf("audioStart     : %.6f s\n", audioStart);

    // 解算：逐层位置（这正是 PLAN.md §0 那条链里的"时间线/解算"）
    size_t solved = 0;
    double minX = 0.0, maxX = 0.0;
    for (size_t i = 0; i < level.tiles.size(); i++) {
        glm::dvec2 red(0.0), blue(0.0);
        adofai::PositionSolver::positionAt(timeline, times.empty() ? 0.0 : times[i], red, blue);
        if (i == 0) { minX = maxX = red.x; }
        minX = std::min(minX, red.x);
        maxX = std::max(maxX, red.x);
        solved++;
    }
    std::printf("solvedPositions: %zu  (x range %.6f .. %.6f)\n", solved, minX, maxX);

    const std::vector<double> hits = timeline.getHitsoundTimestamps();
    std::printf("hits           : %zu\n", hits.size());
    if (!hits.empty()) {
        std::printf("firstHit       : %.6f s\n", hits.front());
        std::printf("lastHit        : %.6f s\n", hits.back());
    }

    if (!outWav.empty()) {
        if (!writeClickTrackWav(outWav, hits, total)) {
            std::fprintf(stderr, "write failed: %s\n", outWav.c_str());
            return 1;
        }
        std::printf("wrote          : %s\n", outWav.c_str());
    }
    return 0;
}
