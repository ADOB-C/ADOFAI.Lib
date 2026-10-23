// 导出模块的往返验收（**库自有**，不在镜像白名单里；它就是这个模块的漂移护栏）。
//
// 为什么这条测试能挡漂移：它比的是 `parse(原文件)` 与 `parse(导出物)` 的**建模字段指纹**。
// 上游哪天给 LevelData 加一个字段（或在解析里改语义），导出这边没跟上 → 导出物缺那一项 →
// 重新解析出来的值与原来不同 → 指纹不等 → 这条测试立刻红。也就是说，护栏判据来自解析器本身，
// 而不是我在测试里再抄一份字段表（抄一份才是真正会静默漂移的那种做法）。
//
// 每个输入 × 每种格式都做四件事：
//   ① 导出 → 重新解析 → 指纹必须与原来相同（保真）
//   ② 同一输入再导出一次 → 与文件里的字节逐字节相同（确定性；`.adocao` 头里带 writerCommit，
//      那个是**编译期常量**，所以同一次构建内确定性成立）
//   ③ `.adofai` 额外：强制老路径（ADOCAO_FORCE_DOM_PARSE=1）也要读得动、且指纹相同
//      —— 我们写出来的 JSON 必须落在**两条**解析路径的交集里，而不是只有快路径认
//   ④ `.adocao` 额外：`pack(原)` 与 `pack(重解析)` 逐字节相同（说明导出没丢掉打包器要用的任何一列）
// 末尾还有一组负向对照：认不出的后缀、被截断/改坏的容器，都必须**明确失败**。
//
// 用法：adofai_export_roundtrip_test <out-dir> <level> [<level>...]

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "core/level/LevelData.hpp"
#include "export/LevelExport.hpp"
#include "export/LevelJsonWriter.hpp"

#ifdef ADOFAI_EXPORT_TEST_HAVE_ARCHIVE
#  include "archive/AdocaoWriter.hpp"
#  include "archive/Install.hpp"
#endif

using adofai::LevelData;
namespace exporter = adofai::exporter;
namespace fs = std::filesystem;

namespace {

int g_fail = 0;
int g_checked = 0;
int g_refused = 0;   // 含"没有 JSON 表示"的事件、于是明确拒绝写明文的输入数

void fail(const std::string& what, const std::string& detail)
{
    std::printf("FAIL: %s —— %s\n", what.c_str(), detail.c_str());
    g_fail++;
}

// setenv/unsetenv 是 POSIX 的，Windows（MinGW/MSVC）只有 _putenv_s（它才会更新 CRT 自己的
// environ）。与 tests/level_parse_test.cpp 的写法一致。
void setEnv(const char* name, const char* value)
{
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

void unsetEnv(const char* name)
{
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

std::vector<char> readRawFile(const std::string& path)
{
    std::vector<char> out;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return out;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n > 0) {
        out.resize((size_t)n);
        const size_t got = std::fread(out.data(), 1, out.size(), f);
        out.resize(got);
    }
    std::fclose(f);
    return out;
}

bool writeRawFile(const std::string& path, const void* data, size_t n)
{
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = (n == 0) || (std::fwrite(data, 1, n, f) == n);
    std::fclose(f);
    return ok;
}

std::string stemOf(const std::string& path)
{
    return fs::path(path).stem().string();
}

const char* kAll[] = {"angleData", "settings", "actions"};

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <out-dir> <level> [<level>...]\n", argv[0]);
        return 2;
    }
    const std::string outDir = argv[1];
    std::error_code ec;
    fs::create_directories(outDir, ec);

#ifdef ADOFAI_EXPORT_TEST_HAVE_ARCHIVE
    // 读回 `.adocao` / `.xz` / `.zst` 需要注册 archive 后端（core 只认接口）。
    adofai::archive::install();
    std::printf("archive        : linked（四种格式全部参与）\n");
#else
    std::printf("archive        : not linked（只应当出得了 .adofai）\n");
#endif

