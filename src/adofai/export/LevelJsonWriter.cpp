#include "export/LevelJsonWriter.hpp"

#include <algorithm>   // std::stable_sort（putRichEvents 按 order 排序）—— MinGW 不隐式带
#include <cmath>
#include <cstdio>      // std::snprintf（错误信息与 hex 颜色）—— 同上
#include <cstring>

#include "core/level/LevelData.hpp"
#include "core/level/TileColor.hpp"   // TileColorConfig（ColorTrack 的富字段）
#include "core/util/Easing.hpp"      // easeName：富事件的 ease 要写回**名字**

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

// ═════════════════════════════════════════════════════════════════════════════
// 富事件（五类）的 JSON 写方
//
// 上游 2026-10-18 把 MoveCamera / RepeatEvents / ColorTrack+RecolorTrack / Flash /
// MoveTrack 从 `actions` 搬进了富结构（`cameraEvents` 等）。它们原本就是 actions 里的条目，
// 所以这里按各自 `order`（文件顺序）写回去 —— **跨类型的先后不影响语义**（解析按 eventType
// 分派），只有同类型内部的先后有意义（同 floor 时后者覆盖），而排序保证了那一点。
//
// 字段名与取值口径全部照 `LevelData.cpp` 里的 `jsonRead*Event()` 反着写，这样"写出去再读回来"
// 得到同一份富结构。`level` 只用来查字符串池（`eventTag` 等不在池里，直接用结构里的 std::string）。
// ═════════════════════════════════════════════════════════════════════════════
namespace {

using C = LevelData;

const char* camRelName(uint8_t r)
{
    switch (r) {
    case C::CamPlayer: return "Player";
    case C::CamTile: return "Tile";
    case C::CamGlobal: return "Global";
    case C::CamLastPosition: return "LastPosition";
    case C::CamLastPositionNoRotation: return "LastPositionNoRotation";
    case C::CamUnknown: return "Unknown";
    default: return nullptr;            // CamAbsent：不写
    }
}

const char* flashStyleName(uint8_t s)
{
    switch (s) {
    case 1: return "Kill";
    case 2: return "StayBlack";
    default: return "Flash";
    }
}

// 颜色一律写成 6 位小写 hex（解析侧 `parseColorHex` 也接受 `#` 前缀与 AA 后缀）。
void writeHex(Writer& w, const uint8_t rgb[3])
{
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02x%02x%02x", rgb[0], rgb[1], rgb[2]);
    w.String(buf, 6);
}

void writeStr(Writer& w, const std::string& v)
{
    w.String(v.c_str(), (rapidjson::SizeType)v.size());
}

// `startTile` / `endTile`：解析侧 `jsonTileRef` 接受 数字 / "ThisTile" / "Start±N" / "End±N" /
// [n, rel]。这里用**字符串**形式（最贴近真实 .adofai）。
void writeTileRef(Writer& w, int32_t off, int8_t rel)
{
    if (rel < 0) {                        // 绝对层号
        w.Int(off);
        return;
    }
    const char* base = (rel == 1) ? "Start" : (rel == 2) ? "End" : "ThisTile";
    if (off == 0) { w.String(base); return; }     // 偏移 0 就写裸名字（真实 .adofai 的写法）
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%s%+d", base, (int)off);
    w.String(buf);
}

// 百分比字段：解析侧做 `GetDouble() / 100.0` 再转 float。写成 `(double)v * 100.0` 是为了让
// "×100 再 ÷100" 精确回到原 float（double 这一层有足够余量，误差远小于 float 的 1 ULP）。
bool putPercent(Writer& w, float v, std::string& err, const char* what)
{
    return putNumber(w, (double)v * 100.0, err, what);
}

void writeActiveEditorOnly(Writer& w, bool inactive, bool editorOnly)
{
    if (inactive) { w.Key("active"); w.Bool(false); }
    if (editorOnly) { w.Key("editorOnly"); w.Bool(true); }
}

bool putCamera(Writer& w, const C::CameraEvent& e, std::string& err)
{
    w.StartObject();
    w.Key("floor"); w.Int(e.floor);
    w.Key("eventType"); w.String("MoveCamera");
    if (e.id != 0xFFFFFFFFu) { w.Key("id"); w.Uint(e.id); }
    // 这两个默认就是 0，非 0 才写 —— 解析侧不区分"写了 0"与"没写"。
    if (e.angleOffset != 0.0f) { w.Key("angleOffset"); if (!putF(w, e.angleOffset, err, "MoveCamera.angleOffset")) return false; }
    if (e.duration != 0.0f) { w.Key("duration"); if (!putF(w, e.duration, err, "MoveCamera.duration")) return false; }
    if (e.flags & C::kCamHasPosition) {
        w.Key("position");
        w.StartArray();
        if (e.flags & C::kCamHasX) { if (!putF(w, e.x, err, "MoveCamera.position[0]")) return false; }
        else w.Null();
        if (e.flags & C::kCamHasY) { if (!putF(w, e.y, err, "MoveCamera.position[1]")) return false; }
        else w.Null();
        w.EndArray();
    }
    if (e.flags & C::kCamHasRotation) { w.Key("rotation"); if (!putF(w, e.rotation, err, "MoveCamera.rotation")) return false; }
    if (e.flags & C::kCamHasZoom) { w.Key("zoom"); if (!putF(w, e.zoom, err, "MoveCamera.zoom")) return false; }
    if (e.flags & C::kCamEaseWritten) {
        const char* n = easeName((Ease)e.ease);
        if (n && *n) { w.Key("ease"); w.String(n); }
    }
    if (const char* rel = camRelName(e.relativeTo)) { w.Key("relativeTo"); w.String(rel); }
    if (e.disabled) {
        w.Key("disabled");
        w.StartObject();
        if (e.disabled & C::kCamDisPosition) { w.Key("position"); w.Bool(true); }
        if (e.disabled & C::kCamDisRotation) { w.Key("rotation"); w.Bool(true); }
        if (e.disabled & C::kCamDisZoom) { w.Key("zoom"); w.Bool(true); }
        if (e.disabled & C::kCamDisRel) { w.Key("relativeTo"); w.Bool(true); }
        w.EndObject();
    }
    writeActiveEditorOnly(w, (e.flags & C::kCamInactive) != 0, (e.flags & C::kCamEditorOnly) != 0);
    if (!e.tag.empty()) { w.Key("tag"); writeStr(w, e.tag); }
    w.EndObject();
    return true;
}

bool putRepeat(Writer& w, const C::RepeatEvent& e, std::string& err)
{
    (void)err;
    const bool isBeat = (e.flags & 1) != 0;
    w.StartObject();
    w.Key("floor"); w.Int(e.floor);
    w.Key("eventType"); w.String("RepeatEvents");
    w.Key("repeatType"); w.String(isBeat ? "Beat" : "Floor");
    w.Key(isBeat ? "repetitions" : "floorCount");
    w.Double((double)e.repetitions);
    w.Key("interval"); w.Double((double)e.interval);
    w.Key("gapLength"); w.Double((double)e.gapLength);
    if (e.flags & 2) { w.Key("executeOnCurrentFloor"); w.Bool(true); }
    if (!e.tag.empty()) { w.Key("tag"); writeStr(w, e.tag); }
    w.EndObject();
    return true;
}

bool putColor(Writer& w, const C::ColorEvent& e, std::string& err)
{
    const bool recolor = (e.flags & C::kColorIsRecolor) != 0;
    w.StartObject();
    w.Key("floor"); w.Int(e.floor);
    w.Key("eventType"); w.String(recolor ? "RecolorTrack" : "ColorTrack");
    w.Key("trackColorType"); writeStr(w, e.cfg.colorType);
    w.Key("trackColor"); writeStr(w, e.cfg.color);
    w.Key("secondaryTrackColor"); writeStr(w, e.cfg.secondary);
    w.Key("trackColorPulse"); writeStr(w, e.cfg.pulse);
    w.Key("trackColorAnimDuration"); if (!putF(w, e.cfg.animDuration, err, "ColorTrack.trackColorAnimDuration")) return false;
    w.Key("trackPulseLength"); if (!putF(w, e.cfg.pulseLength, err, "ColorTrack.trackPulseLength")) return false;
    w.Key("trackStyle"); writeStr(w, e.cfg.style);
    // 只有"事件自己写了"才写回去（否则解析侧会把它当成 kColorHasGlow 并覆盖 settings 的语义）
    if (e.flags & C::kColorHasGlow) {
        w.Key("trackGlowIntensity"); if (!putF(w, e.cfg.glowIntensity, err, "ColorTrack.trackGlowIntensity")) return false;
    }
    if (e.duration != 0.0f) { w.Key("duration"); if (!putF(w, e.duration, err, "ColorTrack.duration")) return false; }
    if (const char* n = easeName((Ease)e.ease); n && *n) { w.Key("ease"); w.String(n); }
    if (e.flags & C::kColorJustThisTile) { w.Key("justThisTile"); w.Bool(true); }
    writeActiveEditorOnly(w, (e.flags & C::kColorInactive) != 0, (e.flags & C::kColorEditorOnly) != 0);
    if (recolor) {
        w.Key("startTile"); writeTileRef(w, e.startOff, e.startRel);
        w.Key("endTile"); writeTileRef(w, e.endOff, e.endRel);
        if (e.gapLength) { w.Key("gapLength"); w.Int(e.gapLength); }
        if (e.angleOffset != 0.0f) { w.Key("angleOffset"); if (!putF(w, e.angleOffset, err, "RecolorTrack.angleOffset")) return false; }
    }
    w.EndObject();
    return true;
}

bool putFlash(Writer& w, const C::FlashEvent& e, std::string& err)
{
    w.StartObject();
    w.Key("floor"); w.Int(e.floor);
    w.Key("eventType"); w.String("Flash");
    w.Key("duration"); if (!putF(w, e.duration, err, "Flash.duration")) return false;
    if (e.angleOffset != 0.0f) { w.Key("angleOffset"); if (!putF(w, e.angleOffset, err, "Flash.angleOffset")) return false; }
    w.Key("startOpacity"); if (!putPercent(w, e.startOpacity, err, "Flash.startOpacity")) return false;
    w.Key("endOpacity"); if (!putPercent(w, e.endOpacity, err, "Flash.endOpacity")) return false;
    w.Key("startColor"); writeHex(w, e.startColor);
    w.Key("endColor"); writeHex(w, e.endColor);
    w.Key("plane"); w.String(e.plane == 1 ? "Background" : "Foreground");
    w.Key("flashStyle"); w.String(flashStyleName(e.style));
    if (const char* n = easeName((Ease)e.ease); n && *n) { w.Key("ease"); w.String(n); }
    w.EndObject();
    return true;
}

bool putMoveTrack(Writer& w, const C::MoveTrackEvent& e, std::string& err)
{
    w.StartObject();
    w.Key("floor"); w.Int(e.floor);
    w.Key("eventType"); w.String("MoveTrack");
    w.Key("startTile"); writeTileRef(w, e.startOff, e.startRel);
    w.Key("endTile"); writeTileRef(w, e.endOff, e.endRel);
    if (e.gap) { w.Key("gapLength"); w.Int(e.gap); }
    w.Key("duration"); if (!putF(w, e.durBeats, err, "MoveTrack.duration")) return false;
    if (e.angleOffset != 0.0f) { w.Key("angleOffset"); if (!putF(w, e.angleOffset, err, "MoveTrack.angleOffset")) return false; }
    if (!e.tag.empty()) { w.Key("eventTag"); writeStr(w, e.tag); }
    if (const char* n = easeName((Ease)e.ease); n && *n) { w.Key("ease"); w.String(n); }
    if (e.flags & (C::kMoveOffX | C::kMoveOffY)) {
        w.Key("positionOffset");
        w.StartArray();
        // 缺的分量写 null（不是 0）：解析侧按 IsNumber() 置 kMoveOffX/Y，写 0 会多出一个位。
        if (e.flags & C::kMoveOffX) { if (!putF(w, e.offX, err, "MoveTrack.positionOffset[0]")) return false; }
        else w.Null();
        if (e.flags & C::kMoveOffY) { if (!putF(w, e.offY, err, "MoveTrack.positionOffset[1]")) return false; }
        else w.Null();
        w.EndArray();
    }
    if (e.flags & C::kMoveRot) { w.Key("rotationOffset"); if (!putF(w, e.rotDeg, err, "MoveTrack.rotationOffset")) return false; }
    if (e.flags & C::kMoveScale) {
        w.Key("scale");
        w.StartArray();
        if (!putPercent(w, e.scaleX, err, "MoveTrack.scale[0]")) return false;
        if (!putPercent(w, e.scaleY, err, "MoveTrack.scale[1]")) return false;
        w.EndArray();
    }
    if (e.flags & C::kMoveOpacity) { w.Key("opacity"); if (!putPercent(w, e.opacity, err, "MoveTrack.opacity")) return false; }
    writeActiveEditorOnly(w, (e.flags & C::kMoveInactive) != 0, (e.flags & C::kMoveEditorOnly) != 0);
    w.EndObject();
    return true;
}

// 五类富事件按各自 `order` 升序写进当前数组（调用方已 StartArray）。
bool putRichEvents(Writer& w, const LevelData& level, std::string& err)
{
    // 同类型内部有序即可（解析按 eventType 分派，跨类型先后无语义）。
    struct Item { uint32_t order; int kind; size_t idx; };
    std::vector<Item> items;
    items.reserve(level.cameraEvents.size() + level.repeatEvents.size() + level.colorEvents.size() +
                  level.flashEvents.size() + level.moveTrackEvents.size());
    for (size_t i = 0; i < level.cameraEvents.size(); ++i) items.push_back({level.cameraEvents[i].order, 0, i});
    // RepeatEvent **没有** order 字段（解析侧也不需要）：它本来就是文件顺序，用下标当序。
    for (size_t i = 0; i < level.repeatEvents.size(); ++i)
        items.push_back({(uint32_t)i, 1, i});
    for (size_t i = 0; i < level.colorEvents.size(); ++i) items.push_back({level.colorEvents[i].order, 2, i});
    // FlashEvent 没有 order 字段：按 floor 稳定排序即可（同 floor 的顺序解析侧本来也不区分）
    for (size_t i = 0; i < level.flashEvents.size(); ++i)
        items.push_back({(uint32_t)level.flashEvents[i].floor, 3, i});
    for (size_t i = 0; i < level.moveTrackEvents.size(); ++i) items.push_back({level.moveTrackEvents[i].order, 4, i});
    std::stable_sort(items.begin(), items.end(),
                     [](const Item& a, const Item& b) { return a.order < b.order; });

    for (const Item& it : items) {
        bool ok = true;
        switch (it.kind) {
        case 0: ok = putCamera(w, level.cameraEvents[it.idx], err); break;
        case 1: ok = putRepeat(w, level.repeatEvents[it.idx], err); break;
        case 2: ok = putColor(w, level.colorEvents[it.idx], err); break;
        case 3: ok = putFlash(w, level.flashEvents[it.idx], err); break;
        default: ok = putMoveTrack(w, level.moveTrackEvents[it.idx], err); break;
        }
        if (!ok) return false;
    }
    return true;
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

    // actions 段：经典 7 类按原序写，然后接上五类富事件（各自按 `order` 升序）。
    // 未知 eventType 在解析期就没了，所以这里不可能见到 Other。
    w.Key("actions");
    w.StartArray();
    for (const T& a : level.actions)
        if (!putAction(w, level, a, err)) return false;
    if (!putRichEvents(w, level, err)) return false;
    w.EndArray();

    w.EndObject();
    if (w.IsComplete()) {
        out.assign(buf.GetString(), buf.GetSize());
        return true;
    }
    if (err.empty()) err = "JSON 写出未完成（Writer::IsComplete() 为假）";
    return false;
}

bool writeSettingsObject(const LevelData& level, std::string& out, std::string& err)
{
    out.clear();
    err.clear();
    rapidjson::StringBuffer buf;
    Writer w(buf);
    if (!putSettings(w, level.settings, err)) return false;
    if (!w.IsComplete()) {
        if (err.empty()) err = "settings 写出未完成";
        return false;
    }
    out.assign(buf.GetString(), buf.GetSize());
    return true;
}

bool canExport(const LevelData& level, std::string* why)
{
    // `.adofai` 的 actions 里只有 7 种经典事件；2026-10-18 上游把相机/砖色/闪屏/轨道动画/重复
    // 这五类搬进了**富结构**（`cameraEvents` 等），ScaleRadius 更是只被烘进 tile 位置、
    // 原始事件都没留。这些**没有** JSON 表示 —— 写 `.adofai` 就会静默丢掉它们。
    // 有 **v7 事件段**时，导出会走本库的 `writeV7Json`（`LevelV7Json.cpp`）：按 schema 把原始
    // 事件 JSON 原样吐出来（含 decorations、未知 eventType、以及 rawSettingsJson 里那些我们
    // 没建模的键）—— 那条路是无损的，所以这时**什么都写得出来**（ScaleRadius 也不例外）。
    if (!level.v7SchemaBytes.empty() && !level.v7ColumnsBytes.empty()) return true;

    // 否则（模型里只剩建模字段）就只剩 **ScaleRadius** 写不出来：上游解析它时就把结果烘进了
    // `tiles[i].position`，**原始事件一个字段都没留**（没有 floor、没有数值），
    // 所以任何实现都不可能还原来 —— 这不是"还没做"，是"模型里没有"。
    // 其余五类（MoveCamera / RepeatEvents / ColorTrack+RecolorTrack / Flash / MoveTrack）
    // 现在都由 `putRichEvents()` 按各自 `order` 写回 actions。
    if (!level.hasScaleRadiusEvents()) return true;
    if (why) {
        *why = "这份谱含 ScaleRadius 事件 —— 上游解析时只把结果烘进砖位、**原始事件一个字段都没留**，"
               "所以没有 `.adofai` 的 JSON 表示（`.adocao` 也不存它）。这不是本模块的缺口，"
               "要能导出就得先在上游把那条事件的数据留下";
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

    // ── 五类富事件（2026-10-22 起进指纹）─────────────────────────────────────
    // 只算**输入字段**：`order` 会被重新编号、`trigger`/`durSec`/`lo`/`hi` 是解析后才算出来的
    // 派生值 —— 把它们算进去会让"写出去再读回来"永远不等。
    d.f64((double)level.cameraEvents.size());
    for (const LevelData::CameraEvent& e : level.cameraEvents) {
        d.i32(e.floor); d.u32(e.id);
        d.f32(e.angleOffset); d.f32(e.duration);
        d.f32(e.x); d.f32(e.y); d.f32(e.rotation); d.f32(e.zoom);
        d.u8(e.ease); d.u8(e.relativeTo); d.u8(e.flags); d.u8(e.disabled);
        d.str(e.tag);
    }
    d.f64((double)level.repeatEvents.size());
    for (const LevelData::RepeatEvent& e : level.repeatEvents) {
        d.i32(e.floor); d.f32(e.repetitions); d.f32(e.interval); d.f32(e.gapLength);
        d.u8(e.flags); d.str(e.tag);
    }
    d.f64((double)level.colorEvents.size());
    for (const LevelData::ColorEvent& e : level.colorEvents) {
        d.i32(e.floor); d.i32(e.startOff); d.i32(e.endOff);
        d.u8((uint8_t)e.startRel); d.u8((uint8_t)e.endRel); d.i32(e.gapLength);
        d.f32(e.duration); d.u8(e.ease); d.u8(e.flags); d.f32(e.angleOffset);
        const TileColorConfig& c = e.cfg;
        d.str(c.colorType); d.str(c.color); d.str(c.secondary); d.str(c.pulse);
        d.f32(c.animDuration); d.f32(c.pulseLength); d.str(c.style); d.f32(c.glowIntensity);
    }
    d.f64((double)level.flashEvents.size());
    for (const LevelData::FlashEvent& e : level.flashEvents) {
        d.i32(e.floor); d.f32(e.duration); d.f32(e.angleOffset);
        d.f32(e.startOpacity); d.f32(e.endOpacity);
        d.bytes(e.startColor, 3); d.bytes(e.endColor, 3);
        d.u8(e.plane); d.u8(e.style); d.u8(e.ease);
    }
    d.f64((double)level.moveTrackEvents.size());
    for (const LevelData::MoveTrackEvent& e : level.moveTrackEvents) {
        d.i32(e.floor); d.f32(e.angleOffset); d.str(e.tag);
        d.i32(e.startOff); d.i32(e.endOff); d.u8((uint8_t)e.startRel); d.u8((uint8_t)e.endRel);
        d.i32(e.gap); d.f32(e.durBeats); d.u8(e.ease);
        d.u8(e.flags); d.f32(e.offX); d.f32(e.offY); d.f32(e.rotDeg);
        d.f32(e.scaleX); d.f32(e.scaleY); d.f32(e.opacity);
    }
    return d.h;
}
}  // namespace exporter
}  // namespace adofai
