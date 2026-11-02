/* splash.c —— 启动页
 *
 * 设计稿（res/splash.png）已经作为 RCDATA 打进 exe，运行时解码成位图，
 * 用无边框置顶窗口居中显示一小会儿。显示期间照样泵消息，界面不会假死；
 * 点一下或按任意键可以提前跳过。
 */
#include "splash.h"
#include "app.h"
#include "cover.h"

#include <windows.h>

static HBITMAP g_bmp;
static int     g_w, g_h;

static LRESULT CALLBACK SplashProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)wp;
    (void)lp;

    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        if (g_bmp) {
            HDC mem = CreateCompatibleDC(dc);
            HGDIOBJ old = SelectObject(mem, g_bmp);
            BitBlt(dc, 0, 0, g_w, g_h, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteDC(mem);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_KEYDOWN:
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void splash_show(HINSTANCE hInst, int dpi, int ms)
{
    WNDCLASSEXW wc;
    HWND hwnd;
    HRSRC hr;
    HGLOBAL hg;
    const void *data;
    DWORD len;
    DWORD t0;

    if (dpi <= 0)
        dpi = 96;

    /* 从资源里取设计稿图片（3:4 竖图） */
    hr = FindResourceW(hInst, MAKEINTRESOURCEW(IDR_SPLASH), RT_RCDATA);
    if (!hr)
        return;
    hg = LoadResource(hInst, hr);
    if (!hg)
        return;
    data = LockResource(hg);
    len = SizeofResource(hInst, hr);
    if (!data || len < 64)
        return;

    g_w = MulDiv(400, dpi, 96);
    g_h = MulDiv(536, dpi, 96);
    g_bmp = cover_fit_box(data, (int)len, g_w, g_h);
    if (!g_bmp)
        return;

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = SplashProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = SPLASH_CLASS;
    RegisterClassExW(&wc);

    hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, SPLASH_CLASS, L"",
                           WS_POPUP,
                           (GetSystemMetrics(SM_CXSCREEN) - g_w) / 2,
                           (GetSystemMetrics(SM_CYSCREEN) - g_h) / 2,
                           g_w, g_h, NULL, NULL, hInst, NULL);
    if (!hwnd) {
        DeleteObject(g_bmp);
        g_bmp = NULL;
        return;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    t0 = GetTickCount();
    while (GetTickCount() - t0 < (DWORD)(ms < 0 ? 0 : ms)) {
        MSG m;
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
            if (m.message == WM_QUIT) {
                DestroyWindow(hwnd);
                if (g_bmp) { DeleteObject(g_bmp); g_bmp = NULL; }
                return;
            }
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        Sleep(15);
    }

    DestroyWindow(hwnd);
    if (g_bmp) {
        DeleteObject(g_bmp);
        g_bmp = NULL;
    }
}