    const exporter::Format formats[] = {exporter::Format::Adofai, exporter::Format::AdofaiXz,
                                        exporter::Format::AdofaiZst, exporter::Format::Adocao};

    for (int ai = 2; ai < argc; ++ai) {
        const std::string input = argv[ai];
        const int failsBefore = g_fail;
        LevelData a;
        if (!a.loadFromFile(input)) {
            fail(input, "加载失败");
            continue;
        }
        const uint64_t da = exporter::modelDigest(a);
        // 2026-10-19：上游把相机/砖色/闪屏/轨道动画/重复事件搬进了富结构，它们**没有**
        // `.adofai` 的 JSON 表示（ScaleRadius 连原始事件都不留）。这类谱必须**明确拒绝**
        // 写明文 —— 静默丢掉五类事件是比"导不出来"严重得多的失败。下面按两种情况分别断言。
        std::string unsupported;
        const bool representable = exporter::canExport(a, &unsupported);
        if (!representable) g_refused++;

        // `canExport` 的判据是**逐类枚举**的（C++ 没有反射），所以它自己也会漂：上游再加一类
        // 富事件、或者有人删掉其中一条检查，它就会"说能写"。这条断言把 canExport 与模型现状
        // 钉在一起 —— 至少已建模的这几类不能漏。
        // 2026-10-22 起五类富事件都能写了；**只有 ScaleRadius 仍然拒**（上游解析时就把
        // 原始事件丢了，只把结果烘进砖位）。所以这条对照的判据就是它。
        if (a.hasScaleRadiusEvents() && representable)
            fail(input, "canExport 说能写明文，但这份谱含 ScaleRadius —— 写出来会丢事件");
        if (!a.hasScaleRadiusEvents() && !representable)
            fail(input, "canExport 拒绝了，但这份谱没有 ScaleRadius：" + unsupported);

        for (exporter::Format f : formats) {
            const std::string label = input + " -> " + exporter::formatExtension(f);
            std::vector<uint8_t> bytes;
            std::string err;

            if (!exporter::formatAvailable(f)) {
                // 不可用必须是**明确失败 + 有原因**，不能静默换格式。
                if (exporter::writeTo(f, a, bytes, err))
                    fail(label, "formatAvailable() 说不可用，writeTo 却成功了");
                else if (err.empty())
                    fail(label, "不可用但 err 是空的（调用方无从知道为什么）");
                else
                    std::printf("SKIP  %s（%s）\n", label.c_str(), err.c_str());
                continue;
            }

            const std::string path = outDir + "/" + stemOf(input) + exporter::formatExtension(f);
            exporter::Report rep;
            const bool wrote = exporter::exportLevel(a, f, path, err, exporter::Options(), &rep);

            if (!representable && f != exporter::Format::Adocao) {
                // 明文三条路：必须失败，而且原因里要指名是哪几类事件。
                if (wrote) fail(label, "含无 JSON 表示的事件，却**静默写成功了**（数据会丢）");
                else if (err.empty()) fail(label, "拒绝了却没给原因");
                else if (err.find("JSON 表示") == std::string::npos)
                    fail(label, "拒绝原因没解释清楚：" + err);
                else std::printf("SKIP  %s（%s）\n", label.c_str(), unsupported.c_str());
                continue;
            }
            if (!wrote) {
                fail(label, "导出失败：" + err);
                continue;
            }
            if (rep.bytes == 0) fail(label, "Report.bytes 是 0");
            g_checked++;

            // ① 导出物自己读得回来，而且建模字段逐位相同
            LevelData b;
            if (!b.loadFromFile(path)) {
                fail(label, "导出物自己读不回来");
                continue;
            }
            const uint64_t db = exporter::modelDigest(b);
            if (db != da) {
                char buf[128];
                std::snprintf(buf, sizeof(buf), "往返后建模字段不一致（指纹 %016llx vs %016llx）",
                              (unsigned long long)da, (unsigned long long)db);
                fail(label, buf);
            }

            // ② 确定性：再导出一次，与刚写下去的文件逐字节相同
            std::vector<uint8_t> again;
            if (!exporter::writeTo(f, a, again, err)) {
                fail(label, "第二次 writeTo 失败：" + err);
            } else {
                const std::vector<char> onDisk = readRawFile(path);
                if (onDisk.size() != again.size() ||
                    (again.size() && std::memcmp(onDisk.data(), again.data(), again.size()) != 0))
                    fail(label, "同一输入两次导出不是逐字节相同");
            }

            // ③ `.adofai`：老路径（cleanJson + RapidJSON DOM）也要读得动，且指纹相同
            if (f == exporter::Format::Adofai) {
                setEnv("ADOCAO_FORCE_DOM_PARSE", "1");
                LevelData c;
                const bool ok = c.loadFromFile(path);
                unsetEnv("ADOCAO_FORCE_DOM_PARSE");
                if (!ok) fail(label, "老路径读不了导出物");
                else if (exporter::modelDigest(c) != da) fail(label, "老路径读出来的建模字段不一致");
            }

#ifdef ADOFAI_EXPORT_TEST_HAVE_ARCHIVE
            // ④ `.adocao`：导出物与原件的 pack 必须逐字节相同 —— 说明导出一列都没丢
            if (f == exporter::Format::Adocao) {
                std::vector<uint8_t> pa, pb;
                std::string e1, e2;
                if (!adofai::adocao::packLevel(a, pa, e1)) fail(label, "pack(原件) 失败：" + e1);
                else if (!adofai::adocao::packLevel(b, pb, e2)) fail(label, "pack(导出物) 失败：" + e2);
                else if (pa != pb) fail(label, "pack(原件) 与 pack(导出物) 不是逐字节相同");
            }
#endif
        }
        if (g_fail == failsBefore)
            std::printf("OK    %s（%zu 层 / %zu 事件）\n", input.c_str(), a.angleData.size(),
                        a.actions.size());
    }

