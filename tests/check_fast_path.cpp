// 快路径必须吃下**非整数**的 action 字段（带负向对照）。
//
// ## 为什么这条要单独存在
//
// `LevelData::parseNumber()` 曾把"token 正好填满该值的跨度"误判成截断而返回 false，
// 于是**所有含小数 action 字段的谱**（bpmMultiplier / beatsPerMinute / angleOffset…）
// 整份退回旧路径。而三路对拍测试对此**永远绿** —— 因为快路径一放弃，"快路径那次加载"
// 跑的就是旧路径。上游 MYC 实测：旧路径 4.60 s / 4.94 GB，快路径 1.39 s / 1.47 GB。
// 所以必须有一条"不许回退"的显式断言。
//
// ## 为什么不在 tests/level_parse_test.cpp 里
//
// 那个文件是**镜像来的**（PLAN.md §3 白名单），库里不许手改 —— 加了东西下次对齐就冲突。
// 上游对应的护栏是 `scripts/check-fast-path.sh`，但它跑的是**本体 app**（`build/adocao image`），
// 库里没有 app。所以这条是**库自己的**等价版：直接盯 core 的 `loadFromBuffer` +
// `ADOCAO_FAST_REQUIRE`（后者会把放弃阶段连字节偏移一起打到 stderr）。

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "core/level/LevelData.hpp"

using adofai::LevelData;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
    if (ok) std::printf("ok   %s\n", what);
    else { std::printf("FAIL %s\n", what); ++g_fail; }
}

void setEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

void unsetEnv(const char* name) {
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

}  // namespace

int main() {
    // 正向：bpmMultiplier 是小数，快路径必须吃下（ADOCAO_FAST_REQUIRE=1 时**回退即失败**）。
    const char* okText =
        "{\"angleData\":[0,90,180],\"settings\":{\"bpm\":120},"
        "\"actions\":[{\"floor\":1,\"eventType\":\"SetSpeed\",\"speedType\":\"Multiplier\","
        "\"bpmMultiplier\":0.5}],\"decorations\":[]}";

    LevelData fast;
    setEnv("ADOCAO_FAST_REQUIRE", "1");
    const bool fastOk = fast.loadFromString(okText);
    unsetEnv("ADOCAO_FAST_REQUIRE");
    check(fastOk, "含小数 bpmMultiplier 的谱被快路径吃下（没有静默回退）");
    check(fast.actions.size() == 1, "小数 action 解析后条数正确");

    // 同一份文本走旧路径 —— 两条路的 action 载荷必须一致（拿不到就说明这条没意义）。
    LevelData legacy;
    setEnv("ADOCAO_FORCE_DOM_PARSE", "1");
    const bool legacyOk = legacy.loadFromString(okText);
    unsetEnv("ADOCAO_FORCE_DOM_PARSE");
    check(legacyOk, "同一份文本旧路径也能解析（对拍前提）");
    if (legacyOk && fastOk) {
        bool same = legacy.actions.size() == fast.actions.size() &&
                    legacy.angleData.size() == fast.angleData.size() &&
                    legacy.tiles.size() == fast.tiles.size();
        check(same, "快路径与旧路径的结构一致");
    }

    // 负向对照：非整数 floor 本来就该让快路径放弃（旧路径 GetInt() 会 UB）。
    // 它必须在 ADOCAO_FAST_REQUIRE 下**失败**，否则那个开关形同虚设、上面那条断言也没意义。
    const char* badText =
        "{\"angleData\":[0,90,180],\"settings\":{\"bpm\":120},"
        "\"actions\":[{\"floor\":1.5,\"eventType\":\"Twirl\"}],\"decorations\":[]}";
    LevelData bad;
    setEnv("ADOCAO_FAST_REQUIRE", "1");
    const bool badOk = bad.loadFromString(badText);
    unsetEnv("ADOCAO_FAST_REQUIRE");
    check(!badOk, "负向对照：非整数 floor 仍被快路径拒绝");

    std::printf(g_fail ? "\n快路径非整数字段自检失败（%d 处）\n" : "\n快路径非整数字段自检通过\n", g_fail);
    return g_fail ? 1 : 0;
}
