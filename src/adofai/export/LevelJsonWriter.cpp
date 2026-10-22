#include "export/LevelJsonWriter.hpp"

#include <cmath>
#include <cstring>

#include "core/level/LevelData.hpp"

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace adofai {
namespace exporter {
namespace {

using Writer = rapidjson::Writer<rapidjson::StringBuffer>;
using T = LevelData::FastAction;

// 非有限值：JSON 里没有 nan/inf 字面量，但**解析侧是收的** —— `parseNumber` 走 strtod，
// `"bpmMultiplier": 1e400` 会解成 +inf。所以写方不能假装它不存在（静默写成 0 就是"给了个
// 假值"），而是写成能**逐位解回 inf** 的形式（1e999 溢出到 inf）。nan 没有任何 JSON 表示，
// 只能明确失败 —— 这正是 EXPORT.md §5 那条"坏数据必须明确失败"。
bool putNumber(Writer& w, double v, std::string& err, const char* what)
{
    if (std::isnan(v)) {
        err = std::string("无法把 nan 写成 JSON（字段：") + what + "）";
        return false;
    }
    if (std::isinf(v)) {
        const char* lit = v > 0 ? "1e999" : "-1e999";
        w.RawValue(lit, std::strlen(lit), rapidjson::kNumberType);
        return true;
    }
    if (!w.Double(v)) {
        err = std::string("写数字失败（字段：") + what + "）";
        return false;
    }
    return true;
}

bool putF(Writer& w, float v, std::string& err, const char* what)
{
    // float 提升成 double 后再取最短往返表示：解回来仍是同一个 float（见头里的一致性说明）。
    return putNumber(w, (double)v, err, what);
}

bool putSettings(Writer& w, const LevelData::Settings& s, std::string& err)
{
    w.StartObject();
    w.Key("version");      w.Int(s.version);
    // 模型字段叫 `songFilename`，而 `.adofai` 里的键是 `song`（`readSettings` 里 `song` 优先，
    // 还接受 `songFilename` 当回退）。2026-10-17 上游新加的字段：漏写它就会把"这首歌是哪个
    // 音频"整条丢掉 —— 这正是 modelDigest 必须把它一起算进去的原因。
    w.Key("song");         w.String(s.songFilename.c_str(), (rapidjson::SizeType)s.songFilename.size());
    w.Key("bpm");          if (!putF(w, s.bpm, err, "settings.bpm")) return false;
    w.Key("offset");       if (!putF(w, s.offset, err, "settings.offset")) return false;
    w.Key("countdownTicks"); w.Int(s.countdownTicks);
    w.Key("zoom");         if (!putF(w, s.zoom, err, "settings.zoom")) return false;
    w.Key("rotation");     if (!putF(w, s.rotation, err, "settings.rotation")) return false;
    w.Key("relativeTo");   w.String(s.relativeTo.c_str(), (rapidjson::SizeType)s.relativeTo.size());
    w.Key("position");
    w.StartArray();
    if (!putF(w, s.position[0], err, "settings.position[0]")) return false;
    if (!putF(w, s.position[1], err, "settings.position[1]")) return false;
    w.EndArray();
    w.Key("hitsound");     w.String(s.hitsound.c_str(), (rapidjson::SizeType)s.hitsound.size());
    w.Key("hitsoundVolume"); if (!putF(w, s.hitsoundVolume, err, "settings.hitsoundVolume")) return false;
    w.Key("trackColor");   w.String(s.trackColor.c_str(), (rapidjson::SizeType)s.trackColor.size());
    w.Key("secondaryTrackColor");
    w.String(s.secondaryTrackColor.c_str(), (rapidjson::SizeType)s.secondaryTrackColor.size());
    w.Key("backgroundColor");
    w.String(s.backgroundColor.c_str(), (rapidjson::SizeType)s.backgroundColor.size());
    w.Key("stickToFloors"); w.Bool(s.stickToFloors);
    w.Key("planetEase");   w.String(s.planetEase.c_str(), (rapidjson::SizeType)s.planetEase.size());
    w.Key("trackDisappearAnimation");
    w.String(s.trackDisappearAnimation.c_str(), (rapidjson::SizeType)s.trackDisappearAnimation.size());
    w.Key("trackAnimation");
    w.String(s.trackAnimation.c_str(), (rapidjson::SizeType)s.trackAnimation.size());
    w.Key("beatsBehind");  if (!putF(w, s.beatsBehind, err, "settings.beatsBehind")) return false;
    w.Key("beatsAhead");   if (!putF(w, s.beatsAhead, err, "settings.beatsAhead")) return false;
    // 2026-10-18 上游加的 8 个字段（砖色模型 + 音高 + 相机的老语义开关）。与 songFilename 同一个
    // 教训：漏写任何一个，导出就是"看起来正常"的静默降级，所以 modelDigest 也必须逐个覆盖。
    auto putS = [&](const char* key, const std::string& v) {
        w.Key(key); w.String(v.c_str(), (rapidjson::SizeType)v.size());
    };
    putS("trackColorType", s.trackColorType);
    putS("trackColorPulse", s.trackColorPulse);
    w.Key("trackColorAnimDuration");
    if (!putF(w, s.trackColorAnimDuration, err, "settings.trackColorAnimDuration")) return false;
    w.Key("trackPulseLength");
    if (!putF(w, s.trackPulseLength, err, "settings.trackPulseLength")) return false;
    putS("trackStyle", s.trackStyle);
    w.Key("trackGlowIntensity");
    if (!putF(w, s.trackGlowIntensity, err, "settings.trackGlowIntensity")) return false;
    w.Key("pitch");        if (!putF(w, s.pitch, err, "settings.pitch")) return false;
    w.Key("legacyCamRelativeTo"); w.Bool(s.legacyCamRelativeTo);
    // 2026-10-21：它决定时间基准（Re_ADOJAS getTimeOrigin）—— 写丢了整条时间线会整体平移。
    w.Key("separateCountdownTime"); w.Bool(s.separateCountdownTime);
    w.EndObject();
    return true;
}

// 每个 eventType 只写**解析方能读回来的那几列**，一个字都不多 —— 这既是"只写输入"，也是
// 往返逐位相同的前提。逐类型的对应关系（写成 JSON 后解析方重新得到同一个 FastAction）：
//
//   Twirl         floor
//   SetSpeed      flag ⇒ speedType="Multiplier" + bpmMultiplier=val1；否则 speedType="BPM" + beatsPerMinute=val1
//   Pause         duration = val1（恒写：val1 缺席时解析方也取 0）
//   PositionTrack positionOffset=[val1,val2]（恒写）+ justThisTile=flag（恒写）
//   SetHitsound   hitsound=actionStr(a)（恒写）+ hitsoundVolume=val1 仅当 flag（**flag 单独记"写了音量"**，
//                 因为 0 与负值都是合法音量，不能拿 val1>0 反推）
//   Bookmark      floor
//   AnimateTrack  trackDisappearAnimation=actionStr(a) 仅当 strId≠0；
//                 trackAnimation 仅当 flag —— 注意**它的值从来没被建模**（两条解析路径都只记"这个键在不在"，
//                 真正生效的是 settings.trackAnimation，atStates 就是从那里取的），所以这里写
//                 settings.trackAnimation 而不是编一个值；
//                 beatsBehind/beatsAhead 仅当 val1/val2 ≥ 0（−1 是"键缺席"的哨兵）
bool putAction(Writer& w, const LevelData& level, const T& a, std::string& err)
{
    const std::string& str = level.actionStr(a);
    w.StartObject();
    w.Key("floor"); w.Int(a.floor);
    w.Key("eventType");
    switch (a.type) {
    case T::Twirl:         w.String("Twirl"); break;
    case T::SetSpeed:      w.String("SetSpeed"); break;
    case T::PositionTrack: w.String("PositionTrack"); break;
    case T::SetHitsound:   w.String("SetHitsound"); break;
    case T::Bookmark:      w.String("Bookmark"); break;
    case T::Pause:         w.String("Pause"); break;
    case T::AnimateTrack:  w.String("AnimateTrack"); break;
    case T::Other:
        err = "内部错误：Other 类型的 action 不该出现在 LevelData.actions 里（解析期就丢了）";
        return false;
    }

    switch (a.type) {
    case T::SetSpeed:
        w.Key("speedType"); w.String(a.flag ? "Multiplier" : "BPM");
        if (a.flag) {
            w.Key("bpmMultiplier");
            if (!putF(w, a.val1, err, "SetSpeed.bpmMultiplier")) return false;
        } else {
            w.Key("beatsPerMinute");
            if (!putF(w, a.val1, err, "SetSpeed.beatsPerMinute")) return false;
        }
        break;
    case T::Pause:
        w.Key("duration");
        if (!putF(w, a.val1, err, "Pause.duration")) return false;
        break;
    case T::PositionTrack:
        w.Key("positionOffset");
        w.StartArray();
        if (!putF(w, a.val1, err, "PositionTrack.positionOffset[0]")) return false;
        if (!putF(w, a.val2, err, "PositionTrack.positionOffset[1]")) return false;
        w.EndArray();
        w.Key("justThisTile"); w.Bool(a.flag);
        break;
    case T::SetHitsound:
        w.Key("hitsound"); w.String(str.c_str(), (rapidjson::SizeType)str.size());
        if (a.flag) {
            w.Key("hitsoundVolume");
            if (!putF(w, a.val1, err, "SetHitsound.hitsoundVolume")) return false;
        }
        break;
    case T::AnimateTrack:
        if (a.strId != 0) {
            w.Key("trackDisappearAnimation");
            w.String(str.c_str(), (rapidjson::SizeType)str.size());
        }
        if (a.flag) {
            // 2026-10-21 起这条是**事件自己的**字符串（`strId2`），不再借 settings 那份。
            // 之前上游只留了一个"有没有写这个键"的 flag，值被丢掉 —— 于是导出的
            // `trackAnimation` 只能拿 settings 顶上，那是近似的；现在可以逐位还原。
            const std::string& ta = level.actionStr2(a);
            w.Key("trackAnimation"); w.String(ta.c_str(), (rapidjson::SizeType)ta.size());
        }
        if (a.val1 >= 0.0f) {
            w.Key("beatsBehind");
            if (!putF(w, a.val1, err, "AnimateTrack.beatsBehind")) return false;
        }
        if (a.val2 >= 0.0f) {
            w.Key("beatsAhead");
            if (!putF(w, a.val2, err, "AnimateTrack.beatsAhead")) return false;
        }
        break;
    default:
        break;   // Twirl / Bookmark：只有 floor + eventType
    }
    w.EndObject();
    return true;
}

// ── 指纹（FNV-1a-64）─────────────────────────────────────────────────────────
struct Digest {
    uint64_t h = 1469598103934665603ull;   // offset basis
    void bytes(const void* p, size_t n)
    {
        const uint8_t* q = (const uint8_t*)p;
        for (size_t i = 0; i < n; ++i) { h ^= q[i]; h *= 1099511628211ull; }
    }
    void u8(uint8_t v) { bytes(&v, 1); }
    void u32(uint32_t v) { bytes(&v, 4); }
    void i32(int32_t v) { bytes(&v, 4); }
    void f32(float v) { bytes(&v, 4); }        // 按位模式，不是按数值
    void f64(double v) { bytes(&v, 8); }
    void str(const std::string& s)
    {
        const uint64_t n = s.size();
        bytes(&n, sizeof(n));
        bytes(s.data(), s.size());
    }
};

void digestSettings(Digest& d, const LevelData::Settings& s)
{
    d.i32(s.version);
    d.str(s.songFilename);          // `.adofai` 的 `"song"`
    d.f32(s.bpm);
    d.f32(s.offset);
    d.i32(s.countdownTicks);
    d.f32(s.zoom);
    d.f32(s.rotation);
    d.str(s.relativeTo);
    d.f32(s.position[0]);
    d.f32(s.position[1]);
    d.str(s.hitsound);
    d.f32(s.hitsoundVolume);
    d.str(s.trackColor);
    d.str(s.secondaryTrackColor);
    d.str(s.backgroundColor);
    d.u8(s.stickToFloors ? 1 : 0);
    d.str(s.planetEase);
    d.str(s.trackDisappearAnimation);
    d.str(s.trackAnimation);
    d.f32(s.beatsBehind);
    d.f32(s.beatsAhead);
    d.str(s.trackColorType);
    d.str(s.trackColorPulse);
    d.f32(s.trackColorAnimDuration);
    d.f32(s.trackPulseLength);
    d.str(s.trackStyle);
    d.f32(s.trackGlowIntensity);
    d.f32(s.pitch);
    d.u8(s.legacyCamRelativeTo ? 1 : 0);
    d.u8(s.separateCountdownTime ? 1 : 0);
}

}  // namespace

bool writeJson(const LevelData& level, std::string& out, std::string& err)
{
    out.clear();
    err.clear();

    rapidjson::StringBuffer buf;
    Writer w(buf);

    w.StartObject();

    // 角度一律走 `angleCount()` / `angleAt()`，**不直接读 `angleData`**：上游 2026-10 加了
    // 紧凑存储（`releaseAngleData()` 会 `swap` 掉密集数组，只留字典 + 位打包下标），
    // 那之后 `angleData` 是空的、值在 AngleStore 里。这两个访问器是官方口径
    // （LevelData.hpp：「运行期读角度一律走 angleAt()」），未压缩时行为与直读逐位相同。
    const int64_t nAngles = level.angleCount();
    if (nAngles > 0) {
        w.Key("angleData");
        w.StartArray();
        for (int64_t i = 0; i < nAngles; ++i)
            if (!putNumber(w, level.angleAt(i), err, "angleData")) return false;
        w.EndArray();
    }
    if (!level.pathData.empty()) {
        w.Key("pathData");
        w.String(level.pathData.c_str(), (rapidjson::SizeType)level.pathData.size());
    }

    w.Key("settings");
    if (!putSettings(w, level.settings, err)) return false;

    // actions 段：原序、原数量。未知 eventType 在解析期就没了，所以这里不可能见到 Other。
    w.Key("actions");
    w.StartArray();
    for (const T& a : level.actions)
        if (!putAction(w, level, a, err)) return false;
    w.EndArray();

    w.EndObject();
    if (w.IsComplete()) {
        out.assign(buf.GetString(), buf.GetSize());
        return true;
    }
    if (err.empty()) err = "JSON 写出未完成（Writer::IsComplete() 为假）";
    return false;
}

bool canExport(const LevelData& level, std::string* why)
{
    // `.adofai` 的 actions 里只有 7 种经典事件；2026-10-18 上游把相机/砖色/闪屏/轨道动画/重复
    // 这五类搬进了**富结构**（`cameraEvents` 等），ScaleRadius 更是只被烘进 tile 位置、
    // 原始事件都没留。这些**没有** JSON 表示 —— 写 `.adofai` 就会静默丢掉它们。
    struct Item { const char* name; size_t n; };
    const Item items[] = {
        {"MoveCamera", level.cameraEvents.size()},
        {"RepeatEvents", level.repeatEvents.size()},
        {"ColorTrack/RecolorTrack", level.colorEvents.size()},
        {"Flash", level.flashEvents.size()},
        {"MoveTrack", level.moveTrackEvents.size()},
    };
    std::string got;
    size_t total = 0;
    for (const Item& it : items) {
        if (it.n == 0) continue;
        if (!got.empty()) got += "、";
        got += std::string(it.name) + " ×" + std::to_string(it.n);
        total += it.n;
    }
    if (level.hasScaleRadiusEvents()) {
        if (!got.empty()) got += "、";
        got += "ScaleRadius（只烘进砖位，原始事件上游没留）";
        ++total;
    }
    if (total == 0) return true;
    if (why) {
        *why = "这份谱含 " + got +
               " —— 它们没有 `.adofai` 的 JSON 表示，写明文会静默丢掉这些事件。"
               "用 `.adocao`（第 9~13 段专门存它们，ScaleRadius 除外），"
               "或者先在上游把这五类事件的 JSON 写方做出来";
    }
    return false;
}

uint64_t modelDigest(const LevelData& level)
{
    Digest d;
    d.str("adofai.model.v1");            // 指纹自身的版本，改字段就一起改（免得新旧指纹看起来一样）
    digestSettings(d, level.settings);
    // 同 writeJson：走 angleAt()，于是"释放过角度"的 LevelData 也能算指纹
    const int64_t nAngles = level.angleCount();
    d.f64((double)nAngles);
    for (int64_t i = 0; i < nAngles; ++i) d.f64(level.angleAt(i));
    d.str(level.pathData);

    // actionStrTable **不进指纹**，指纹里进的是每个 action **解析出来的字符串**（下面）。
    //
    // 原因是实测出来的一个上游不对称：`packLevel` 的字符串池 = `actionStrTable` 原序
    // **再接 settings 用到的字符串**，而 `unpackLevel` 把整张池原样读回 `actionStrTable` ——
    // 于是 `unpack(pack(x)).actionStrTable` 比 `x.actionStrTable` **长**（多出 settings 那几项）。
    // 那些多出来的项没有任何读者（只有 actions 的 strId 指向池头），所以它是良性的；
    // 但"池的形状"因此**不是**建模内容，把它算进指纹只会让自检误报。
    // （要跨实现比内容就用 `.adocao` 的 inputHash；要断言 pack 的字节一致性就直接比 pack 的产物。）
    d.f64((double)level.actions.size());
    for (const T& a : level.actions) {
        d.i32(a.floor);
        d.u8((uint8_t)a.type);
        d.str(level.actionStr(a));      // 解出来的字符串才是语义，strId 只是池里的下标
        d.str(level.actionStr2(a));     // AnimateTrack 的 trackAnimation（2026-10-21 起建模）
        d.u8(a.flag ? 1 : 0);
        d.f32(a.val1);
        d.f32(a.val2);
    }
    return d.h;
}

}  // namespace exporter
}  // namespace adofai
