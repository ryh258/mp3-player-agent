# 分析 MP3 文件结构：ID3 标签 + 是否含有合法的 MPEG 音频帧
import sys, os

path = sys.argv[1]
data = open(path, "rb").read()
print("文件:", os.path.basename(path), "大小:", len(data))
print("前 48 字节:", data[:48].hex(" "))

# --- ID3v2 头 ---
if data[:3] == b"ID3":
    ver = data[3], data[4]
    flags = data[5]
    sz = data[6:10]
    size = (sz[0] << 21) | (sz[1] << 14) | (sz[2] << 7) | sz[3]
    print(f"ID3v2.{ver[0]}.{ver[1]}  flags=0x{flags:02X}  size={size}  -> 音频应从偏移 {10+size} 开始")
    audio_off = 10 + size
else:
    print("没有 ID3v2 标签")
    audio_off = 0

# 解析前几个 ID3 帧，看看标签是否正常
if data[:3] == b"ID3":
    p = 10
    n = 0
    print("--- ID3 帧 ---")
    while p + 10 <= len(data) and n < 8:
        fid = data[p:p+4]
        if not fid.strip(b"\x00") or fid[0:1] == b"\x00":
            break
        fsz = int.from_bytes(data[p+4:p+8], "big")
        print(f"  {fid.decode('latin1')}  size={fsz}  @{p}")
        if fsz <= 0 or fsz > len(data):
            print("  !! 帧长度异常，标签结构可疑")
            break
        p += 10 + fsz
        n += 1

# --- 找第一个 MPEG 帧同步字 ---
print("--- 搜索 MPEG 帧同步 (0xFFEx) ---")
found = None
for i in range(audio_off, min(len(data) - 4, audio_off + 200000)):
    if data[i] == 0xFF and (data[i+1] & 0xE0) == 0xE0:
        b1, b2 = data[i+1], data[i+2]
        ver_bits = (b1 >> 3) & 0x03
        layer = (b1 >> 1) & 0x03
        bitrate_idx = (b2 >> 4) & 0x0F
        sr_idx = (b2 >> 2) & 0x03
        if ver_bits != 1 and layer != 0 and bitrate_idx not in (0, 15) and sr_idx != 3:
            found = (i, b1, b2, ver_bits, layer, bitrate_idx, sr_idx)
            break
if found:
    i, b1, b2, ver_bits, layer, br, sr = found
    vname = {0: "MPEG2.5", 2: "MPEG2", 3: "MPEG1"}.get(ver_bits, "?")
    lname = {1: "Layer3", 2: "Layer2", 3: "Layer1"}.get(layer, "?")
    brate = [0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0]
    srate = {0:44100,1:48000,2:32000}
    print(f"  找到合法帧 @{i}: {vname} {lname} 比特率≈{brate[br] if br<16 else '?'}kbps "
          f"采样率={srate.get(sr,'?')}")
    print(f"  该帧头字节: FF {b1:02X} {b2:02X}")
else:
    print("  !! 在标签之后 200KB 内没找到合法 MPEG 帧 —— 这个文件可能不是有效 MP3")

# 统计全文件合法帧数量（粗估）
cnt = 0
i = audio_off
while i < len(data) - 2:
    if data[i] == 0xFF and (data[i+1] & 0xE0) == 0xE0:
        b1, b2 = data[i+1], data[i+2]
        if ((b1 >> 3) & 3) != 1 and ((b1 >> 1) & 3) != 0 and ((b2 >> 2) & 3) != 3:
            cnt += 1
            i += 100
    i += 1
print(f"--- 全文件粗估合法帧同步数(抽样): {cnt} ---")
