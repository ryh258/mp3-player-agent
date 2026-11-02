/* nav.c —— 左侧导航栏（自绘控件）
 *
 * 视觉：浅灰底 + 选中项白色圆角浮起卡片 + 左侧强调色竖条。
 * 图标全部用 GDI 现画（音符 / 列表 / 时钟），这样任意 DPI 都清晰，
 * 也不需要额外的图片资源。
 */
/* 让本文件不依赖编译命令行的 -municode：自己保证走宽字符版 API */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "nav.h"
#include "theme.h"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <stdlib.h>
#include <wchar.h>
#include <math.h>

/* ---- 配色（设计稿：深蓝侧栏 + 白字） ---- */
#define CLR_BG        TH_NAV_TOP     /* 渐变由 theme_fill_vgrad 负责，这里只作兜底 */
#define CLR_SEL       TH_NAV_SEL
#define CLR_HOVER     TH_NAV_HOVER
#define CLR_TEXT      TH_ON_DARK
#define CLR_TEXT_SEL  TH_ON_DARK
#define CLR_TEXT_SUB  TH_ON_DARK_DIM
#define CLR_ACCENT    TH_WHITE       /* 选中项左侧的白条 */
#define CLR_LINE      TH_TOOLBAR_LN
#define CLR_TITLE     TH_ON_DARK

#define NAV_HEADER_H  96      /* 侧栏顶部品牌区高度（设计尺寸） */

typedef struct NavData {
    NavItem items[NAV_MAX_ITEMS];
    int     count;
    int     sel;
    int     hover;
    int     dpi;
    HFONT   font;
    HFONT   fontSel;
    HFONT   fontTitle;
} NavData;

static int SC(NavData *d, int px)
{
    int dpi = d->dpi ? d->dpi : 96;
    return MulDiv(px, dpi, 96);
}

/* ---------------- 图标（纯 GDI 矢量） ---------------- */

