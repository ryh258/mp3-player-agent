/* lyricview.c —— 歌词面板（自绘）
 *
 * 逐行自动换行、按时间高亮、自动把当前行滚到中间、点行跳转。
 */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "lyricview.h"
#include "lrc.h"

#include <windows.h>
#include <windowsx.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define CLR_DEF_BG     RGB(253, 253, 254)
#define CLR_DEF_TEXT   RGB(150, 155, 163)
#define CLR_DEF_CUR    RGB( 30,  34,  42)
#define CLR_DEF_ACCENT RGB( 46, 107, 230)

typedef struct LvData {
    const Lrc *lrc;
    wchar_t    title[256];

    int        cur;          /* 当前高亮行索引 */
    int        timeMs;
    int        scroll;       /* 纵向滚动偏移（像素） */
    int        targetScroll;

    int        dpi;
    HFONT      fNormal;
    HFONT      fCur;
    HFONT      baseFont;

    COLORREF   bg, text, curColor, accent;

    /* 每行的排版结果 */
    int       *lineTop;      /* 行顶 y（相对内容原点） */
    int       *lineH;
    int        layoutW;      /* 布局时用的宽度，宽度变了要重排 */
    int        contentH;
} LvData;

static int SC(LvData *d, int px)
{
    return MulDiv(px, d->dpi ? d->dpi : 96, 96);
}

static void build_fonts(LvData *d)
{
    LOGFONTW lf;

    if (d->fNormal) { DeleteObject(d->fNormal); d->fNormal = NULL; }
    if (d->fCur)    { DeleteObject(d->fCur);    d->fCur = NULL; }

    if (d->baseFont && GetObjectW(d->baseFont, sizeof(lf), &lf) == sizeof(lf)) {
        d->fNormal = CreateFontIndirectW(&lf);
        lf.lfWeight = FW_BOLD;
        lf.lfHeight = (LONG)((double)lf.lfHeight * 1.15);
        d->fCur = CreateFontIndirectW(&lf);
    }
}

/* 重新排版：计算每行的 y 和高度（含自动换行） */
static void relayout(HWND hwnd, LvData *d, HDC dc, int W)
{
    int i;
    int padX = SC(d, 18), padY = SC(d, 16);
    int wrapW = W - padX * 2;
    int y = padY;

    (void)hwnd;

    if (d->lrc && d->lrc->count > 0) {
        if (!d->lineTop || d->layoutW != W) {
            free(d->lineTop);
            free(d->lineH);
            d->lineTop = (int *)malloc((size_t)d->lrc->count * sizeof(int));
            d->lineH   = (int *)malloc((size_t)d->lrc->count * sizeof(int));
            d->layoutW = W;
        }
    }

    if (!d->lrc || d->lrc->count <= 0) {
        d->contentH = 0;
        return;
    }

    for (i = 0; i < d->lrc->count; i++) {
        RECT r;
        int hgt;
        HGDIOBJ old = SelectObject(dc, (i == d->cur && d->fCur) ? d->fCur : d->fNormal);
        r.left = 0; r.right = wrapW; r.top = 0; r.bottom = 0;
        DrawTextW(dc, d->lrc->lines[i].text, -1, &r,
                  DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, old);
        hgt = (r.bottom - r.top) + SC(d, 10);
        d->lineTop[i] = y;
        d->lineH[i] = hgt;
        y += hgt;
    }
    d->contentH = y + padY;
}

static void clamp_scroll(HWND hwnd, LvData *d)
{
    RECT rc;
    int H;
    int maxScroll;
    GetClientRect(hwnd, &rc);
    H = rc.bottom;
    maxScroll = d->contentH - H;
    if (maxScroll < 0) maxScroll = 0;
    if (d->targetScroll > maxScroll) d->targetScroll = maxScroll;
    if (d->targetScroll < 0) d->targetScroll = 0;
}

/* 把当前行滚到面板中间 */
static void center_current(HWND hwnd, LvData *d)
{
    RECT rc;
    int H, mid, y;

    if (!d->lrc || d->cur < 0 || !d->lineTop) {
        d->targetScroll = 0;
        return;
    }
    GetClientRect(hwnd, &rc);
    H = rc.bottom;
    mid = d->lineTop[d->cur] + d->lineH[d->cur] / 2;
    y = mid - H / 2;
    d->targetScroll = y;
    clamp_scroll(hwnd, d);
}

