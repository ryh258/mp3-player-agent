/* nowplaying.c —— 正在播放页（自绘黑胶唱片 + 旋转动画）
 *
 * 全部 GDI 手绘，不依赖任何图片资源：
 *   唱片 —— 径向渐变的黑胶盘 + 同心纹路 + 中间标签（贴专辑封面）+ 中心孔
 *   旋转 —— 定时器推进角度，标签里的封面跟着转（同心圆转起来看不出来，
 *           所以额外画一道会转的高光扫过，视觉上才有"在转"的感觉）
 *   唱臂 —— 从右上斜伸到盘面，带配重和唱头
 *   控件 —— 随机 / 上一首 / 播放暂停 / 下一首 / 循环，外加心形收藏与列表
 */
#include "nowplaying.h"
#include "app.h"
#include "theme.h"
#include "cover.h"
#include "tag_id3.h"
#include "util.h"

#include <windows.h>
#include <windowsx.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define NP_TIMER 0x6E50

typedef struct NpData {
    App    *app;
    int     angle;          /* 唱盘角度，单位 0.1 度 */
    int     dpi;
    HBITMAP cover;          /* 当前曲目的封面（标签用） */
    int     coverSize;
    wchar_t coverPath[MAX_PATH];
    int     hot;            /* 鼠标悬停的按钮 */
    RECT    hit[8];         /* 各控件命中区 */
} NpData;

#define NP_SHUFFLE 0
#define NP_PREV    1
#define NP_PLAY    2
#define NP_NEXT    3
#define NP_REPEAT  4
#define NP_FAV     5
#define NP_LIST    6
#define NP_COUNT   7

static int SC(NpData *d, int px) { return MulDiv(px, d->dpi ? d->dpi : 96, 96); }

/* ---- 小工具 ---- */

static void fill_vgrad(HDC dc, const RECT *rc, COLORREF top, COLORREF bot)
{
    TRIVERTEX v[2];
    GRADIENT_RECT g;
    v[0].x = rc->left;  v[0].y = rc->top;
    v[0].Red = (COLOR16)(GetRValue(top) << 8);
    v[0].Green = (COLOR16)(GetGValue(top) << 8);
    v[0].Blue = (COLOR16)(GetBValue(top) << 8);
    v[0].Alpha = 0;
    v[1].x = rc->right; v[1].y = rc->bottom;
    v[1].Red = (COLOR16)(GetRValue(bot) << 8);
    v[1].Green = (COLOR16)(GetGValue(bot) << 8);
    v[1].Blue = (COLOR16)(GetBValue(bot) << 8);
    v[1].Alpha = 0;
    g.UpperLeft = 0; g.LowerRight = 1;
    GradientFill(dc, v, 2, &g, 1, GRADIENT_FILL_RECT_V);
}

