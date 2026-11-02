# 从用户提供的设计稿里提取素材，替换掉我之前程序化生成的图片
#
#   res/app.ico      应用图标：深蓝底 + 白花（从设计稿提取的真实标志）
#   res/logo256.png  单张大图
#   res/splash.png   启动页（顺手修掉原稿里 "ENGINEENG" 的拼写错误）
#
# 设计稿是 1773x2364 的竖屏图，标志在画面上部。
import os
from PIL import Image, ImageDraw, ImageFilter

DESIGN = r"..\design"
OUT    = r"..\res"
os.makedirs(OUT, exist_ok=True)

SPLASH_SRC = os.path.join(DESIGN, "ui-01-splash.png")


def white_bbox(im, y0f=0.08, y1f=0.42, thr=200):
    """在指定纵向范围内找近白色像素的外接框"""
    W, H = im.size
    px = im.load()
    x0, y0, x1, y1 = W, H, 0, 0
    for y in range(int(H * y0f), int(H * y1f), 2):
        for x in range(0, W, 2):
            r, g, b = px[x, y][:3]
            if r > thr and g > thr and b > thr:
                if x < x0: x0 = x
                if y < y0: y0 = y
                if x > x1: x1 = x
                if y > y1: y1 = y
    return (x0, y0, x1, y1)


def extract_flower(path):
    """提取标志并转成带 alpha 的白色图形"""
    im = Image.open(path).convert("RGB")
    box = white_bbox(im)
    print("  标志外接框:", box)
    x0, y0, x1, y1 = box

    # 取正方形，稍微留点边
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    half = max(x1 - x0, y1 - y0) / 2 * 1.06
    sq = im.crop((int(cx - half), int(cy - half), int(cx + half), int(cy + half)))

    # 用亮度当 alpha：白花 -> 不透明
    S = 1024
    sq = sq.resize((S, S), Image.LANCZOS)
    gray = sq.convert("L")
    # 归一化：把较亮的都拉成纯白
    lo, hi = 150, 215
    alpha = gray.point(lambda v: 0 if v <= lo else (255 if v >= hi else int((v - lo) * 255 / (hi - lo))))
    flower = Image.new("RGBA", (S, S), (255, 255, 255, 0))
    flower.putalpha(alpha)
    white = Image.new("RGBA", (S, S), (255, 255, 255, 255))
    white.putalpha(alpha)
    return white


def fix_typo(im):
    """原稿把 ENGINEERING 拼成了 ENGINEENG。
       文字块实测在 y 1636..1668（H=2364）。背景是平滑竖向渐变，
       所以从上方一条干净像素带覆盖掉文字，再重写正确内容即可。"""
    W, H = im.size
    ty0, ty1 = 1630, 1676            # 要覆盖的文字带
    sy = 1578                        # 取干净背景的位置（字标 1572 结束，下面是干净的）
    strip = im.crop((0, sy, W, sy + (ty1 - ty0)))
    im.paste(strip, (0, ty0))

    d = ImageDraw.Draw(im)
    from PIL import ImageFont
    fp = None
    for c in [r"C:\Windows\Fonts\seguisb.ttf", r"C:\Windows\Fonts\arial.ttf",
              r"C:\Windows\Fonts\msyh.ttc"]:
        if os.path.exists(c):
            fp = c
            break

    txt = "CHONGQING POLYTECHNIC UNIVERSITY OF ENGINEERING"
    size = 36
    f = ImageFont.truetype(fp, size) if fp else None
    if f:
        # 让新文字宽度接近原稿
        target = W * 0.82
        for _ in range(12):
            w = d.textlength(txt, font=f)
            if abs(w - target) < 8:
                break
            size = max(10, int(size * target / max(w, 1)))
            f = ImageFont.truetype(fp, size)
    tw = d.textlength(txt, font=f)
    y = ty0 + ((ty1 - ty0) - size) // 2
    d.text(((W - tw) / 2, y), txt, font=f, fill=(228, 236, 248))
    print(f"  副标题已重写: 字号={size} 宽={int(tw)} y={y}")
    return im


def make_icon(flower):
    sizes = [256, 128, 64, 48, 32, 24, 16]
    imgs = []
    for s in sizes:
        S = s * 4
        im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        c = S / 2
        R = S / 2 - S * 0.015

        # 深蓝圆底 + 竖向渐变
        grad = Image.new("RGBA", (S, S), (0, 0, 0, 0))
        gd = ImageDraw.Draw(grad)
        top = (17, 82, 156)
        bot = (8, 45, 92)
        for y in range(S):
            t = y / (S - 1)
            gd.line([(0, y), (S, y)],
                    fill=(int(top[0] + (bot[0] - top[0]) * t),
                          int(top[1] + (bot[1] - top[1]) * t),
                          int(top[2] + (bot[2] - top[2]) * t), 255))
        mask = Image.new("L", (S, S), 0)
        ImageDraw.Draw(mask).ellipse([c - R, c - R, c + R, c + R], fill=255)
        im.paste(grad, (0, 0), mask)

        # 白花（占圆直径的 62%）
        fr = int(R * 2 * 0.62)
        fl = flower.resize((fr, fr), Image.LANCZOS)
        im.alpha_composite(fl, (int(c - fr / 2), int(c - fr / 2)))

        imgs.append(im.resize((s, s), Image.LANCZOS))

    path = os.path.join(OUT, "app.ico")
    imgs[0].save(path, format="ICO", sizes=[(s, s) for s in sizes], append_images=imgs[1:])
    print("  图标 ->", path, os.path.getsize(path), "bytes")
    imgs[0].save(os.path.join(OUT, "logo256.png"))
    print("  大图 ->", os.path.join(OUT, "logo256.png"))


if __name__ == "__main__":
    print("[1] 提取白玉兰标志 ...")
    flower = extract_flower(SPLASH_SRC)
    flower.save(os.path.join(OUT, "flower.png"))
    print("  -> res/flower.png")

    print("[2] 生成应用图标 ...")
    make_icon(flower)

    print("[3] 处理启动页（修拼写）...")
    sp = Image.open(SPLASH_SRC).convert("RGB")
    sp = fix_typo(sp)
    sp.save(os.path.join(OUT, "splash.png"))
    sp.save(os.path.join(OUT, "splash.jpg"), "JPEG", quality=93, subsampling=0)  # exe 里嵌的是 jpg
    print("  启动页 ->", os.path.join(OUT, "splash.png"),
          os.path.getsize(os.path.join(OUT, "splash.png")), "bytes")
