#!/usr/bin/env python3
"""导出模块的**独立**验收（不与 C++ 那条共用任何代码）。

C++ 的 export_roundtrip_test 验的是"写方 ∘ 读方"自洽；这里补的是它**证不了**的那一半：

  1. 我们写出来的 `.adofai` 是不是**真的 JSON** —— 用 Python 的 `json` 解，而不是用我们自己的解析器
     （自己的解析器认的语法比 JSON 宽：尾随逗号、裸 CR、缺逗号都收，所以"自己能读回来"不等于"是合法 JSON"）；
  2. `.adofai.xz` 用 Python 标准库 `lzma` 解开，必须与明文那份**逐字节相同**（第三方实现说了算）；
  3. `.adofai.zst` 用 `zstd` CLI（或 python-zstandard）解开，同样逐字节比对；
     两样都没有时退化成"验 magic + 用我们自己的读方读一遍"，并**明确打印 SKIP**而不是假装验过；
  4. `.adocao` 的头字段自洽（magic/version/fileSize）；
  5. **独立建模投影**：把"库建模的那些字段"在 Python 里**重新表述一遍**，然后断言
     原件与导出物的投影相同。这份投影是故意抄的 —— 抄的那一份才会在上游给模型加字段时
     与 C++ 那份对不上，从而把漂移暴露出来（同源就不会）；
  6. 确定性：同一输入导出两次，两份文件逐字节相同；
  7. 负向对照：认不出的后缀必须报错；ARCHIVE=OFF 时三种压缩格式必须**明确失败**；
     截断的 `.adofai.xz` 读不了；
  8. 端到端闭环：把导出的 `.adofai.xz` / `.zst` / `.adocao` 再喂回库，必须能加载。

用法：
  check_export.py <headless 可执行> <out-dir> <archive:on|off> [--src-dir <本库源码根>]
                  <level> [<level>...]

`--src-dir` 给了的话，`.adocao` 的版本号会从 `src/adofai/archive/AdocaoFormat.hpp` 里的
`kVersion` 读出来对账（而不是在这里写死一个数字 —— 格式升版是上游的正常动作，写死会让这条
检查每次升版都假红；而从源码读仍然抓得住"写方与读方不是同一个版本"这种真错）。
"""

import json
import lzma
import os
import re
import shutil
import struct
import subprocess
import sys

# Windows 控制台默认不是 UTF-8（cp1252/cp936），直接 print 非 ASCII 会抛 UnicodeEncodeError。
for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

FAILS = []


def fail(msg):
    print(f"FAIL: {msg}")
    FAILS.append(msg)


def run(args):
    p = subprocess.run(args, capture_output=True, encoding="utf-8", errors="replace")
    return p.returncode, p.stdout or "", p.stderr or ""


# ─────────────────────────── 独立建模投影 ───────────────────────────
# 这一节是"库建模了什么"的**第二份表述**。改动它请同时改 src/adofai/export/LevelJsonWriter.cpp。

# settings 的 28 个建模字段与默认值（默认值取解析器的默认，用于"键缺席"的情形）。
SETTINGS_DEFAULTS = {
    "version": 15,
    # 模型字段叫 `songFilename`，`.adofai` 里的键是 `song`（解析器还接受 `songFilename` 当回退，
    # 见下面的 MODEL_TO_JSON）。
    "song": "",
    "bpm": 100.0,
    "offset": 0.0,
    "countdownTicks": 4,
    "zoom": 100.0,
    "rotation": 0.0,
    "relativeTo": "Player",
    "position": [0.0, 0.0],
    "hitsound": "Kick",
    "hitsoundVolume": 100.0,
    "trackColor": "debb7b",
    "secondaryTrackColor": "ffffff",
    # 2026-10-18 上游加的 8 个（砖色模型 + 音高 + 相机的老语义开关）
    "trackColorType": "Single",
    "trackColorPulse": "None",
    "trackColorAnimDuration": 2.0,
    "trackPulseLength": 10.0,
    "trackStyle": "Standard",
    "trackGlowIntensity": 100.0,
    "pitch": 100.0,
    "legacyCamRelativeTo": False,
    "backgroundColor": "000000",
    "stickToFloors": True,
    "planetEase": "Linear",
    "trackDisappearAnimation": "None",
    "trackAnimation": "None",
    "beatsBehind": 4.0,
    "beatsAhead": 3.0,
}
FLOAT_SETTINGS = {
    "bpm", "offset", "zoom", "rotation", "hitsoundVolume", "beatsBehind", "beatsAhead",
    "trackColorAnimDuration", "trackPulseLength", "trackGlowIntensity", "pitch",
}
# 模型字段名 → `.adofai` 里的键名（只有不一致的才需要写）
MODEL_TO_JSON = {"songFilename": "song"}
# 备选拼写：解析器认这些键，但导出统一写成规范的那个
JSON_ALIASES = {"song": ("songFilename",)}