    // ── 负向对照 ────────────────────────────────────────────────────────────
    {
        exporter::Format f = exporter::Format::Adofai;
        if (exporter::formatFromPath("level.txt", f))
            fail("formatFromPath", "把 .txt 认成了谱面格式（不该猜）");
        if (!exporter::formatFromPath("level.ADOFAI.XZ", f) || f != exporter::Format::AdofaiXz)
            fail("formatFromPath", "大小写不敏感的 .adofai.xz 没认出来（或长后缀优先级错了）");
        if (!exporter::formatFromPath("level.adocao", f) || f != exporter::Format::Adocao)
            fail("formatFromPath", ".adocao 没认出来");

        LevelData tiny;
        tiny.loadFromBuffer("{\"angleData\":[0,90],\"actions\":[]}", 32);
        std::string err;
        exporter::Report rep;
        if (exporter::exportLevel(tiny, "no-extension", err))
            fail("exportLevel", "没有后缀却导出成功了（应当报错而不是猜格式）");
        else if (err.empty())
            fail("exportLevel", "认不出格式却没给原因");
    }

    // 手搓的 LevelData：**只有 pathData、没有 angleData**（不是解析器的产物）。
    // 导出必须成功（文件里只有 pathData）、而且必须读得回来；自检会**跳过角度那一段的指纹比对**
    // —— 重新解析会补出推导出来的 angleData，那不是丢数据、是补数据（见 LevelExport.cpp）。
    {
        LevelData pd;
        pd.pathData = "RRLL";
        const std::string p = outDir + "/pathdata_only.adofai";
        std::string err;
        if (!exporter::exportLevel(pd, exporter::Format::Adofai, p, err)) {
            fail("pathData-only 导出", err);
        } else {
            LevelData back;
            if (!back.loadFromFile(p)) fail("pathData-only 导出", "读不回来");
            else if (back.pathData != "RRLL") fail("pathData-only 导出", "pathData 没保住");
            else if (back.angleData.size() != 4)
                fail("pathData-only 导出", "重新解析应当补出 4 个角度");
            else std::printf("OK    只有 pathData 的手搓 LevelData（导出成功且读得回来）\n");
        }
    }

#ifdef ADOFAI_EXPORT_TEST_HAVE_ARCHIVE
    {
        // 截断的容器、改坏一个字节的容器必须明确失败：`.xz` 有 CRC64、`.adocao` 有段级 crc32c、
        // `.zst` 靠我们显式开的内容校验和（默认的一次性 ZSTD_compress 不带校验和，
        // 那种帧翻一个字节有可能被静默解成错数据 —— 这正是开它的理由）。
        LevelData lv;
        const char* json = "{\"angleData\":[0,90,180],\"actions\":[]}";
        if (!lv.loadFromBuffer(json, std::strlen(json))) {
            fail("负向对照", "最小谱面都加载不了");
        } else {
            for (exporter::Format f : {exporter::Format::AdofaiXz, exporter::Format::AdofaiZst,
                                       exporter::Format::Adocao}) {
                std::vector<uint8_t> bytes;
                std::string err;
                if (!exporter::writeTo(f, lv, bytes, err)) {
                    fail("负向对照", std::string("造 ") + exporter::formatExtension(f) + " 失败：" + err);
                    continue;
                }
                const std::string p = outDir + "/broken" + exporter::formatExtension(f);
                // 砍掉尾巴
                if (bytes.size() > 8 && writeRawFile(p, bytes.data(), bytes.size() - 8)) {
                    LevelData bad;
                    if (bad.loadFromFile(p)) fail("负向对照", std::string("截断的 ") + p + " 竟然读成功了");
                }
                // 改坏中间一个字节
                if (bytes.size() > 40) {
                    bytes[bytes.size() / 2] ^= 0xFF;
                    if (writeRawFile(p, bytes.data(), bytes.size())) {
                        LevelData bad;
                        if (bad.loadFromFile(p)) fail("负向对照", std::string("改坏的 ") + p + " 竟然读成功了");
                    }
                }
            }
        }
    }
#endif

#ifdef ADOFAI_EXPORT_TEST_HAVE_ARCHIVE
    // ── v7 事件段：**无损**回写（2026-10-22 上游给 `.adocao` 加了自描述事件库）───────
    // 有 v7 段时导出改走 `adocao::writeLevelJson`：它按 schema 把原始事件 JSON 原样吐回来，
    // 连**未知 eventType** 和 decorations 都不丢（建模写方会把未知类型丢掉 —— 那是解析期的行为）。
    {
        std::string pick;
        for (int ai = 2; ai < argc && pick.empty(); ++ai) {
            const std::vector<char> raw = readRawFile(argv[ai]);
            if (std::string(raw.begin(), raw.end()).find("UnknownType") != std::string::npos)
                pick = argv[ai];
        }
        if (pick.empty()) {
            std::printf("SKIP  v7 无损回写（这批输入里没有带未知 eventType 的谱）\n");
        } else {
            LevelData a;
            // 上游默认 **不**捕获原始成员（`preserveRawJson=false`，那是内存/时间成本）——
            // 想打带 v7 段的容器必须先打开它。
            a.preserveRawJson = true;
            if (!a.loadFromFile(pick)) {
                fail("v7 无损回写", "加载失败：" + pick);
            } else {
                adofai::adocao::PackOptions opts;
                opts.withV7Events = true;        // 写 EventSchema + EventColumns
                opts.v7IncludeLegacy = true;     // 不去重：v6 段有的类型也进 v7（回写需要）
                std::vector<uint8_t> bytes;
                std::string err;
                const std::string cont = outDir + "/v7_lossless.adocao";
                if (!adofai::adocao::packLevel(a, bytes, err, nullptr, opts)) {
                    fail("v7 无损回写", "pack(withV7Events) 失败：" + err);
                } else if (!writeRawFile(cont, bytes.data(), bytes.size())) {
                    fail("v7 无损回写", "写不出 " + cont);
                } else {
                    LevelData b;
                    if (!b.loadFromFile(cont)) {
                        fail("v7 无损回写", "自己打的 v7 容器读不回来");
                    } else if (b.v7SchemaBytes.empty() || b.v7ColumnsBytes.empty()) {
                        fail("v7 无损回写", "读回来的 LevelData 没有 v7 段（写方没写？）");
                    } else {
                        const std::string out = outDir + "/v7_lossless.adofai";
                        if (!exporter::exportLevel(b, exporter::Format::Adofai, out, err)) {
                            fail("v7 无损回写", "导出失败：" + err);
                        } else {
                            const std::vector<char> got = readRawFile(out);
                            const std::string txt(got.begin(), got.end());
                            if (txt.find("UnknownType") == std::string::npos)
                                fail("v7 无损回写", "导出的 .adofai 里没有 UnknownType —— 无损路径没生效");
                            else
                                std::printf("OK    v7 段无损回写（未知 eventType 被保留）\n");
                        }
                    }
                }
            }
        }
    }
#endif