static void paint(HWND hwnd, LvData *d)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    HDC mem;
    HBITMAP bmp, obmp;
    int W, H, i;

    GetClientRect(hwnd, &rc);
    W = rc.right;
    H = rc.bottom;

    mem = CreateCompatibleDC(dc);
    bmp = CreateCompatibleBitmap(dc, W, H);
    obmp = (HBITMAP)SelectObject(mem, bmp);

    {
        HBRUSH b = CreateSolidBrush(d->bg);
        FillRect(mem, &rc, b);
        DeleteObject(b);
    }

    relayout(hwnd, d, mem, W);
    SetBkMode(mem, TRANSPARENT);

    if (!d->lrc || d->lrc->count <= 0) {
        /* 没有歌词：逐行居中绘制。
           注意不能用 DT_CENTER + 行首空格 —— 居中是按整行宽度算的，
           前导空格会把那一行整体右推，看起来像右对齐。 */
        const wchar_t *lines[6];
        int nl = 0, i;
        int lh = MulDiv(24, d->dpi ? d->dpi : 96, 96);
        int y;

        SelectObject(mem, d->fNormal);
        SetTextColor(mem, d->text);

        lines[nl++] = L"没有找到歌词";
        lines[nl++] = L"";
        lines[nl++] = L"把同名的 .lrc 文件放在歌曲旁边即可";
        lines[nl++] = L"例如下面两个文件放在同一个文件夹里：";

        /* 先把前面几行按行高排好，从面板偏上位置开始 */
        y = rc.top + (rc.bottom - rc.top) / 2 - lh * 5;

        for (i = 0; i < nl; i++) {
            RECT r;
            r.left = rc.left + 12;
            r.right = rc.right - 12;
            r.top = y;
            r.bottom = y + lh * 2;
            if (lines[i][0])
                DrawTextW(mem, lines[i], -1, &r,
                          DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            y += lh * 2;
        }

        /* 用一个浅色小方块示意"两个同名文件"，比纯文字清楚 */
        {
            RECT box;
            int bh = lh * 3;
            box.left = rc.left + (rc.right - rc.left) / 2 - 78;
            box.right = box.left + 156;
            box.top = y + 4;
            box.bottom = box.top + bh;
            {
                HBRUSH b = CreateSolidBrush(RGB(238, 243, 250));
                HBRUSH ob = (HBRUSH)SelectObject(mem, b);
                HPEN op = (HPEN)SelectObject(mem, GetStockObject(NULL_PEN));
                RoundRect(mem, box.left, box.top, box.right, box.bottom, 8, 8);
                SelectObject(mem, ob);
                SelectObject(mem, op);
                DeleteObject(b);
            }
            {
                RECT r = box;
                r.top += 4;
                SetTextColor(mem, RGB(90, 104, 122));
                DrawTextW(mem, L"歌曲.mp3\n歌曲.lrc", -1, &r,
                          DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_NOPREFIX);
            }
        }

        BitBlt(dc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
        SelectObject(mem, obmp);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return;
    }

    /* 平滑靠近目标滚动位置 */
    if (d->scroll != d->targetScroll) {
        int diff = d->targetScroll - d->scroll;
        d->scroll += (diff > 0) ? (diff + 3) / 4 : (diff - 3) / 4;
        if (abs(d->targetScroll - d->scroll) < 3)
            d->scroll = d->targetScroll;
    }

    {
        int padX = SC(d, 18);
        for (i = 0; i < d->lrc->count; i++) {
            int y = d->lineTop[i] - d->scroll;
            RECT r;
            int isCur = (i == d->cur);

            if (y + d->lineH[i] < 0 || y > H)
                continue;                       /* 屏幕外不画 */

            r.left = padX;
            r.right = W - padX;
            r.top = y;
            r.bottom = y + d->lineH[i];

            SelectObject(mem, (isCur && d->fCur) ? d->fCur : d->fNormal);
            SetTextColor(mem, isCur ? d->curColor : d->text);
            DrawTextW(mem, d->lrc->lines[i].text, -1, &r,
                      DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
        }
    }

    BitBlt(dc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
    SelectObject(mem, obmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK lv_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    LvData *d = (LvData *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_NCCREATE:
        d = (LvData *)calloc(1, sizeof(LvData));
        if (d) {
            d->cur = -1;
            d->dpi = 96;
            d->bg = CLR_DEF_BG;
            d->text = CLR_DEF_TEXT;
            d->curColor = CLR_DEF_CUR;
            d->accent = CLR_DEF_ACCENT;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)d);
        }
        return TRUE;

    case WM_NCDESTROY:
        if (d) {
            if (d->fNormal) DeleteObject(d->fNormal);
            if (d->fCur) DeleteObject(d->fCur);
            free(d->lineTop);
            free(d->lineH);
            free(d);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        if (d) paint(hwnd, d);
        return 0;

    case WM_SIZE:
        if (d) {
            d->layoutW = 0;                 /* 宽度变了要重排 */
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_MOUSEWHEEL:
        if (d) {
            int delta = GET_WHEEL_DELTA_WPARAM(wp);
            d->targetScroll -= delta * SC(d, 40) / WHEEL_DELTA;
            clamp_scroll(hwnd, d);
            d->scroll = d->targetScroll;    /* 手动滚动不做动画 */
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_LBUTTONDOWN:
        if (d && d->lrc && d->lrc->count > 0 && d->lineTop) {
            int y = GET_Y_LPARAM(lp) + d->scroll;
            int i;
            for (i = 0; i < d->lrc->count; i++) {
                if (y >= d->lineTop[i] && y < d->lineTop[i] + d->lineH[i]) {
                    HWND parent = GetParent(hwnd);
                    if (parent)
                        SendMessageW(parent, WM_COMMAND,
                                     MAKEWPARAM((WORD)i, LYRIC_NOTIFY), (LPARAM)hwnd);
                    break;
                }
            }
        }
        return 0;

    case WM_SETCURSOR:
        SetCursor(LoadCursorW(NULL, IDC_HAND));
        return TRUE;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void lyricview_register_class(HINSTANCE hInst)
{
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = lv_proc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = LYRICVIEW_CLASS;
    RegisterClassExW(&wc);
}

void lyricview_set_lrc(HWND h, const Lrc *lrc, const wchar_t *songTitle)
{
    LvData *d = (LvData *)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (!d) return;
    d->lrc = lrc;
    d->cur = -1;
    d->scroll = 0;
    d->targetScroll = 0;
    d->layoutW = 0;
    free(d->lineTop); d->lineTop = NULL;
    free(d->lineH);   d->lineH = NULL;
    if (songTitle) {
        wcsncpy(d->title, songTitle, 255);
        d->title[255] = 0;
    } else {
        d->title[0] = 0;
    }
    InvalidateRect(h, NULL, FALSE);
}

void lyricview_set_time(HWND h, int ms)
{
    LvData *d = (LvData *)GetWindowLongPtrW(h, GWLP_USERDATA);
    int idx;
    if (!d) return;

    d->timeMs = ms;
    idx = lrc_index_at(d->lrc, ms);
    if (idx != d->cur) {
        d->cur = idx;
        center_current(h, d);
        InvalidateRect(h, NULL, FALSE);
    } else if (d->scroll != d->targetScroll) {
        InvalidateRect(h, NULL, FALSE);     /* 动画推进 */
    }
}

void lyricview_set_font(HWND h, HFONT base)
{
    LvData *d = (LvData *)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (!d) return;
    d->baseFont = base;
    build_fonts(d);
    d->layoutW = 0;
    InvalidateRect(h, NULL, FALSE);
}

void lyricview_set_dpi(HWND h, int dpi)
{
    LvData *d = (LvData *)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (d) d->dpi = dpi;
}

void lyricview_set_colors(HWND h, COLORREF bg, COLORREF text, COLORREF cur, COLORREF accent)
{
    LvData *d = (LvData *)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (!d) return;
    d->bg = bg;
    d->text = text;
    d->curColor = cur;
    d->accent = accent;
    InvalidateRect(h, NULL, FALSE);
}