# **元件里出现、但库不建模的 settings 键**（导出会丢掉它们）。这是一份显式清单：
# 上游哪天加了新键、而夹具里也带上了，下面那条"未知键"检查就会红 —— 逼你做一个决定
# （要么建模，要么记进这里并说明为什么丢得起）。
SETTINGS_NOT_MODELED = {
    "songFilename",        # `song` 的备选拼写，值会写进模型的 songFilename
    "artist",              # 只影响编辑器显示
    "levelDesc",           # 同上
    # `trackStyle` 2026-10-18 起**已建模**（砖色模型要用），所以从这份清单里挪走了。
}
KNOWN_TYPES = {
    "Twirl", "SetSpeed", "PositionTrack", "SetHitsound", "Bookmark", "Pause", "AnimateTrack",
}


def f32(v):
    """按 float32 的**位模式**比，而不是按十进制字面量。

    原件写 `0.1`，导出写 `0.10000000149011612`（float32→double 的最短往返表示）——这两者
    作为 double 不相等，但作为 float32 是同一个值。库里的字段本来就是 float，所以这才是
    正确的比较口径。"""
    return struct.pack("<f", v)


def just_this_tile(v):
    """PositionTrack.justThisTile 的读法（与 buildAction 一致：bool / 字符串 / 整数）。"""
    if isinstance(v, bool):
        return v
    if isinstance(v, str):
        return v in ("Enabled", "true", "True")
    if isinstance(v, int):
        return v != 0
    return False


def proj_action(a):
    t = a.get("eventType")
    floor = a["floor"]
    if t in ("Twirl", "Bookmark"):
        return (t, floor)
    if t == "SetSpeed":
        is_mult = a.get("speedType", "BPM") == "Multiplier"
        v = a.get("bpmMultiplier", 1.0) if is_mult else a.get("beatsPerMinute", 0.0)
        return (t, floor, is_mult, f32(v))
    if t == "Pause":
        return (t, floor, f32(a.get("duration", 0.0)))
    if t == "PositionTrack":
        off = a.get("positionOffset")
        x, y = (off[0], off[1]) if isinstance(off, list) and len(off) >= 2 else (0.0, 0.0)
        return (t, floor, f32(x), f32(y), just_this_tile(a.get("justThisTile", False)))
    if t == "SetHitsound":
        has_vol = "hitsoundVolume" in a
        return (t, floor, a.get("hitsound", ""), has_vol, f32(a.get("hitsoundVolume", 0.0)))
    if t == "AnimateTrack":
        return (
            t,
            floor,
            a.get("trackDisappearAnimation", ""),
            "trackAnimation" in a,          # 只记"键在不在"：值从来没被建模
            f32(a.get("beatsBehind", -1.0)),  # −1 是"键缺席"的哨兵
            f32(a.get("beatsAhead", -1.0)),
        )
    raise AssertionError(f"投影遇到未知 eventType {t!r}")


def proj(doc):
    s = doc.get("settings") or {}
    settings = {}
    for k, dflt in SETTINGS_DEFAULTS.items():
        v = s.get(k, dflt)
        for alias in JSON_ALIASES.get(k, ()):
            if k not in s and alias in s:
                v = s[alias]
        if k in FLOAT_SETTINGS:
            settings[k] = f32(v)
        elif k == "stickToFloors":
            settings[k] = just_this_tile(v) if not isinstance(v, bool) else v
        elif k == "position":
            settings[k] = (f32(v[0]), f32(v[1]))
        else:
            settings[k] = v
    actions = doc.get("actions")
    if actions is None:
        actions = []
    actions = [proj_action(a) for a in actions
               if isinstance(a, dict) and "floor" in a and a.get("eventType") in KNOWN_TYPES]
    # 注意**不**比 settings 的键集合：原件往往只写几个键，而导出物是自描述的（20 个全写），
    # 这是有意的差异。导出物那份键集合由下面单独断言。
    # 原件里**未建模**的 settings 键在这里也正是被忽略的 —— 它们在解析期就没了。
    return {
        "settings": settings,
        # angleData 只在**原件本来就有**时比：pathData 型谱面的 angleData 是库里现推的
        # （pathCharToAngle 映射表），那份等价由 C++ 的指纹测试负责，Python 这边不抄第二份表。
        "angleData": doc.get("angleData"),
        "pathData": doc.get("pathData"),
        "actions": actions,
    }