    // ── 角度紧凑存储（上游 2026-10 加的 `releaseAngleData`）──────────────────
    // 压缩成功后 `angleData` 会被 swap 掉、值只留在 AngleStore 里。导出必须照样写得出同一批
    // 角度 —— 所以 LevelJsonWriter 走的是 `angleCount()/angleAt()`，不是直读 `angleData`
    // （上游 LevelData.hpp 的口径：「运行期读角度一律走 angleAt()」）。
    // 顺带钉住另一件事：`.adocao` 在上游的 `packLevel` 里仍直读密集数组，所以这种状态下
    // 必须先**明确拒绝**，而不是悄悄写出一列空角度。
    {
        int compacted = 0, refused = 0, refusedThenCompacted = 0;
        for (int ai = 2; ai < argc; ++ai) {
            const std::string input = argv[ai];
            LevelData a;
            if (!a.loadFromFile(input)) continue;        // 上面已经报过了
            const uint64_t before = exporter::modelDigest(a);
            std::string cerr;
            if (!a.releaseAngleData(&cerr)) { refused++; continue; }   // 字典主导 / 不划算
            ++compacted;
            if (exporter::modelDigest(a) != before)
                fail(input + "（角度已压缩）", "releaseAngleData 之后指纹变了（不该）");

            const std::string p = outDir + "/" + stemOf(input) + ".compacted.adofai";
            std::string err;
            if (!exporter::canExport(a, nullptr)) {
                // 这段只验"角度压缩之后照样写得出来"，含富事件的谱本来就写不了明文。
                ++refusedThenCompacted;
                continue;
            }
            if (!exporter::exportLevel(a, exporter::Format::Adofai, p, err)) {
                fail(input + "（角度已压缩）", "导出 .adofai 失败：" + err);
                continue;
            }
            LevelData back;
            if (!back.loadFromFile(p)) {
                fail(input + "（角度已压缩）", "导出物读不回来");
            } else if (exporter::modelDigest(back) != before) {
                fail(input + "（角度已压缩）", "导出物重新解析后指纹不一致（角度没写全？）");
            }

            std::vector<uint8_t> bytes;
            std::string e2;
            if (exporter::writeTo(exporter::Format::Adocao, a, bytes, e2))
                fail(input + "（角度已压缩）", "还写得出 .adocao（上游打包器只读密集数组）");
            else if (e2.empty())
                fail(input + "（角度已压缩）", "拒绝 .adocao 却没给原因");
        }
        std::printf("角度压缩：%d 个输入压缩后验过 .adofai；%d 个不划算被跳过；"
                    "%d 个因含富事件不适用\n", compacted, refused, refusedThenCompacted);
    }

    std::printf("\n%d 次导出·往返·确定性检查（另有 %d 个输入因含无 JSON 表示的事件被明确拒绝），"
                "%d 处失败\n", g_checked, g_refused, g_fail);
    return g_fail == 0 ? 0 : 1;
}
