#!/usr/bin/env python3
"""打拍音验收：用合成出来的音色 WAV 走一遍 ADOFAI::audio 的真混音。

用法：
    check_hitsound.py <headless 可执行> <level.adofai> <Kick.wav> <out.wav>

为什么要在测试里**合成**音色而不是用 ADOCAO 的 assets/hitsounds/：那些 WAV 不在本库里
（PLAN.md §3 白名单不含 assets/），用例不该依赖隔壁仓库的目录。这里现场写一个 16-bit
单声道 Kick.wav（一个快速衰减的正弦），够 HitsoundManager 读出非空样本、把混音跑起来。

断言的是"链路真的产出了音频"，不是音色本身：
  * 退出码 0，且 stdout 里出现 `real mix` —— 说明 preSynthesize 成功、writeWav 写了文件
    （没走到就说明我们退回合成点击音了，那是 FAIL）；
  * 输出是结构合法的 WAV，双声道（HitsoundManager 固定输出立体声）；
  * 帧数足够长（≥ 谱面总时长的一半），且 PCM 里既有非零样本、也不是恒定值
    （后者意味着只写了一个 DC 偏移，没有真的混音）。
"""
import math
import os
import struct
import subprocess
import sys
import wave


def make_kick(path, rate=48000, dur=0.08):
    """写一个 16-bit 单声道 Kick.wav：1.2 kHz 正弦 × 快速指数衰减。"""
    n = int(rate * dur)
    frames = bytearray()
    for i in range(n):
        env = math.exp(-i / (rate * 0.012))
        v = int(max(-1.0, min(1.0, math.sin(2 * math.pi * 1200 * i / rate) * env)) * 30000)
        frames += struct.pack("<h", v)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(bytes(frames))


def main():
    if len(sys.argv) != 5:
        print("usage: check_hitsound.py <headless> <level> <Kick.wav> <out.wav>")
        return 2
    exe, level, kick_path, out_path = sys.argv[1:5]

    make_kick(kick_path)
    hits_dir = os.path.dirname(os.path.abspath(kick_path))

    for p in (out_path,):
        if os.path.exists(p):
            os.remove(p)

    proc = subprocess.run([exe, level, out_path, hits_dir],
                          capture_output=True, text=True)
    if proc.returncode != 0:
        print(f"FAIL: headless 退出码 {proc.returncode}\n{proc.stdout}\n{proc.stderr}")
        return 1
    if "real mix" not in proc.stdout:
        print("FAIL: 没走到真混音（退回合成点击音了）\n" + proc.stdout + proc.stderr)
        return 1
    if not os.path.exists(out_path):
        print("FAIL: 没有输出文件")
        return 1

    with wave.open(out_path, "rb") as w:
        channels, width, rate, nframes = (w.getnchannels(), w.getsampwidth(),
                                          w.getframerate(), w.getnframes())
        raw = w.readframes(nframes)
    if width != 2:
        print(f"FAIL: 期望 16-bit，实际 {width * 8}-bit")
        return 1
    if channels != 2:
        print(f"FAIL: 期望双声道（HitsoundManager 固定输出立体声），实际 {channels}")
        return 1
    if rate != 48000:
        print(f"FAIL: 期望 48000 Hz，实际 {rate}")
        return 1
    if nframes < 24000:  # 至少半秒，谱面 16.5 s -> 实际远大于此
        print(f"FAIL: 只有 {nframes} 帧，太短")
        return 1

    vals = struct.unpack_from(f"<{len(raw) // 2}h", raw)
    nonzero = sum(1 for v in vals if v != 0)
    if nonzero == 0:
        print("FAIL: 整段 PCM 全是 0 —— 没有真的混进任何打拍音")
        return 1
    distinct = len(set(vals))
    if distinct < 50:
        print(f"FAIL: PCM 只有 {distinct} 个不同取值 —— 更像直流偏移而不是混音")
        return 1

    print(f"OK: 真混音 {nframes} 帧 / {channels}ch / {rate}Hz，非零样本 {nonzero}，"
          f"不同取值 {distinct}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
