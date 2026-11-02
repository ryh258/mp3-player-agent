# 生成应用图标 res/app.ico（多尺寸）：圆角蓝底 + 白色音符
import os
from PIL import Image, ImageDraw

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "res", "app.ico")
S = 256


def rr(d, box, radius, fill):
    d.rounded_rectangle(box, radius=radius, fill=fill)


img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(img)

# 背景：圆角方块 + 竖向渐变
bg = Image.new("RGBA", (S, S), (0, 0, 0, 0))
grad = Image.new("RGBA", (1, S))
for y in range(S):
    t = y / (S - 1)
    r = int(46 + (26 - 46) * t)
    g = int(104 + (66 - 104) * t)
    b = int(196 + (140 - 196) * t)
    grad.putpixel((0, y), (r, g, b, 255))
grad = grad.resize((S, S))
mask = Image.new("L", (S, S), 0)
ImageDraw.Draw(mask).rounded_rectangle([0, 0, S - 1, S - 1], radius=56, fill=255)
img.paste(grad, (0, 0), mask)

d = ImageDraw.Draw(img)
W = (255, 255, 255, 255)

# 双八分音符：两个符头 + 两条符干 + 顶部符杠
head_r = 30
h1 = (78, 178)    # 左符头中心
h2 = (166, 158)   # 右符头中心
for (cx, cy) in (h1, h2):
    d.ellipse([cx - head_r, cy - head_r * 0.78,
               cx + head_r, cy + head_r * 0.78], fill=W)

stem_w = 12
d.rectangle([h1[0] + head_r - stem_w, 68, h1[0] + head_r, h1[1]], fill=W)
d.rectangle([h2[0] + head_r - stem_w, 48, h2[0] + head_r, h2[1]], fill=W)

# 符杠
d.polygon([(h1[0] + head_r - stem_w, 62),
           (h1[0] + head_r, 72),
           (h2[0] + head_r, 52),
           (h2[0] + head_r - stem_w, 42)], fill=W)

sizes = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]
img.save(OUT, format="ICO", sizes=sizes)
print("icon ->", os.path.abspath(OUT), os.path.getsize(OUT), "bytes")
