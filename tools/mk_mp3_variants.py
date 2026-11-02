# 生成两个对照文件，定位 MCI 打不开 MP3 的原因
#   A: 纯 ASCII 文件名（排除 mciqtz32 不支持 Unicode 路径）
#   B: 去掉 ID3 标签，只留音频帧（排除大 APIC 封面干扰解析）
import os
import shutil

SRC = r"..\test_music\周杰伦 - 最伟大的作品_L.mp3"
DST = r"..\test_music"

data = open(SRC, "rb").read()

# A: 原样复制，但用 ASCII 文件名
shutil.copyfile(SRC, os.path.join(DST, "zz_a_asciiname.mp3"))
print("A 已生成: zz_a_asciiname.mp3", os.path.getsize(os.path.join(DST, "zz_a_asciiname.mp3")))

# B: 剥掉 ID3v2 标签
sz = data[6:10]
size = (sz[0] << 21) | (sz[1] << 14) | (sz[2] << 7) | sz[3]
audio = data[10 + size:]
open(os.path.join(DST, "zz_b_noid3.mp3"), "wb").write(audio)
print("B 已生成: zz_b_noid3.mp3", len(audio), "起始字节:", audio[:4].hex(" "))

# C: 再用 mciqtz32 常见的 "M3U" 无关；加一个只含 1 帧的极小 MP3，排除文件大小因素
open(os.path.join(DST, "zz_c_short.mp3"), "wb").write(audio[:200000])
print("C 已生成: zz_c_short.mp3", 200000)
