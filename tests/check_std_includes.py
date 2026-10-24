#!/usr/bin/env python3
"""标准库头缺失守卫：用了 `std::sort` / `std::snprintf` / `std::filesystem` … 就必须**直接**
include 对应的头。

为什么需要它：libc++（macOS）常常经由别的头把 `std::stable_sort`、`std::snprintf` 顺手带进来，
于是 mac 上永远绿；而 MinGW / libstdc++ 直接报 `'stable_sort' is not a member of 'std'`。
本库真踩过两次：上游 ADOCAO 的 `windows-portable`（两次），以及本库自己的
`src/adofai/export/LevelJsonWriter.cpp`（2026-10-22，CI 的 windows-default 红）。

上游在 `scripts/check-std-includes.sh` 里有一份 shell 版；这里是**库自有**的 Python 版，
覆盖范围按库的目录来（`src/adofai/**`、`tests/**`、`examples/**`），并把它挂成 ctest 用例，
这样每次本地跑测试都会查。

用法：check_std_includes.py <仓库根>
"""
import re
import sys
from pathlib import Path

# Windows 控制台默认不是 UTF-8（cp1252/cp936），直接 print 非 ASCII 会抛 UnicodeEncodeError
# —— 2026-10-23 CI 的 windows-default 就是这条守卫自己红了（其余 check_*.py 早就有这段，
# 这个新脚本漏了）。放最前面，让所有输出路径（含报错）都不会炸。
for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

# (用法的正则, 需要 include 的头, 说明)
RULES = [
    (r"std::(?:stable_)?sort\s*[<(]", "<algorithm>", "std::sort"),
    (r"std::(?:min|max|clamp)\s*[<(]", "<algorithm>", "std::min/max/clamp"),
    (r"std::(?:sn)?printf\s*\(", "<cstdio>", "std::printf/std::snprintf"),
    (r"std::(?:fopen|fread|fwrite|fclose|FILE)\b", "<cstdio>", "C 文件 API"),
    (r"std::(?:strlen|strcmp|strncmp|strstr|memcpy|memcmp|memset|memchr)\s*\(", "<cstring>",
     "std::str*/mem*"),
    (r"std::filesystem\b", "<filesystem>", "std::filesystem"),
    (r"std::(?:deque|list|map|set|unordered_map|unordered_set)\s*<", "<deque>/<map>/<set> 等容器",
     "STL 容器"),
    (r"std::(?:tolower|toupper|isdigit|isalpha|isspace)\s*\(", "<cctype>", "std::tolower 等"),
    (r"std::numeric_limits\s*<", "<limits>", "std::numeric_limits"),
    (r"std::(?:ifstream|ofstream|fstream)\b", "<fstream>", "文件流"),
    (r"std::(?:to_string|stoi|stod|stof)\s*\(", "<string>", "std::to_string/stod"),
]

# 容器那条容易误报（头文件里可能只是前置声明/引用），单独弱化：只在 .cpp 里查。
CONTAINER_HINT = "<deque>/<map>/<set> 等容器"

# 允许放行的文件（每条都要写清为什么）。目前为空。
ALLOW = set()


def strip_comments_and_strings(src: str) -> str:
    """去掉注释与字符串字面量 —— 否则注释里提一句 `std::sort` 就会误报。"""
    out = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == "/" and i + 1 < n and src[i + 1] == "/":
            while i < n and src[i] != "\n":
                i += 1
        elif c == "/" and i + 1 < n and src[i + 1] == "*":
            i += 2
            while i + 1 < n and not (src[i] == "*" and src[i + 1] == "/"):
                i += 1
            i += 2
        elif c == '"' or c == "'":
            quote = c
            i += 1
            while i < n:
                if src[i] == "\\":
                    i += 2
                    continue
                if src[i] == quote:
                    i += 1
                    break
                i += 1
        elif c == "R" and i + 1 < n and src[i + 1] == '"':
            # 原始字符串 R"( ... )"
            end = src.find(')"', i + 2)
            i = n if end < 0 else end + 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_std_includes.py <仓库根>")
        return 2
    root = Path(sys.argv[1])
    # **只查库自有的文件**：镜像来的 `src/adofai/**` 一律不许手改（补头也算），而且上游自己
    # 有同款守卫（`scripts/check-std-includes.sh`）会在它那边拦住。这里盯的是"我们自己写的
    # 那些 .cpp"—— 2026-10-22 的 windows-default 就是 `src/adofai/export/LevelJsonWriter.cpp`
    # 缺 `<algorithm>` 炸的。
    files = []
    files += sorted((root / "src" / "adofai" / "export").glob("*.cpp"))
    files += sorted((root / "src" / "adofai" / "export").glob("*.hpp"))
    files += sorted(p for p in (root / "tests").glob("check_*.cpp"))
    files += [root / "tests" / "export_roundtrip_test.cpp"]
    files += sorted((root / "examples").rglob("*.cpp"))
    files = [f for f in files if f.exists()]

    bad = 0
    for f in files:
        rel = f.relative_to(root).as_posix()
        if rel in ALLOW:
            continue
        src = strip_comments_and_strings(f.read_text(encoding="utf-8", errors="replace"))
        includes = set(re.findall(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', src, re.M))
        for pat, header, what in RULES:
            if not re.search(pat, src):
                continue
            if what == CONTAINER_HINT:
                # 容器：<map> 这类可以有多种写法，只查"有没有任何容器头"
                if any(h in includes for h in ("map", "set", "deque", "list", "unordered_map",
                                               "unordered_set")):
                    continue
                need = "<map>/<set>/<deque>/<list>/<unordered_map>"
            else:
                # RULES 里写的是 `<cstdio>` 这种形式，而 includes 里存的是裸名（`cstdio`）
                need = header.strip("<>")
                if need in includes:
                    continue
                # 允许等价写法（例如 <stdio.h> 也是 C 头）
                if need == "cstdio" and "stdio.h" in includes:
                    continue
                if need == "cstring" and "string.h" in includes:
                    continue
                if need == "cctype" and "ctype.h" in includes:
                    continue
                need = header
            print(f"  缺 {need}（用了 {what}）：{rel}")
            bad += 1

    if bad:
        print(f"\nFAIL: {bad} 处缺标准库头 —— libc++ 常经由别的头间接带进来（macOS 绿），"
              f"MinGW / libstdc++ 会直接报错。给源文件补上对应的 #include。")
        return 1
    print(f"OK: {len(files)} 个文件的 std 头齐全（{len(RULES)} 条规则）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