static void draw_icon(HDC dc, int kind, int cx, int cy, int size, COLORREF clr)
{
    HBRUSH br = CreateSolidBrush(clr);
    HPEN   pen = CreatePen(PS_SOLID, size <= 12 ? 1 : 2, clr);
    HGDIOBJ ob = SelectObject(dc, br);
    HGDIOBJ op = SelectObject(dc, pen);
    int r = size / 2;

    switch (kind) {
    case NAV_ICON_SONG: {
        /* 音符：符头 + 符干 + 符尾 */
        int hr = r / 2 + 1;
        int hx = cx - r / 3;
        int hy = cy + r - hr;
        int sx = hx + hr - 1;
        int top = cy - r;
        POINT flag[4];
        Ellipse(dc, hx - hr, hy - hr, hx + hr, hy + hr);
        Rectangle(dc, sx - 2, top, sx + 1, hy);
        flag[0].x = sx;      flag[0].y = top;
        flag[1].x = cx + r;  flag[1].y = top + r;
        flag[2].x = cx + r;  flag[2].y = top + r + 3;
        flag[3].x = sx;      flag[3].y = top + r - 2;
        Polygon(dc, flag, 4);
        break;
    }
    case NAV_ICON_LIST:
    case NAV_ICON_PLAYLIST: {
        /* 列表：三条横线 + 三个小方块 */
        int i, gap = (r * 2) / 3;
        for (i = 0; i < 3; i++) {
            int y = cy - r + i * gap + gap / 2;
            Rectangle(dc, cx - r, y - 2, cx - r + 3, y + 1);
            Rectangle(dc, cx - r + 6, y - 2, cx + r, y + 1);
        }
        break;
    }
    case NAV_ICON_CLOCK: {
        /* 时钟：圆 + 两根指针 */
        SelectObject(dc, GetStockObject(NULL_BRUSH));
        Ellipse(dc, cx - r, cy - r, cx + r, cy + r);
        SelectObject(dc, br);
        MoveToEx(dc, cx, cy, NULL);
        LineTo(dc, cx, cy - r + 3);
        MoveToEx(dc, cx, cy, NULL);
        LineTo(dc, cx + r - 3, cy + 1);
        break;
    }
    case NAV_ICON_MIC: {
        /* 话筒：胶囊头 + U 形托架 + 支架 + 底座 */
        int mw = r * 2 / 5;
        int top = cy - r;
        int bot = cy + r / 4;
        RECT cap;

        cap.left = cx - mw; cap.right = cx + mw;
        cap.top = top;      cap.bottom = bot;
        RoundRect(dc, cap.left, cap.top, cap.right, cap.bottom, mw, mw);

        SelectObject(dc, GetStockObject(NULL_BRUSH));
        Arc(dc, cx - r, cy - r, cx + r, cy + r + r / 2, cx - r, cy + r / 4, cx + r, cy + r / 4);
        MoveToEx(dc, cx, cy + r * 3 / 4, NULL);
        LineTo(dc, cx, cy + r);
        MoveToEx(dc, cx - r * 3 / 5, cy + r, NULL);
        LineTo(dc, cx + r * 3 / 5, cy + r);
        break;
    }
    case NAV_ICON_HEART: {
        POINT t[10];
        int k;
        for (k = 0; k < 10; k++) {
            double tt = -3.14159265 / 2 + k * 3.14159265 / 5;
            double rr = (k % 2 == 0) ? 1.0 : 0.42;
            t[k].x = cx + (int)(cos(tt) * r * rr);
            t[k].y = cy + (int)(sin(tt) * r * rr);
        }
        Polygon(dc, t, 10);
        break;
    }
    case NAV_ICON_PLAY: {
        POINT t[3];
        t[0].x = cx - r + 2; t[0].y = cy - r;
        t[1].x = cx - r + 2; t[1].y = cy + r;
        t[2].x = cx + r;     t[2].y = cy;
        Polygon(dc, t, 3);
        break;
    }
    case NAV_ICON_DISC: {
        /* 唱片：外圆 + 内圈 + 中心孔 */
        SelectObject(dc, GetStockObject(NULL_BRUSH));
        Ellipse(dc, cx - r, cy - r, cx + r, cy + r);
        Ellipse(dc, cx - r * 2 / 3, cy - r * 2 / 3, cx + r * 2 / 3, cy + r * 2 / 3);
        SelectObject(dc, br);
        Ellipse(dc, cx - r / 4, cy - r / 4, cx + r / 4, cy + r / 4);
        break;
    }
    default:
        break;
    }

    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(br);
    DeleteObject(pen);
}

/* ---------------- 条目几何 ---------------- */

static int item_height(NavData *d, int idx)
{
    if (idx < 0 || idx >= d->count) return 0;
    return d->items[idx].level ? SC(d, 32) : SC(d, 40);
}

static int item_top(NavData *d, int idx)
{
    int y = SC(d, NAV_HEADER_H), i;       /* 顶部留给品牌区 */
    for (i = 0; i < idx && i < d->count; i++) {
        y += item_height(d, i);
        if (d->items[i].gap) y += SC(d, 12);
    }
    if (idx < d->count && d->items[idx].gap) y += SC(d, 12);
    return y;
}

static int hit_test(NavData *d, int y)
{
    int i;
    for (i = 0; i < d->count; i++) {
        int t = item_top(d, i), h = item_height(d, i);
        if (y >= t && y < t + h) return i;
    }
    return -1;
}

/* ---------------- 绘制 ---------------- */