# ─────────────────────────── 主流程 ───────────────────────────


def export_via_cli(binary, level, out_path):
    rc, out, err = run([binary, level, "--export", out_path])
    return rc, out, err


def adocao_version_from_source(src_dir):
    """从 AdocaoFormat.hpp 读 `kVersion`（见文件头的说明）。读不到就返回 None。"""
    hpp = os.path.join(src_dir, "src", "adofai", "archive", "AdocaoFormat.hpp")
    try:
        with open(hpp, encoding="utf-8") as f:
            for line in f:
                m = re.search(r"kVersion\s*=\s*(\d+)", line)
                if m:
                    return int(m.group(1))
    except OSError:
        return None
    return None


def read_bytes(path):
    with open(path, "rb") as f:
        return f.read()


def main():
    argv = sys.argv[1:]
    src_dir = None
    pos = []
    i = 0
    while i < len(argv):
        if argv[i] == "--src-dir":
            if i + 1 >= len(argv):
                print("FAIL: --src-dir 后面要跟路径")
                return 2
            src_dir = argv[i + 1]
            i += 2
        else:
            pos.append(argv[i])
            i += 1
    if len(pos) < 4:
        print("usage: check_export.py <headless> <out-dir> <archive:on|off> "
              "[--src-dir <本库源码根>] <level> [...]")
        return 2
    binary, out_dir, archive_expect = pos[0], pos[1], pos[2]
    levels = pos[3:]

    want_version = adocao_version_from_source(src_dir) if src_dir else None
    if want_version is None:
        print("note: 没给 --src-dir（或 AdocaoFormat.hpp 里找不到 kVersion），"
              "跳过 .adocao 的版本对账（「读得回来」那条仍然是权威判据）")

    if not os.path.isfile(binary):
        fail(f"找不到可执行文件 {binary}")
        return 1
    if archive_expect not in ("on", "off"):
        fail(f"expect 只能是 on/off，收到 {archive_expect!r}")
        return 1
    have_archive = archive_expect == "on"

    os.makedirs(out_dir, exist_ok=True)

    zstd_cli = shutil.which("zstd")
    try:
        import zstandard  # noqa: F401
        have_pyzstd = True
    except Exception:
        have_pyzstd = False

    for level in levels:
        tag = os.path.splitext(os.path.basename(level))[0]

        # ---- ① 明文 .adofai ----
        plain = os.path.join(out_dir, f"{tag}.adofai")
        rc, out, err = export_via_cli(binary, level, plain)
        if rc != 0:
            fail(f"{tag}: .adofai 导出失败（exit {rc}）：{err.strip()}")
            continue
        try:
            with open(plain, "r", encoding="utf-8") as f:
                exported_doc = json.load(f)
        except Exception as e:
            fail(f"{tag}: 导出的 .adofai 不是合法 JSON（{e}）")
            continue

        # ---- ⑤ 独立建模投影 ----
        with open(level, "r", encoding="utf-8-sig") as f:
            src = f.read()
        # 原件可能是宽容语法（尾随逗号/裸 CR），Python 的 json 不认；认不了就只做"导出物自身"的检查。
        try:
            src_doc = json.loads(src)
        except Exception:
            src_doc = None
        if src_doc is not None:
            # 元件里有没有"库不认识的 settings 键"：那意味着导出会静默丢掉它。
            # 见 SETTINGS_NOT_MODELED 上的说明 —— 这是防"上游加字段、导出悄悄落后"的那道闸。
            src_keys = set((src_doc.get("settings") or {}).keys())
            unknown = src_keys - set(SETTINGS_DEFAULTS) - SETTINGS_NOT_MODELED
            if unknown:
                fail(f"{tag}: 元件里有库不建模的 settings 键 {sorted(unknown)} —— "
                     f"导出会丢掉它们。要么在 LevelJsonWriter 里建模，要么记进 "
                     f"SETTINGS_NOT_MODELED 并写明为什么丢得起")
            a, b = proj(src_doc), proj(exported_doc)
            if a != b:
                diff = [k for k in a if a[k] != b[k]]
                fail(f"{tag}: 独立投影不一致（差异字段：{diff}）")
            else:
                print(f"OK: {tag} 独立投影一致"
                      f"（settings {len(a['settings'])} 项 / actions {len(a['actions'])} 条）")
        else:
            print(f"note: {tag} 原件不是严格 JSON，跳过独立投影（C++ 那条仍会覆盖）")

        # 导出物必须自带全部建模 settings 键（自描述，不靠默认值补）
        if sorted(exported_doc.get("settings", {}).keys()) != sorted(SETTINGS_DEFAULTS.keys()):
            fail(f"{tag}: 导出的 settings 键集合不是那 {len(SETTINGS_DEFAULTS)} 个建模字段"
                 f"（导出 {sorted(exported_doc.get('settings', {}).keys())}）")

        # ---- ⑥ 确定性 ----
        plain2 = os.path.join(out_dir, f"{tag}.again.adofai")
        rc2, _, err2 = export_via_cli(binary, level, plain2)
        if rc2 != 0:
            fail(f"{tag}: 第二次导出失败：{err2.strip()}")
        elif read_bytes(plain) != read_bytes(plain2):
            fail(f"{tag}: 同一输入两次导出的 .adofai 不是逐字节相同")
        os.path.exists(plain2) and os.remove(plain2)

        # ---- ② .adofai.xz ----
        xz_path = os.path.join(out_dir, f"{tag}.adofai.xz")
        rc, out, err = export_via_cli(binary, level, xz_path)
        if have_archive:
            if rc != 0:
                fail(f"{tag}: .adofai.xz 导出失败（exit {rc}）：{err.strip()}")
            else:
                try:
                    got = lzma.decompress(read_bytes(xz_path))
                except Exception as e:
                    fail(f"{tag}: Python 的 lzma 解不开我们写的 .xz（{e}）")
                    got = None
                if got is not None and got != read_bytes(plain):
                    fail(f"{tag}: .xz 解开后与明文不是逐字节相同")
                else:
                    print(f"OK: {tag} .adofai.xz 经第三方 lzma 解开与明文逐字节相同")
                # 端到端：再喂回库
                rc2, _, err2 = run([binary, xz_path])
                if rc2 != 0:
                    fail(f"{tag}: 导出的 .xz 自己读不回来：{err2.strip()}")
                # 负向对照：截断的 .xz 必须读不了
                bad = os.path.join(out_dir, f"{tag}.broken.adofai.xz")
                with open(bad, "wb") as f:
                    f.write(read_bytes(xz_path)[:-8])
                rc3, _, _ = run([binary, bad])
                if rc3 == 0:
                    fail(f"{tag}: 截断的 .adofai.xz 竟然读成功了")
        else:
            if rc == 0:
                fail(f"{tag}: ARCHIVE=OFF 却导出成功了 .adofai.xz")
            elif "ARCHIVE=OFF" not in err:
                fail(f"{tag}: ARCHIVE=OFF 的失败原因不清楚：{err.strip()}")

        # ---- ③ .adofai.zst ----
        zst_path = os.path.join(out_dir, f"{tag}.adofai.zst")
        rc, out, err = export_via_cli(binary, level, zst_path)
        if have_archive:
            if rc != 0:
                fail(f"{tag}: .adofai.zst 导出失败（exit {rc}）：{err.strip()}")
            else:
                raw = read_bytes(zst_path)
                if raw[:4] != bytes([0x28, 0xB5, 0x2F, 0xFD]):
                    fail(f"{tag}: .zst 的 magic 不对（{raw[:4].hex()}）")
                got = None
                if zstd_cli:
                    p = subprocess.run([zstd_cli, "-dc", zst_path], capture_output=True)
                    if p.returncode != 0:
                        fail(f"{tag}: zstd CLI 解不开我们写的 .zst："
                             f"{p.stderr.decode('utf-8', 'replace').strip()}")
                    else:
                        got = p.stdout
                elif have_pyzstd:
                    import zstandard
                    got = zstandard.ZstdDecompressor().decompress(raw)
                if got is None:
                    print(f"note: {tag} 没有 zstd CLI / python-zstandard，"
                          f"跳过 .zst 的第三方解码（只验 magic + 自家读回）")
                elif got != read_bytes(plain):
                    fail(f"{tag}: .zst 解开后与明文不是逐字节相同")
                else:
                    print(f"OK: {tag} .adofai.zst 经第三方解码与明文逐字节相同")
                rc2, _, err2 = run([binary, zst_path])
                if rc2 != 0:
                    fail(f"{tag}: 导出的 .zst 自己读不回来：{err2.strip()}")
                # 负向对照：改坏压缩数据必须读不了。这条能成立是因为**我们写出的 zstd 帧带
                # 内容校验和**（LevelEncode.cpp 里显式开了 ZSTD_c_checksumFlag）——
                # 默认的一次性 ZSTD_compress 不带，那种帧翻一个字节有可能被静默解成错数据。
                bad = bytearray(raw)
                bad[len(bad) // 2] ^= 0xFF
                bad_path = os.path.join(out_dir, f"{tag}.broken.adofai.zst")
                with open(bad_path, "wb") as f:
                    f.write(bytes(bad))
                rc3, _, _ = run([binary, bad_path])
                if rc3 == 0:
                    fail(f"{tag}: 改坏的 .adofai.zst 竟然读成功了（内容校验和没起作用）")
        else:
            if rc == 0:
                fail(f"{tag}: ARCHIVE=OFF 却导出成功了 .adofai.zst")
            elif "ARCHIVE=OFF" not in err:
                fail(f"{tag}: ARCHIVE=OFF 的失败原因不清楚：{err.strip()}")

        # ---- ④ .adocao ----
        adocao_path = os.path.join(out_dir, f"{tag}.adocao")
        rc, out, err = export_via_cli(binary, level, adocao_path)
        if have_archive:
            if rc != 0:
                fail(f"{tag}: .adocao 导出失败（exit {rc}）：{err.strip()}")
            else:
                raw = read_bytes(adocao_path)
                if raw[:4] != b"ADO1":
                    fail(f"{tag}: .adocao 的 magic 不是 ADO1（{raw[:4]!r}）")
                else:
                    version, = struct.unpack_from("<H", raw, 4)
                    file_size, = struct.unpack_from("<Q", raw, 12)
                    if want_version is not None and version != want_version:
                        fail(f"{tag}: .adocao 的 version 是 {version}，"
                             f"而 AdocaoFormat.hpp 的 kVersion 是 {want_version}")
                    elif file_size != len(raw):
                        fail(f"{tag}: .adocao 头里写着 {file_size} 字节，实际 {len(raw)}")
                    else:
                        print(f"OK: {tag} .adocao 头自洽（ADO1 v{version} / {len(raw)} 字节）")
                rc2, _, err2 = run([binary, adocao_path])
                if rc2 != 0:
                    fail(f"{tag}: 导出的 .adocao 自己读不回来：{err2.strip()}")
                # 负向对照：改坏一个段载荷字节必须读不了（段头有 crc32c）
                bad = bytearray(raw)
                bad[len(bad) // 2] ^= 0xFF
                bad_path = os.path.join(out_dir, f"{tag}.broken.adocao")
                with open(bad_path, "wb") as f:
                    f.write(bytes(bad))
                rc3, _, _ = run([binary, bad_path])
                if rc3 == 0:
                    fail(f"{tag}: 改坏的 .adocao 竟然读成功了（段 crc32c 没起作用）")
        else:
            if rc == 0:
                fail(f"{tag}: ARCHIVE=OFF 却导出成功了 .adocao")
            elif "ARCHIVE=OFF" not in err:
                fail(f"{tag}: ARCHIVE=OFF 的失败原因不清楚：{err.strip()}")

    # ---- ⑦ 认不出的后缀必须报错（不许猜成明文）----
    rc, out, err = export_via_cli(binary, levels[0], os.path.join(out_dir, "bad.txt"))
    if rc == 0:
        fail("后缀 .txt 竟然导出成功了（应当报错而不是猜格式）")
    elif "认不出" not in err and "格式" not in err:
        fail(f"后缀 .txt 的失败原因不清楚：{err.strip()}")
    else:
        print(f"OK: 认不出的后缀被干净拒绝：{err.strip()}")

    print()
    if FAILS:
        print(f"{len(FAILS)} 处失败")
        return 1
    print(f"导出验收全过（archive={archive_expect}）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
