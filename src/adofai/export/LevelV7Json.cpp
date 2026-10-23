#include "export/LevelV7Json.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

#include "core/level/LevelData.hpp"
#include "export/LevelJsonWriter.hpp"

#ifdef ADOFAI_LIB_EXPORT_HAVE_ARCHIVE
#  include <rapidjson/stringbuffer.h>
#  include <rapidjson/writer.h>
#
#  include "archive/AdocaoEventColumns.hpp"
#  include "archive/AdocaoSchema.hpp"
#endif

namespace adofai {
namespace exporter {

#ifdef ADOFAI_LIB_EXPORT_HAVE_ARCHIVE

bool writeV7Json(const LevelData& level, std::string& out, std::string& err)
{
    using namespace adocao;   // Schema / SchemaBuilder / EventColumns / EventSource 都在这里

    out.clear();
    err.clear();

    // 判据只是"段在不在"。**不猜**：缺了就说清怎么才有。
    if (level.v7SchemaBytes.empty() || level.v7ColumnsBytes.empty()) {
        err = "这份 LevelData 没有 v7 事件段：要先 preserveRawJson=true 读谱、再用 "
              "PackOptions{withV7Events=true, v7IncludeLegacy=true} 打包成 .adocao，读回来才有";
        return false;
    }

    Schema schema;
    if (!SchemaBuilder::deserialize(level.v7SchemaBytes.data(), level.v7SchemaBytes.size(), schema,
                                    err))
        return false;

    std::vector<std::string> events;   // 每条的**原始 JSON 对象文本**（按原顺序）
    std::vector<uint16_t> typeIds;
    if (level.v7ColumnsBlocked) {
        if (!decodeEventColumnsBlockedAll(schema, level.v7ColumnsBytes.data(),
                                          level.v7ColumnsBytes.size(), events, err, &typeIds))
            return false;
    } else {
        EventColumns cols;
        if (!deserializeEventColumns(schema, level.v7ColumnsBytes.data(), level.v7ColumnsBytes.size(),
                                     cols, err))
            return false;
        if (!decodeEventColumns(schema, cols, cols.typeIds.size(), events, err)) return false;
        typeIds = cols.typeIds;
    }

    // 解码结果是**原顺序**，但 actions 与 decorations 是混排的 —— 按 schema 的 source 分回去。
    std::vector<size_t> actionIdx, decoIdx;
    actionIdx.reserve(events.size());
    for (size_t i = 0; i < typeIds.size(); ++i) {
        if (typeIds[i] >= schema.types.size()) {
            err = "v7 schema 的类型下标越界（文件损坏？）";
            return false;
        }
        const auto& td = schema.types[typeIds[i]];
        (td.source == (uint8_t)EventSource::Decorations ? decoIdx : actionIdx).push_back(i);
    }

    rapidjson::StringBuffer buf;
    rapidjson::Writer<rapidjson::StringBuffer> w(buf);
    w.StartObject();

    // ---- settings ----
    // 有原始切片就**原样**输出：ADOFAI 的 settings 键远多于我们建模的那 29 个，逐键建模必然丢
    // 字段（`captureRawMembers` 抓的就是这段）。没有切片时退回建模写方。
    w.Key("settings");
    if (!level.rawSettingsJson.empty()) {
        w.RawValue(level.rawSettingsJson.c_str(), (rapidjson::SizeType)level.rawSettingsJson.size(),
                   rapidjson::kObjectType);
    } else {
        std::string settingsJson;
        if (!writeSettingsObject(level, settingsJson, err)) return false;
        w.RawValue(settingsJson.c_str(), (rapidjson::SizeType)settingsJson.size(),
                   rapidjson::kObjectType);
    }

    // ---- angleData / pathData ----
    // 角度走 `angleCount()/angleAt()`（与建模写方同一个口径，压缩过角度也写得出来）。
    if (!level.pathData.empty()) {
        w.Key("pathData");
        w.String(level.pathData.c_str(), (rapidjson::SizeType)level.pathData.size());
    } else {
        const int64_t n = level.angleCount();
        w.Key("angleData");
        w.StartArray();
        for (int64_t i = 0; i < n; ++i) w.Double(level.angleAt(i));
        w.EndArray();
    }

    // ---- actions / decorations：v7 解出来的**原始 JSON 对象**，原样吐回 ----
    auto emitList = [&](const char* key, const std::vector<size_t>& idx) {
        w.Key(key);
        w.StartArray();
        for (size_t i : idx)
            w.RawValue(events[i].c_str(), (rapidjson::SizeType)events[i].size(),
                       rapidjson::kObjectType);
        w.EndArray();
    };
    emitList("actions", actionIdx);
    if (!decoIdx.empty()) emitList("decorations", decoIdx);

    w.EndObject();
    if (!w.IsComplete()) {
        if (err.empty()) err = "v7 回写未完成（Writer::IsComplete() 为假）";
        return false;
    }
    out.assign(buf.GetString(), buf.GetSize());
    return true;
}

#else   // !ADOFAI_LIB_EXPORT_HAVE_ARCHIVE

bool writeV7Json(const LevelData&, std::string& out, std::string& err)
{
    out.clear();
    err = "本库配置为 ADOFAI_LIB_ARCHIVE=OFF：v7 事件段在 archive 模块里，回写不了";
    return false;
}

#endif

}  // namespace exporter
}  // namespace adofai
