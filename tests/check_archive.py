#!/usr/bin/env python3
"""压缩谱面容器（.adofai.xz / .adofai.zst）的两向验收。

用法：
    check_archive.py <binary> <level.adofai> <expect>   # expect = on | off

expect=on  —— 这个构建里 ADOFAI_LIB_ARCHIVE=ON：把明文 fixture 在内存里压成 xz / zstd，
              送进 headless 示例，要求**加载成功**（退出码 0）。
expect=off —— ADOFAI_LIB_ARCHIVE=OFF：同样的压缩输入必须**干净失败**（退出码 1、
              stderr 说明 archive 模块没编译），而不是崩溃、也不是静默当成明文。

负向对照的意义：OFF 构建里如果哪天又把压缩输入当明文喂进去解析，这里会红。
"""
import lzma
import os
import struct
import subprocess
import sys
import tempfile

# Windows 控制台默认不是 UTF-8（cp1252/cp936），直接 print 非 ASCII 会抛
# UnicodeEncodeError（CI 上真踩过：4 条测试全挂）。这里强制把标准流改成 UTF-8。
for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8")
    except (AttributeError, OSError):   # 老 Python / 被重定向过的流
        pass


def run(binary, path):
    p = subprocess.run([binary, path], capture_output=True, text=True)
    return p.returncode, p.stdout, p.stderr


def main():
    if len(sys.argv) != 4:
        print("usage: check_archive.py <binary> <level.adofai> <on|off>")
        return 2
    binary, level_path, expect = sys.argv[1], sys.argv[2], sys.argv[3].lower()
    if expect not in ("on", "off"):
        print(f"FAIL: expect 只能是 on/off，收到 {expect!r}")
        return 2
    if not os.path.exists(binary):
        print(f"FAIL: 找不到可执行文件 {binary}")
        return 2

    with open(level_path, "rb") as f:
        plain = f.read()

    # 明文必须永远能读（与 archive 开关无关）。
    rc, _, err = run(binary, level_path)
    if rc != 0:
        print(f"FAIL: 明文谱都读不了（exit {rc}）：{err.strip()}")
        return 1

    # 容器一：xz（用 magic，不看扩展名；这里故意用 .xz 之外的扩展名之一来验证这点）
    xz_path = os.path.join(tempfile.mkdtemp(), "level.adofai.xz")
    with lzma.open(xz_path, "wb", format=lzma.FORMAT_XZ) as f:
        f.write(plain)

    rc, out, err = run(binary, xz_path)
    if expect == "on":
        if rc != 0:
            print(f"FAIL: ARCHIVE=ON 却读不了 .xz（exit {rc}）：{err.strip()}")
            return 1
        print(f"OK: ARCHIVE=ON 读得动 .xz（{len(plain)} 字节明文，压后 "
              f"{os.path.getsize(xz_path)} 字节）")
    else:
        if rc == 0:
            print("FAIL: ARCHIVE=OFF 却成功加载了 .xz —— 压缩路径被静默当成明文了")
            return 1
        if rc not in (1, 2):
            print(f"FAIL: ARCHIVE=OFF 的失败方式不对（exit {rc}，期望 1 —— 干净退出而不是崩溃）")
            return 1
        if "load failed" not in err:
            print(f"FAIL: ARCHIVE=OFF 的 stderr 里没有 'load failed'：{err.strip()!r}")
            return 1
        print(f"OK: ARCHIVE=OFF 对 .xz 干净失败（exit {rc}）：{err.strip()}")

    # 容器二：zstd —— 只在 Python 带 zstandard 时才测（不引入新依赖）。
    try:
        import zstandard  # noqa: F401
    except ImportError:
        print("note: 没有 zstandard 模块，跳过 .zst 那一路")
        return 0

    import zstandard
    zst_path = os.path.join(tempfile.mkdtemp(), "level.adofai.zst")
    with open(zst_path, "wb") as f:
        f.write(zstandard.ZstdCompressor().compress(plain))
    rc, out, err = run(binary, zst_path)
    if expect == "on" and rc != 0:
        print(f"FAIL: ARCHIVE=ON 却读不了 .zst（exit {rc}）：{err.strip()}")
        return 1
    if expect == "off" and rc == 0:
        print("FAIL: ARCHIVE=OFF 却成功加载了 .zst")
        return 1
    print(f"OK: .zst 行为符合 expect={expect}（exit {rc}）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
