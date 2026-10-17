// `releaseMemory()` 的**存活契约**（库自有；镜像区里只写在注释里，没有机械判据）。
//
// 起因（2026-10-17，上游 `3f73ca2`）：本体 app 的 `releaseMeshTemporaries()` 在网格建完之后
// 清掉 `tileHasTwirl` / `tileHasSetSpeed`，于是**事件图标在实机里全部消失** —— 因为图标位不是
// 加载期固化好的，而是 `TileMesh::deriveRange()` 在**绘制期**按可见窗口现从这三张表派生的：
//
//     const bool ht = level.tileHasTwirl[i];        // TileMesh.cpp:176
//     const bool hs = level.tileHasSetSpeed[i];     // TileMesh.cpp:177
//     if (hs && i > 0) r = level.tileBPMs[i] / level.tileBPMs[i-1];   // 上行/下行图标
//
// 那句 `i < size()` 的越界保护把"表被清空"变成了**恒假**：不崩、不报错，只是图标全没了。
// 上游的修法是把那个函数整个删掉，契约改由 `LevelData.hpp` 的字段注释承载。
//
// **本库自己的风险**：`releaseMemory()`（镜像代码）目前确实留着这三张表，但**没有任何测试**
// 盯这件事。上游哪天为了省内存给 `releaseMemory()` 加一句 `tileHasTwirl.clear()`，本库的每个
// 消费者都会静默丢图标，而本库没有像素门槛（那要跑本体 app 抓帧）。所以这条测试盯的就是那句
// 契约：**放掉什么都行，但这三张表必须原样留着**。
//
// 它不碰 GL（不需要 `TileMesh` 实例），所以 CI 上真的会跑 —— 与 `geom_probe` 的 SKIP 不同。
//
// 用法：adofai_release_contract_test <level> [<level>...]

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/level/LevelData.hpp"

using adofai::LevelData;

namespace {

int g_fail = 0;
int g_checked = 0;
int g_iconsSeen = 0;      // 这些输入里一共见到多少"有图标"的砖（防"测试是空转的"）

void fail(const std::string& what, const std::string& detail)
{
    std::printf("FAIL: %s —— %s\n", what.c_str(), detail.c_str());
    g_fail++;
}

// 逐位比较（float 用位模式 —— `==` 会把 -0.0/0.0 与 NaN 判成"相同"）
bool sameBits(const std::vector<float>& a, const std::vector<float>& b)
{
    if (a.size() != b.size()) return false;
    return a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

// `std::vector<bool>` 是位打包特化：没有 `data()`，只能逐元素比。
bool sameFlags(const std::vector<bool>& a, const std::vector<bool>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i] != b[i]) return false;
    return true;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <level> [<level>...]\n", argv[0]);
        return 2;
    }

    for (int ai = 1; ai < argc; ++ai) {
        const std::string input = argv[ai];
        const int failsBefore = g_fail;
        LevelData lv;
        if (!lv.loadFromFile(input)) {
            fail(input, "加载失败");
            continue;
        }

        // ① 快照（在 releaseMemory 之前）
        const int64_t n = lv.tileCount();
        const std::vector<float> bpm0 = lv.tileBPMs;
        const std::vector<bool> twirl0 = lv.tileHasTwirl;
        const std::vector<bool> setSpeed0 = lv.tileHasSetSpeed;

        size_t icons = 0;
        for (size_t i = 0; i < twirl0.size(); ++i)
            if (twirl0[i] || (i < setSpeed0.size() && setSpeed0[i])) ++icons;
        g_iconsSeen += (int)icons;

        // ② 放内存
        lv.releaseMemory();
        g_checked++;

        // ③ 这三张表必须还在、且逐位不变 —— 图标位在绘制期要现算
        if (lv.tileBPMs.size() != bpm0.size() || !sameBits(lv.tileBPMs, bpm0))
            fail(input, "releaseMemory 之后 tileBPMs 变了（图标的上行/下行判定要用它）");
        if (lv.tileHasTwirl.size() != twirl0.size() || !sameFlags(lv.tileHasTwirl, twirl0))
            fail(input, "releaseMemory 之后 tileHasTwirl 变了（Twirl 图标会全部消失）");
        if (lv.tileHasSetSpeed.size() != setSpeed0.size() || !sameFlags(lv.tileHasSetSpeed, setSpeed0))
            fail(input, "releaseMemory 之后 tileHasSetSpeed 变了（SetSpeed 图标会全部消失）");

        // 层数口径不能因为放内存而变（`tileCount()` 是运行期"这块谱有几层"的唯一答案）
        if (lv.tileCount() != n) fail(input, "releaseMemory 之后 tileCount() 变了");

        // ④ 文档里写明**会被放掉**的东西，顺手也钉住 —— 免得"契约"只是单向的
        if (!lv.actions.empty()) fail(input, "releaseMemory 之后 actions 竟然还在（文档说会放掉）");
        if (lv.hitsoundCachesReleased != true)
            fail(input, "releaseMemory 没置 hitsoundCachesReleased（Timeline 会静默给错的打拍音表）");

        // ⑤ 重复调用必须安全（幂等）：实际运行期会出现"手动放一次 + 析构再放一次"
        lv.releaseMemory();
        if (lv.tileHasTwirl.size() != twirl0.size() || !sameFlags(lv.tileHasTwirl, twirl0))
            fail(input, "第二次 releaseMemory 之后 tileHasTwirl 变了");

        if (g_fail == failsBefore)
            std::printf("OK    %s（%lld 层，其中 %zu 层有事件图标）\n", input.c_str(),
                        (long long)n, icons);
    }

    // 防"空转"：如果所有输入都没有 Twirl/SetSpeed，上面那三条断言等于什么都没验。
    // 上游那次 bug 的判据是"图标像素 833 → 0"，我们这边对应的判据就是"这些层真的存在"。
    if (g_checked > 0 && g_iconsSeen == 0) {
        std::printf("FAIL: 所有输入里都没有 Twirl/SetSpeed 层 —— 这三条断言等于空转，"
                    "换带事件图标的谱来跑\n");
        g_fail++;
    }

    std::printf("\n%d 个输入的存活契约检查，见到 %d 处事件图标，%d 处失败\n", g_checked,
                g_iconsSeen, g_fail);
    return g_fail == 0 ? 0 : 1;
}