/* 把一张位图绕着 (cx,cy) 旋转 angDeg 后贴到 dc（用于标签里的封面） */
static void draw_rotated(HDC dc, HBITMAP src, int cx, int cy, int size, double angDeg)
{
    HDC m1, m2;
    HGDIOBJ o1, o2;
    XFORM xf;
    double rad = angDeg * M_PI / 180.0;
    HBITMAP tmp;
    void *bitsRef = NULL;
    int i;

    if (!src) return;

    m1 = CreateCompatibleDC(dc);
    o1 = SelectObject(m1, src);
    /* 用 32 位 DIB 当中间缓冲，并在拷贝后把 alpha 刷成 255 —— 
       BitBlt 不写 alpha，否则旋转粘贴会整片透明。 */
    {
        BITMAPINFO bi;
        void *bits = NULL;
        ZeroMemory(&bi, sizeof(bi));
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = size;
        bi.bmiHeader.biHeight = -size;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        tmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
        bitsRef = bits;
        if (tmp && bits) {
            unsigned char *q = (unsigned char *)bits;
            int k;
            for (k = 0; k < size * size; k++) q[k * 4 + 3] = 255;
        }
    }
    m2 = CreateCompatibleDC(dc);
    o2 = SelectObject(m2, tmp);

    /* 圆外区域涂成深色，免得旋转后露出四角 */
    {
        HBRUSH b = CreateSolidBrush(RGB(10, 42, 84));
        RECT r = { 0, 0, size, size };
        FillRect(m2, &r, b);
        DeleteObject(b);
    }
    {
        /* 用三次旋转近似：这里直接一次性旋转贴图 */
        HBRUSH hole = CreateSolidBrush(RGB(20, 60, 120));
        HRGN rgn = CreateEllipticRgn(0, 0, size, size);
        SelectClipRgn(m2, rgn);
        DeleteObject(rgn);
        DeleteObject(hole);
    }

    /* 先把 src 拷进中间缓冲（调用方已保证同尺寸），再补 alpha */
    BitBlt(m2, 0, 0, size, size, m1, 0, 0, SRCCOPY);
    if (bitsRef) {
        unsigned char *q = (unsigned char *)bitsRef;
        int k;
        for (k = 0; k < size * size; k++) q[k * 4 + 3] = 255;
    }

    /* 绕中心旋转贴到目标 */
    SetGraphicsMode(dc, GM_ADVANCED);
    xf.eM11 = (FLOAT)cos(rad);
    xf.eM12 = (FLOAT)sin(rad);
    xf.eM21 = (FLOAT)-sin(rad);
    xf.eM22 = (FLOAT)cos(rad);
    xf.eDx = (FLOAT)(cx - (cos(rad) * cx - sin(rad) * cy));
    xf.eDy = (FLOAT)(cy - (sin(rad) * cx + cos(rad) * cy));
    SetWorldTransform(dc, &xf);

    {
        HRGN rgn = CreateEllipticRgn(cx - size / 2, cy - size / 2,
                                     cx + size / 2, cy + size / 2);
        SelectClipRgn(dc, rgn);
        DeleteObject(rgn);
    }
    BitBlt(dc, cx - size / 2, cy - size / 2, size, size, m2, 0, 0, SRCCOPY);

    SelectClipRgn(dc, NULL);
    ModifyWorldTransform(dc, NULL, MWT_IDENTITY);
    SetGraphicsMode(dc, GM_COMPATIBLE);

    SelectObject(m1, o1); DeleteDC(m1);
    SelectObject(m2, o2); DeleteDC(m2);
    DeleteObject(tmp);
    (void)i;
}

/* 取当前曲目的封面（按路径缓存） */
static void update_cover(NpData *d)
{
    const wchar_t *path = NULL;

    if (!d->app) return;
    if (d->app->current >= 0 && d->app->current < d->app->pl.count)
        path = d->app->pl.items[d->app->current].path;

    if (!path) {
        if (d->cover) { DeleteObject(d->cover); d->cover = NULL; }
        d->coverPath[0] = 0;
        return;
    }
    if (d->cover && wcscmp(d->coverPath, path) == 0)
        return;

    if (d->cover) { DeleteObject(d->cover); d->cover = NULL; }
    wcsncpy(d->coverPath, path, MAX_PATH - 1);
    d->coverPath[MAX_PATH - 1] = 0;

    {
        unsigned char *data = NULL;
        unsigned len = 0;
        if (id3_cover(path, &data, &len) && len > 32) {
            d->cover = cover_from_memory(data, (int)len, d->coverSize);
            free(data);
        }
    }
}

/* ---- 绘制 ---- */