static void paint(HWND hwnd, NavData *d)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    HDC mem;
    HBITMAP bmp, obmp;
    int i, W, H;

    GetClientRect(hwnd, &rc);
    W = rc.right;
    H = rc.bottom;

    /* 双缓冲，避免闪烁 */
    mem = CreateCompatibleDC(dc);
    bmp = CreateCompatibleBitmap(dc, W, H);
    obmp = (HBITMAP)SelectObject(mem, bmp);

    /* 设计稿是深蓝竖向渐变 */
    theme_fill_vgrad(mem, &rc, TH_NAV_TOP, TH_NAV_BOT);

    SetBkMode(mem, TRANSPARENT);

    /* ---- 顶部品牌区：描边徽章 + "重工程学子 / 重庆工程职业技术大学" ---- */
    {
        int cy2 = SC(d, 46);
        SIZE sz;
        int tx = SC(d, 66);
        RECT tr;

        /* 白圈 + 深蓝底 + 白色描边花（设计稿里的标志样式） */
        theme_draw_badge_outline(mem, SC(d, 36), cy2, SC(d, 22), SC(d, 2),
                                 TH_ON_DARK, TH_NAV_BOT, TH_ON_DARK);

        SelectObject(mem, d->fontTitle ? d->fontTitle : d->font);
        SetTextColor(mem, TH_ON_DARK);
        GetTextExtentPoint32W(mem, L"重工程学子", 5, &sz);

        tr.left = tx;
        tr.right = W - SC(d, 4);
        tr.top = SC(d, 26);
        tr.bottom = SC(d, 54);
        DrawTextW(mem, L"重工程学子", -1, &tr,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(mem, d->font);
        SetTextColor(mem, TH_ON_DARK_DIM);
        tr.left = tx;
        tr.right = W - SC(d, 4);
        tr.top = SC(d, 58);
        tr.bottom = SC(d, 76);
        DrawTextW(mem, L"重庆工程职业技术大学", -1, &tr,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

        /* 分隔线 */
        {
            HPEN p = CreatePen(PS_SOLID, 1, CLR_LINE);
            HGDIOBJ op2 = SelectObject(mem, p);
            MoveToEx(mem, SC(d, 12), SC(d, NAV_HEADER_H) - SC(d, 8), NULL);
            LineTo(mem, W - SC(d, 12), SC(d, NAV_HEADER_H) - SC(d, 8));
            SelectObject(mem, op2);
            DeleteObject(p);
        }
    }

    for (i = 0; i < d->count; i++) {
        NavItem *it = &d->items[i];
        int t = item_top(d, i);
        int hgt = item_height(d, i);
        int isSel = (i == d->sel);
        int isHover = (i == d->hover && !isSel);
        int indent = it->level ? SC(d, 26) : SC(d, 8);
        RECT ir;

        ir.left   = 0;                  /* 设计稿里选中块是通栏的 */
        ir.right  = W;
        ir.top    = t;
        ir.bottom = t + hgt;

        if (isSel) {
            HBRUSH b = CreateSolidBrush(CLR_SEL);
            FillRect(mem, &ir, b);
            DeleteObject(b);

            /* 左边缘的白色指示条（通高） */
            {
                HBRUSH ab = CreateSolidBrush(CLR_ACCENT);
                RECT bar;
                bar.left  = 0;
                bar.right = SC(d, 4);
                bar.top   = ir.top;
                bar.bottom = ir.bottom;
                FillRect(mem, &bar, ab);
                DeleteObject(ab);
            }
        } else if (isHover) {
            HBRUSH b = CreateSolidBrush(CLR_HOVER);
            FillRect(mem, &ir, b);
            DeleteObject(b);
        }

        /* 图标 */
        draw_icon(mem, it->icon,
                  ir.left + indent + SC(d, 14),
                  t + hgt / 2,
                  it->level ? SC(d, 10) : SC(d, 13),
                  isSel ? TH_ON_DARK : (it->level ? CLR_TEXT_SUB : TH_ON_DARK_DIM));

        /* 文字 */
        {
            RECT tr = ir;
            tr.left = ir.left + indent + SC(d, 30);
            tr.right = ir.right - SC(d, 10);
            SelectObject(mem, (isSel && d->fontSel) ? d->fontSel : d->font);
            SetTextColor(mem, isSel ? TH_ON_DARK : (it->level ? CLR_TEXT_SUB : TH_ON_DARK_DIM));
            DrawTextW(mem, it->text, -1, &tr,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
    }

    /* 底部版本信息（设计稿：重庆大工程music v1.0.0） */
    {
        RECT tr;
        SelectObject(mem, d->font);
        SetTextColor(mem, TH_ON_DARK_DIM2);
        tr.left = SC(d, 16);
        tr.right = W - SC(d, 8);
        tr.top = H - SC(d, 34);
        tr.bottom = H - SC(d, 10);
        DrawTextW(mem, L"重庆大工程music v1.0.0", -1, &tr,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }

    /* 右侧分隔线 */
    {
        HPEN p = CreatePen(PS_SOLID, 1, CLR_LINE);
        HGDIOBJ op = SelectObject(mem, p);
        MoveToEx(mem, W - 1, 0, NULL);
        LineTo(mem, W - 1, H);
        SelectObject(mem, op);
        DeleteObject(p);
    }

    BitBlt(dc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
    SelectObject(mem, obmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

/* ---------------- 窗口过程 ---------------- */

static LRESULT CALLBACK nav_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    NavData *d = (NavData *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_NCCREATE:
        d = (NavData *)calloc(1, sizeof(NavData));
        if (d) {
            d->sel = -1;
            d->hover = -1;
            d->dpi = 96;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)d);
        }
        return TRUE;

    case WM_NCDESTROY:
        if (d) {
            if (d->font) DeleteObject(d->font);
            if (d->fontSel) DeleteObject(d->fontSel);
            if (d->fontTitle) DeleteObject(d->fontTitle);
            free(d);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;                    /* 全部在 WM_PAINT 里画，避免闪烁 */

    case WM_PAINT:
        if (d) paint(hwnd, d);
        return 0;

    case WM_MOUSEMOVE: {
        int idx;
        if (!d) break;
        idx = hit_test(d, GET_Y_LPARAM(lp));
        if (idx != d->hover) {
            d->hover = idx;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        {
            TRACKMOUSEEVENT t = { sizeof(t), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&t);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        if (d && d->hover != -1) {
            d->hover = -1;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_LBUTTONDOWN: {
        int idx;
        HWND parent;
        if (!d) break;
        idx = hit_test(d, GET_Y_LPARAM(lp));
        if (idx >= 0 && idx != d->sel) {
            d->sel = idx;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        if (idx >= 0) {
            parent = GetParent(hwnd);
            if (parent)
                SendMessageW(parent, WM_COMMAND,
                             MAKEWPARAM((WORD)idx, NAV_NOTIFY), (LPARAM)hwnd);
        }
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void nav_register_class(HINSTANCE hInst)
{
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = nav_proc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = NAV_CLASS;
    RegisterClassExW(&wc);
}

void nav_set_items(HWND h, const NavItem *items, int n)
{
    NavData *d = (NavData *)GetWindowLongPtrW(h, GWLP_USERDATA);
    int i;
    if (!d) return;
    if (n > NAV_MAX_ITEMS) n = NAV_MAX_ITEMS;
    d->count = n;
    for (i = 0; i < n; i++) {
        d->items[i] = items[i];
        d->items[i].text[NAV_TEXT_MAX - 1] = 0;
    }
    if (d->sel >= n) d->sel = -1;
    InvalidateRect(h, NULL, FALSE);
}

void nav_set_sel(HWND h, int sel)
{
    NavData *d = (NavData *)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (!d) return;
    d->sel = sel;
    InvalidateRect(h, NULL, FALSE);
}

int nav_get_sel(HWND h)
{
    NavData *d = (NavData *)GetWindowLongPtrW(h, GWLP_USERDATA);
    return d ? d->sel : -1;
}

void nav_set_dpi(HWND h, int dpi)
{
    NavData *d = (NavData *)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (d) d->dpi = dpi;
}

void nav_set_font(HWND h, HFONT f)
{
    NavData *d = (NavData *)GetWindowLongPtrW(h, GWLP_USERDATA);
    LOGFONTW lf;
    if (!d) return;

    d->font = f;
    if (GetObjectW(f, sizeof(lf), &lf) == sizeof(lf)) {
        lf.lfWeight = FW_SEMIBOLD;
        if (d->fontSel) DeleteObject(d->fontSel);
        d->fontSel = CreateFontIndirectW(&lf);

        /* 品牌区标题：稍大 + 加粗 */
        lf.lfWeight = FW_BOLD;
        lf.lfHeight = (LONG)((double)lf.lfHeight * 1.25);
        if (d->fontTitle) DeleteObject(d->fontTitle);
        d->fontTitle = CreateFontIndirectW(&lf);
    }
    InvalidateRect(h, NULL, FALSE);
}
