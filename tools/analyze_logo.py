# 分析校徽图片：提取主色、判断结构比例
import os
from collections import Counter
from PIL import Image

SRC = r"path\to\logo.png"

im = Image.open(SRC).convert("RGB")
W, H = im.size
print("尺寸:", W, "x", H)

# 主色统计（量化到 16 级，减少噪声）
q = im.resize((200, 200)).quantize(colors=12).convert("RGB")
cnt = Counter(q.getdata())
print("\n主要颜色（按占比）:")
total = 200 * 200
for c, n in cnt.most_common(12):
    print(f"  rgb{c}  #{c[0]:02X}{c[1]:02X}{c[2]:02X}   {n/total*100:5.1f}%")

# 圆心一行/一列的颜色变化，用来判断各环的半径比例
cx, cy = W // 2, H // 2
print("\n沿水平中轴（从中心向右）的颜色变化:")
prev = None
for x in range(cx, W):
    p = im.getpixel((x, cy))
    b = tuple(v // 32 for v in p)
    if b != prev:
        r = (x - cx) / (W / 2)
        print(f"  r={r:0.3f}  x={x}  rgb{p}  #{p[0]:02X}{p[1]:02X}{p[2]:02X}")
        prev = b

# 取"最深的蓝"作为主色
blues = [c for c, n in cnt.items() if c[2] > c[0] + 30 and c[2] > 80]
if blues:
    main = min(blues, key=lambda c: sum(c))
    print(f"\n=> 主蓝色 rgb{main}  #{main[0]:02X}{main[1]:02X}{main[2]:02X}")
bg = cnt.most_common(1)[0][0]
print(f"=> 背景色 rgb{bg}  #{bg[0]:02X}{bg[1]:02X}{bg[2]:02X}")

# 中心区域（花心和 1951 文字）的颜色
print("\n中心 1/8 区域的颜色:", im.getpixel((cx, cy)), im.getpixel((cx, cy - 10)))