static void draw_disc(HDC dc, NpData *d, int cx, int cy, int R)
{
    int i;
    double ang = d->angle / 10.0;

    /* 盘身：深色圆 + 一圈稍亮的边 */
    {
        HBRUSH b = CreateSolidBrush(RGB(16, 18, 22));
        HGDIOBJ ob = SelectObject(dc, b);
        HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, cx - R, cy - R, cx + R, cy + R);
        SelectObject(dc, ob);
        SelectObject(dc, op);
        DeleteObject(b);
    }
    {
        HPEN p = CreatePen(PS_SOLID, SC(d, 2), RGB(58, 64, 74));
        HGDIOBJ op = SelectObject(dc, p);
        HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Ellipse(dc, cx - R + 1, cy - R + 1, cx + R - 1, cy + R - 1);
        SelectObject(dc, op);
        SelectObject(dc, ob);
        DeleteObject(p);
    }

    /* 同心纹路 */
    {
        HPEN p = CreatePen(PS_SOLID, 1, RGB(38, 42, 50));
        HGDIOBJ op = SelectObject(dc, p);
        HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        for (i = 1; i <= 16; i++) {
            int r = R * (40 + i * 3) / 100;
            if (r >= R - SC(d, 3)) break;
            Ellipse(dc, cx - r, cy - r, cx + r, cy + r);
        }
        SelectObject(dc, op);
        SelectObject(dc, ob);
        DeleteObject(p);
    }

    /* 标签（中间那张圆）+ 里面的封面，封面跟着角度转 */
    {
        int lr = R * 36 / 100;
        HBRUSH b = CreateSolidBrush(TH_BLUE);
        HGDIOBJ ob = SelectObject(dc, b);
        HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, cx - lr, cy - lr, cx + lr, cy + lr);
        SelectObject(dc, ob);
        SelectObject(dc, op);
        DeleteObject(b);

        if (d->cover) {
            int cs = lr * 2 - SC(d, 6);
            if (cs > 8 && cs <= d->coverSize + 2)
                draw_rotated(dc, d->cover, cx, cy, cs, ang);
        } else {
            /* 没封面就画校徽风格的白花 */
            theme_draw_flower(dc, cx, cy, lr * 3 / 4, TH_ON_DARK);
        }
    }

    /* 中心孔 */
    {
        int hr = R * 4 / 100;
        HBRUSH b = CreateSolidBrush(RGB(12, 14, 18));
        HGDIOBJ ob = SelectObject(dc, b);
        HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, cx - hr, cy - hr, cx + hr, cy + hr);
        SelectObject(dc, ob);
        SelectObject(dc, op);
        DeleteObject(b);
    }

    /* 会跟着转的高光扫过 —— 同心圆本身转起来看不出，靠这道光才像在转 */
    {
        double a0 = ang * M_PI / 180.0;
        int rr = R - SC(d, 6);
        HPEN p = CreatePen(PS_SOLID, SC(d, 3), RGB(96, 104, 118));
        HGDIOBJ op = SelectObject(dc, p);
        HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        int x1 = cx + (int)(cos(a0) * (R * 0.42));
        int y1 = cy + (int)(sin(a0) * (R * 0.42));
        int x2 = cx + (int)(cos(a0) * rr);
        int y2 = cy + (int)(sin(a0) * rr);
        MoveToEx(dc, x1, y1, NULL);
        LineTo(dc, x2, y2);
        SelectObject(dc, op);
        SelectObject(dc, ob);
        DeleteObject(p);
    }
}

static void draw_tonearm(HDC dc, NpData *d, int cx, int cy, int R)
{
    int px = cx + R + SC(d, 30);
    int py = cy - R - SC(d, 18);
    int ex = cx + (int)(R * 0.30);
    int ey = cy - (int)(R * 0.62);
    HPEN p;
    HGDIOBJ op, ob;

    /* 臂杆 */
    p = CreatePen(PS_SOLID, SC(d, 5), RGB(206, 212, 222));
    op = SelectObject(dc, p);
    MoveToEx(dc, px, py, NULL);
    LineTo(dc, ex, ey);
    SelectObject(dc, op);
    DeleteObject(p);

    /* 转轴 */
    {
        HBRUSH b = CreateSolidBrush(RGB(226, 232, 240));
        ob = SelectObject(dc, b);
        op = SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, px - SC(d, 8), py - SC(d, 8), px + SC(d, 8), py + SC(d, 8));
        SelectObject(dc, ob);
        SelectObject(dc, op);
        DeleteObject(b);
    }
    /* 唱头 */
    {
        HBRUSH b = CreateSolidBrush(RGB(238, 242, 248));
        ob = SelectObject(dc, b);
        op = SelectObject(dc, GetStockObject(NULL_PEN));
        RoundRect(dc, ex - SC(d, 9), ey - SC(d, 7),
                  ex + SC(d, 9), ey + SC(d, 7), SC(d, 4), SC(d, 4));
        SelectObject(dc, ob);
        SelectObject(dc, op);
        DeleteObject(b);
    }
}

