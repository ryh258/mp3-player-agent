# 以校徽为蓝本重绘应用图标与封面（程序化绘制，非生成式模型）
#
#   res/app.ico     多尺寸应用图标（16/24/32/48/64/128/256）
#   res/cover.png   软件封面（宽幅横幅，可作启动图/宣传图）
#   res/logo256.png 单张 256 大图，便于查看效果
#
# 配色取自校徽实测值：主蓝 #064B96，白色。
import math
import os
from PIL import Image, ImageDraw, ImageFont

BLUE   = (6, 75, 150)      # 校徽主蓝 #064B96
BLUE_L = (26, 108, 196)    # 稍亮，用于渐变
WHITE  = (255, 255, 255)
BG     = (238, 241, 245)

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "res")
os.makedirs(OUT, exist_ok=True)

FONT_CANDIDATES = [
    r"C:\Windows\Fonts\msyhbd.ttc", r"C:\Windows\Fonts\msyh.ttc",
    r"C:\Windows\Fonts\simhei.ttf", r"C:\Windows\Fonts\msyhbd.ttf",
]


def font(size, bold=True):
    for p in FONT_CANDIDATES:
        if os.path.exists(p):
            try:
                return ImageFont.truetype(p, size)
            except Exception:
                pass
    return ImageFont.load_default()


def flower_layer(size, color, petal_ratio=1.0):
    """返回一张居中五瓣花（白玉兰）的 RGBA 图。
       花瓣是饱满的椭圆瓣，根部略收、彼此在中心相连。"""
    size = int(size)
    S = size
    c = S / 2
    L = S * 0.46 * petal_ratio       # 花瓣长度（从中心算到花尖）
    Wd = S * 0.285 * petal_ratio     # 花瓣最宽处

    petal = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    pd = ImageDraw.Draw(petal)
    # 一片花瓣：沿半径方向的长椭圆，中心落在 0.55L 处
    cyc = c - L * 0.55
    pd.ellipse([c - Wd / 2, cyc - L * 0.5, c + Wd / 2, cyc + L * 0.5], fill=color)
    # 中心补一小块，让五瓣在花心连成一体
    r0 = Wd * 0.34
    pd.ellipse([c - r0, c - r0, c + r0, c + r0], fill=color)

    lay = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    for k in range(5):
        rot = petal.rotate(-72 * k, resample=Image.BICUBIC, center=(c, c))
        lay = Image.alpha_composite(lay, rot)
    return lay


def badge(size, rings=True, bg=None):
    """画校徽风格的圆形徽章：外蓝环 + 白环 + 内蓝盘 + 白花"""
    S = size * 4                       # 4 倍超采样，缩小时边缘才干净
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    c = S / 2
    R = S / 2 - S * 0.01

    def circ(r, col):
        d.ellipse([c - r, c - r, c + r, c + r], fill=col)

    if rings:
        circ(R, BLUE)                  # 外蓝环
        circ(R * 0.865, WHITE)         # 白环（校徽里是文字环）
        circ(R * 0.615, BLUE)          # 内蓝盘
        disc = R * 0.615
    else:
        circ(R, BLUE)
        disc = R * 0.86

    # 内盘上加一点自上而下的渐变，避免死板
    grad = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    gd = ImageDraw.Draw(grad)
    for y in range(S):
        t = y / (S - 1)
        col = (int(BLUE_L[0] + (BLUE[0] - BLUE_L[0]) * t),
               int(BLUE_L[1] + (BLUE[1] - BLUE_L[1]) * t),
               int(BLUE_L[2] + (BLUE[2] - BLUE_L[2]) * t), 255)
        gd.line([(0, y), (S, y)], fill=col)
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).ellipse([c - disc, c - disc, c + disc, c + disc], fill=255)
    im.paste(grad, (0, 0), mask)

    # 白花（同一尺寸的透明层，直接 alpha 叠加）
    fl = flower_layer(S, WHITE + (255,), petal_ratio=0.62)
    im = Image.alpha_composite(im, fl)
    return im.resize((size, size), Image.LANCZOS)


def make_ico():
    sizes = [256, 128, 64, 48, 32, 24, 16]
    imgs = []
    for s in sizes:
        # 小尺寸去掉外环，只留蓝圆 + 白花，保证辨识度
        imgs.append(badge(s, rings=(s >= 48)))
    path = os.path.join(OUT, "app.ico")
    imgs[0].save(path, format="ICO", sizes=[(s, s) for s in sizes],
                 append_images=imgs[1:])
    print("图标 ->", path, os.path.getsize(path), "bytes")

    imgs[0].save(os.path.join(OUT, "logo256.png"))
    print("大图 ->", os.path.join(OUT, "logo256.png"))


def make_cover():
    W, H = 1200, 320
    im = Image.new("RGB", (W, H), WHITE)
    d = ImageDraw.Draw(im)
    # 顶部蓝色块
    d.rectangle([0, 0, W, 8], fill=BLUE)
    d.rectangle([0, H - 8, W, H], fill=BLUE)
    # 左侧徽章
    b = badge(220, rings=True)
    im.paste(b, (46, (H - 220) // 2), b)
    # 文字
    d.text((300, 92), "重庆大工程music", font=font(62), fill=BLUE)
    d.text((304, 176), "Chongqing Polytechnic University of Engineering · Music Player",
           font=font(20, bold=False), fill=(110, 122, 138))
    d.line([(304, 226), (304 + 560, 226)], fill=(210, 218, 228), width=2)
    d.text((304, 240), "我的歌曲 · 播放列表 · 最近播放 · 歌词同步",
           font=font(22, bold=False), fill=(140, 150, 165))
    path = os.path.join(OUT, "cover.png")
    im.save(path)
    print("封面 ->", path, os.path.getsize(path), "bytes")


if __name__ == "__main__":
    make_ico()
    make_cover()
