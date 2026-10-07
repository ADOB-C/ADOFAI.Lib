#!/usr/bin/env python3
"""headless 示例的验收：它导出的那个点击音 WAV 必须是结构合法的 16-bit 单声道 PCM。

理由：这个例子的价值在于"core 的公共 API 真的能独立跑通整条链"。若 Timeline 没算出任何
打拍时刻、或时间线总长为 0，我们写出来的会是一个 0 帧的 WAV —— 那种"跑完了但什么都没发生"
必须判失败，所以下面除了结构检查还断言帧数/命中数下限。

用法：check_wav.py <level.adofai> <out.wav> [min_frames]
"""
import struct
import sys


def fail(msg):
    print(f"FAIL: {msg}")
    sys.exit(1)


def main():
    if len(sys.argv) < 3:
        fail("usage: check_wav.py <level.adofai> <out.wav> [min_frames]")
    level_path, wav_path = sys.argv[1], sys.argv[2]
    min_frames = int(sys.argv[3]) if len(sys.argv) > 3 else 1

    with open(wav_path, "rb") as f:
        data = f.read()

    if len(data) < 44:
        fail(f"{wav_path} 只有 {len(data)} 字节，连 44 字节的 WAV 头都不够")
    if data[0:4] != b"RIFF":
        fail(f"缺少 RIFF 标记: {data[0:4]!r}")
    if data[8:12] != b"WAVE":
        fail(f"缺少 WAVE 标记: {data[8:12]!r}")

    riff_size = struct.unpack_from("<I", data, 4)[0]
    if riff_size != len(data) - 8:
        fail(f"RIFF 声明大小 {riff_size} != 实际 {len(data) - 8}")

    pos = 12
    fmt = None
    data_off = data_len = None
    while pos + 8 <= len(data):
        cid = data[pos:pos + 4]
        csize = struct.unpack_from("<I", data, pos + 4)[0]
        body = pos + 8
        if cid == b"fmt ":
            if csize < 16:
                fail(f"fmt chunk 只有 {csize} 字节")
            fmt = struct.unpack_from("<HHIIHH", data, body)
        elif cid == b"data":
            data_off, data_len = body, csize
        pos = body + csize + (csize & 1)  # chunk 按偶数字节对齐

    if fmt is None:
        fail("没有 fmt chunk")
    if data_off is None:
        fail("没有 data chunk")

    audio_format, channels, rate, byte_rate, block_align, bits = fmt
    if audio_format != 1:
        fail(f"期望 PCM(1)，实际 {audio_format}")
    if channels != 1:
        fail(f"期望单声道，实际 {channels} 声道")
    if bits != 16:
        fail(f"期望 16-bit，实际 {bits}")
    if rate != 48000:
        fail(f"期望 48000 Hz，实际 {rate}")
    if block_align != channels * bits // 8:
        fail(f"block align {block_align} 与声道/位深不符")
    if byte_rate != rate * block_align:
        fail(f"byte rate {byte_rate} 与采样率/block align 不符")

    if data_off + data_len > len(data):
        fail(f"data chunk 声明 {data_len} 字节，但文件里只剩 {len(data) - data_off}")
    if data_len % 2:
        fail(f"data chunk 长度 {data_len} 不是 16-bit 样本的整数倍")

    frames = data_len // 2
    if frames < min_frames:
        fail(f"只有 {frames} 帧，少于下限 {min_frames}（时间线可能没算出来）")

    nz = sum(1 for i in range(0, data_len, 2)
             if struct.unpack_from("<h", data, data_off + i)[0] != 0)
    if nz == 0:
        fail("整段 PCM 全是 0 —— 时间线算出了时长，但一个打拍音都没落进去")

    print(f"OK: {wav_path} — 48000 Hz / 16-bit / 单声道 / {frames} 帧 / 非零样本 {nz} 个")
    return 0


if __name__ == "__main__":
    sys.exit(main())