/* 画一个图标按钮 */
static void draw_btn(HDC dc, NpData *d, int which, RECT rc, int active)
{
    int cx = (rc.left + rc.right) / 2;
    int cy = (rc.top + rc.bottom) / 2;
    int r = (rc.bottom - rc.top) / 2 - SC(d, 6);
    COLORREF col = active ? TH_ON_DARK : RGB(206, 216, 232);
    HPEN p = CreatePen(PS_SOLID, SC(d, 2), col);
    HGDIOBJ op = SelectObject(dc, p);
    HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    HBRUSH br = CreateSolidBrush(col);
    int i;

    switch (which) {
    case NP_SHUFFLE:
        MoveToEx(dc, cx - r, cy - r / 2, NULL);
        LineTo(dc, cx + r, cy - r / 2);
        MoveToEx(dc, cx - r, cy + r / 2, NULL);
        LineTo(dc, cx + r, cy + r / 2);
        LineTo(dc, cx + r - 3, cy + r / 2 - 4);
        MoveToEx(dc, cx + r, cy + r / 2, NULL);
        LineTo(dc, cx + r - 3, cy + r / 2 + 4);
        break;
    case NP_REPEAT:
        Ellipse(dc, cx - r, cy - r, cx + r, cy + r);
        MoveToEx(dc, cx + r - 2, cy - r + 4, NULL);
        LineTo(dc, cx + r - 6, cy - r + 2);
        break;
    case NP_PREV:
    case NP_NEXT: {
        int dir = (which == NP_NEXT) ? 1 : -1;
        POINT t[3];
        t[0].x = cx + dir * r;          t[0].y = cy - r;
        t[1].x = cx + dir * r;          t[1].y = cy + r;
        t[2].x = cx - dir * r;          t[2].y = cy;
        SelectObject(dc, br);
        SelectObject(dc, GetStockObject(NULL_PEN));
        Polygon(dc, t, 3);
        {
            RECT s;
            s.left = (dir > 0) ? cx + r - SC(d, 2) : cx - r;
            s.right = s.left + SC(d, 3);
            s.top = cy - r; s.bottom = cy + r;
            SelectObject(dc, br);
            SelectObject(dc, GetStockObject(NULL_PEN));
            Rectangle(dc, s.left, s.top, s.right, s.bottom);
        }
        break;
    }
    case NP_PLAY: {
        /* 大圆钮 */
        HBRUSH wb = CreateSolidBrush(TH_ON_DARK);
        HGDIOBJ ob2 = SelectObject(dc, wb);
        SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, cx - r - SC(d, 8), cy - r - SC(d, 8),
                    cx + r + SC(d, 8), cy + r + SC(d, 8));
        SelectObject(dc, ob2);
        DeleteObject(wb);
        {
            POINT t[3];
            HBRUSH db = CreateSolidBrush(RGB(16, 52, 100));
            SelectObject(dc, db);
            SelectObject(dc, GetStockObject(NULL_PEN));
            if (d->app && app_is_playing(d->app)) {
                RECT s;
                s.top = cy - r / 2; s.bottom = cy + r / 2;
                s.left = cx - r / 2 - 2; s.right = s.left + r / 3;
                Rectangle(dc, s.left, s.top, s.right, s.bottom);
                s.left = cx + r / 6; s.right = s.left + r / 3;
                Rectangle(dc, s.left, s.top, s.right, s.bottom);
            } else {
                t[0].x = cx - r / 2; t[0].y = cy - r / 2 - 2;
                t[1].x = cx - r / 2; t[1].y = cy + r / 2 + 2;
                t[2].x = cx + r / 2 + 4; t[2].y = cy;
                Polygon(dc, t, 3);
            }
            DeleteObject(db);
        }
        break;
    }
    case NP_FAV: {
        int fav = (d->app && d->app->current >= 0 &&
                   d->app->current < d->app->pl.count &&
                   app_is_favorite(d->app, d->app->pl.items[d->app->current].path));
        POINT t[10];
        int k;
        double s = r / 16.0;
        for (k = 0; k < 10; k++) {
            double tt = -M_PI / 2 + k * M_PI / 5;
            double rr = (k % 2 == 0) ? 1.0 : 0.42;
            t[k].x = cx + (int)(cos(tt) * r * rr);
            t[k].y = cy + (int)(sin(tt) * r * rr);
        }
        (void)s;
        if (fav) {
            SelectObject(dc, br);
            SelectObject(dc, GetStockObject(NULL_PEN));
        } else {
            SelectObject(dc, GetStockObject(NULL_BRUSH));
            SelectObject(dc, p);
        }
        Polygon(dc, t, 10);
        break;
    }
    case NP_LIST:
        for (i = -1; i <= 1; i++) {
            MoveToEx(dc, cx - r, cy + i * r * 2 / 3, NULL);
            LineTo(dc, cx + r, cy + i * r * 2 / 3);
        }
        break;
    }

    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(p);
    DeleteObject(br);
}

