# 查看 ID3v2 文本帧的编码方式和内容（用来确认解码器该怎么写）
import sys, os

path = sys.argv[1] if len(sys.argv) > 1 else \
    r"..\test_music\周杰伦 - 最伟大的作品_L.mp3"
data = open(path, "rb").read()

ENC = {0: "ISO-8859-1", 1: "UTF-16(带BOM)", 2: "UTF-16BE", 3: "UTF-8"}
major, rev, flags = data[3], data[4], data[5]
sz = data[6:10]
size = (sz[0] << 21) | (sz[1] << 14) | (sz[2] << 7) | sz[3]
print(f"ID3v2.{major}.{rev}  flags=0x{flags:02X}  tag大小={size}")


def decode(payload):
    if not payload:
        return ""
    enc = payload[0]
    body = payload[1:]
    if enc in (1, 2):
        if enc == 1 and body[:2] in (b"\xff\xfe", b"\xfe\xff"):
            le = body[:2] == b"\xff\xfe"
            body = body[2:]
        else:
            le = (enc != 2)
        return body.decode("utf-16-le" if le else "utf-16-be", "replace").rstrip("\x00")
    if enc == 3:
        return body.decode("utf-8", "replace").rstrip("\x00")
    # enc 0：规范说是 Latin-1，但中文 tag 常写成 GBK
    try:
        return body.decode("gbk").rstrip("\x00")
    except Exception:
        return body.decode("latin-1", "replace").rstrip("\x00")


pos = 10
while pos + 10 <= 10 + size:
    fid = data[pos:pos+4]
    if fid[0:1] == b"\x00":
        break
    fsz = int.from_bytes(data[pos+4:pos+8], "big")
    flg = int.from_bytes(data[pos+8:pos+10], "big")
    name = fid.decode("latin-1")
    payload = data[pos+10:pos+10+fsz]
    if name.startswith("T") and name != "TXXX":
        enc = payload[0] if payload else -1
        print(f"  {name}  size={fsz:6d}  flags=0x{flg:04X}  enc={enc}({ENC.get(enc,'?')})"
              f"  -> {decode(payload)!r}")
    else:
        print(f"  {name}  size={fsz:6d}  flags=0x{flg:04X}  (非文本帧，跳过)")
    pos += 10 + fsz
