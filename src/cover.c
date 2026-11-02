/* cover.c —— 专辑封面：解码内嵌封面 + 生成校徽风格默认封面
 *
 * 全部用纯 C + Win32：
 *   解码 —— OleLoadPicture（系统自带的图片解码器，支持 JPEG/PNG/BMP/GIF）
 *   缩放 —— StretchBlt + HALFTONE
 *   默认封面 —— 直接往 32 位 DIB 上画（渐变 + 五瓣花 + 文字）
 *
 * 早先版本用 GDI+（C++ 类），但那样会引入 libstdc++-6.dll 运行时依赖，
 * 程序在别的机器上会以 0xC0000135（找不到 DLL）启动失败。改用纯 C 后
 * 只有一个 exe，不需要任何额外 DLL。
 */
#include "cover.h"
#include "wicdec.h"
#include "theme.h"

#include <windows.h>
#include <ole2.h>
#include <olectl.h>
#include <objbase.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

int cover_startup(void)
{
    /* OleLoadPicture 需要 OLE 初始化。已经初始化过会返回 S_FALSE，不算失败。 */
    HRESULT hr = OleInitialize(NULL);
    return (hr == S_OK || hr == S_FALSE || hr == RPC_E_CHANGED_MODE);
}

void cover_shutdown(void)
{
    OleUninitialize();
}



/* 对外入口：失败就重试。
   实测同一个 exe 在有的机器/时机上 OleLoadPicture 会偶发失败（封面退化成
   兜底图），补一次 COM 初始化 + 重试能救回来。 */
HBITMAP cover_from_memory(const void *data, int len, int maxSize)
{
    if (!data || len <= 0 || maxSize < 8)
        return NULL;
    /* 交给 WIC：按短边等比 + 居中裁剪成正方形 */
    return wic_render(data, len, maxSize, maxSize, 1, RGB(255, 255, 255));
}

/* 按字符串算一个稳定的颜色，让不同专辑/歌手的默认封面不一样，
   但都留在"校徽蓝"的邻近色域里。返回 0 表示用默认蓝。 */
static COLORREF auto_color(const wchar_t *text)
{
    unsigned h = 2166136261u;
    int i;
    static const BYTE R[6] = {  6,  14,  28,  44,  10,  36 };
    static const BYTE G[6] = { 75,  92, 110, 100,  62,  84 };
    static const BYTE B[6] = {150, 168, 186, 176, 138, 160 };
    int k;

    if (!text || !text[0])
        return TH_BLUE;
    for (i = 0; text[i]; i++)
        h = (h ^ (unsigned)text[i]) * 16777619u;
    k = (int)(h % 6);
    return RGB(R[k], G[k], B[k]);
}