static void paint(HWND hwnd, NpData *d)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    HDC mem;
    HBITMAP bmp, obmp;
    int W, H, i;

    GetClientRect(hwnd, &rc);
    W = rc.right; H = rc.bottom;

    mem = CreateCompatibleDC(dc);
    bmp = CreateCompatibleBitmap(dc, W, H);
    obmp = (HBITMAP)SelectObject(mem, bmp);

    fill_vgrad(mem, &rc, RGB(13, 56, 106), RGB(7, 28, 58));
    SetBkMode(mem, TRANSPARENT);

    update_cover(d);

    /* 唱片 + 唱臂 */
    {
        int top = SC(d, 10);
        int bot = H - SC(d, 190);
        int R = (W < (bot - top) ? W : (bot - top)) / 2 - SC(d, 24);
        int cx = W / 2;
        int cy = top + (bot - top) / 2;

        if (R < SC(d, 40)) R = SC(d, 40);
        if (R > SC(d, 150)) R = SC(d, 150);

        /* 盘后面一圈淡淡的光晕 */
        {
            HBRUSH b = CreateSolidBrush(RGB(18, 62, 112));
            HGDIOBJ ob = SelectObject(mem, b);
            HGDIOBJ op = SelectObject(mem, GetStockObject(NULL_PEN));
            Ellipse(mem, cx - R - SC(d, 14), cy - R - SC(d, 14),
                        cx + R + SC(d, 14), cy + R + SC(d, 14));
            SelectObject(mem, ob);
            SelectObject(mem, op);
            DeleteObject(b);
        }

        draw_disc(mem, d, cx, cy, R);
        draw_tonearm(mem, d, cx, cy, R);
    }

    /* 歌名 / 歌手 */
    {
        const wchar_t *title = L"未在播放";
        const wchar_t *artist = L"双击列表里的歌曲开始播放";
        RECT r;

        if (d->app && d->app->current >= 0 && d->app->current < d->app->pl.count) {
            Track *t = &d->app->pl.items[d->app->current];
            title = t->title[0] ? t->title : util_basename(t->path);
            artist = t->artist[0] ? t->artist : L"未知歌手";
        }

        r.left = SC(d, 20); r.right = W - SC(d, 20);
        r.top = H - SC(d, 176); r.bottom = H - SC(d, 140);
        SetTextColor(mem, TH_ON_DARK);
        {
            HFONT f = CreateFontW(-SC(d, 26), 0, 0, 0, FW_BOLD, 0, 0, 0,
                                  DEFAULT_CHARSET, 0, 0, 0, 0, L"Microsoft YaHei UI");
            HGDIOBJ of = SelectObject(mem, f);
            DrawTextW(mem, title, -1, &r,
                      DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            SelectObject(mem, of);
            DeleteObject(f);
        }
        r.top = H - SC(d, 140); r.bottom = H - SC(d, 116);
        SetTextColor(mem, TH_ON_DARK_DIM);
        {
            HFONT f = CreateFontW(-SC(d, 15), 0, 0, 0, FW_NORMAL, 0, 0, 0,
                                  DEFAULT_CHARSET, 0, 0, 0, 0, L"Microsoft YaHei UI");
            HGDIOBJ of = SelectObject(mem, f);
            DrawTextW(mem, artist, -1, &r,
                      DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            SelectObject(mem, of);
            DeleteObject(f);
        }
    }

    /* 进度条 + 时间 */
    {
        int cur = 0, total = 0;
        RECT pb;
        wchar_t sb[32];

        if (d->app) {
            if (d->app->current >= 0 && d->app->current < d->app->pl.count)
                total = d->app->pl.items[d->app->current].length_ms;
            if (total <= 0) total = app_current_length(d->app);
            cur = app_current_position(d->app);
        }
        if (cur < 0) cur = 0;
        if (total < 0) total = 0;

        pb.left = SC(d, 26); pb.right = W - SC(d, 26);
        pb.top = H - SC(d, 100); pb.bottom = pb.top + SC(d, 4);

        {
            HBRUSH b = CreateSolidBrush(RGB(46, 84, 134));
            RECT r = pb;
            HGDIOBJ ob = SelectObject(mem, b);
            HGDIOBJ op = SelectObject(mem, GetStockObject(NULL_PEN));
            r.bottom = pb.top + SC(d, 4);
            RoundRect(mem, r.left, pb.top, r.right, pb.bottom, 4, 4);
            SelectObject(mem, ob);
            SelectObject(mem, op);
            DeleteObject(b);
        }
        if (total > 0) {
            HBRUSH b = CreateSolidBrush(TH_ON_DARK);
            RECT r = pb;
            HGDIOBJ ob, op;
            r.right = pb.left + (int)((double)(pb.right - pb.left) * cur / total);
            if (r.right < pb.left + 3) r.right = pb.left + 3;
            ob = SelectObject(mem, b);
            op = SelectObject(mem, GetStockObject(NULL_PEN));
            RoundRect(mem, r.left, pb.top, r.right, pb.bottom, 4, 4);
            SelectObject(mem, ob);
            SelectObject(mem, op);
            DeleteObject(b);
        }

        SetTextColor(mem, TH_ON_DARK_DIM);
        {
            HFONT f = CreateFontW(-SC(d, 13), 0, 0, 0, FW_NORMAL, 0, 0, 0,
                                  DEFAULT_CHARSET, 0, 0, 0, 0, L"Microsoft YaHei UI");
            HGDIOBJ of = SelectObject(mem, f);
            RECT lr = { SC(d, 26), H - SC(d, 90), W / 2, H - SC(d, 70) };
            RECT rr2 = { W / 2, H - SC(d, 90), W - SC(d, 26), H - SC(d, 70) };
            util_format_time(cur, sb, 32);
            DrawTextW(mem, sb, -1, &lr, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            util_format_time(total, sb, 32);
            DrawTextW(mem, sb, -1, &rr2, DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            SelectObject(mem, of);
            DeleteObject(f);
        }
    }

    /* 一排控件 */
    {
        int bw = W / 8;
        int cy = H - SC(d, 48);
        int order[5] = { NP_SHUFFLE, NP_PREV, NP_PLAY, NP_NEXT, NP_REPEAT };
        int startX = (W - bw * 5) / 2;

        for (i = 0; i < 5; i++) {
            RECT r;
            r.left = startX + i * bw;
            r.right = r.left + bw;
            r.top = cy - SC(d, 22);
            r.bottom = cy + SC(d, 22);
            d->hit[order[i]] = r;
            draw_btn(mem, d, order[i], r, 0);
        }

        /* 左下心形、右下列表 */
        {
            RECT r;
            r.left = SC(d, 20); r.right = r.left + SC(d, 40);
            r.top = H - SC(d, 34) - SC(d, 16); r.bottom = H - SC(d, 34) + SC(d, 16);
            d->hit[NP_FAV] = r;
            draw_btn(mem, d, NP_FAV, r, 0);

            r.left = W - SC(d, 60); r.right = W - SC(d, 20);
            d->hit[NP_LIST] = r;
            draw_btn(mem, d, NP_LIST, r, 0);
        }
    }

    BitBlt(dc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
    SelectObject(mem, obmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

/* ---- 窗口过程 ---- */

static LRESULT CALLBACK NpProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    NpData *d = (NpData *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        HDC dc = GetDC(hwnd);
        d = (NpData *)calloc(1, sizeof(NpData));
        if (!d) return -1;
        d->dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
        if (dc) ReleaseDC(hwnd, dc);
        d->coverSize = SC(d, 128);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)d);
        SetTimer(hwnd, NP_TIMER, 33, NULL);
        return 0;
    }

    case WM_TIMER:
        if (d && wp == NP_TIMER) {
            if (d->app && app_is_playing(d->app)) {
                d->angle = (d->angle + 25) % 3600;    /* 约 2.5 秒一圈 */
                {
                    RECT rc;
                    GetClientRect(hwnd, &rc);
                    InvalidateRect(hwnd, &rc, FALSE);
                }
            } else {
                /* 没在播放就不重绘，省电 */
                static int lastAngle = -1;
                if (lastAngle != d->angle) { lastAngle = d->angle; InvalidateRect(hwnd, NULL, FALSE); }
            }
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        int i;
        POINT pt = { x, y };
        if (!d || !d->app) return 0;
        for (i = 0; i < NP_COUNT; i++) {
            if (PtInRect(&d->hit[i], pt)) {
                HWND par = GetParent(hwnd);
                switch (i) {
                case NP_SHUFFLE: SendMessageW(par, WM_COMMAND, IDM_MODE_SHUFFLE, 0); break;
                case NP_PREV:    SendMessageW(par, WM_COMMAND, IDM_PLAY_PREV, 0); break;
                case NP_PLAY:    SendMessageW(par, WM_COMMAND, IDM_PLAY_TOGGLE, 0); break;
                case NP_NEXT:    SendMessageW(par, WM_COMMAND, IDM_PLAY_NEXT, 0); break;
                case NP_REPEAT:  SendMessageW(par, WM_COMMAND, IDM_MODE_LOOP_ALL, 0); break;
                case NP_FAV:     SendMessageW(par, WM_COMMAND, IDM_FAV_TOGGLE, 0); break;
                case NP_LIST:    SendMessageW(par, WM_COMMAND, IDM_VIEW_LIBRARY, 0); break;
                }
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
        }
        return 0;
    }

    case WM_PAINT:
        if (d) paint(hwnd, d);
        else {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc; GetClientRect(hwnd, &rc);
            HBRUSH b = CreateSolidBrush(RGB(10, 40, 80));
            FillRect(dc, &rc, b);
            DeleteObject(b);
            EndPaint(hwnd, &ps);
        }
        return 0;

    case WM_NCDESTROY:
        KillTimer(hwnd, NP_TIMER);
        if (d) {
            if (d->cover) DeleteObject(d->cover);
            free(d);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void nowplaying_register_class(HINSTANCE hi)
{
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = NpProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = NOWPLAYING_CLASS;
    RegisterClassExW(&wc);
}

void nowplaying_set_app(HWND h, App *app)
{
    NpData *d = (NpData *)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (d) {
        d->app = app;
        d->coverPath[0] = 0;      /* 强制下次重取封面 */
    }
}
