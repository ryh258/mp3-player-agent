# 生成 WAV 测试音频（用于验证播放链路，非项目代码）
import math
import os
import struct
import wave

SR = 44100
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "test_music")
os.makedirs(OUT, exist_ok=True)


def write_wav(name, samples, sr=SR):
    path = os.path.join(OUT, name)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sr)
        frames = bytearray()
        for s in samples:
            v = int(max(-1.0, min(1.0, s)) * 32000)
            frames += struct.pack("<h", v)
        w.writeframes(bytes(frames))
    print("  %-28s %6.2f s  %8d bytes" % (name, len(samples) / sr, os.path.getsize(path)))


def fade(i, n, ms=15):
    """首尾淡入淡出，避免爆音"""
    k = int(SR * ms / 1000)
    if i < k:
        return i / k
    if i > n - k:
        return (n - i) / k
    return 1.0


def sine(freq, dur, amp=0.6):
    n = int(SR * dur)
    return [amp * fade(i, n) * math.sin(2 * math.pi * freq * i / SR) for i in range(n)]


def sweep(f0, f1, dur, amp=0.5):
    n = int(SR * dur)
    out = []
    for i in range(n):
        t = i / SR
        # 线性扫频的相位积分
        ph = 2 * math.pi * (f0 * t + (f1 - f0) * t * t / (2 * dur))
        out.append(amp * fade(i, n) * math.sin(ph))
    return out


def melody(notes, amp=0.5):
    """notes: [(freq, dur), ...]"""
    out = []
    for f, d in notes:
        out += sine(f, d, amp)
    return out


A4 = 440.0
def nt(semitones_from_a4):
    return A4 * (2 ** (semitones_from_a4 / 12.0))

print("生成 WAV 测试音频 -> %s" % os.path.abspath(OUT))

# 1) 纯音 A4，3 秒（最简单，先证明能出声）
write_wav("01_纯音A4_440Hz_3s.wav", sine(440.0, 3.0))

# 2) 扫频 200Hz -> 2000Hz，6 秒（听得出进度条位置对不对）
write_wav("02_扫频_200-2000Hz_6s.wav", sweep(200.0, 2000.0, 6.0))

# 3) 小星星，约 12 秒（听得出切歌、快进、模式）
star = [nt(0), nt(0), nt(7), nt(7), nt(9), nt(9), nt(7),
        nt(5), nt(5), nt(4), nt(4), nt(2), nt(2), nt(0)]
write_wav("03_小星星_约12s.wav", melody([(f, 0.42) for f in star]))

# 4) 中文名 + 3 秒静音头，用于验证中文路径与 seek
write_wav("04_中文文件名测试_2s静音后出声.wav",
          [0.0] * int(SR * 2) + sine(523.25, 3.0))

print("完成。")
