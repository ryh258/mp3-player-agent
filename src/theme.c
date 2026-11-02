/* theme.c —— 主题绘制：校徽风格的五瓣花与圆形徽章（纯 GDI，任意 DPI 都清晰） */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "theme.h"

#include <windows.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* 一片花瓣：绕中心旋转的椭圆轮廓；angDeg 是 0=朝上 */
static void petal_points(POINT *pts, int n, int cx, int cy, int R, double angDeg)
{
    /* 花瓣沿半径方向的长椭圆：中心在距圆心 0.55R 处 */
    double a = R * 0.30;                  /* 半宽 */
    double b = R * 0.50;                  /* 半长 */
    double pcx = 0.0, pcy = -R * 0.55;    /* 花瓣中心（未旋转，朝上） */
    double rad = angDeg * M_PI / 180.0;
    double cs = cos(rad), sn = sin(rad);
    int i;

    for (i = 0; i < n; i++) {
        double t = 2 * M_PI * i / n;
        double x = a * cos(t);
        double y = b * sin(t);
        double rx = x * cs - y * sn;
        double ry = x * sn + y * cs;
        pts[i].x = cx + (int)(pcx + rx + 0.5);
        pts[i].y = cy + (int)(pcy + ry + 0.5);
    }
}

void theme_draw_flower(HDC dc, int cx, int cy, int R, COLORREF color)
{
    HBRUSH br = CreateSolidBrush(color);
    HGDIOBJ ob = SelectObject(dc, br);
    HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
    POINT pts[28];
    int k;

    for (k = 0; k < 5; k++)
        petal_points(pts, 28, cx, cy, R, 72.0 * k);

    /* 一片一片画（每片单独 Polygon，避免连成一块怪形状） */
    for (k = 0; k < 5; k++) {
        petal_points(pts, 28, cx, cy, R, 72.0 * k);
        Polygon(dc, pts, 28);
    }
    /* 花心补圆，让五瓣连成一体 */
    {
        int r0 = (int)(R * 0.30 + 0.5);
        Ellipse(dc, cx - r0, cy - r0, cx + r0, cy + r0);
    }

    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(br);
}

void theme_draw_badge(HDC dc, int cx, int cy, int R, COLORREF blue, COLORREF white)
{
    HBRUSH bBlue = CreateSolidBrush(blue);
    HBRUSH bWhite = CreateSolidBrush(white);
    HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
    int r;

    /* 外蓝环 */
    SelectObject(dc, bBlue);
    Ellipse(dc, cx - R, cy - R, cx + R, cy + R);
    /* 白环 */
    r = (int)(R * 0.865 + 0.5);
    SelectObject(dc, bWhite);
    Ellipse(dc, cx - r, cy - r, cx + r, cy + r);
    /* 内蓝盘 */
    r = (int)(R * 0.615 + 0.5);
    SelectObject(dc, bBlue);
    Ellipse(dc, cx - r, cy - r, cx + r, cy + r);

    /* 白花 */
    theme_draw_flower(dc, cx, cy, (int)(R * 0.62 + 0.5), white);

    SelectObject(dc, op);
    DeleteObject(bBlue);
    DeleteObject(bWhite);
}

/* 设计稿里的标志：一个描边圆 + 深蓝底 + 白色花（花瓣是实心白，中间留深蓝）
   这里用"白花上面再压一层缩小一圈的底色花"来做出描边效果 */
void theme_draw_badge_outline(HDC dc, int cx, int cy, int R, int ringW,
                              COLORREF line, COLORREF fill, COLORREF petal)
{
    HBRUSH bLine = CreateSolidBrush(line);
    HBRUSH bFill = CreateSolidBrush(fill);
    HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
    int r;

    /* 外圈线 */
    SelectObject(dc, bLine);
    Ellipse(dc, cx - R, cy - R, cx + R, cy + R);
    /* 里面填底色 */
    r = R - ringW;
    SelectObject(dc, bFill);
    Ellipse(dc, cx - r, cy - r, cx + r, cy + r);

    /* 花瓣：先画一圈白（外轮廓），再画一圈底色（内芯），形成描边花瓣 */
    theme_draw_flower(dc, cx, cy, (int)(r * 0.78 + 0.5), petal);
    theme_draw_flower(dc, cx, cy, (int)(r * 0.55 + 0.5), fill);

    SelectObject(dc, op);
    DeleteObject(bLine);
    DeleteObject(bFill);
}

/* 竖向渐变：用 TRIVERTEX + GradientFill（msimg32），比逐行 FillRect 快 */
void theme_fill_vgrad(HDC dc, const RECT *rc, COLORREF top, COLORREF bot)
{
    TRIVERTEX v[2];
    GRADIENT_RECT g;

    if (!rc || rc->right <= rc->left || rc->bottom <= rc->top)
        return;

    v[0].x = rc->left;              v[0].y = rc->top;
    v[0].Red   = (COLOR16)(GetRValue(top) << 8);
    v[0].Green = (COLOR16)(GetGValue(top) << 8);
    v[0].Blue  = (COLOR16)(GetBValue(top) << 8);
    v[0].Alpha = 0;

    v[1].x = rc->right;             v[1].y = rc->bottom;
    v[1].Red   = (COLOR16)(GetRValue(bot) << 8);
    v[1].Green = (COLOR16)(GetGValue(bot) << 8);
    v[1].Blue  = (COLOR16)(GetBValue(bot) << 8);
    v[1].Alpha = 0;

    g.UpperLeft = 0;
    g.LowerRight = 1;
    GradientFill(dc, v, 2, &g, 1, GRADIENT_FILL_RECT_V);
}
