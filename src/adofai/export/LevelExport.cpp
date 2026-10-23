#include "export/LevelExport.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "core/level/LevelData.hpp"
#include "export/LevelEncode.hpp"
#include "export/LevelJsonWriter.hpp"
#include "export/LevelV7Json.hpp"

#ifdef ADOFAI_LIB_EXPORT_HAVE_ARCHIVE
#  include "archive/AdocaoReader.hpp"
#  include "archive/AdocaoWriter.hpp"
#  include "archive/LevelArchive.hpp"
#endif

namespace adofai {
namespace exporter {
namespace {

std::string lower(const std::string& s)
{
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return r;
}

bool endsWith(const std::string& s, const char* suffix)
{
    const size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

// 先写 `.tmp` 再改名：写一半失败（磁盘满 / 进程被杀）不会留一个"看起来是成品"的截断文件。
// 改名在 Windows 上遇到已存在的目标会失败，所以失败了就先删目标再改一次。
bool writeFileAtomic(const std::string& path, const void* data, size_t n, std::string& err)
{
    namespace fs = std::filesystem;
    const fs::path target(path);
    fs::path tmp = target;
    tmp += ".tmp";

    std::error_code ec;
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            err = "打不开临时文件：" + tmp.string();
            return false;
        }
        f.write((const char*)data, (std::streamsize)n);
        f.flush();
        if (!f) {
            err = "写入失败（磁盘满？）：" + tmp.string();
            f.close();
            fs::remove(tmp, ec);
            return false;
        }
    }
    fs::rename(tmp, target, ec);
    if (ec) {
        std::error_code ec2;
        fs::remove(target, ec2);          // Windows：rename 不覆盖已存在的目标
        ec.clear();
        fs::rename(tmp, target, ec);
    }
    if (ec) {
        err = "改名失败（" + tmp.string() + " -> " + target.string() + "）：" + ec.message();
        std::error_code ec3;
        fs::remove(tmp, ec3);
        return false;
    }
    return true;
}

#ifdef ADOFAI_LIB_EXPORT_HAVE_ARCHIVE

// 自检：xz/zst 解回来必须与本来的 JSON 逐字节相同。用 archive 的**读取方**验 —— 那是消费者
// 实际会走的那条路（而不是另写一个只给测试用的解码器）。
bool verifyCompressed(const std::vector<uint8_t>& bytes, LevelArchiveKind kind,
                      const std::string& expect, std::string& err)
{
    std::string got;
    std::string reason;
    if (!decompressLevelArchive((const char*)bytes.data(), bytes.size(), kind, got, reason)) {
        err = "自检失败：写出的压缩帧自己解不开（" + reason + "）";
        return false;
    }
    if (got != expect) {
        err = "自检失败：压缩帧解回来与源 JSON 不一致（" + std::to_string(got.size()) + " vs "
              + std::to_string(expect.size()) + " 字节）";
        return false;
    }
    return true;
}

bool verifyAdocao(const std::vector<uint8_t>& bytes, const LevelData& level, std::string& err)
{
    LevelData back;
    std::string reason;
    if (!adocao::unpackLevel(bytes.data(), bytes.size(), back, reason)) {
        err = "自检失败：写出的 `.adocao` 自己解不开（" + reason + "）";
        return false;
    }
    // 判据用**"再打一次包，字节必须完全相同"**，而不是比 modelDigest。
    // 理由（2026-10-19）：`.adocao` 现在有 13 个段（含 Camera/Repeat/Colour/Flash/MoveTrack），
    // 而 modelDigest 是**逐字段枚举**的 —— 上游每加一类事件，那份枚举就得跟一次，漏了就是
    // "自检绿灯但数据已经丢了"。pack 的产物只由建模字段决定，所以"重打包字节相同"是
    // **字段无关**的等价判据：unpack 丢了任何一项，第二次 pack 就一定不同。
    // （代价：多打一次包。`Options::verify=false` 可以关掉这一整层。）
    std::vector<uint8_t> again;
    std::string e2;
    if (!adocao::packLevel(back, again, e2)) {
        err = "自检失败：把解出来的 `.adocao` 再打一次包时失败（" + e2 + "）";
        return false;
    }
    if (again != bytes) {
        char buf[200];
        std::snprintf(buf, sizeof(buf),
                      "自检失败：`.adocao` 往返后重打包不是逐字节相同（%zu vs %zu 字节）"
                      " —— 说明有一项建模字段在 pack/unpack 之间丢了",
                      bytes.size(), again.size());
        err = buf;
        return false;
    }
    return true;
}

#endif   // ADOFAI_LIB_EXPORT_HAVE_ARCHIVE

}  // namespace

const char* formatName(Format f)
{
    switch (f) {
    case Format::Adofai:    return "adofai";
    case Format::AdofaiXz:  return "adofai.xz";
    case Format::AdofaiZst: return "adofai.zst";
    case Format::Adocao:    return "adocao";
    }
    return "?";
}

const char* formatExtension(Format f)
{
    switch (f) {
    case Format::Adofai:    return ".adofai";
    case Format::AdofaiXz:  return ".adofai.xz";
    case Format::AdofaiZst: return ".adofai.zst";
    case Format::Adocao:    return ".adocao";
    }
    return "";
}

bool formatFromPath(const std::string& path, Format& out)
{
    const std::string p = lower(path);
    // 长后缀优先：`.adofai.xz` 必须比 `.adofai` 先判。
    if (endsWith(p, ".adofai.xz"))  { out = Format::AdofaiXz;  return true; }
    if (endsWith(p, ".adofai.zst")) { out = Format::AdofaiZst; return true; }
    if (endsWith(p, ".adofai"))     { out = Format::Adofai;    return true; }
    if (endsWith(p, ".adocao"))     { out = Format::Adocao;    return true; }
    return false;
}

bool formatAvailable(Format f)
{
    if (f == Format::Adofai) return true;
#ifdef ADOFAI_LIB_EXPORT_HAVE_ARCHIVE
    return true;
#else
    (void)f;
    return false;
#endif
}

bool writeTo(Format f, const LevelData& level, std::vector<uint8_t>& out, std::string& err,
             const Options& opt, Report* report)
{
    out.clear();
    err.clear();
    Report rep;
    rep.format = f;
    rep.angleCount = (uint64_t)level.angleCount();
    rep.actionCount = level.actions.size();

    if (f == Format::Adocao) {
#ifndef ADOFAI_LIB_EXPORT_HAVE_ARCHIVE
        err = "本库配置为 ADOFAI_LIB_ARCHIVE=OFF：`.adocao` 的打包器在 archive 模块里，写不了";
        return false;
#else
        // 上游的 `packLevel` 直接读**密集**的 `angleData`（不像我们走 angleAt()），所以
        // 调过 `releaseAngleData()` 之后它只会写出一列空角度。这里提前明确拒绝，而不是
        // 等自检报一句含糊的"指纹不一致"。
        if (level.anglesCompacted()) {
            err = "`.adocao` 的打包器只读密集 angleData，而这份 LevelData 已经把角度换成紧凑"
                  "存储（releaseAngleData）—— 改用 .adofai / .adofai.xz / .adofai.zst，"
                  "或者用没压缩过角度的那份再打 .adocao";
            return false;
        }
        if (!adocao::packLevel(level, out, err)) {
            if (err.empty()) err = "packLevel 失败";
            return false;
        }
        if (opt.verify && !verifyAdocao(out, level, err)) {
            out.clear();
            return false;
        }
        rep.bytes = out.size();
        if (report) *report = rep;
        return true;
#endif
    }

    // 明文 JSON 是所有路线的中间产物（`.adocao` 除外 —— 它是列式表示，不是"压过的 JSON"）。
    // 先问一句"这份谱能不能用 JSON 表示"：五类富事件 / ScaleRadius 没有 JSON 形式，
    // 硬写就是静默丢事件（见 LevelJsonWriter.hpp 的 canExport 说明）。
    {
        std::string why;
        if (!canExport(level, &why)) {
            err = why;
            return false;
        }
    }
    std::string json;
#ifdef ADOFAI_LIB_EXPORT_HAVE_ARCHIVE
    // 有 v7 事件段时走**无损**回写：按 schema 把原始事件 JSON 原样吐回来，连 decorations、
    // 未知 eventType、以及 rawSettingsJson 里我们没建模的 settings 键都不丢。
    // 这条路失败就**直接失败**，不退回建模写方 —— 那会把"无损"悄悄降级。
    if (!level.v7SchemaBytes.empty() && !level.v7ColumnsBytes.empty()) {
        // 库自己的实现（见 LevelV7Json.hpp 里为什么不再调上游那个函数）。
        if (!writeV7Json(level, json, err)) return false;
        rep.jsonBytes = json.size();
        if (f == Format::Adofai) {
            out.assign(json.begin(), json.end());
            rep.bytes = out.size();
            if (opt.verify) {
                LevelData back;
                if (!back.loadFromBuffer(json.data(), json.size(), nullptr, /*exportOnly=*/true)) {
                    out.clear();
                    err = "自检失败：v7 无损回写的 .adofai 自己解析不了";
                    return false;
                }
            }
            if (report) *report = rep;
            return true;
        }
    } else
#endif
    if (!writeJson(level, json, err)) return false;
    rep.jsonBytes = json.size();

    if (f == Format::Adofai) {
        out.assign(json.begin(), json.end());
        rep.bytes = out.size();
        if (opt.verify) {
            // 明文这条路也自解一遍 —— 于是"写出来的一定读得回来"对**四种格式**是同一句话。
            // exportOnly=true：只要输入那些字段（跳过逐层位置），指纹口径也就与导出用的一致。
            //
            // 唯一的例外：**只有 pathData、没有 angleData** 的输入不是解析器的产物
            // （解析收尾 `finishLoad` 一定会把 pathData 推成 angleData），而写出的文件里只有
            // pathData —— 重新解析会补出那一份推导结果，于是角度那一段必然"不一致"。
            // 那种输入下仍然要求"读得回来"，但跳过指纹比对（这不是丢数据，是补数据）。
            const bool derivedAnglesPending =
                level.angleCount() == 0 && !level.pathData.empty();
            LevelData back;
            if (!back.loadFromBuffer(json.data(), json.size(), nullptr, /*exportOnly=*/true)) {
                out.clear();
                err = "自检失败：写出的 .adofai 自己解析不了";
                return false;
            }
            const uint64_t a = modelDigest(level);
            const uint64_t b = modelDigest(back);
            if (!derivedAnglesPending && a != b) {
                out.clear();
                char buf[160];
                std::snprintf(buf, sizeof(buf),
                              "自检失败：写出的 .adofai 重新解析后建模字段不一致（指纹 %016llx vs %016llx）",
                              (unsigned long long)a, (unsigned long long)b);
                err = buf;
                return false;
            }
        }
        if (report) *report = rep;
        return true;
    }

    // ARCHIVE=OFF（压缩库不在构建里）时由 encodeXz/encodeZstd 自己给出针对格式的原因，
    // 这里不抢先判 compressionAvailable() —— 那会把同一个事实写在两个地方。
    if (f == Format::AdofaiXz) {
        if (!encodeXz(json.data(), json.size(), out, err, opt.xzPreset)) return false;
    } else if (f == Format::AdofaiZst) {
        if (!encodeZstd(json.data(), json.size(), out, err, opt.zstdLevel)) return false;
    } else {
        err = "内部错误：未知格式";
        return false;
    }
    rep.compressedBytes = out.size();
    rep.bytes = out.size();

#ifdef ADOFAI_LIB_EXPORT_HAVE_ARCHIVE
    if (opt.verify) {
        const LevelArchiveKind kind = (f == Format::AdofaiXz) ? LevelArchiveKind::Xz
                                                              : LevelArchiveKind::Zstd;
        if (!verifyCompressed(out, kind, json, err)) {
            out.clear();
            return false;
        }
    }
#endif
    if (report) *report = rep;
    return true;
}

bool exportLevel(const LevelData& level, Format f, const std::string& path, std::string& err,
                 const Options& opt, Report* report)
{
    std::vector<uint8_t> bytes;
    if (!writeTo(f, level, bytes, err, opt, report)) return false;
    if (bytes.empty()) {   // 不该发生；真要发生了也别把 data()==nullptr 递给 ofstream
        err = "内部错误：写出的字节是空的";
        return false;
    }
    if (!writeFileAtomic(path, bytes.data(), bytes.size(), err)) return false;
    return true;
}

bool exportLevel(const LevelData& level, const std::string& path, std::string& err,
                 const Options& opt, Report* report)
{
    Format f = Format::Adofai;
    if (!formatFromPath(path, f)) {
        err = "认不出导出格式（按后缀判）：" + path
              + " —— 支持 " + formatExtension(Format::Adofai) + " / "
              + formatExtension(Format::AdofaiXz) + " / "
              + formatExtension(Format::AdofaiZst) + " / "
              + formatExtension(Format::Adocao);
        return false;
    }
    return exportLevel(level, f, path, err, opt, report);
}

}  // namespace exporter
}  // namespace adofai
