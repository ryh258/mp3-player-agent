# 生成一组等长（10 秒）的测试音频，供播放模式的行为测试使用
# 每首音高不同，便于人工分辨；长度一致便于断言。
import math, os, struct, wave

SR = 44100
DUR = 10.0
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "uitest_music")
os.makedirs(OUT, exist_ok=True)

# 清掉旧文件，避免上次的残留影响断言
for n in os.listdir(OUT):
    if n.lower().endswith(".wav"):
        os.remove(os.path.join(OUT, n))

for i, freq in enumerate([262, 330, 392, 440, 523], start=1):
    n = int(SR * DUR)
    fade = int(SR * 0.02)
    frames = bytearray()
    for k in range(n):
        a = 0.5
        if k < fade:
            a *= k / fade
        elif k > n - fade:
            a *= (n - k) / fade
        frames += struct.pack("<h", int(a * 30000 * math.sin(2 * math.pi * freq * k / SR)))
    path = os.path.join(OUT, "trk%d_%dHz.wav" % (i, freq))
    with wave.open(path, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes(bytes(frames))
    print("  %-18s %5.1f s  %8d bytes" % (os.path.basename(path), DUR, os.path.getsize(path)))
print("完成 ->", os.path.abspath(OUT))
