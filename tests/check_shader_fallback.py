#!/usr/bin/env python3
"""内嵌回退 GLSL 必须与 assets/shaders/ 下的文件**逐字相同**。

为什么：`render/Shaders.hpp` 里每个 shader 都有一份内嵌回退串，找不到 shader 文件时用它。
两者漂移就是"文件版本对、打包版本错"的静默 bug —— 而且**只在资产缺失时才看得见**，
所以正常构建里永远不会红。PLAN.md §7 把它列为产品铁律之一（不搬走，库使用者就会改坏）。

上游 ADOCAO 用 scripts/check-shader-fallback.sh 机械校验；这里等价重建，但：
  * 用库自己的脚本命名（tests/check_*.py）；
  * 接受一个基目录参数（源码树根），这样 ctest 在构建目录里跑也能找到两边文件。

用法：check_shader_fallback.py <源码树根>
"""
import re
import sys
from pathlib import Path

# 内嵌串名 → assets/shaders 下的文件。与上游的清单一致（7 对）。
CHECKS = [
    ("kTileVertSrc", "tile.vert"),
    ("kTileFragSrc", "tile.frag"),
    ("kHighlightFragSrc", "highlight.frag"),
    ("kPlanetVertSrc", "planet.vert"),
    ("kPlanetFragSrc", "planet.frag"),
    ("kTrailVertSrc", "trail.vert"),
    ("kTrailFragSrc", "trail.frag"),
]


def main():
    if len(sys.argv) != 2:
        print("usage: check_shader_fallback.py <源码树根（含 src/ 与 assets/）>")
        return 2
    root = Path(sys.argv[1])
    header = root / "src" / "adofai" / "render" / "Shaders.hpp"
    shader_dir = root / "assets" / "shaders"

    if not header.exists():
        print(f"FAIL: 找不到 {header}")
        return 1

    sh = header.read_text(encoding="utf-8")

    def grab(name):
        m = re.search(r'constexpr const char\* ' + name + r' = R"\((.*?)\)";', sh, re.S)
        return m.group(1) if m else None

    bad = 0
    for name, fname in CHECKS:
        path = shader_dir / fname
        if not path.exists():
            print(f"  ✗ {fname} 不存在（{path}）")
            bad += 1
            continue
        embedded = grab(name)
        on_disk = path.read_text(encoding="utf-8")
        if embedded is None:
            print(f"  ✗ Shaders.hpp 里找不到 {name}")
            bad += 1
        elif embedded != on_disk:
            print(f"  ✗ {name} 与 {fname} 不一致（回退串必须与文件逐字相同）")
            # 给出定位信息，别只说"不一致"
            a, b = embedded.splitlines(), on_disk.splitlines()
            for i in range(max(len(a), len(b))):
                la = a[i] if i < len(a) else "<缺行>"
                lb = b[i] if i < len(b) else "<缺行>"
                if la != lb:
                    print(f"      首个差异在第 {i + 1} 行：")
                    print(f"        Shaders.hpp: {la!r}")
                    print(f"        {fname}: {lb!r}")
                    break
            else:
                print("      （行内容相同但整体不等 —— 行尾/末尾换行差异）")
            bad += 1

    if bad:
        print(f"\nFAIL: {bad} 处不一致 —— 内嵌回退 GLSL 改了就要同步改 assets/shaders/（反之亦然）")
        return 1
    print(f"OK: 内嵌回退 GLSL 与 assets/shaders/ 逐字一致（{len(CHECKS)} 对）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
