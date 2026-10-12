#include "export/LevelEncode.hpp"

#include <limits>

#ifdef ADOFAI_LIB_EXPORT_HAVE_ARCHIVE
#  include <lzma.h>    // archive 的 PRIVATE 依赖；export 自己也要链（静态库不传递依赖）
#  include <zstd.h>
#endif

namespace adofai {
namespace exporter {

#ifdef ADOFAI_LIB_EXPORT_HAVE_ARCHIVE

bool compressionAvailable() { return true; }

bool encodeXz(const char* data, size_t length, std::vector<uint8_t>& out, std::string& err,
              uint32_t preset)
{
    out.clear();
    err.clear();
    if (preset > 9) {
        err = "xz preset 必须在 0..9";
        return false;
    }
    if (length == 0) {
        err = "拒绝压空输入（xz 空帧没有意义，调用方应该先判断有没有内容）";
        return false;
    }

    // 上界：lzma_stream_buffer_bound 对极大输入会溢出，先挡一下（真实谱面远不会到这儿，
    // 但"静默算出一个太小的上界"会变成缓冲区溢出，不是能靠运气的东西）。
    const size_t bound = lzma_stream_buffer_bound(length);
    if (bound < length || bound == std::numeric_limits<size_t>::max()) {
        err = "输入太大，xz 的输出上界溢出";
        return false;
    }
    out.resize(bound);
    size_t outPos = 0;
    // CHECK_CRC64 与 `xz -6` 的默认校验一致（读方 archive/LevelArchive 会验它）。
    const lzma_ret rc = lzma_easy_buffer_encode(preset, LZMA_CHECK_CRC64, nullptr,
                                                (const uint8_t*)data, length,
                                                out.data(), &outPos, out.size());
    if (rc != LZMA_OK) {
        out.clear();
        err = std::string("lzma_easy_buffer_encode 失败（lzma_ret=") + std::to_string((int)rc) + "）";
        return false;
    }
    out.resize(outPos);
    return true;
}

bool encodeZstd(const char* data, size_t length, std::vector<uint8_t>& out, std::string& err,
                int level)
{
    out.clear();
    err.clear();
    if (level < 1 || level > ZSTD_maxCLevel()) {
        err = std::string("zstd level 必须在 1..") + std::to_string(ZSTD_maxCLevel());
        return false;
    }
    if (length == 0) {
        err = "拒绝压空输入";
        return false;
    }
    const size_t bound = ZSTD_compressBound(length);
    if (ZSTD_isError(bound)) {
        err = std::string("ZSTD_compressBound 失败：") + ZSTD_getErrorName(bound);
        return false;
    }
    out.resize(bound);

    // 不用一次性的 `ZSTD_compress`：它没法开**内容校验和**。zstd 帧默认不带校验和
    // （`ZSTD_c_checksumFlag` 默认 0），于是"压缩数据里翻了一个字节、解码器还把它当合法的"
    // 就是可能的 —— 对一个**容器格式**来说静默给错数据是最坏的失败方式，而这个工程的口径是
    // "坏数据必须明确失败"（EXPORT.md §5）。`zstd` CLI 压文件时也是默认开校验和的。
    //   代价：4 字节；读方那边 ZSTD_decompressStream 会自己验（archive/LevelArchive.cpp）。
    // 同时显式把 worker 数钉成 0：确定性（同一输入两次编码逐字节相同）是写入方的硬要求。
    ZSTD_CCtx* cctx = ZSTD_createCCtx();
    if (!cctx) {
        out.clear();
        err = "ZSTD_createCCtx 失败";
        return false;
    }
    auto setParam = [&](ZSTD_cParameter p, int v) {
        return !ZSTD_isError(ZSTD_CCtx_setParameter(cctx, p, v));
    };
    if (!setParam(ZSTD_c_compressionLevel, level) || !setParam(ZSTD_c_checksumFlag, 1)) {
        ZSTD_freeCCtx(cctx);
        out.clear();
        err = "ZSTD_CCtx_setParameter 失败（level / checksumFlag）";
        return false;
    }
    // nbWorkers 只是"确定性"的显式声明；设不上不算致命（老 zstd 或未编 MT 支持的构建）。
    (void)ZSTD_CCtx_setParameter(cctx, ZSTD_c_nbWorkers, 0);

    const size_t got = ZSTD_compress2(cctx, out.data(), out.size(), data, length);
    ZSTD_freeCCtx(cctx);
    if (ZSTD_isError(got)) {
        out.clear();
        err = std::string("ZSTD_compress2 失败：") + ZSTD_getErrorName(got);
        return false;
    }
    out.resize(got);
    return true;
}

#else   // !ADOFAI_LIB_EXPORT_HAVE_ARCHIVE

// ARCHIVE=OFF 时压缩库不在构建里。三个函数都留着（同样的签名），失败原因写明 ——
// 枚举是运行期选的，编译期消失会让调用方只能靠宏来写 if，那更容易漏。

bool compressionAvailable() { return false; }

bool encodeXz(const char*, size_t, std::vector<uint8_t>& out, std::string& err, uint32_t)
{
    out.clear();
    err = "本库配置为 ADOFAI_LIB_ARCHIVE=OFF：压缩库（lzma/zstd）不在构建里，写不了 .xz";
    return false;
}

bool encodeZstd(const char*, size_t, std::vector<uint8_t>& out, std::string& err, int)
{
    out.clear();
    err = "本库配置为 ADOFAI_LIB_ARCHIVE=OFF：压缩库（lzma/zstd）不在构建里，写不了 .zst";
    return false;
}

#endif

}  // namespace exporter
}  // namespace adofai