HBITMAP cover_default(int size, const wchar_t *text, COLORREF color)
{
    HDC screen, mem;
    HGDIOBJ old;
    BITMAPINFO bi;
    unsigned char *bits = NULL;
    HBITMAP dst;
    int x, y;

    if (size < 16)
        return NULL;
    if (color == 0)
        color = auto_color(text);

    screen = GetDC(NULL);

    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = size;
    bi.bmiHeader.biHeight = -size;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    dst = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    if (!dst) {
        ReleaseDC(NULL, screen);
        return NULL;
    }

    /* --- 1. 自上而下的竖直渐变（直接写像素，快且可控） --- */
    if (bits) {
        int r1 = GetRValue(color) + 28, g1 = GetGValue(color) + 28, b1 = GetBValue(color) + 28;
        int r2 = GetRValue(color) / 2,  g2 = GetGValue(color) / 2,  b2 = GetBValue(color) / 2;
        if (r1 > 255) r1 = 255;
        if (g1 > 255) g1 = 255;
        if (b1 > 255) b1 = 255;

        for (y = 0; y < size; y++) {
            int r = r1 + (r2 - r1) * y / (size - 1);
            int g = g1 + (g2 - g1) * y / (size - 1);
            int b = b1 + (b2 - b1) * y / (size - 1);
            unsigned char *row = bits + (size_t)y * size * 4;
            for (x = 0; x < size; x++) {
                row[x * 4 + 0] = (unsigned char)b;
                row[x * 4 + 1] = (unsigned char)g;
                row[x * 4 + 2] = (unsigned char)r;
                row[x * 4 + 3] = 255;
            }
        }
    }

    mem = CreateCompatibleDC(screen);
    old = SelectObject(mem, dst);

    /* --- 2. 半透明白花：这里直接用"介于底色和白色之间"的颜色近似 --- */
    {
        int ar = (GetRValue(color) + 255 * 3) / 4;
        int ag = (GetGValue(color) + 255 * 3) / 4;
        int ab = (GetBValue(color) + 255 * 3) / 4;
        theme_draw_flower(mem, size / 2, size / 2, (int)(size * 0.34), RGB(ar, ag, ab));
    }

    /* --- 3. 正中间的字符 --- */
    if (text && text[0]) {
        HFONT f, of;
        wchar_t one[3];
        RECT rc;
        LOGFONTW lf;

        one[0] = text[0];
        one[1] = 0;

        ZeroMemory(&lf, sizeof(lf));
        lf.lfHeight = -(LONG)(size * 0.42);
        lf.lfWeight = FW_BOLD;
        lf.lfCharSet = DEFAULT_CHARSET;
        wcscpy(lf.lfFaceName, L"Microsoft YaHei UI");
        f = CreateFontIndirectW(&lf);
        of = (HFONT)SelectObject(mem, f);

        SetBkMode(mem, TRANSPARENT);
        SetTextColor(mem, RGB(255, 255, 255));
        rc.left = 0; rc.top = 0; rc.right = size; rc.bottom = size;
        DrawTextW(mem, one, 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(mem, of);
        DeleteObject(f);
    }

    SelectObject(mem, old);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
    return dst;
}


/* 歌手头像：圆形徽章（白圈 + 深蓝底 + 白色五瓣花）+ 名字首字 */
HBITMAP cover_artist(int size, const wchar_t *name)
{
    HDC screen, mem;
    HGDIOBJ old;
    BITMAPINFO bi;
    unsigned char *bits = NULL;
    HBITMAP dst;
    int x, y;

    if (size < 16)
        return NULL;

    screen = GetDC(NULL);
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = size;
    bi.bmiHeader.biHeight = -size;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    dst = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    if (!dst) { ReleaseDC(NULL, screen); return NULL; }

    /* 深蓝竖向渐变底 + alpha 置满 */
    if (bits) {
        int r1 = 17, g1 = 82, b1 = 156;
        int r2 = 8,  g2 = 45, b2 = 92;
        for (y = 0; y < size; y++) {
            int r = r1 + (r2 - r1) * y / (size - 1);
            int g = g1 + (g2 - g1) * y / (size - 1);
            int b = b1 + (b2 - b1) * y / (size - 1);
            unsigned char *row = bits + (size_t)y * size * 4;
            for (x = 0; x < size; x++) {
                row[x * 4 + 0] = (unsigned char)b;
                row[x * 4 + 1] = (unsigned char)g;
                row[x * 4 + 2] = (unsigned char)r;
                row[x * 4 + 3] = 255;
            }
        }
    }

    mem = CreateCompatibleDC(screen);
    old = SelectObject(mem, dst);

    /* 白色圆环 */
    {
        int m = size / 14;
        HPEN pn = CreatePen(PS_SOLID, m, RGB(255, 255, 255));
        HGDIOBJ op = SelectObject(mem, pn);
        HGDIOBJ ob = SelectObject(mem, GetStockObject(NULL_BRUSH));
        Ellipse(mem, m, m, size - m, size - m);
        SelectObject(mem, op);
        SelectObject(mem, ob);
        DeleteObject(pn);
    }

    /* 中间的白花 */
    theme_draw_flower(mem, size / 2, size * 42 / 100, size * 26 / 100, RGB(255, 255, 255));

    /* 下面写名字首字 */
    if (name && name[0]) {
        HFONT f, of;
        wchar_t one[3];
        RECT rc;
        LOGFONTW lf;
        one[0] = name[0];
        one[1] = 0;
        ZeroMemory(&lf, sizeof(lf));
        lf.lfHeight = -(LONG)(size * 0.24);
        lf.lfWeight = FW_BOLD;
        lf.lfCharSet = DEFAULT_CHARSET;
        wcscpy(lf.lfFaceName, L"Microsoft YaHei UI");
        f = CreateFontIndirectW(&lf);
        of = (HFONT)SelectObject(mem, f);
        SetBkMode(mem, TRANSPARENT);
        SetTextColor(mem, RGB(255, 255, 255));
        rc.left = 0; rc.top = size * 60 / 100; rc.right = size; rc.bottom = size * 90 / 100;
        DrawTextW(mem, one, 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(mem, of);
        DeleteObject(f);
    }

    SelectObject(mem, old);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
    return dst;
}

/* 等比缩放放进 boxW x boxH（不足的地方留黑），启动页用 */
HBITMAP cover_fit_box(const void *data, int len, int boxW, int boxH)
{
    if (!data || len <= 0 || boxW < 16 || boxH < 16)
        return NULL;
    /* 等比缩放放进框里，不足处填启动页那种深蓝 */
    return wic_render(data, len, boxW, boxH, 0, RGB(6, 32, 64));
}