#!/usr/bin/env python3
"""打拍音的产品铁律护栏（源码级，机械校验）。

PLAN.md §7 与 AGENTS.md 的 "Hitsounds" 里有两条**不能放宽**的规则，而它们恰好都是
"性能优化"最容易破坏的那类：

  1. 每个命中都要混音 —— 不许抄 ADOFAI_HitSound 的 Nyquist 去重（丢掉间隔 < 41.7 µs 的命中）。
     实测那次去重让 95% 的输出采样变了，也就是"换了个音色"。
  2. 16 位累加，而且**每次相加都要 clamp**（HitSoundGenerator 语义）。float + 软限幅被试过并删除：
     94.5% 的采样变了，响的部分变成畸变平台、轻的部分掉电平。

这条护栏检查的是"混音内核还在用饱和加法"，而不是听感 —— 听感由 check_hitsound.py 的真混音
用例覆盖。谁把饱和加法换成普通加法（或引入去重），这里立刻红。

用法：check_hitsound_rules.py <HitsoundManager.cpp> [<AudioEngine.cpp> ...]
"""
import re
import sys

# Windows 控制台默认不是 UTF-8（cp1252/cp936），直接 print 非 ASCII 会抛
# UnicodeEncodeError（CI 上真踩过：4 条测试全挂）。这里强制把标准流改成 UTF-8。
for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8")
    except (AttributeError, OSError):   # 老 Python / 被重定向过的流
        pass

# 必须存在的：饱和加法（每次相加即 clamp）的三种实现
REQUIRED = [
    (r"\bvqaddq_s16\b", "NEON 饱和加法 vqaddq_s16"),
    (r"\b_mm_adds_epi16\b", "SSE2 饱和加法 _mm_adds_epi16"),
    (r"32767", "标量回退路径的 +32767 clamp"),
    (r"v\s*<\s*-32768", "标量回退路径的 -32768 clamp"),
]

# 不许存在的：非饱和加法（会静默丢掉"每次相加都 clamp"这条语义）
FORBIDDEN = [
    (r"\bvaddq_s16\b", "NEON **非饱和**加法 vaddq_s16 —— 它不 clamp，破坏铁律 2"),
    (r"\b_mm_add_epi16\b", "SSE2 **非饱和**加法 _mm_add_epi16 —— 它不 clamp，破坏铁律 2"),
]

# 不许存在的：Nyquist / 去重那类"丢命中"的优化
DEDUP = [
    (r"(?i)nyquist", "Nyquist 去重"),
    (r"(?i)\bdedup", "去重逻辑"),
    (r"(?i)de-?duplicat", "去重逻辑"),
]


def main():
    if len(sys.argv) < 2:
        print("usage: check_hitsound_rules.py <HitsoundManager.cpp> ...")
        return 2

    failed = False
    for path in sys.argv[1:]:
        for enc in ("utf-8", "latin-1"):
            try:
                text = open(path, encoding=enc).read()
                break
            except UnicodeDecodeError:
                continue
        else:
            print(f"FAIL: 读不了 {path}")
            return 1

        print(f"-- {path}")
        for pat, what in REQUIRED:
            if re.search(pat, text):
                print(f"   OK   {what}")
            else:
                print(f"   FAIL 缺少 {what}（铁律 2：每次相加都要 clamp）")
                failed = True

        for pat, what in FORBIDDEN + DEDUP:
            m = re.search(pat, text)
            if not m:
                continue
            # 这些名字出现在注释里（解释"为什么不这么做"）是允许的 —— 实际代码里不允许。
            line = text[:m.start()].count("\n") + 1
            raw = text.splitlines()[line - 1].strip()
            in_comment = raw.startswith("//") or raw.startswith("*") or raw.startswith("/*")
            if in_comment:
                print(f"   OK   {what} 只出现在注释里（第 {line} 行）")
            else:
                print(f"   FAIL {what}（第 {line} 行）")
                failed = True

    if failed:
        print("\nFAIL: 打拍音铁律被破坏（见上）。这两条是忠实度要求，不是性能取舍。")
        return 1
    print("\nOK: 混音内核仍是饱和加法，且没有 Nyquist/去重那类丢命中的优化")
    return 0


if __name__ == "__main__":
    sys.exit(main())
